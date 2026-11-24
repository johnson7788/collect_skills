#!/usr/bin/env bash
# Start an isolated, visible Chrome with CDP enabled.
set -euo pipefail

PORT="${CDP_PORT:-9222}"
PROFILE="${CDP_PROFILE:-/tmp/dsh-crawl-profile}"
LOG="${CDP_LOG:-/tmp/chrome-crawl.log}"
PIDFILE="${CDP_PIDFILE:-/tmp/chrome-crawl.pid}"

CHROME="${CHROME_BIN:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
if [[ ! -x "$CHROME" ]]; then
  for candidate in \
    "/Applications/Chromium.app/Contents/MacOS/Chromium" \
    "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge" \
    "$(command -v google-chrome || true)" \
    "$(command -v chromium || true)"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then CHROME="$candidate"; break; fi
  done
fi
if [[ ! -x "$CHROME" ]]; then
  echo "No Chrome/Chromium found. Set CHROME_BIN." >&2
  exit 1
fi

if curl -fsS "http://127.0.0.1:${PORT}/json/version" >/dev/null 2>&1; then
  echo "CDP already running on ${PORT}"
  curl -fsS "http://127.0.0.1:${PORT}/json/version"
  exit 0
fi

mkdir -p "$PROFILE"
ARGS=(
  "--remote-debugging-port=${PORT}"
  "--user-data-dir=${PROFILE}"
  "--no-first-run"
  "--no-default-browser-check"
  "--disable-background-networking"
  "--disable-component-update"
  "--disable-sync"
  "--remote-allow-origins=*"
)
if [[ "${CDP_HEADLESS:-0}" == "1" ]]; then
  ARGS+=("--headless=new" "--disable-gpu")
fi

nohup "$CHROME" "${ARGS[@]}" about:blank >"$LOG" 2>&1 &
echo $! >"$PIDFILE"

for _ in $(seq 1 30); do
  if curl -fsS "http://127.0.0.1:${PORT}/json/version" >/dev/null 2>&1; then
    echo "CDP ready on ${PORT} (pid $(cat "$PIDFILE"))"
    curl -fsS "http://127.0.0.1:${PORT}/json/version"
    exit 0
  fi
  sleep 0.5
done

echo "Chrome did not expose CDP on ${PORT}. See ${LOG}" >&2
tail -20 "$LOG" >&2 || true
exit 1
