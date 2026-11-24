---
name: browser-assisted-crawl
description: "用可见 Chrome + CDP 做浏览器辅助采集：处理 JS 渲染、反爬/403、登录页、验证码、附件下载，并把下载文件抽取入库。当用户要求用浏览器抓取网页/下载附件、fetch 返回 403/验证码/空 HTML、需要登录后取文件、或需要人工输入验证码时使用。验证码优先人工输入；用户明确授权时才用截图识别。不绕过登录、权限、付费墙或网站访问控制。"
---

# 浏览器辅助采集

用真实 Chrome 解决 `web_fetch`/curl 拿不到的内容：JS 页面、403/412、验证码、登录后附件、下载链接。核心是**可见浏览器 + CDP + 人工验证码 + 下载后入库**，不是无人值守的验证码破解。

## 硬规则

1. **先确认授权与范围**：只采集用户指定站点/项目；不碰无关账号、通讯录、支付、隐私数据。
2. **验证码优先人工**：截图给用户输入，或用 `read_image` 识别后让用户确认；识别不确定就停下来问。不要接入打码平台，不要绕过网站的人机校验。
3. **不读密码**：需要登录时，让用户在自己的 Chrome 窗口里输入；不抓取、不打印 cookie、localStorage、密码。
4. **限速**：同一站点串行，默认 2–5 秒间隔；遇到 429/403 立即停，不重试轰炸。
5. **尊重 robots/条款**：只下载公开或用户有权访问的材料；不绕过付费墙、权限、水印或下载限制。
6. **下载后校验**：检查文件头（`file`/magic）、大小、文本可读性；不是目标格式就标记失败，不伪造。
7. **入库留痕**：记录来源 URL、抓取时间、文件名、sha256、能力分类（可渲染/内容包/仅参考）。

## 工作流

### 1. 启动隔离浏览器

```bash
bash scripts/start-chrome.sh
# 默认端口 9222，临时 profile /tmp/dsh-crawl-profile，可见窗口
```

- 不要用用户日常 Chrome profile；不要把 `--user-data-dir` 指到 `~/Library/Application Support/Google/Chrome`。
- 需要无头时设 `CDP_HEADLESS=1`，但验证码/登录场景保持可见。
- 已启动则直接复用；`curl -s http://127.0.0.1:9222/json/version` 验证。

### 2. 打开页面并看真实内容

```bash
node scripts/cdp.mjs goto "https://example.com/page"
node scripts/cdp.mjs shot /tmp/page.png
node scripts/cdp.mjs text | head -80
node scripts/cdp.mjs links "附件|申报|docx|pdf"
```

- 先截图确认页面不是验证码/登录/错误页；再读 DOM。
- `links` 输出 `{text, href}`；附件链接常是 `attachment.jspx`、`download.jsp`、`P020...docx`。
- 页面是 SPA 时，等 `networkidle` 不够就 `wait 3000` 再取。

### 3. 点击、填写、翻页

```bash
node scripts/cdp.mjs click "a[href*='附件']"
node scripts/cdp.mjs type "#code" "9600"
node scripts/cdp.mjs click "button.confirm"
node scripts/cdp.mjs wait 2000
```

- 选择器优先 `id`、`name`、`href` 片段；避免依赖易变 class。
- 填写用原生 setter + `input/change` 事件，兼容 Vue/React。
- 点击后重新 `shot`，确认状态变化，不要盲点。

### 4. 验证码

1. `shot /tmp/captcha.png`，用 `read_image` 看数字/字母。
2. 简单数字验证码可直接 `type` + 点击；**不确定就截图给用户**，让用户输入。
3. 图形/滑块/点选/短信/扫码：交给用户，不尝试自动化绕过。
4. 用户输入后 `click` 确认；用 `watch` 等下载。

### 5. 下载

**直接文件 URL：**

```bash
node scripts/cdp.mjs download "https://.../file.docx" "/path/to/downloads"
```

**页面点击/验证码后下载：**

```bash
node scripts/cdp.mjs watch "/path/to/downloads" 120
# 另开一个终端/步骤让用户在浏览器里点下载；watch 会等新文件出现
```

- Chrome 的下载目录可能被策略改到 `~/Downloads`；`download`/`watch` 会同时检查目标目录和 `~/Downloads`。
- 下载完成后 `file` 验证；HTML 登录页伪装成 `.doc` 很常见。

### 6. 入库

```bash
python3 scripts/ingest.py \
  --root /path/to/corpus \
  --source /path/to/downloads/file.docx \
  --id E48 --title "某某项目申报书" --org "某卫健委" --year 2026 \
  --category provincial-xx --capability renderable \
  --source-url "https://..." --notes "关键约束"
```

- `ingest.py` 复制原件到 `raw/`、抽取文本到 `text/`、追加 `manifest.json` + `manifest.md`，按 sha256 去重。
- 能力分类：
  - `renderable`：纯 docx/pdf/结构稳定，可做底稿；
  - `content-pack`：宏/受保护/在线系统/老 .doc/双盲活页，只出内容包；
  - `reference`：通知/指南/审查表，只提取要求。
- 入库后抽查文本前 30 行；空文本/乱码要修，不要直接算完成。

### 7. 收尾

```bash
node scripts/cdp.mjs tabs     # 确认没有遗留敏感页面
# 关闭临时 Chrome：kill $(cat /tmp/chrome-crawl.pid)
```

- 保留截图和来源记录；删除临时 profile 前确认不需要登录态。
- 报告中写清楚：拿到了什么、哪些没拿到、为什么（登录/验证码/无公开附件）。

## 命令速查

| 命令 | 用途 |
|---|---|
| `start-chrome.sh` | 启动可见 Chrome（端口 9222） |
| `cdp.mjs status` | 查看 Chrome/标签页 |
| `cdp.mjs goto <url>` | 当前页导航 |
| `cdp.mjs shot <png>` | 截图 |
| `cdp.mjs text` | 页面可见文本 |
| `cdp.mjs links [regex]` | 列出链接 |
| `cdp.mjs click <selector>` | 点击 |
| `cdp.mjs type <selector> <text>` | 填值并触发 input/change |
| `cdp.mjs wait <ms>` | 等待 |
| `cdp.mjs download <url> <dir>` | 直接下载文件 |
| `cdp.mjs watch <dir> [sec]` | 等用户点击后的新下载 |
| `ingest.py ...` | 抽取 + 入库 + 去重 |

## 常见故障

| 现象 | 处理 |
|---|---|
| `/json/version` 连不上 | Chrome 未启动/端口被占；换 `CDP_PORT=9333` |
| 页面是验证码 | 截图给用户；不要反复刷新 |
| 页面 403/412 | 用浏览器打开；仍不行就记录“需登录/反爬”，不要硬撞 |
| 下载文件是 HTML | 登录态/验证码失效；`file` 检查后重试或让用户登录 |
| 下载目录找不到 | 检查 `~/Downloads`；用 `watch` 而不是只设一次 behavior |
| 点击无反应 | 选择器命中隐藏元素；先 `scrollIntoView`，或改用 `links` 拿 href |
| 中文乱码 | `.doc` 是 Word HTML/UTF-16；`ingest.py` 会自动识别，仍乱码就手工指定编码 |

## 参考

- CDP 配方与选择器：[`references/cdp-recipes.md`](references/cdp-recipes.md)
- 入库/分类/清单规范：[`references/ingest.md`](references/ingest.md)
- 安全与合规：[`references/safety.md`](references/safety.md)
