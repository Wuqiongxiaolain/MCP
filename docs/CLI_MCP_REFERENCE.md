# CLI & MCP 指令参考

> graphmcp 1.1.0 — 完整命令行与 MCP 工具接口文档

---

## 一、CLI 指令（12 个命令族）

### 通用选项

| 选项 | 说明 |
|------|------|
| `--file <path>` | 输入文件路径 |
| `--content <text>` | 内联输入文本 |
| `--output <path>` / `-o <path>` | 输出文件路径 |
| `--stdout` | 强制输出到标准输出 |
| `--id <graph-id>` | 图标识符 |
| `--store <dir>` | 存储目录（环境变量 `GRAPHMCP_STORE`） |

### 输入/输出格式

| 输入格式 | 输出格式 |
|----------|----------|
| `mermaid` | `drawio` |
| `markdown` | `mermaid` |
| `csv` | `excalidraw` |
| `xml` | `svg` |
| `excalidraw` | `png` |
| `drawio` | `pdf` |
| `model` (JSON) | `url` (mermaid.live 编辑链接) |
| `auto` (自动检测) | `model` (JSON) |
| | `model` (JSON) |

---

### `create` — 从各种格式创建图

解析结构化图表内容，可选校验与自动布局，保存到版本化存储。

```bash
graphmcp create from-mermaid    --file flow.mmd --name "登录流程"
graphmcp create from-markdown   --file mindmap.md --name "思维导图"
graphmcp create from-csv        --file org.csv --type orgchart
graphmcp create from-xml        --file arch.xml
graphmcp create from-excalidraw --file whiteboard.excalidraw
graphmcp create from-model      --file model.json
graphmcp create from-input      --file any.format    # 自动检测格式
graphmcp create from-mermaid    --content "flowchart TD\nA-->B" --name "内联"
```

选项：`--name`、`--id`、`--type`（flowchart|architecture|er|orgchart|mindmap|whiteboard）、`--format`、`--no_validate`、`--no_layout`、`--note`

---

### `convert` — 格式转换（不存储）

一次性格式转换，不写入存储。

```bash
graphmcp convert to-drawio     --file flow.mmd -o out.drawio
graphmcp convert to-mermaid    --file mindmap.md --stdout
graphmcp convert to-excalidraw --file flow.mmd -o out.excalidraw
graphmcp convert to-svg        --file flow.mmd -o out.svg
graphmcp convert to-png        --file flow.mmd -o out.png
graphmcp convert to-pdf        --file flow.mmd -o out.pdf
graphmcp convert to-url        --file flow.mmd     # mermaid.live 编辑链接
graphmcp convert to-model      --file flow.mmd     # JSON 模型输出
```

选项：`--input-format`、`--output` / `-o`、`--stdout`

---

### `export` — 导出已存储的图

从存储中读取图并导出为指定格式。

```bash
graphmcp export to-drawio     --id my-graph -o out.drawio
graphmcp export to-mermaid    --id my-graph -o out.mmd
graphmcp export to-excalidraw --id my-graph -o out.excalidraw
graphmcp export to-svg        --id my-graph -o out.svg
graphmcp export to-png        --id my-graph -o out.png
graphmcp export to-pdf        --id my-graph -o out.pdf
graphmcp export to-url        --id my-graph          # mermaid.live 链接
graphmcp export to-model      --id my-graph --stdout # JSON 模型
```

选项：`--version <n>`（指定版本，默认最新）

---

### `edit` — 外部编辑器打开

生成文件并使用系统默认程序打开。

```bash
graphmcp edit with-browser    --id my-graph  # mermaid.live 浏览器链接
graphmcp edit with-drawio     --id my-graph  # .drawio 文件
graphmcp edit with-excalidraw --id my-graph  # .excalidraw 文件
graphmcp edit with-svg        --id my-graph  # .svg 文件
```

选项：`--version <n>`、`--editor-path <path>`（显式指定编辑器，自动发现 draw.io / VS Code）

---

### `import` — 编辑回导

重新导入外部编辑后的文件，解析、校验、入库为新版本。

```bash
graphmcp import --id my-graph                     # 自动探测 open.* 文件
graphmcp import --id my-graph --file edited.drawio  # 指定文件
graphmcp import --id my-graph --content "flowchart TD\nX-->Y" --format mermaid
```

选项：`--format <fmt>`（drawio|excalidraw|mermaid|markdown|csv|xml|auto，默认 auto）

> **编辑闭环**：`edit with-drawio --id X` → 在 draw.io 中编辑保存 → `import --id X` → 图库版本 +1

---

### `layout` — 自动布局

对已存储图应用自动布局算法。

```bash
graphmcp layout auto    --id my-graph          # 自动选择策略
graphmcp layout layered --id my-graph           # 分层布局（流程图）
graphmcp layout tree-h  --id my-graph           # 水平树（思维导图）
graphmcp layout tree-v  --id my-graph           # 垂直树（组织架构图）
graphmcp layout grid    --id my-graph           # 网格布局
graphmcp layout auto    --id my-graph --save    # 保存为新版本
```

---

### `validate` — 结构校验

校验图结构：重复 ID、悬空边、层次循环、孤立节点。

```bash
graphmcp validate graph --id my-graph
graphmcp validate input --file flow.mmd --input-format mermaid
graphmcp validate input --file flow.mmd --strict   # warning → error
```

返回 `valid: no issues found` 或问题列表（`[error]` / `[warning]`）。

---

### `store` — 存储管理

列出、查看、导出、删除存储中的图。

```bash
graphmcp store list                          # 所有图
graphmcp store list --type architecture      # 按类型过滤
graphmcp store list --format json            # JSON 格式
graphmcp store show --id my-graph            # 摘要信息
graphmcp store load --id my-graph            # 完整 JSON 到 stdout
graphmcp store delete --id my-graph --force  # 不可逆删除
```

---

### `version` — 版本控制

Draft → Stage → Commit 工作流，支持历史回溯与版本对比。

```bash
# 工作树状态
graphmcp version status my-graph              # HEAD / draft / stage 状态

# 草稿
graphmcp version draft show my-graph          # 查看待处理操作
graphmcp version draft reset my-graph         # 丢弃所有未提交更改

# 暂存区
graphmcp version stage my-graph               # 暂存所有 draft 操作
graphmcp version stage add my-graph           # 同上
graphmcp version stage show my-graph          # 查看暂存内容
graphmcp version stage clear my-graph         # 清空暂存区

# 提交
graphmcp version commit my-graph -m "优化流程"      # 提交暂存区
graphmcp version commit my-graph -m "直接提交" --all # 跳过暂存，提交全部 draft

# 历史
graphmcp version log my-graph                          # 完整历史
graphmcp version log my-graph --limit 5                # 最近 5 条
graphmcp version log my-graph --format json            # JSON 格式
graphmcp version log my-graph --format oneline         # 单行摘要

# 查看与对比
graphmcp version show my-graph                         # 最新版本详情
graphmcp version show my-graph --version 3             # 指定版本
graphmcp version diff my-graph 1 3                     # v1 vs v3
graphmcp version diff my-graph 1 3 --format json       # JSON diff

# 回滚
graphmcp version checkout my-graph 2                   # HEAD → v2（需 clean draft）
graphmcp version checkout my-graph 2 --force           # 强制（丢弃 draft）
```

---

### `graph` — 图编辑（Draft 模式）

对图进行节点/边的增删改操作，变更写入 draft。

```bash
# 查看
graphmcp graph show my-graph                           # 完整结构
graphmcp graph show my-graph --node A                  # 节点详情
graphmcp graph show my-graph --edge e1                 # 边详情
graphmcp graph show my-graph --format json             # JSON 格式

# 更新
graphmcp graph update my-graph --node A --set label="开始"
graphmcp graph update my-graph --node A --set shape=diamond
graphmcp graph update my-graph --edge e1 --set style=dashed
graphmcp graph update my-graph --selector "shape=rect" --set shape=round  # 批量

# 插入
graphmcp graph insert my-graph --node --type rect --label "新节点" --position 400 200
graphmcp graph insert my-graph --node --type diamond --label "判断" --size 120 60 --parent g1
graphmcp graph insert my-graph --edge --from A --to B --label "连接" --style dashed

# 删除（级联：删节点会同时删关联边）
graphmcp graph delete my-graph --node X
graphmcp graph delete my-graph --edge e3
graphmcp graph delete my-graph --selector "shape=group"  # 批量
```

---

### `cursor` — 游标遍历

持久化游标，支持逐元素遍历图的 nodes 或 edges。

```bash
graphmcp cursor open  my-graph                     # 打开 nodes 游标
graphmcp cursor open  my-graph --target edges      # 打开 edges 游标
graphmcp cursor get   my-graph --cursor <cid>      # 读取当前位置
graphmcp cursor next  my-graph --cursor <cid>      # 下一条
graphmcp cursor prev  my-graph --cursor <cid>      # 上一条
graphmcp cursor close my-graph --cursor <cid>      # 关闭游标
```

---

### `draft` — 草稿管理

```bash
graphmcp draft status  my-graph    # 草稿状态（与最新版本的增/删/改统计）
graphmcp draft discard my-graph    # 丢弃所有未提交更改（不可逆）
```

---

### `serve` — MCP 服务模式

启动 JSON-RPC 服务端，通过 stdin/stdout 与 MCP 客户端通信。

```bash
graphmcp serve
```

MCP 客户端配置示例（`mcp-config.example.json`）：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "graphmcp",
      "args": ["serve"]
    }
  }
}
```

---

## 二、MCP 工具（23 个）

所有工具通过 `tools/call` 方法调用，参数与 CLI 对应。

### 图生命周期

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_create` | 解析 → 校验 → 布局 → 存储 | `content` |
| `graph_convert` | 格式直转（不存储） | `content`, `to` |
| `graph_export` | 导出已存储图 | `id`, `to` |
| `graph_open` | 外部编辑器打开（自动发现编辑器） | `id` |
| `graph_import` | 重新导入外部编辑后的文件 | `id` |
| `graph_validate` | 结构校验 | (id 或 content) |
| `graph_list` | 列出所有图 | (无) |
| `graph_delete` | 删除图及所有版本 | `id`, `force` |

### 图查看

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_show` | 查看图/节点/边详情 | `id` |
| `graph_history` | 版本历史 | `id` |
| `graph_diff` | 两版本对比 | `id`, `v1`, `v2` |
| `graph_status` | 工作树状态 | `id` |

### 图编辑（Draft 模式）

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_update` | 更新节点/边属性 | `id`, `set` |
| `graph_insert` | 插入节点/边 | `id`, `element` |
| `graph_delete_element` | 删除节点/边 | `id` |
| `graph_layout` | 自动布局 | `id` |

### 版本控制

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_draft` | 草稿管理（show/reset/status） | `id` |
| `graph_stage` | 暂存区管理（add/show/clear） | `id` |
| `graph_commit` | 提交为新版本 | `id`, `message` |
| `graph_rollback` | 回滚到旧版本 | `id`, `version` |
| `graph_checkout` | HEAD 移动到指定版本 | `id`, `version` |

### 游标操作

| 工具名 | 功能 | 必填参数 |
|--------|------|----------|
| `graph_cursor_open` | 打开持久游标 | `id` |
| `graph_cursor_get` | 读取当前位置 | `id`, `cursor` |
| `graph_cursor_move` | 移动游标（delta±1） | `id`, `cursor` |
| `graph_cursor_close` | 关闭游标 | `id`, `cursor` |

---

## 三、常用示例

### 快速上手

```bash
# 从 Mermaid 创建图
graphmcp create from-mermaid --file examples/example_input/flowchart.mmd --name "我的流程图"

# 查看存储列表
graphmcp store list

# 导出为 SVG
graphmcp export to-svg --id <graph-id> -o output.svg

# 导出为 Draw.io
graphmcp export to-drawio --id <graph-id> -o output.drawio

# 生成 mermaid.live 在线编辑链接
graphmcp convert to-url --file examples/example_input/flowchart.mmd
```

### 版本工作流

```bash
# 修改图
graphmcp graph update my-graph --node A --set label="优化后的步骤"

# 暂存并提交
graphmcp version stage my-graph
graphmcp version commit my-graph -m "优化节点 A 标签"

# 查看历史
graphmcp version log my-graph

# 对比版本
graphmcp version diff my-graph 1 2

# 回滚
graphmcp version checkout my-graph 1
```

### MCP 调用示例

```json
// graph_create
{"method": "tools/call", "params": {
  "name": "graph_create",
  "arguments": {
    "content": "flowchart TD\nA[开始]-->B[处理]-->C[结束]",
    "name": "简单流程"
  }
}}

// graph_commit
{"method": "tools/call", "params": {
  "name": "graph_commit",
  "arguments": {
    "id": "<graph-id>",
    "message": "初始版本"
  }
}}
```
