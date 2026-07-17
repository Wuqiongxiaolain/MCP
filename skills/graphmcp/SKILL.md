---
name: graphmcp
description: >-
  Optional host-side guidance for graphmcp MCP diagram tools. Use when editing
  versioned graphs via graph_create/graph_apply/graph_export. Installing this
  Skill does not install the MCP server; the server ships as graphmcp[.exe].
---

# graphmcp（可选增益 Skill）

> latest update: v0.2.9-beta, 2026-07-17

本 Skill **不是**产品正确性依赖。防误用黄金路径已写入 MCP `tools/list`（随 exe 发布）。
仅在 Cursor 等支持 Skill 的宿主中额外安装后生效；Claude Desktop 等只配 exe 的环境不会加载本文件。

## 黄金路径

1. 首次入库：`graph_create`（不要用 `graph_import`）
2. 查 id：优先 `graph_show`（`graph_export to=model` 仅在需要深查整图结构时）
3. **原子修改并提交**（优先顺序）：
   - 多点改动：一次 `graph_apply`（ops + message）
   - 折线拐点：`graph_set_edge_route`（或 `graph_clear_edge_route`）
   - 微调节点位置：`graph_nudge_node`
   - 箭头装饰：`graph_set_edge_heads`
   - 其它字段：`graph_update` / `graph_insert` 后 `graph_commit` 且 **`all=true`**
4. 导出发布：`graph_export`（或 `graph_apply` 的 `export_to`）

## 下下策（可行，但尽量不用）

- `graph_export to=model` → 手改 JSON → `graph_import` / `graph_create` 覆盖：
  **的确可以**，但是下下策；**仅当**上述原子工具无法表达该修改时再用。

## 其它注意

- 不要用 `graph_cursor_*` 做「改几点」
- 不要空 stage 后 `graph_commit`（不加 `all=true`）
- 不要默认 `graph_layout` 却期望落盘（必须 `save=true`）；强制重排会覆盖手改几何
- 大导出用 `path=` / `export_path=`，勿把整图 model 灌进对话

## 安装说明

将本目录复制到宿主 Skill 搜索路径，或从 Release 中的 `skills/graphmcp/` 解压安装。
**安装 MCP ≠ 安装本 Skill。**
