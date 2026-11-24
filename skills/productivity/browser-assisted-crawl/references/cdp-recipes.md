# CDP 配方

## 环境

- Node 22+（使用全局 `WebSocket`，无需 npm 包）。
- Chrome/Chromium/Edge 任一；`start-chrome.sh` 会自动找。
- 端口默认 `9222`，可用 `CDP_PORT=9333` 改。
- profile 默认 `/tmp/dsh-crawl-profile`；不要指向用户日常 profile。

## 常用命令

```bash
node scripts/cdp.mjs status
node scripts/cdp.mjs goto "https://example.com"
node scripts/cdp.mjs shot /tmp/page.png
node scripts/cdp.mjs text
node scripts/cdp.mjs links "附件|申报|docx|pdf"
node scripts/cdp.mjs click "a[href*='download']"
node scripts/cdp.mjs type "#code" "1234"
node scripts/cdp.mjs wait 2000
node scripts/cdp.mjs download "https://.../file.docx" /tmp/dl
node scripts/cdp.mjs watch /tmp/dl 120
```

## 找附件

1. `links` 过滤 `附件|申报|模板|docx|doc|pdf|download|attach`。
2. 链接可能是：
   - `attachment.jspx?cid=...&i=1&...`
   - `system/_content/download.jsp?...`
   - `P020...docx`
   - `files/...pdf`
3. 有些页面把附件放在 `iframe` 或 JS 变量里：
   ```js
   JSON.stringify(Array.from(document.querySelectorAll('iframe')).map(f => f.src))
   ```
4. 点击后可能触发浏览器下载；用 `watch` 等文件，而不是反复看 DOM。

## 登录

- 打开登录页，让用户在自己的 Chrome 窗口里输入账号/密码/验证码。
- 登录成功后，同一 profile 的 cookie 在本次会话有效；直接 `goto` 目标页。
- 不要读取或打印 `document.cookie`、`localStorage`、密码字段。
- 需要切换账号时，先问用户，不要复用别人的登录态。

## 验证码

- 简单数字/字母：`shot` → `read_image` → `type` → `click`。
- 识别不确定：把截图给用户，让用户输入。
- 滑块/点选/短信/扫码：交给用户；不要接入打码平台或模拟鼠标轨迹破解。
- 失败一次就停，刷新重试最多 2 次；频繁失败说明站点限制，记录并退出。

## 下载

- `download <url> <dir>`：适合直接文件 URL。内部用 `Browser.setDownloadBehavior`，并同时检查目标目录和 `~/Downloads`。
- `watch <dir> <sec>`：适合“用户点下载/过验证码后”的场景；保持浏览器连接，等新文件出现。
- Chrome 可能把文件放到 `~/Downloads`（策略/用户设置），脚本会同时查。
- **PDF 可能被 Chrome 内联打开**，不会触发下载；优先找页面的“下载”按钮/附件链接，或在 URL 上加站点支持的 `?download=1`。`Page.printToPDF` 打出来的是网页渲染，不是原 PDF。
- 下载后用 `file` 检查：
  ```bash
  file /path/download.docx
  ```
  如果是 HTML/XML，说明下载的是登录页/错误页。

## 常见 DOM 操作

```js
// 找所有下载链接
Array.from(document.querySelectorAll('a')).filter(a => /download|attach|附件/.test(a.href + a.innerText))

// 找表单
Array.from(document.querySelectorAll('input,textarea,select')).map(e => ({tag:e.tagName,id:e.id,name:e.name,type:e.type}))

// 找包含验证码的图片
Array.from(document.images).filter(i => /code|captcha|verify/i.test(i.src + i.id + i.alt)).map(i => i.src)

// 等待某个元素出现
new Promise((resolve, reject) => {
  const t = setInterval(() => {
    if (document.querySelector('.result')) { clearInterval(t); resolve(true); }
  }, 300);
  setTimeout(() => { clearInterval(t); reject(new Error('timeout')); }, 10000);
})
```

`eval` 支持 `awaitPromise: true`，可以直接跑 Promise。

## 故障排查

| 现象 | 原因/处理 |
|---|---|
| `ECONNREFUSED 9222` | Chrome 未启动；跑 `start-chrome.sh` |
| `No page target` | Chrome 只有后台页；`goto` 会自动新建 |
| 点击无反应 | 元素被遮挡/隐藏；先 `scrollIntoView`，或直接 `goto` href |
| 下载超时 | 文件在 `~/Downloads`；用 `download`/`watch` 而非手动设置 |
| 文件是 0 字节 | 网络中断/权限；重试一次，仍失败就记录 |
| 页面中文乱码 | 站点编码 GBK；`ingest.py` 会尝试 gb18030 |
| 403/412 | 换真实浏览器仍不行，说明需登录/风控；不要硬撞 |

## 清理

```bash
kill "$(cat /tmp/chrome-crawl.pid)" 2>/dev/null || true
rm -rf /tmp/dsh-crawl-profile
```

删除 profile 会丢失登录态；确认不再需要后再删。
