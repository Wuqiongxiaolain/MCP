# 无 Skill 宿主验收清单

> 产品视角：只装 `graphmcp[.exe]`，不安装仓库 AGENTS/Skills，也不打开本仓库工作区。

用于确认 AI 友好契约已编进二进制 `tools/list`，避免「仓库内有 Skill 看起来好、用户只装 exe 退化」。

## 前置

1. 从构建产物或 Release 取得 `graphmcp` / `graphmcp.exe`
2. 配置任意 MCP 宿主，仅指向该二进制 + `serve`
3. 确认工作区 **没有** 本仓库的 `skills/`、`AGENTS.md`、`.cursor/rules`

## 清单

| # | 检查项 | 期望 |
|---|--------|------|
| 1 | `tools/list` 含 `graph_apply` | 描述写明批量编辑+commit，勿用 cursor_* |
| 2 | `graph_commit` 描述 | 明确 Agents 应使用 `all=true` |
| 3 | `graph_update` 描述 | 无「via cursor」误导；指向 in-memory selector |
| 4 | `graph_import` 描述 | 标明非首次导入，应使用 `graph_create` |
| 5 | `graph_layout` 描述/`save` | 强调不 `save=true` 则不落盘 |
| 6 | 空 stage `graph_commit` | `isError`，文案含 `all=true` 可操作提示 |
| 7 | `graph_create` → `graph_apply` → 导出 | 无需 Skill 即可完成中等任务 |
| 8 | 制品内容 | Release 包默认有 exe；Skill 若存在须标为可选 |

自动回归：`make perf-smoke` / `scripts/mcp_perf_smoke.py`（不依赖 Skill）。
