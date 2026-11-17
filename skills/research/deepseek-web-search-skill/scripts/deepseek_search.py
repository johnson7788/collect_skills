#!/usr/bin/env python3
"""Call DeepSeek Search API and print the JSON response."""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

ENDPOINT = "https://api.deepseek.com/v1/tools/search"
RETRYABLE = {408, 429, 500, 502, 503, 504}


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
            with urllib.request.urlopen(req, timeout=60) as response:
                body = response.read().decode("utf-8")
                return json.loads(body)
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", errors="replace")
            if exc.code not in RETRYABLE or attempt >= retries:
                raise RuntimeError(f"DeepSeek Search HTTP {exc.code}: {body}") from exc
            time.sleep(min(2 ** attempt, 8))
        except (urllib.error.URLError, TimeoutError) as exc:
            if attempt >= retries:
                raise RuntimeError(f"DeepSeek Search network error: {exc}") from exc
            time.sleep(min(2 ** attempt, 8))


def main():
    parser = argparse.ArgumentParser(description="Search the web with DeepSeek.")
    parser.add_argument("--query", required=True, help="Search query (1-128 characters)")
    parser.add_argument("--count", type=int, default=10, help="Number of results (1-50)")
    parser.add_argument("--ori-question", help="Original user question")
    parser.add_argument("--localization", help="IP address for localized results")
    parser.add_argument("--need-content", action="store_true", help="Request page content in results")
    args = parser.parse_args()

    query = args.query.strip()
    if not query or len(query) > 128:
        parser.error("--query must contain 1-128 characters")
    if not 1 <= args.count <= 50:
        parser.error("--count must be between 1 and 50")
    if args.ori_question is not None and not 1 <= len(args.ori_question) <= 128:
        parser.error("--ori-question must contain 1-128 characters")

    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("Missing DEEPSEEK_API_KEY environment variable.", file=sys.stderr)
        return 2

    payload = {"query": query, "count": args.count, "need_content": args.need_content}
    if args.ori_question is not None:
        payload["ori_question"] = args.ori_question
    if args.localization:
        payload["localization"] = args.localization

    try:
        result = request_json(payload, api_key)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
