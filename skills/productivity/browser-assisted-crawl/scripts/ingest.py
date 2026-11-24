#!/usr/bin/env python3
"""Ingest a downloaded file into a corpus: copy, extract text, manifest, dedupe.

No third-party dependency is required. python-docx and pdftotext are used when
available; otherwise safe fallbacks are attempted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path


class TextHTMLParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.out: list[str] = []
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in ("style", "script", "noscript"):
            self.skip += 1
        if tag in ("p", "div", "tr", "br", "h1", "h2", "h3", "h4", "li", "table"):
            self.out.append("\n")
        if tag in ("td", "th"):
            self.out.append(" | ")

    def handle_endtag(self, tag):
        if tag in ("style", "script", "noscript") and self.skip:
            self.skip -= 1

    def handle_data(self, data):
        if self.skip:
            return
        text = " ".join(data.split())
        if text:
            self.out.append(text + " ")


def decode_bytes(data: bytes) -> str:
    if data.startswith(b"\xef\xbb\xbf"):
        return data.decode("utf-8-sig", errors="replace")
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", errors="replace")
    if b"\x00" in data[:2000]:
        try:
            return data.decode("utf-16", errors="replace")
        except UnicodeError:
            pass
    for enc in ("utf-8", "gb18030"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", errors="replace")


def html_to_text(text: str) -> str:
    parser = TextHTMLParser()
    parser.feed(text)
    lines = [line.strip() for line in "".join(parser.out).splitlines()]
    return "\n".join(line for line in lines if line)


def extract_docx(src: Path) -> str:
    try:
        from docx import Document  # type: ignore

        doc = Document(str(src))
        lines = [p.text.strip() for p in doc.paragraphs if p.text.strip()]
        for i, table in enumerate(doc.tables):
            lines.append(f"\n[TABLE {i}]")
            for row in table.rows:
                cells = [c.text.replace("\n", " / ").strip() for c in row.cells]
                line = " | ".join(cells)
                if line.strip(" |"):
                    lines.append(line)
        return "\n".join(lines)
    except Exception:
        # Fallback: read document.xml from the docx zip.
        try:
            with zipfile.ZipFile(src) as z:
                xml = z.read("word/document.xml").decode("utf-8", errors="replace")
            xml = re.sub(r"<w:tab[^>]*/>", "\t", xml)
            xml = re.sub(r"</w:p>", "\n", xml)
            xml = re.sub(r"<[^>]+>", "", xml)
            return html_to_text(xml)
        except Exception as error:
            return f"[extract failed: {error}]"


def extract_doc(src: Path) -> str:
    data = src.read_bytes()
    decoded = decode_bytes(data)
    if "<html" in decoded[:5000].lower():
        return html_to_text(decoded)
    for command in (["textutil", "-convert", "txt", "-stdout", str(src)],
                    ["antiword", str(src)]):
        try:
            proc = subprocess.run(command, capture_output=True, text=True, errors="replace")
            if proc.returncode == 0 and proc.stdout.strip():
                return proc.stdout
        except FileNotFoundError:
            continue
    return "[extract failed: no textutil/antiword available]"


def extract_pdf(src: Path) -> str:
    candidates = [
        os.environ.get("PDFTOTEXT_BIN"),
        shutil.which("pdftotext"),
        "/opt/homebrew/bin/pdftotext",
        "/usr/local/bin/pdftotext",
    ]
    for binary in candidates:
        if not binary:
            continue
        try:
            proc = subprocess.run([binary, "-layout", str(src), "-"],
                                  capture_output=True, text=True, errors="replace")
            if proc.returncode == 0 and proc.stdout.strip():
                return proc.stdout
        except FileNotFoundError:
            continue
    try:
        from pypdf import PdfReader  # type: ignore

        return "\n".join((page.extract_text() or "") for page in PdfReader(str(src)).pages)
    except Exception as error:
        return f"[extract failed: install poppler/pdftotext or pypdf ({error})]"


def extract_text(src: Path) -> str:
    suffix = src.suffix.lower()
    if suffix == ".docx":
        return extract_docx(src)
    if suffix == ".doc":
        return extract_doc(src)
    if suffix == ".pdf":
        return extract_pdf(src)
    if suffix in (".html", ".htm", ".md", ".txt"):
        return html_to_text(decode_bytes(src.read_bytes())) if suffix in (".html", ".htm") else decode_bytes(src.read_bytes())
    return decode_bytes(src.read_bytes())


def safe_slug(value: str) -> str:
    value = re.sub(r"[\\/:*?\"<>|\r\n\t]+", "-", value).strip(" .-")
    return value[:70] or "file"


def load_manifest(path: Path) -> list[dict]:
    if not path.exists():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(data, list):
        return data
    if isinstance(data, dict) and isinstance(data.get("items"), list):
        return data["items"]
    raise SystemExit(f"unsupported manifest shape: {path}")


def write_manifest(root: Path, rows: list[dict]) -> None:
    (root / "manifest.json").write_text(
        json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        "# 采集清单",
        "",
        f"共 {len(rows)} 项；由 browser-assisted-crawl/ingest.py 生成。",
        "",
        "| ID | 分类 | 名称 | 机构 | 年度 | 能力 | 原始文件 | 文本 | 来源 |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for row in rows:
        lines.append(
            "| {id} | {category} | {title} | {org} | {year} | {capability} | `{raw}` | [{id}](text/{id}.txt) | {source_url} |".format(
                **row
            )
        )
    (root / "manifest.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="corpus directory")
    parser.add_argument("--source", required=True, help="downloaded file")
    parser.add_argument("--id", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--org", default="")
    parser.add_argument("--year", default="")
    parser.add_argument("--category", default="uncategorized")
    parser.add_argument("--capability", default="reference",
                        choices=["renderable", "content-pack", "reference"])
    parser.add_argument("--source-url", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).expanduser().resolve()
    src = Path(args.source).expanduser().resolve()
    if not src.is_file():
        raise SystemExit(f"source not found: {src}")

    raw_dir = root / "raw"
    text_dir = root / "text"
    raw_dir.mkdir(parents=True, exist_ok=True)
    text_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = root / "manifest.json"
    rows = load_manifest(manifest_path)

    digest = hashlib.sha256(src.read_bytes()).hexdigest()
    duplicate = next((r for r in rows if r.get("sha256") == digest), None)
    if duplicate and duplicate.get("id") != args.id and not args.force:
        print(json.dumps({"ok": False, "reason": "duplicate", "existing": duplicate["id"]},
                         ensure_ascii=False))
        return
    if any(r.get("id") == args.id for r in rows) and not args.force:
        raise SystemExit(f"id already exists: {args.id} (use --force to replace)")

    raw_name = f"{args.id}-{safe_slug(args.title)}{src.suffix.lower()}"
    raw_path = raw_dir / raw_name
    text = extract_text(src)
    chars = len(text.strip())

    print(json.dumps({
        "id": args.id, "title": args.title, "kind": src.suffix.lower().lstrip("."),
        "chars": chars, "raw": raw_name, "sha256": digest,
    }, ensure_ascii=False))

    if args.dry_run:
        return

    if raw_path.exists() and raw_path.resolve() != src:
        raw_path.unlink()
    shutil.copyfile(src, raw_path)
    (text_dir / f"{args.id}.txt").write_text(text, encoding="utf-8")

    row = {
        "id": args.id,
        "category": args.category,
        "title": args.title,
        "org": args.org,
        "year": args.year,
        "kind": src.suffix.lower().lstrip("."),
        "capability": args.capability,
        "source_url": args.source_url,
        "notes": args.notes,
        "raw": raw_name,
        "text": f"{args.id}.txt",
        "chars": chars,
        "sha256": digest,
        "fetchedAt": datetime.now(timezone.utc).isoformat(),
    }
    rows = [r for r in rows if r.get("id") != args.id]
    rows.append(row)
    rows.sort(key=lambda r: (r.get("category", ""), r.get("id", "")))
    write_manifest(root, rows)
    print(json.dumps({"ok": True, "id": args.id, "total": len(rows)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
