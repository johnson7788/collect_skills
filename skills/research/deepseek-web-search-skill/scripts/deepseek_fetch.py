#!/usr/bin/env python3
"""Call DeepSeek Fetch API and print the Markdown page response."""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ENDPOINT = "https://api.deepseek.com/v1/tools/fetch"
RETRYABLE = {408, 429, 500, 502, 503, 504}


def validate_url(value):
    if len(value) > 8192:
        raise ValueError("URL must not exceed 8192 characters")
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError("URL must be an absolute HTTP/HTTPS URL with a host")
    if parsed.username is not None or parsed.password is not None:
        raise ValueError("URL must not contain a username or password")


def request_json(payload, api_key, retries=3):
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(
        ENDPOINT,
        data=data,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=90) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            if exc.code not in RETRYABLE or attempt >= retries:
                raise RuntimeError(f"DeepSeek Fetch HTTP {exc.code}: {body}") from exc
            time.sleep(min(2 ** attempt, 8))
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt >= retries:
                raise RuntimeError(f"DeepSeek Fetch network error: {exc}") from exc
            time.sleep(min(2 ** attempt, 8))


def main():
    parser = argparse.ArgumentParser(description="Fetch a webpage with DeepSeek.")
    parser.add_argument("--url", required=True, help="Absolute HTTP/HTTPS URL")
    args = parser.parse_args()

    try:
        validate_url(args.url)
    except ValueError as exc:
        parser.error(str(exc))

    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("Missing DEEPSEEK_API_KEY environment variable.", file=sys.stderr)
        return 2

    try:
        result = request_json({"url": args.url}, api_key)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
