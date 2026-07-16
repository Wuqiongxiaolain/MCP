# CLI & MCP 指令参考

> latest update: v0.2.6-beta, 2026-07-16

> 命令行与 MCP 工具速查（版本以根目录 VERSION 为准）  
> 操作教程与场景说明见 [USER_GUIDE.md](USER_GUIDE.md)。  
> 机器可读契约见 [api_reference/openapi.yaml](api_reference/openapi.yaml)，由 `make docs-api` 从 `toolList()` 自动生成，**勿手改**。

---

## 一、通用选项与格式

| 选项 | 说明 |
|------|------|
| `--file <path>` | 输入文件路径 |
| `--content <text>` | 内联输入文本 |
| `--output <path>` / `-o <path>` | 输出文件路径 |
| `--stdout` | 强制输出到标准输出 |
| `--id <graph-id>` | 图标识符 |
| `--store <dir>` | 存储目录（环境变量 `GRAPHMCP_STORE`，默认 `./graph-store`） |

| 输入格式 | 输出格式 |
|----------|----------|
| `mermaid` / `markdown` / `csv` / `xml` | `drawio` / `mermaid` / `excalidraw` |
| `excalidraw` / `drawio` / `model` | `svg` / `png` / `pdf` |
| `auto`（自动检测） | `url`（mermaid.live）/ `model`（JSON） |

> **注意**：`drawio` 同时是输入格式和输出格式（`--help` 的输入列表仅列常用项，以 `detectFormat`/`parseAny` 实际支持的完整集合为准）。

**表专用格式**（`table` 命令族 / `table_*` MCP 工具）：

| 方向 | 格式 |
|------|------|
| 表输入 | `csv`（默认）、`xml`（表 XML，根 `<table>`）、`model`（Table JSON） |
| 表输出 | `csv`（默认）、`xml`、`model` |

表输入 **不做 auto 探测**，需显式指定 `--format`（CLI）或 `format`（MCP）。

---

## 二、CLI 命令族（15 个）

> `--help` 家族列表未列 `import`，但 `import` 已作为独立命令族分发。所有家族以 `main.cpp` 分发逻辑为准。

### `create` — 创建并入库

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `from-mermaid` | 从 Mermaid 创建 | `--file` / `--content`、`--name`、`--id`、`--type` |
| `from-markdown` | 从 Markdown 大纲创建 | `--note`、`--no_validate`、`--no_layout` |
| `from-csv` | 从 CSV（边表/层级表）创建 | `--format`（可显式指定；默认 auto） |
| `from-xml` | 从图 XML（`<graph>`）创建 | |
| `from-excalidraw` | 从 Excalidraw JSON 创建 | |
| `from-model` | 从统一模型 JSON 创建 | |
| `from-input` | 自动检测格式创建 | |

示例：`graphmcp create from-mermaid --file flow.mmd --name "登录流程"`

### `convert` — 格式转换（不存储）

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `to-drawio` / `to-mermaid` / `to-excalidraw` | 转为对应文本/JSON | `--file` / `--content`、`--input-format`（默认 auto） |
| `to-svg` / `to-png` / `to-pdf` | 栅格/矢量导出 | `-o` / `--stdout` |
| `to-url` | mermaid.live 编辑链接 | |
| `to-model` | 统一模型 JSON | |

示例：`graphmcp convert to-svg --file flow.mmd -o out.svg`

### `export` — 导出已存储图

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `to-drawio` / `to-mermaid` / `to-excalidraw` | 导出文本/JSON | `--id`（必填） |
| `to-svg` / `to-png` / `to-pdf` | 导出图片/PDF | `-o` / `--stdout` |
| `to-url` / `to-model` | 链接或模型 JSON | `--version <n>`（默认最新） |

示例：`graphmcp export to-drawio --id my-graph -o out.drawio`

### `edit` — 外部编辑器打开

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `with-browser` | 打开 mermaid.live 编辑链接（不写本地文件） | `--id`、`--version` |
| `with-drawio` | 导出 `.drawio` 并打开 | `--editor-path`（可显式指定；默认可自动发现） |
| `with-excalidraw` | 导出 `.excalidraw` 并打开 | `--output`（覆盖默认路径） |
| `with-svg` | 导出 `.svg` 并打开 | |

设置 `GRAPHMCP_NO_LAUNCH=1` 时只生成目标 URL/文件，不实际拉起外部程序（单元测试与无头 CI 使用）。

自动发现优先级：`--editor-path` > 系统关联 > draw.io Desktop > VS Code（hint）。若已存在 `open.*` 文件会提示先 `import` 保存。

### `import` — 编辑回导（**独立命令族，--help 未列出**）

| 用法 | 说明 | 常用选项 |
|------|------|----------|
| `import --id <id>` | 自动探测 `open.*` 回导 | `--format`（drawio\|excalidraw\|mermaid\|…\|auto） |
| `import --id <id> --file <path>` | 指定文件回导 | `--content` + `--format` |

编辑闭环：`edit with-drawio --id X` → 外部保存 → `import --id X` → 校验 → 版本 +1。

### `layout` — 自动布局

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `auto` | 自动选择策略（按图类型：mindmap→tree-h，orgchart→tree-v，有边→layered，无边→grid） | `--id`、`--save`（保存为新版本） |
| `layered` | 分层布局（Kahn + 环兜底；v0.2.6：层平衡 / barycenter 减交叉 / waypoint 折线路由，尚不完善） | `--force`（已布局图强制重排） |
| `tree-h` | 水平树（脑图） | |
| `tree-v` | 垂直树（组织图） | |
| `grid` | 网格（无边兜底） | |

### `validate` — 结构校验

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `graph` | 校验已存储图 | `--id` |
| `input` | 校验输入文件 | `--file`、`--input-format`、`--strict` |

检查项：重复 ID、悬空边、层次循环、孤立节点。状态图允许 `[*]` 作为起始终止端点。白板允许边端点引用 elements ID。`rawMermaid` 图跳过校验。

### `store` — 存储管理

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `list` | 列出所有图 | `--type`、`--format table\|json`（默认 table） |
| `show` | 摘要（节点/边数量、版本数） | `--id` |
| `load` | 完整模型 JSON → stdout | `--id`、`--version <n>` |
| `delete` | 删除图及所有版本（不可逆） | `--id`、`--force` |

### `table` — 通用表管理（19 个子命令）

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `create` | 从 CSV / 表 XML / model 创建表 | `--file` / `--content`、`--format`（默认 csv）、`--name`、`--id`、`--force`、`--note` |
| `import` | 导入（upsert） | 同上 |
| `export` | 导出为 csv / xml / model | `--id`、`--to`（默认 csv）、`-o`、`--version` |
| `list` | 列出所有表 | `--format json` |
| `show` | 查看表元数据与行 | `--id`、`--limit`、`--version` |
| `update` | 批量补丁或 CLI 便捷修改 | `--id`、见下方详细说明 |
| `delete` | 删除表及版本 | `--id`、`--force` |
| `history` | 版本历史 | `--id` |
| `rollback` | 回滚到旧版本（另存新版本） | `--id`、`--version` |
| `from-graph` | 图→表有损投影 | `--graph-id`（或 `--graph`）、`--mode`（skeleton\|edgelist\|hierarchylist\|nodelist）、`--with-hint-row`、`--name` |
| `from-table` / `to-graph` | 表→图 | `--id`（表 id）、`--file`/`--content`（代替表 id）、`--from-col`/`--to-col`/`--label-col` 或 `--id-col`/`--parent-col` |
| `align` | 按主键跨表补齐行 | `--primary`、`--target`、`--primary-key`、`--target-key` |
| `check` | 枚举校验 → 违规报告表 | `--id`、`--rules-id` / `--allowed`、`--ignore-hint-row` |
| `rules-from-graph` | 导图→规则表 | `--graph-id`、`--name` |
| `fix-enums` | 按 check suggestion 修枚举 | `--id`、`--allowed` / `--rules-id`、`--no-save` |
| `derive` | 表派生（`animation_checklist`） | `--source-id`、`--name` |
| `transform-column` | 列变换（`slug`） | `--id`、`--source-column`、`--target-column` |
| `sample-rows` | 追加占位样例行 | `--id`、`--count`、`--rules-id` |
| `propose-rows` | 结构化对象行写入 | `--id`、`--rows`（JSON）、`--rules-id` |

**`table update` 详解**：

两种模式：

1. **JSON 补丁**（与 MCP 对齐）：
   - `--set-cells <JSON>` — `[{row,column\|col_index,value}]`
   - `--add-rows <JSON>` — `[["cell1","cell2"],...]`
   - `--delete-rows <JSON>` — `[rowIndex,...]`
   - `--add-columns <JSON>` — `[{name,default?},...]`
   - `--set-column-values <JSON>` — `{column, values:[...]}`
   - `--dry-run` — 仅预览不保存
   - `--detail` — 返回逐格 before/after

2. **CLI 便捷**（简单场景）：
   - `--add-row <CSV>` — 追加一行
   - `--add-column <name>` + `--default <val>`
   - `--set col=value` + `--row <n>`（默认最后一行）
   - `--dry-run` — 预览不保存

### `version` — 版本控制

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `status` | HEAD / draft / stage 状态 | `<id>`（位置参数） |
| `draft show` / `draft reset` | 查看 / 丢弃草稿 | `--id` |
| `stage` / `stage add` / `stage show` / `stage clear` | 暂存区操作 | `--id`、`--all`（暂存全部）、`--select`（按索引选择暂存，逗号分隔） |
| `commit` | 提交新版本（写 draft→stage→HEAD） | `-m`、`--all`（跳过暂存，即 commitAll） |
| `log` | 版本历史 | `--id`、`--limit`、`--format json\|oneline` |
| `show` | 版本详情 | `--id`、`--version <n>` |
| `diff` | 两版本字段级对比 | `--id`、`<v1> <v2>`（位置参数）、`--format json` |
| `checkout` | HEAD 移到指定版本 | `--id`、`--version`、`--force`（丢弃 draft） |

### `graph` — 图编辑（Draft）

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `show` | 查看图/节点/边 | `--id`、`--node` / `--edge`、`--format json` |
| `update` | 更新节点/边属性（含颜色） | `--id`、`--node` / `--edge` / `--selector`、`--set key=val` |
| `insert` | 插入节点、边或节点属性 | `--id`、`--node` / `--edge` / `--attr`、`--from`/`--to`、`--label`、`--shape`、`--position x y`、`--fillColor`/`--strokeColor` |
| `delete` | 删除（节点级联删边） | `--id`、`--node` / `--edge` / `--selector` |

**选择器（`--selector`）**：`id=X`、`type=flowchart`、`label=Step`、`label~=regex`、`shape=rect`、`parent=X`、`all_nodes`、`all_edges`、`connected_to=X`。

### `cursor` — 游标遍历

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `open` | 打开游标（持久化到磁盘） | `--id`、`--target nodes\|edges` |
| `get` | 读取当前位置 | `--id`、`--cursor <cid>` |
| `next` / `prev` | 前移/后移 | `--id`、`--cursor <cid>` |
| `close` | 关闭游标（删除状态文件） | `--id`、`--cursor <cid>` |

游标跨进程不丢，适合 AI「读一项、判一项、改一项」的交互模式。

### `draft` — 草稿快捷入口

| 子命令 | 说明 |
|--------|------|
| `status` | 相对最新版本的增/删/改统计（节点与边分别计数） |
| `discard` | 丢弃未提交 draft + stage（不可逆） |

### `serve` — MCP 服务

| 命令 | 说明 | 环境变量 |
|------|------|----------|
| `graphmcp serve` | JSON-RPC 2.0 over stdio | `GRAPHMCP_LOG=info\|debug`（日志仅 stderr） |

客户端配置见仓库根目录 `mcp-config.example.json`。

### `dump-tools` — 导出工具契约

| 命令 | 说明 |
|------|------|
| `graphmcp dump-tools [--format openapi\|json] [-o path]` | 从运行中的 `toolList()` 导出 OpenAPI YAML（默认）或原始 JSON；`make docs-api` 写入 `docs/api_reference/openapi.yaml` |

---

## 三、MCP 工具（47 个）

参数与 CLI 对应；通过 `tools/call` 调用。完整 schema 以 `toolList()` → OpenAPI 为准，以下为速查。

### 图生命周期

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `graph_create` | 解析 → 校验 → 布局 → 存储 | `content` | `format`（默认 auto）、`name`、`id`、`type`、`note`、`no_validate`、`no_layout` |
| `graph_convert` | 格式直转（不存储） | `content`, `to` | `format`（默认 auto）、`name`、`type` |
| `graph_export` | 导出已存储图 | `id`, `to` | `path`、`version`（默认最新）、`stdout` |
| `graph_open` | 外部编辑器打开 | `id` | `editor`（browser\|drawio\|excalidraw\|svg）、`launch`（默认 true；false 仅生成目标）、`version` |
| `graph_import` | 编辑回导（校验 + 版本） | `id` | `format`（默认 auto）、`note` |
| `graph_validate` | 结构校验 | `id` 或 `content` | `format`、`strict` |
| `graph_list` | 列出所有图 | （无） | `format`（json\|table） |
| `graph_delete` | 删除图及所有版本 | `id`, `force` | — |

### 图查看 / 属性

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `graph_show` | 查看图/节点/边 | `id` | `node`、`edge`、`path`（JSON path 到 properties 子树）、`format`（summary\|json）、`version` |
| `graph_history` | 版本历史（含节点/边数） | `id` | `limit`、`format`（full\|oneline） |
| `graph_diff` | 两版本字段级对比 | `id`, `v1`, `v2` | `format`（unified\|json） |
| `graph_status` | 工作树状态（HEAD/draft/stage） | `id` | — |
| `graph_property` | 读写图级 properties（JSON path） | `id`, `action`, `path` | `value`（set/insert 时需要）、`type` |

`graph_property` action: `get`（读）、`set`（覆盖）、`insert`（数组追加）、`delete`（删除）。

### 图编辑（Draft）

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `graph_update` | 更新节点/边属性 | `id`, `set` | `node`、`edge`、`selector`（6 种选择方式） |
| `graph_insert` | 插入节点/边 | `id`, `element` | `node` / `edge`（element=node\|edge）、`label`、`shape`、`from`/`to`、`parent`、`fillColor`/`strokeColor`、`x`/`y`/`w`/`h` |
| `graph_delete_element` | 删除节点/边 | `id` | `node`、`edge`、`selector` |
| `graph_layout` | 自动布局（v0.2.6 `layered`：层平衡/减交叉/waypoint，尚不完善） | `id` | `strategy`（auto\|layered\|tree-h\|tree-v\|grid）、`force`、`save` |

**颜色字段约定：**

| 字段 | 作用对象 | 说明 |
|------|----------|------|
| `fillColor` | 节点 | 填充色，如 `#eef4ff`；空=默认 |
| `strokeColor` | 节点 / 边 | 描边色，如 `#4a72b8`；空=默认 |

`graph_update` 示例：`--node A --set fillColor=#eef4ff --set strokeColor=#4a72b8`  
`graph_insert` 示例：节点带 `--fillColor`/`--strokeColor`；边带 `--strokeColor`。  
`Node.style` 仅保留线型/遗留提示，**颜色以专用字段为准**。

### 版本控制

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `graph_draft` | 草稿管理 | `id` | `action`（show\|reset\|status；默认 show） |
| `graph_stage` | 暂存管理 | `id` | `action`（add\|show\|clear）、`all`（暂存全部）、`select`（按索引选择暂存，逗号分隔） |
| `graph_commit` | 提交新版本 | `id`, `message` | `all`（跳过暂存直接提交全部 draft） |
| `graph_rollback` | 回滚旧版本为新版本 | `id`, `version` | — |
| `graph_checkout` | HEAD 移到指定版本 | `id`, `version` | `force`（丢弃未提交 draft） |

### 游标操作

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `graph_cursor_open` | 打开持久游标 | `id` | `target`（nodes\|edges，默认 nodes） |
| `graph_cursor_get` | 读取当前位置 | `id`, `cursor` | — |
| `graph_cursor_move` | 移动游标 | `id`, `cursor` | `delta`（±1，默认 +1） |
| `graph_cursor_close` | 关闭游标 | `id`, `cursor` | — |

### 通用表 CRUD 与版本

> **通用表 ≠** `create from-csv`（后者仍是边表/层级表 → Graph）。通用表是并列一等对象；交换格式为 **CSV**、**表 XML**（根 `<table>`）、**model JSON**。默认 `format=csv`。

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `table_create` | 从 CSV/表 XML/model 创建表 | `content` | `format`（默认 csv）、`name`、`id`、`force`（覆盖已存在 id）、`note` |
| `table_import` | 导入（upsert） | `content` | `format`、`name`、`id`、`note` |
| `table_export` | 导出 csv / model / xml | `id` | `to`（默认 csv）、`path`、`version` |
| `table_list` | 列出所有表 | （无） | `format`（json\|table） |
| `table_show` | 查看表 | `id` | `limit`（行数限制）、`version` |
| `table_update` | 批量补丁 | `id` | 见下方详解 |
| `table_delete` | 删除表及版本 | `id`, `force` | — |
| `table_history` | 版本历史 | `id` | — |
| `table_rollback` | 回滚（另存新版本） | `id`, `version` | — |
| `table_diff` | 版本对比 | `id` | `v1`, `v2`（0=latest）、`content`（与外部内容对比）、`format` |

**`table_update` 可选参数**：

| 参数 | 类型 | 说明 |
|------|------|------|
| `set_cells` | JSON 数组 | `[{row, column\|col_index, value}]`；旧字段 `col` 仍接受但已弃用 |
| `add_rows` | JSON 数组 | `[["c1","c2"],...]` |
| `delete_rows` | JSON 数组 | `[rowIndex,...]` |
| `add_columns` | JSON 数组 | `[{name, default?},...]` |
| `set_column_values` | JSON 对象 | `{column, values:[...]}` |
| `dry_run` | boolean | 仅预览不保存 |
| `detail` | boolean | 返回逐格 before/after |
| `note` | string | 版本备注 |

### 图↔表桥接与协作

| 工具名 | 功能 | 必填参数 | 主要可选参数 |
|--------|------|----------|-------------|
| `table_from_graph` | 图→表有损投影 | `graph_id` | `mode`（skeleton\|edgelist\|hierarchylist\|nodelist；默认 skeleton）、`with_hint_row`、`name`、`save`、`preview_rows`（默认 20） |
| `graph_from_table` | 表→图 | （见下） | `table_id` 或 `content`+`format`、`from_col`/`to_col`/`label_col`（边表）、`id_col`/`parent_col`（层级表）、`name`、`save` |
| `table_align` | 按主键跨表补齐行 | `primary_id`, `target_id`, `primary_key`, `target_key` | `note` |
| `table_check` | 枚举校验 → 违规报告 | `id` | `allowed`（JSON `{col:[values]}`）、`rules_id`、`save`、`ignore_hint_row`、`name` |
| `table_rules_from_graph` | 导图→规则表 | `graph_id` | `name`、`id`、`save` |
| `table_fix_enums` | 按 suggestion 修枚举 | `id` | `allowed` 或 `rules_id`、`ignore_hint_row`、`save`、`save_skipped`、`note` |
| `table_derive` | 表派生 | `source_id` | `mode`（仅 `animation_checklist`）、`name`、`id`、`save` |
| `table_transform_column` | 列变换 | `id`, `source_column`, `target_column` | `transform`（仅 `slug`）、`save`、`note` |
| `table_sample_rows` | 追加占位行 | `id` | `count`（默认 1）、`rules_id`、`save`、`note` |
| `table_propose_rows` | JSON 行批量写入 | `id`, `rows` | `rules_id`（含枚举校验）、`save`、`note` |

### 环境变量速查

| 环境变量 | 说明 |
|----------|------|
| `GRAPHMCP_STORE` | 存储根目录（默认 `./graph-store`） |
| `GRAPHMCP_LOG` | MCP 日志级别（`info`\|`debug`，仅 stderr） |
| `GRAPHMCP_NO_LAUNCH` | 设为 `1` 时 `edit`/`graph_open` 仅生成文件不拉起编辑器 |
| `GRAPHMCP_EDITOR` | 覆盖默认编辑器路径（`edit` 命令族） |
| `GRAPHMCP_ASSETS` | Excalidraw 资源路径（字体等），默认搜索 `<exe旁>/third_party/excalidraw-assets` |
| `GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT` | 设为 `1` 或 `true` 时 `table_create` 恢复旧 upsert 行为（无需 `force`） |
| `GRAPHMCP_TABLE_CHECK_LEGACY_HINT` | 设为 `1` 或 `true` 时 `table_check` 默认不跳过 hint 行 |

---

## 四、关键语义约束

- `table_create` 默认**不覆盖**已存在 id（同 id 需 `force=true`）；`table_import` 保留 upsert。环境变量 `GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT=1`（或 `true`）可临时恢复旧 upsert（响应含 `compat_warnings`）。
- `table_update.set_cells` 使用 `{row, column, value}` 或 `{row, col_index, value}`；旧字段 `col` 仍接受但已弃用（同文案 warning 去重）。`dry_run=true` 只预览不落盘；`detail=true` 返回逐格 before/after。
- `table_from_graph` 返回 `csv_preview` 默认仅前 20 行（截断时带 `truncated`/`hint`）；全量请用 `table_export`。skeleton：优先「子节点全为叶子」的父节点作列、子节点文案作 hint/枚举。
- `table_rules_from_graph` 与上述 skeleton 启发式一致，产物为规则表供 `table_check` / `table_fix_enums`。
- `table_fix_enums`：须提供非空 `allowed` 或 `rules_id`；`suggestion` 非空则写回；空则 `reason=empty_suggestion` 记入 skipped（可 `save_skipped`）。无写回时不 bump 目标表版本。
- `table_derive` 首版仅 `animation_checklist`（含「动画」的列 + `√` → checklist 行）。
- `table_transform_column` 仅 `slug`（去除非 ASCII，空格→下划线，空值回落 `col_<N>`）。
- `table_sample_rows` 响应含 `placeholder=true`；枚举列取首个 allowed，动画列默认 `x`，其他列填 `TODO`。
- `table_propose_rows` 可选 `rules_id` 枚举校验（非空单元格校验；非法整批拒绝）。
- `table_check` 支持 `ignore_hint_row`；当 `table.hasHintRow=true` 时默认忽略首行说明。`GRAPHMCP_TABLE_CHECK_LEGACY_HINT=1`（或 `true`）可将缺省改为不跳过 hint 行。
- **表 XML**：`format=xml` 导入根为 `<table>` 的模式 A 方言（命名字段行，见 USER_GUIDE）；`to=xml` 导出。与 `create from-xml`（图 `<graph>`）无关。
- **维护约定（抽离触发）**：当前表 XML 实现为多新增少修改（`table_xml.hpp`），与 `fromCsv` 装表样板可能少量重复且依赖 `gp::detail::parseXmlDoc`。出现以下**任一**情况时应单独开重构 PR（抽出 `xml_util` + `buildTable`），勿继续叠债：① CSV 与表 XML 在 normalize/缺列/meta 等行为漂移；② 再增加第三种表交换格式；③ 表侧需脱离 `parsers.hpp`。详见 `APPLICATION_LOGIC.md`。

---

## 五、最小示例

| 场景 | 命令 / 调用 |
|------|-------------|
| 创建 | `graphmcp create from-mermaid --file examples/example_input/flowchart.mmd --name "我的流程图"` |
| 列表 | `graphmcp store list` |
| 导出 | `graphmcp export to-svg --id <graph-id> -o output.svg` |
| 转链接 | `graphmcp convert to-url --file examples/example_input/flowchart.mmd` |
| 编辑闭环 | `graphmcp edit with-drawio --id X` → 外部修改保存 → `graphmcp import --id X` |
| 改图并提交 | `graphmcp graph update --id X --node A --set label=NewLabel` → `graphmcp version stage --id X --all` → `graphmcp version commit --id X -m "rename node"` |
| 建通用表 | `graphmcp table create --file examples/example_input/enemy_sample.csv --name enemies`（样见 `examples/example_output/enemy_sample.csv_out/`） |
| 技能表→关系图 | `graphmcp table from-table --file examples/example_input/skill_relations.csv`（样见 `examples/example_output/skill_relations.csv_out/`） |
| 导图→规则→修复 | `graphmcp table rules-from-graph --graph-id X` → `graphmcp table check --id Y --rules-id R` → `graphmcp table fix-enums --id Y --rules-id R` |
| MCP 创建 | `tools/call` → `graph_create`，参数 `content` + `name` |
| MCP 提交 | `tools/call` → `graph_commit`，参数 `id` + `message` |
| MCP 表校验 | `tools/call` → `table_check`，参数 `id` + `rules_id` |
