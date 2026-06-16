#!/bin/bash
# html2video skill setup script
# 从 GitHub 克隆项目、安装依赖、编译 CLI，供 SKILL.md 引导 Claude 在新机器上自动运行。

set -e

SKILL_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="$SKILL_DIR/install"
CLI_BIN="$INSTALL_DIR/packages/cli/dist/bin.js"

echo "=== html2video setup ==="

# ── 1. 检查必要工具 ──────────────────────────────────────────────────────────
check() {
  command -v "$1" >/dev/null 2>&1 || { echo "ERROR: '$1' not found. $2"; exit 1; }
}

check node  "Install Node.js: https://nodejs.org"
check git   "Install git: https://git-scm.com"
check ffmpeg "Install ffmpeg: brew install ffmpeg  (macOS) / apt install ffmpeg  (Linux)"

# ── 2. 安装 pnpm（若未装）───────────────────────────────────────────────────
if ! command -v pnpm >/dev/null 2>&1; then
  echo "Installing pnpm..."
  npm install -g pnpm
fi

# ── 3. 克隆或更新仓库 ────────────────────────────────────────────────────────
if [ -d "$INSTALL_DIR/.git" ]; then
  echo "Updating existing install at $INSTALL_DIR ..."
  git -C "$INSTALL_DIR" pull --ff-only
else
  echo "Cloning nexu-io/html-video into $INSTALL_DIR ..."
  git clone https://github.com/nexu-io/html-video.git "$INSTALL_DIR"
fi

# ── 4. 安装依赖 & 编译 ───────────────────────────────────────────────────────
cd "$INSTALL_DIR"
echo "Installing dependencies (pnpm install)..."
pnpm install --frozen-lockfile

echo "Building packages (pnpm -r build)..."
pnpm -r build

# ── 5. 安装 Playwright Chromium ──────────────────────────────────────────────
echo "Installing Playwright Chromium..."
node "$INSTALL_DIR/node_modules/.bin/playwright" install chromium || \
  npx playwright install chromium

# ── 6. 验证 ──────────────────────────────────────────────────────────────────
if [ ! -f "$CLI_BIN" ]; then
  echo "ERROR: Build failed, $CLI_BIN not found."
  exit 1
fi

echo ""
echo "✓ Setup complete!"
echo "  CLI: node $CLI_BIN"
echo ""
echo "Quick test:"
echo "  node \"$CLI_BIN\" doctor"
