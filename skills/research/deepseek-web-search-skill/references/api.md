# DeepSeek Search / Fetch API 速查

Base URL：`https://api.deepseek.com`

## Search

`POST /v1/tools/search`

请求头：
- `Authorization: Bearer <DEEPSEEK_API_KEY>`
- `Content-Type: application/json`

请求体字段：
- `query`：必填，1～128 个 Unicode 字符。
- `count`：可选，1～50，默认 10。
- `ori_question`：可选，1～128 个字符。
- `localization`：可选，本地化搜索 IP。
- `need_content`：可选，默认 false。

响应 `results[]` 字段：
`rerank_score`、`title`、`url`、`snippet`、`summary`、`content`、`site_name`、`site_icon`、`date`、`authority`。

## Fetch

`POST /v1/tools/fetch`

请求体：`{"url":"https://example.com/article"}`

URL 必须是包含 host 的 HTTP/HTTPS URL，最长 8192 字符，不能包含用户名或密码。

响应：`{"title":"...","content":"# Markdown ..."}`

## 限速与计费（以用户提供的文档为准）

- Search：100 QPS；Fetch：25 QPS；Fetch 另有目标域名限速。
- Search：成功且结果非空时，每 1,000 次 5 元。
- Fetch：成功且正文非空时，每 1,000 次 5 元。
- 408、429、5xx 可有限重试；其他 4xx 应先修正问题。
