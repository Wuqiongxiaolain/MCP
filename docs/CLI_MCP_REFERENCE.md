# CLI & MCP 指令参考

> latest update: v0.1.1, 2026-07-14

> 命令行与 MCP 工具速查（版本以根目录 VERSION 为准）  
> 操作教程与场景说明见 [USER_GUIDE.md](USER_GUIDE.md)。

---

## 一、通用选项与格式

| 选项 | 说明 |
|------|------|
| `--file <path>` | 输入文件路径 |
| `--content <text>` | 内联输入文本 |
| `--output <path>` / `-o <path>` | 输出文件路径 |
| `--stdout` | 强制输出到标准输出 |
| `--id <graph-id>` | 图标识符 |
| `--store <dir>` | 存储目录（环境变量 `GRAPHMCP_STORE`） |

| 输入格式 | 输出格式 |
|----------|----------|
| `mermaid` / `markdown` / `csv` / `xml` | `drawio` / `mermaid` / `excalidraw` |
| `excalidraw` / `drawio` / `model` | `svg` / `png` / `pdf` |
| `auto`（自动检测） | `url`（mermaid.live）/ `model`（JSON） |

---

## 二、CLI 命令族

### `create` — 创建并入库

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `from-mermaid` | 从 Mermaid 创建 | `--file` / `--content`、`--name`、`--id` |
| `from-markdown` | 从 Markdown 大纲创建 | `--type`、`--format` |
| `from-csv` | 从 CSV 创建 | `--type`（flowchart\|architecture\|er\|orgchart\|mindmap\|whiteboard） |
| `from-xml` | 从 XML 创建 | `--no_validate`、`--no_layout`、`--note` |
| `from-excalidraw` | 从 Excalidraw JSON 创建 | |
| `from-model` | 从统一模型 JSON 创建 | |
| `from-input` | 自动检测格式创建 | |

示例：`graphmcp create from-mermaid --file flow.mmd --name "登录流程"`

### `convert` — 格式转换（不存储）

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `to-drawio` / `to-mermaid` / `to-excalidraw` | 转为对应文本/JSON | `--file`、`--input-format` |
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
| `with-browser` | 打开 mermaid.live | `--id`、`--version` |
| `with-drawio` | 用 Draw.io 打开 | `--editor-path`（可显式指定；默认可自动发现） |
| `with-excalidraw` | 打开 `.excalidraw` | |
| `with-svg` | 打开 `.svg` | |

设置 `GRAPHMCP_NO_LAUNCH=1` 时只生成目标 URL/文件，不实际拉起外部程序（单元测试与无头 CI 使用）。

### `import` — 编辑回导

| 用法 | 说明 | 常用选项 |
|------|------|----------|
| `import --id <id>` | 自动探测 `open.*` 回导 | `--format`（drawio\|excalidraw\|mermaid\|…\|auto） |
| `import --id <id> --file <path>` | 指定文件回导 | `--content` + `--format` |

编辑闭环：`edit with-drawio --id X` → 外部保存 → `import --id X` → 版本 +1

### `layout` — 自动布局

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `auto` | 自动选择策略 | `--id`、`--save`（保存为新版本） |
| `layered` | 分层（流程图） | |
| `tree-h` / `tree-v` | 水平/垂直树 | |
| `grid` | 网格 | |

### `validate` — 结构校验

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `graph` | 校验已存储图 | `--id` |
| `input` | 校验输入文件 | `--file`、`--input-format`、`--strict` |

检查项：重复 ID、悬空边、层次循环、孤立节点。

### `store` — 存储管理

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `list` | 列出图 | `--type`、`--format json` |
| `show` | 摘要 | `--id` |
| `load` | 完整 JSON → stdout | `--id` |
| `delete` | 删除（不可逆） | `--id`、`--force` |

### `version` — 版本控制

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `status` | HEAD / draft / stage | `<id>` |
| `draft show` / `draft reset` | 查看 / 丢弃草稿 | |
| `stage` / `stage add` / `stage show` / `stage clear` | 暂存区 | |
| `commit` | 提交新版本 | `-m`、`--all`（跳过暂存） |
| `log` | 历史 | `--limit`、`--format json\|oneline` |
| `show` | 版本详情 | `--version <n>` |
| `diff` | 两版本对比 | `<v1> <v2>`、`--format json` |
| `checkout` | HEAD 移到指定版本 | `--force`（丢弃 draft） |

### `graph` — 图编辑（Draft）

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `show` | 查看图/节点/边 | `--node` / `--edge`、`--format json` |
| `update` | 更新属性 | `--node` / `--edge` / `--selector`、`--set key=val` |
| `insert` | 插入节点或边 | `--node` / `--edge`、`--from`/`--to`、`--label` |
| `delete` | 删除（节点级联删边） | `--node` / `--edge` / `--selector` |

### `cursor` — 游标遍历

| 子命令 | 说明 | 常用选项 |
|--------|------|----------|
| `open` | 打开游标 | `--target nodes\|edges` |
| `get` / `next` / `prev` | 读当前位置 / 移动 | `--cursor <cid>` |
| `close` | 关闭游标 | `--cursor <cid>` |

### `draft` — 草稿快捷入口

| 子命令 | 说明 |
|--------|------|
| `status` | 相对最新版本的增/删/改统计 |
| `discard` | 丢弃未提交更改（不可逆） |

### `serve` — MCP 服务

| 命令 | 说明 | 环境变量 |
|------|------|----------|
| `graphmcp serve` | JSON-RPC 2.0 over stdio | `GRAPHMCP_LOG=info\|debug`（日志仅 stderr） |

### `dump-tools` — 导出工具契约

| 命令 | 说明 |
|------|------|
| `graphmcp dump-tools [--format openapi\|json] [-o path]` | 从运行中的 `toolList()` 导出 OpenAPI YAML（默认）或原始 JSON；`make docs-api` 写入 `docs/api_reference/openapi.yaml` |

客户端配置见仓库根目录 `mcp-config.example.json`。

---

## 三、MCP 工具（47 个）

参数与 CLI 对应；通过 `tools/call` 调用。

### 图生命周期

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_create` | 解析 → 校验 → 布局 → 存储 | `content` |
| `graph_convert` | 格式直转（不存储） | `content`, `to` |
| `graph_export` | 导出已存储图 | `id`, `to` |
| `graph_open` | 外部编辑器打开（`launch=false` 仅生成目标） | `id` |
| `graph_import` | 编辑回导 | `id` |
| `graph_validate` | 结构校验 | `id` 或 `content` |
| `graph_list` | 列出所有图 | （无） |
| `graph_delete` | 删除图及所有版本 | `id`, `force` |

### 图查看

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_show` | 查看图/节点/边 | `id` |
| `graph_history` | 版本历史 | `id` |
| `graph_diff` | 两版本对比 | `id`, `v1`, `v2` |
| `graph_status` | 工作树状态 | `id` |
| `graph_property` | 读写图级 properties（结构化 Mermaid 扩展数据） | `id`, `action` |

### 图编辑（Draft）

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_update` | 更新节点/边属性（含 `fillColor`/`strokeColor`） | `id`, `set` |
| `graph_insert` | 插入节点/边（可选 `fillColor`/`strokeColor`） | `id`, `element` |
| `graph_delete_element` | 删除节点/边 | `id` |
| `graph_layout` | 自动布局 | `id` |

**颜色字段约定：**

| 字段 | 作用对象 | 说明 |
|------|----------|------|
| `fillColor` | 节点 | 填充色，如 `#eef4ff`；空=默认 |
| `strokeColor` | 节点 / 边 | 描边色，如 `#4a72b8`；空=默认 |

`graph_update` 示例：`--node A --set fillColor=#eef4ff --set strokeColor=#4a72b8`  
`graph_insert` 示例：节点带 `--fillColor`/`--strokeColor`；边带 `--strokeColor`。  
`Node.style` 仅保留线型/遗留提示，**颜色以专用字段为准**。

### 版本控制

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_draft` | 草稿（show/reset/status） | `id` |
| `graph_stage` | 暂存（add/show/clear） | `id` |
| `graph_commit` | 提交新版本 | `id`, `message` |
| `graph_rollback` | 回滚到旧版本 | `id`, `version` |
| `graph_checkout` | HEAD 移到指定版本 | `id`, `version` |

### 游标操作

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_cursor_open` | 打开持久游标 | `id` |
| `graph_cursor_get` | 读取当前位置 | `id`, `cursor` |
| `graph_cursor_move` | 移动游标（delta±1） | `id`, `cursor` |
| `graph_cursor_close` | 关闭游标 | `id`, `cursor` |

### 通用表（CSV / 表 XML）与图↔表协作

> **通用表 ≠** `create from-csv`（后者仍是边表/层级表 → Graph）。通用表是并列一等对象；交换格式为 **CSV** 与 **表 XML**（根 `<table>`，与图 XML 根 `<graph>` 无关）；仓库内为 JSON。不实现 Excel 全量读写。默认 `format=csv`。

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `table_create` | 从 CSV / 表 XML / model 创建表并入库 | `content`（可选 `format`） |
| `table_import` | 导入 CSV / 表 XML / model（可指定 id） | `content`（可选 `format`） |
| `table_export` | 导出 csv / model / xml | `id` |
| `table_list` | 列出表 | （无） |
| `table_show` | 查看表 | `id` |
| `table_update` | 批量补丁并返回变更摘要 | `id` |
| `table_delete` | 删除表 | `id`, `force` |
| `table_history` / `table_rollback` / `table_diff` | 版本与差异 | 见 schema |
| `table_from_graph` | 图→表有损投影（skeleton/edgelist/…） | `graph_id` |
| `graph_from_table` | 表→图（边/层级列或映射列） | `table_id` 或 `content` |
| `table_align` | 按主键跨表补行 | `primary_id`, `target_id`, keys |
| `table_check` | 枚举校验 → 违规清单表 | `id` |
| `table_rules_from_graph` | 导图→规则表 `column/allowed/hint` | `graph_id` |
| `table_fix_enums` | 按 check suggestion 自动修枚举 | `id` |
| `table_derive` | 表派生（`animation_checklist`） | `source_id` |
| `table_transform_column` | 列变换（`slug`） | `id`, columns |
| `table_sample_rows` | 追加占位样例行 | `id` |
| `table_propose_rows` | 结构化对象行写入 | `id`, `rows` |

CLI：`graphmcp table create|import|export|list|show|update|delete|history|rollback|from-graph|from-table|align|check|rules-from-graph|fix-enums|derive|transform-column|sample-rows|propose-rows`

关键语义约束：

- `table_create` 默认**不覆盖**已存在 id（同 id 需 `force=true`）；`table_import` 保留 upsert。环境变量 `GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT=1`（或 `true`）可临时恢复旧 upsert（响应含 `compat_warnings`）。
- `table_update.set_cells` 使用 `{row,column,value}` 或 `{row,col_index,value}`；旧字段 `col` 仍接受但已弃用（同文案 warning 去重）。`dry_run=true` 只预览不落盘；`detail=true` 返回逐格 before/after。CLI `table update --dry-run` 在存在兼容告警时会额外打印 `compat_warnings`。
- `table_from_graph` 返回 `csv_preview` 默认仅前 20 行（截断时带 `truncated`/`hint`）；全量请用 `table_export`。skeleton：优先「子节点全为叶子」的父节点作列、子节点文案作 hint/枚举。
- `table_rules_from_graph` 与上述 skeleton 启发式一致，产物为规则表供 `table_check` / `table_fix_enums`。
- `table_fix_enums`：须提供非空 `allowed` 或 `rules_id`（CLI：`--allowed`/`--rules-id`）；`suggestion` 非空则写回；空则 `reason=empty_suggestion` 记入 skipped（可 `save_skipped`）。无写回时不 bump 目标表版本；CLI 可用 `--no-save`。
- `table_derive` 首版仅 `animation_checklist`；`table_transform_column` 仅 `slug`。
- `table_sample_rows` 响应含 `placeholder=true`；`table_propose_rows` 可选 `rules_id` 枚举校验（非法整批拒绝）。
- `table_check` 支持 `ignore_hint_row`；当 `table.hasHintRow=true` 时默认忽略首行说明。`GRAPHMCP_TABLE_CHECK_LEGACY_HINT=1`（或 `true`）可将缺省改为不跳过 hint 行。CLI 可用 `--ignore-hint-row=false` 显式关闭。
- **表 XML**：`format=xml` 导入根为 `<table>` 的模式 A 方言（命名字段行，见 USER_GUIDE）；`to=xml` 导出。与 `create from-xml`（图）无关。
- **维护约定（抽离触发）**：当前表 XML 实现为多新增少修改（`table_xml.hpp`），与 `fromCsv` 装表样板可能少量重复且依赖 `gp::detail::parseXmlDoc`。出现以下**任一**情况时应单独开重构 PR（抽出 `xml_util` + `buildTable`），勿继续叠债：① CSV 与表 XML 在 normalize/缺列/meta 等行为漂移；② 再增加第三种表交换格式；③ 表侧需脱离 `parsers.hpp`。详见 `APPLICATION_LOGIC.md`。

---

## 四、最小示例

| 场景 | 命令 / 调用 |
|------|-------------|
| 创建 | `graphmcp create from-mermaid --file examples/example_input/flowchart.mmd --name "我的流程图"` |
| 列表 | `graphmcp store list` |
| 导出 | `graphmcp export to-svg --id <graph-id> -o output.svg` |
| 转链接 | `graphmcp convert to-url --file examples/example_input/flowchart.mmd` |
| 改图并提交 | `graph update …` → `version stage` → `version commit -m "…"` |
| 建通用表 | `graphmcp table create --file examples/example_input/enemy_sample.csv --name enemies`（样见 `examples/example_output/enemy_sample.csv_out/`） |
| 技能表→关系图 | `graphmcp table from-table --file examples/example_input/skill_relations.csv`（样见 `examples/example_output/skill_relations.csv_out/`） |
| 导图→规则→修复 | `table rules-from-graph` → `table check` → `table fix-enums` |
| MCP 创建 | `tools/call` → `graph_create`，参数 `content` + `name` |
| MCP 提交 | `tools/call` → `graph_commit`，参数 `id` + `message` |
