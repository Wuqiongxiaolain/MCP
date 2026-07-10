# CLI & MCP 指令参考

> graphmcp 0.1.0 — 命令行与 MCP 工具速查（表格版）  
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

客户端配置见仓库根目录 `mcp-config.example.json`。

---

## 三、MCP 工具（25 个）

参数与 CLI 对应；通过 `tools/call` 调用。

### 图生命周期

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_create` | 解析 → 校验 → 布局 → 存储 | `content` |
| `graph_convert` | 格式直转（不存储） | `content`, `to` |
| `graph_export` | 导出已存储图 | `id`, `to` |
| `graph_open` | 外部编辑器打开 | `id` |
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

### 图编辑（Draft）

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_update` | 更新节点/边属性 | `id`, `set` |
| `graph_insert` | 插入节点/边 | `id`, `element` |
| `graph_delete_element` | 删除节点/边 | `id` |
| `graph_layout` | 自动布局 | `id` |

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

---

## 四、最小示例

| 场景 | 命令 / 调用 |
|------|-------------|
| 创建 | `graphmcp create from-mermaid --file examples/example_input/flowchart.mmd --name "我的流程图"` |
| 列表 | `graphmcp store list` |
| 导出 | `graphmcp export to-svg --id <graph-id> -o output.svg` |
| 转链接 | `graphmcp convert to-url --file examples/example_input/flowchart.mmd` |
| 改图并提交 | `graph update …` → `version stage` → `version commit -m "…"` |
| MCP 创建 | `tools/call` → `graph_create`，参数 `content` + `name` |
| MCP 提交 | `tools/call` → `graph_commit`，参数 `id` + `message` |
