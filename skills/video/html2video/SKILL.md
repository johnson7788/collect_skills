---
name: html2video
description: "把 HTML 文件或网页 URL 渲染成 MP4 视频。用户提供一个 HTML 文件路径、http/https URL 或已写好的 HTML 内容，直接输出 MP4。支持单文件快速渲染（render-html）和多帧项目工作流（project-*）。当用户说"把这个 HTML 转成视频"、"渲染成 MP4"、"html 转视频"、"把这个网页录成视频"、"html2video" 或上传 HTML 文件、给出网页链接要求出视频时使用。"
metadata:
  requires:
    bins: ["node", "ffmpeg"]
---

# html2video

把任意 HTML 文件渲染成 MP4。底层用 Playwright（Chromium 无头录屏）+ ffmpeg 转码。

---

## CRITICAL：每次使用前必须执行此检查流程

### 第零步：定位 SKILL_DIR

本 skill 可能安装在任意位置。执行渲染前，必须先定位 SKILL.md 所在目录：

```bash
SKILL_DIR="$(cd "$(dirname "$(find "$HOME" -path "*/html2video/SKILL.md" -print -quit 2>/dev/null)")" && pwd)"
echo "${SKILL_DIR:?ERROR: SKILL.md not found}"
```

后续所有路径均基于 `$SKILL_DIR`，**禁止硬编码任何绝对路径**。

### 第一步：检查 CLI 是否已安装

```bash
test -f "$SKILL_DIR/install/packages/cli/dist/bin.js" && echo "READY" || echo "NEED_SETUP"
```

### 第二步：若输出 NEED_SETUP → 立即自动运行安装，无需询问用户

**不要告诉用户去手动运行任何命令。直接执行：**

```bash
bash "$SKILL_DIR/scripts/setup.sh"
```

等待安装完成（约 2-5 分钟）。安装完成后继续执行渲染任务。

> setup.sh 自动完成：clone nexu-io/html-video → pnpm install → pnpm -r build → playwright install chromium

### 第三步：CLI 就绪后，执行渲染

```bash
# CLI 固定路径（setup 后始终在此）
CLI="node $SKILL_DIR/install/packages/cli/dist/bin.js"
```

---

## 渲染命令

### 场景 A：用户已有 HTML 文件或网页 URL（最常见）

`--input` 支持本地文件路径和 http/https URL，用法完全一样：

```bash
# 本地文件
node "$SKILL_DIR/install/packages/cli/dist/bin.js" render-html \
  --input "/abs/path/to/file.html" \
  --output "/abs/path/to/output.mp4" \
  --duration 10 \
  --width 1920 --height 1080 --fps 30 \
  --stream-progress

# 网页 URL（直接录屏）
node "$SKILL_DIR/install/packages/cli/dist/bin.js" render-html \
  --input "https://example.com/page" \
  --output "/abs/path/to/output.mp4" \
  --duration 10 \
  --width 1920 --height 1080 --fps 30 \
  --stream-progress
```

> URL 录制注意：Playwright 使用无头 Chromium，页面需要能在无登录态下正常渲染。若页面需要登录，文件很小（< 200KB）时通常是空白页，需告知用户。

成功响应：
```json
{
  "input": "/abs/path/file.html",
  "output_path": "/abs/path/output.mp4",
  "duration_sec": 10,
  "file_size_bytes": 421192,
  "resolution": { "width": 1920, "height": 1080 },
  "fps": 30
}
```

### 场景 B：用户粘贴 HTML 代码

1. 用 Write 工具把内容写到 `/tmp/html2video-input.html`
2. 执行 render-html，输出到 `/tmp/html2video-output.mp4`

### 场景 C：多帧 / 项目工作流

```bash
CLI="node $SKILL_DIR/install/packages/cli/dist/bin.js"
$CLI project-create --name "my-video"
$CLI search-templates --intent "你的需求" --top 3
$CLI project-set-template <id> --template <template_id>
$CLI project-render <id> --output out.mp4 --stream-progress
```

---

## 参数说明

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--input` | 必填 | HTML 文件绝对路径 |
| `--output` | 与 input 同目录，同名 .mp4 | 输出 MP4 路径 |
| `--duration` | auto（自动录完动画） | 秒数；交互式 demo 建议设 10-30 |
| `--fps` | 30 | 帧率，60 更流畅但文件更大 |
| `--width` | 1920 | 视频宽度（像素） |
| `--height` | 1080 | 视频高度（像素） |
| `--stream-progress` | false | 实时输出进度 |

---

## 错误处理

| 错误 | 原因 | 处理 |
|---|---|---|
| `bin.js: No such file` | setup 未完成 | 重新运行 `bash "$SKILL_DIR/scripts/setup.sh"` |
| `playwright not installed` | Playwright 缺失 | `npx playwright install chromium` |
| `ffmpeg not found on PATH` | ffmpeg 未装 | 告知用户：macOS `brew install ffmpeg`，Linux `apt install ffmpeg` |
| `Source HTML not found` | 路径错误 | 用绝对路径，确认文件存在 |
| `pnpm: command not found` | pnpm 未装 | setup.sh 会自动装；若失败告知用户 `npm install -g pnpm` |

---

## 更新 CLI

```bash
git -C "$SKILL_DIR/install" pull && \
  cd "$SKILL_DIR/install" && pnpm install && pnpm -r build
```
