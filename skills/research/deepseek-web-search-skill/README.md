# deepseek-web-search

一个符合 Agent Skills 目录约定的 DeepSeek 网页搜索与抓取 Skill。

## 目录

```text
deepseek-web-search/
├── SKILL.md
├── README.md
├── scripts/
│   ├── deepseek_search.py
│   └── deepseek_fetch.py
└── references/
```

## 安装

将 `deepseek-web-search` 目录放入目标 Agent 支持的 skills 目录即可。不同 Harness 的 skills 目录位置可能不同，请按对应产品的配置放置。

配置环境变量：

```bash
export DEEPSEEK_API_KEY="你的 API Key"
```

## 使用

```bash
python scripts/deepseek_search.py --query "DeepSeek 搜索 API" --count 5 --need-content
python scripts/deepseek_fetch.py --url "https://api-docs.deepseek.com/"
```

脚本使用 Python 标准库，无需额外 pip install。
