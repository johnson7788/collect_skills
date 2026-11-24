# 入库规范

## 目录

```text
corpus/
├── raw/            # 原件，按 <ID>-<标题>.<ext>
├── text/           # 抽取文本，按 <ID>.txt
├── manifest.json   # 机器可读
└── manifest.md     # 人可读
```

## 调用

```bash
python3 scripts/ingest.py \
  --root /path/to/corpus \
  --source /path/downloads/file.docx \
  --id E48 \
  --title "某省卫健委科研项目申报书" \
  --org "某省卫健委" \
  --year 2026 \
  --category provincial-xx \
  --capability renderable \
  --source-url "https://..." \
  --notes "双盲；1000 字摘要；2 年"
```

先 `--dry-run` 看字符数/文件名，再正式跑。重复 sha256 会提示 `duplicate`；同一 ID 已存在需 `--force`。

## 能力分类

| 分类 | 判定 | 处理 |
|---|---|---|
| `renderable` | 纯 docx/pdf、结构稳定、无宏/无保护 | 可做官方底稿填充 |
| `content-pack` | 宏/受保护/在线系统/老 .doc/双盲活页/HTML Word | 只出内容包 + 填报映射 |
| `reference` | 通知/指南/审查表/招募书 | 只提取要求 |

不确定时先按 `content-pack`，不要承诺 1:1 渲染。

## 文本抽取

| 格式 | 方式 | 备注 |
|---|---|---|
| `.docx` | python-docx；失败则 zip 里 `word/document.xml` | 含表格 |
| `.doc` | 先判断是否 Word HTML；是则 HTMLParser；否则 `textutil`/`antiword` | UTF-16 Word HTML 常见 |
| `.pdf` | `pdftotext -layout`；失败用 pypdf | 需要 poppler 或 `pip install pypdf`；可用 `PDFTOTEXT_BIN` 指定路径；扫描件可能 0 字 |
| `.html` | 去 script/style，按块换行，表格 ` | ` | 编码自动识别 |
| `.md/.txt` | 直接读 | — |

## 质量检查

- `chars` 为 0 或 <50：可能扫描件/加密/下载错；看 `file`，不要直接入库。
- 文本前 30 行应出现标题/机构/章节；乱码要修编码。
- 文件名与 ID 对应；`raw` 与 `text` 都存在。
- 同一文件只入库一次；不同年度/版本用不同 ID。
- 记录来源 URL 和抓取时间，方便过期核对。

## manifest 字段

```jsonc
{
  "id": "E48",
  "category": "provincial-xx",
  "title": "某省卫健委科研项目申报书",
  "org": "某省卫健委",
  "year": "2026",
  "kind": "docx",
  "capability": "renderable",
  "source_url": "https://...",
  "notes": "双盲；1000 字摘要；2 年",
  "raw": "E48-某省卫健委科研项目申报书.docx",
  "text": "E48.txt",
  "chars": 12345,
  "sha256": "...",
  "fetchedAt": "2026-11-24T..."
}
```

## 常见问题

- **下载的是 HTML**：多半是登录/验证码页；回浏览器重新登录/过码。
- **`.doc` 打开是 HTML**：Word 另存的“Word HTML”；`ingest.py` 已处理 UTF-16，若仍乱码手工指定编码。
- **PDF 无文本**：扫描件；需要 OCR（`image-recognize`/`medical-translate`）另走，不要假装有文本。
- **表格丢失**：python-docx 只取单元格文本；合并单元格可能重复，人工抽查关键预算表。
- **文件名含特殊字符**：脚本会清洗；保留 ID 前缀便于和清单对应。
