# MCP 写性能 / JSON / 工具经济性 / AI 误用 — 分析与落地

> 与 [`MCP_PERF_OPT_PLAN.md`](MCP_PERF_OPT_PLAN.md) 范围分开：本文覆盖写放大下一刀、传输层 JSON、Agent 调用经济性、产品视角防误用。

## 1. 写存储（snapshot / latest）

- **问题**：`Store::save` 曾对同一整图写 `versions/vN.json` + 全量 `latest.json`，并二次 `dump`。
- **落地**：`latest.json` 改为指针 `{"version":N}`；`load` 兼容旧纯 model；`toJson` 锁外执行；草稿/stage 紧凑 `dump()`。
- **验收**：smoke 断言 latest 远小于 snapshot 且可读。

## 2. 传输层 JSON

- **原则**：MCP 线上载荷紧凑；大内联受 `GRAPHMCP_INLINE_MAX_BYTES` 约束（已扩到 `table_export`）；`graph_property` 嵌真实 Json 值而非 `dump(2)` 字符串。
- **导出**：`to=model` / Excalidraw 序列化改为 `dump()`。
- **未做**：完整 `structuredContent` / outputSchema（另案）。

## 3. 工具经济性

- 最优中等任务约 6–9 次；目标路径：**create → graph_apply → export**（约 3 次）。
- 新增产品内核工具 **`graph_apply`**（ops + 默认 commit + 可选 export），编进二进制。

## 4. AI 误用与 Skills（产品视角）

### 产品边界

| 载体 | 默认随 Release？ |
|------|------------------|
| `tools/list` description/schema、`graph_apply`、错误文案 | **是** |
| 仓库 `AGENTS.md` / 开发用 Cursor Skill | **否** |
| `skills/graphmcp/` 产品 Skill 包 | **可选增益**（需另装；文档写明「装 MCP ≠ 装 Skill」） |

**约束**：Skill **不得**作为正确性依赖。验收以「仅 exe、无 Skill」为准（见 [`NO_SKILL_HOST_CHECKLIST.md`](NO_SKILL_HOST_CHECKLIST.md)）。

### 高发循环与缓解

| 风险 | 缓解（进 tools/list / 行为） |
|------|------------------------------|
| 空 stage 提交 | commit 描述 + 错误文案指向 `all=true` / `graph_apply` |
| 「via cursor」误导 | update/delete 文案纠正；cursor_* 限遍历场景 |
| layout 不落盘 | 强提示 `save=true` |
| 多步拆解 | `graph_apply` |

## 明确不做

- 多线程 `serve`
- 历史全面改为 patch 内容寻址
- 完整 MCP `outputSchema` 重构
