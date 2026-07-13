# graphmcp 应用运作逻辑详解

> latest update: v0.1.1, 2026-07-10

> 应用运作逻辑说明（版本以根目录 VERSION 为准）  
> 下文已对照当前源码核对；旧版 `ARCHITECTURE.md` 中过时条目未照搬。

---

## 一、整体架构总览

graphmcp 是一个 **C++17 单可执行文件**，零第三方依赖。核心设计哲学：**所有格式归一为统一图模型，再从模型出发做所有操作**。

```
┌──────────┐    ┌──────────────┐    ┌─────────────────────────────────┐    ┌──────────┐
│  用户     │───▶│  AI 客户端    │───▶│       graphmcp MCP 服务器         │───▶│  存储层   │
│ (自然语言) │◀───│ (Claude等)   │◀───│  (解析→模型→校验→布局→导出→版本)    │◀───│ (文件系统) │
└──────────┘    └──────────────┘    └─────────────────────────────────┘    └──────────┘
```

| 层次 | 组件 | 职责 |
|------|------|------|
| 用户层 | 人 | 用自然语言描述图形需求 |
| AI 层 | Claude Code / Desktop 等 | 理解意图，调用 MCP 工具 |
| 协议层 | JSON-RPC 2.0 over stdio | AI 与 MCP 服务器之间的通信协议 |
| 服务层 | `graphmcp serve` | 接收工具调用，分发到内部模块 |
| 逻辑层 | 解析 / 模型 / 校验布局 / 导出 / 存储 / 版本与游标 | 见 §7.2 |
| 存储层 | 文件系统 (`graph-store/`) | JSON 持久化 |

---

## 二、MCP 服务器设计

### 2.1 协议实现

| 特性 | 实现方式 |
|------|---------|
| 协议 | JSON-RPC 2.0 |
| 传输 | stdio（行分隔） |
| 实现文件 | `src/mcp.hpp` |
| 服务版本 | `SERVER_VERSION（读取根目录 VERSION）` |
| 启动方式 | `graphmcp serve` |
| 存储路径 | 环境变量 `GRAPHMCP_STORE`，默认 `./graph-store` |

### 2.2 MCP 协议握手流程

| 步骤 | 方向 | 消息类型 | 内容 |
|------|------|---------|------|
| 1 | AI → 服务器 | `initialize` 请求 | 客户端能力声明 |
| 2 | 服务器 → AI | `initialize` 响应 | 服务器能力 + 协议/服务版本 |
| 3 | AI → 服务器 | `initialized` 通知 | 握手完成（通知不回包） |
| 4 | AI → 服务器 | `tools/list` 请求 | 查询可用工具 |
| 5 | 服务器 → AI | `tools/list` 响应 | **39** 个工具定义 |
| 6 | AI → 服务器 | `tools/call` 请求 | 调用具体工具 |
| 7 | 服务器 → AI | `tools/call` 响应 | 结果文本；异常时 `isError: true` |

### 2.3 MCP 工具（39 个，`listTools()`）

完整参数速查见 [CLI_MCP_REFERENCE.md](CLI_MCP_REFERENCE.md)。按职责分组：

| 分组 | 工具 |
|------|------|
| 生命周期 | `graph_create`、`graph_convert`、`graph_export`、`graph_open`、`graph_import`、`graph_validate`、`graph_list`、`graph_delete` |
| 查看 | `graph_show`、`graph_history`、`graph_diff`、`graph_status` |
| Draft 编辑 | `graph_update`、`graph_insert`、`graph_delete_element`、`graph_layout` |
| 版本 | `graph_draft`、`graph_stage`、`graph_commit`、`graph_rollback`、`graph_checkout` |
| 游标 | `graph_cursor_open`、`graph_cursor_get`、`graph_cursor_move`、`graph_cursor_close` |
| 通用表 | `table_create`、`table_import`、`table_export`、`table_list`、`table_show`、`table_update`、`table_delete`、`table_history`、`table_rollback`、`table_diff` |
| 图↔表 | `table_from_graph`、`graph_from_table`、`table_align`、`table_check` |

注意：`graph_rollback` 走 `Store::rollback`——把旧快照**再 save 成新版本**（版本号递增），与 `graph_checkout`（只改 `HEAD`）不同，见 §十。

通用表存储在 `graph-store/tables/`（`index.json` + `<tableId>/latest.json` + `versions/`），与图目录并列；CSV 为交换格式，**不是**边表转图的 `parseCSV` 路径。图↔表桥接为**有损投影**（见工具 `mode` / 列映射说明）。
---

## 三、存储层（数据库）设计

graphmcp 不使用传统数据库，而是**基于文件系统的 JSON 存储**。

### 3.1 目录结构

```
graph-store/                      ← 根目录（由 GRAPHMCP_STORE 指定）
├── index.json                    ← 全局索引：所有图的目录
├── tables/                       ← 通用 CSV 表（与图并列）
│   ├── index.json
│   └── <tableId>/
│       ├── HEAD
│       ├── latest.json
│       └── versions/vN.json
└── <图ID>/                       ← 每张图一个子目录
    ├── HEAD                      ← 纯文本，当前版本号
    ├── latest.json               ← 最近一次 save/commit 写入的完整模型
    ├── draft.json                ← 未提交修改操作
    ├── stage.json                ← 已暂存待提交操作
    ├── open.drawio / open.svg…   ← edit / graph_open 生成的临时编辑文件
    ├── cursors/                  ← 持久化游标状态（若使用）
    └── versions/
        ├── v1.json               ← 不可变快照 {version, savedAt, note, model, …}
        └── ...
```

### 3.2 各文件职责

| 文件 | 格式 | 读写特征 | 说明 |
|------|------|---------|------|
| `index.json` | JSON | 频繁读写 | 每图 id、name、type、versions、时间戳 |
| `HEAD` | 纯文本 | 读写 | 单行版本号；`save` / `commit` / `checkout` 会更新 |
| `latest.json` | JSON | 读写 | **仅**在 `Store::save`（含 create/rollback/import/commit 落盘）时重写；`checkout` **不**改写它 |
| `draft.json` / `stage.json` | JSON | 读写 | Draft / Stage 工作流 |
| `versions/vN.json` | JSON | 只追加 | 不可变快照 |

### 3.3 存储操作对照

| 操作 | CLI 命令 | 存储层行为 |
|------|---------|-----------|
| 创建图 | `create from-*` | 写 `index.json`、子目录、`latest.json`、`versions/v1.json`、`HEAD` |
| 修改元素 | `graph update/insert/delete` | 写入 `draft.json`，不直接改 `latest` |
| 暂存 | `version stage` | draft → stage |
| 提交 | `version commit` | 应用 stage → `Store::save`（新快照 + 更新 `latest` + `HEAD`）→ 清空 stage |
| 导出 | `export to-*` | 读 `latest` 或指定 `versions/vN` |
| 切换 HEAD | `version checkout` | 写 `HEAD`；清空 draft/stage；**不**新建版本、**不**改 `latest.json` |
| 旧式回滚 | `rollback` / `graph_rollback` | `load(旧版本)` 再 `save` → 新版本号 + 新 `latest` |
| 删除 | `store delete` | 删子目录 + 更新索引 |

---

## 四、文件导入导出设计

### 4.1 设计原则

> **N 种输入 × M 种输出 ≠ N×M 个转换器**，而是通过统一图模型实现 **N + M** 的复杂度。

```
任意输入格式  ──parse──▶  统一图模型 (Graph)  ──export──▶  任意输出格式
    Mermaid        parsers.hpp    model.hpp        exporters.hpp     Drawio
    Markdown           ↓                             SVG / PNG / PDF
    CSV             解析器族                          Excalidraw / URL
    XML / Drawio                                     统一模型 JSON
    Excalidraw JSON
```

### 4.2 输入格式 → 解析器

| 输入格式 | 解析函数 | 关键解析能力 |
|---------|---------|-------------|
| **Mermaid** | `parseMermaid()` | 仅 **flowchart / mindmap / erDiagram**；其它类型抛 `ParseError`。词法含节点形状括号、多种箭头、subgraph 栈 |
| **Markdown** | `parseMarkdownOutline()` | 标题/列表层级 → mindmap 树 |
| **CSV** | `parseCSV()` | 边表 / 层级表自动识别 |
| **XML** | `parseXML()` | 迷你 XML，根须为 `<graph>` |
| **Excalidraw** | `parseExcalidraw()` | elements → 逻辑节点/边，并保留原始元素 |
| **Drawio** | `parseDrawio()` | mxCell → Graph |
| **统一模型** | `parseModel()` / `detectFormat` | Graph JSON |

### 4.3 输出格式 → 导出器

| 输出格式 | 关键能力 |
|---------|---------|
| **Drawio** | 子节点相对坐标、mxCell |
| **Mermaid** | 形状转义、subgraph 还原 |
| **Excalidraw** | 原始 elements 优先（无损往返）、变换、freedraw |
| **SVG** | 边裁剪到节点边界等 |
| **PNG / PDF** | 先 SVG，再走外部转换器链（§4.4） |
| **URL** | Mermaid Base64 → mermaid.live |
| **model** | Graph JSON |

### 4.4 PNG/PDF 渲染回退链

实现见 `exporters.hpp`（栅格化候选与导出路径）：

```
inkscape → rsvg-convert → magick
→ Chrome/Edge 无头（独立 --user-data-dir）
→ 仍失败则写出 .svg 回退文件并提示（不中断为硬失败）
```

| 转换器 | 说明 |
|--------|------|
| Inkscape / rsvg-convert / ImageMagick | PATH 中可用则优先 |
| Chrome/Edge 无头 | Windows 常预装；独立 user-data-dir 避免附着已有浏览器实例 |
| SVG 回退 | 始终可写；消息说明需手动转换或安装转换器 |

---

## 五、统一图模型——系统的心脏

### 5.1 Graph 数据结构

```
Graph {
  id, name, type    // flowchart|architecture|er|orgchart|mindmap|whiteboard
  nodes[]           // id, label, shape, parent, style, attrs[], x,y,w,h
  edges[]           // id, from, to, label, style(solid|dashed|thick), arrow(arrow|none|both)
  elements[]        // Excalidraw 原始元素（白板无损保留）
  files             // Excalidraw 附件（若有）
  laidOut           // 坐标是否已布局
}
```

### 5.2 节点类型与形状

| type | shape 取值（典型） | 用途 |
|------|-------------------|------|
| flowchart | rect, diamond, round, … | 流程 |
| architecture | rect, cylinder, cloud, … | 架构 |
| er | rect + `attrs` 行 | 实体表 |
| orgchart / mindmap | rect, round | 组织 / 脑图 |
| whiteboard | 自由绘制相关 shape + elements | 白板 |

### 5.3 层级结构

`node.parent` 同时服务：流程图 subgraph、脑图/组织树、架构容器嵌套。

### 5.4 ER 关系（代码行为）

`parseMermaidER` 对 `A ||--o{ B : label` 一类语句：左右实体建节点，边使用 `arrow="none"`，基数符号不单独建模，业务标签留在 `label`。

---

## 六、版本管理系统——类 Git 工作流

### 6.1 状态流转

```
                    graph update/insert/delete
    [HEAD]  ──────────────────────────────────▶  [Draft]
                                                    │
                                             version stage
                                                    ▼
                                               [Stage]
                                                    │
                                            version commit
                                                    ▼
                                               [新 versions/vN + latest + HEAD]
```

### 6.2 操作对照

| Git 操作 | graphmcp 操作 | 说明 |
|----------|-------------|------|
| 改文件 | `graph update/insert/delete` | 累积在 `draft.json` |
| `git add` | `version stage` | 暂存 |
| `git commit` | `version commit` | 不可变快照 |
| `git log` / `diff` / `status` | `version log` / `diff` / `status` | |
| `git checkout` | `version checkout` | **只移动 HEAD**（见 §十） |
| （无直接对应） | `rollback` / `graph_rollback` | 旧快照另存为**新**版本 |

---

## 七、用户 → AI → MCP → 存储 交互与模块

### 7.1 创建并导出（示意）

与实现一致的主路径：`tools/call graph_create` → `parseAny` → `validate`（有 error 则 `status:"rejected"` + `isError`）→ `layout` → `Store::save` → 再 `graph_export` 读存储并 `exportGraph`。

### 7.2 模块协作

| 模块 | 文件 | 职责 |
|------|------|------|
| JSON | `json.hpp` | 递归下降解析；**对象键保持插入顺序** |
| 解析 | `parsers.hpp` | 多格式 → Graph；`ParseError` |
| 模型 | `model.hpp` | Graph/Node/Edge |
| 布局+校验 | `layout.hpp` | 校验（重复 ID、悬空边、层级环、孤立点）；布局含 **Kahn 分层（环内节点兜底）**、tree-h/tree-v、grid；分层后对 `shape=group` **包围盒回填** |
| 导出 | `exporters.hpp` | 多格式导出、栅格化、编辑器发现与调起 |
| 存储 | `storage.hpp` | index / latest / versions / `save` 时写 HEAD |
| 版本 | `version_manager.hpp` | Draft/Stage/Commit、checkout、游标磁盘路径辅助 |
| 游标类型 | `cursor_types.hpp` | 游标数据结构 |
| MCP | `mcp.hpp` | JSON-RPC、25 工具 |
| CLI | `main.cpp` | **13** 个命令族：`create`/`convert`/`export`/`edit`/`import`/`layout`/`validate`/`store`/`version`/`graph`/`cursor`/`draft`/`serve`；旧版扁平命令走 `handleLegacyCommand` |

### 7.3 关键设计决策（与代码一致）

| 决策 | 做法 | 原因 / 依据 |
|------|------|-------------|
| 统一图模型 | N 输入 → Graph → M 输出 | 避免 N×M 转换器 |
| 零第三方依赖 | 手写 JSON/XML/Base64 | 单文件可移植 |
| 不可变快照 | `versions/vN.json` 只追加 | 可回溯 |
| 文件存储 | JSON 目录树 | 零配置、可直接查看 |
| Excalidraw 保真 | `elements`（及 `files`）保留 | 白板往返 |
| PNG/PDF | 外部转换器链 + SVG 回退 | 保持核心零依赖 |
| MCP 通知 | 不回包 | JSON-RPC 通知语义 |
| Chromium | `--user-data-dir` 隔离 | 避免附着用户浏览器 |
| JSON 保序 | `json.hpp` 明确保留插入顺序 | 输出稳定、便于比对 |
| Windows CLI | `GetCommandLineW` + UTF-8 | 中文参数不乱码（`#ifdef _WIN32`） |

---

## 八、错误处理约定（对照 `main.cpp` / `mcp.hpp`）

| 场景 | CLI | MCP |
|------|-----|-----|
| 解析等未捕获异常（含 `ParseError`） | `main` 的 `catch` → 退出码 **2** | `call` 捕获后 `textContent(..., true)` → `isError: true` |
| 读文件失败（`readInput`） | `exit(2)` | （工具侧返回错误文本） |
| create/import 校验有 **error** | 打印 `rejected:…`，退出码 **3** | `status: "rejected"` + issues，且 `isError: true` |
| PNG/PDF 无转换器 | 写出 `.svg` 回退并提示；属降级路径 | 同左（经 `exportGraph`） |

另有退出码 1（用法/缺参）、4/5（导出失败等），见各命令族实现。

---

## 九、已知边界

| 边界 | 代码依据 |
|------|----------|
| Mermaid 仅 flowchart / mindmap / erDiagram | `parseMermaid` 对其它类型 `throw ParseError("unsupported mermaid diagram type…")` |
| ER 基数不建模为箭头类型 | `addEdge(..., "none")`，标签保留 |
| 布局 | Kahn + 环兜底；group 包围盒回填；`tree-h` / `tree-v` / `grid` / `auto` |
| PNG/PDF | 依赖本机转换器或浏览器；无则 SVG 回退 |

---

## 十、旧版 CLI 与 checkout / rollback

| 机制 | 代码位置 | 行为 |
|------|----------|------|
| 旧版扁平命令 | `handleLegacyCommand`（`subcommand` 为空时） | 仍支持旧式 `create`/`convert`/`export`/`edit`/`validate`/`list`/`show`/`history`/`rollback` 等 |
| 新版命令族 | `main` 按 `family` 分发 | `create from-mermaid` 等 |
| `Store::rollback` / `graph_rollback` | `storage.hpp` | 加载旧版本再 `save` → **新版本号**，并更新 `latest` + `HEAD` |
| `version checkout` / `graph_checkout` | `GraphVersionManager::checkout` | 有未提交 draft 且无 `--force`/`force` 则失败；否则清空 draft/stage，**只写 HEAD**；不新建 `versions/vN`，不改 `latest.json` |

因此：混用「rollback 另存」与「checkout 移指针」时，`latest.json` 与 `HEAD` 可能暂时不一致——`version status` 以 `HEAD` 为准；读工作副本时常走 `latest` 或 draft materialize，需按命令语义区分。

---

## 十一、测试入口（对照 `Makefile` / CI）

| 命令 | 说明 |
|------|------|
| `make test` | `tests/test_main.cpp` |
| `make test-version` / `make test-cursor` | 版本 / 游标单测 |
| `make test-all` | 上述三者 |
| `make smoke` | `tests/smoke_test.sh`（含 `[fixture-regression]`），写根目录 `SMOKE_REPORT.md` |
| `make mcp-smoke` | `tests/mcp_smoke.sh` |
| `make export-testout` | `scripts/export-example-testout.sh` → `examples/example_testout/` |

`.github/workflows/ci.yml` 默认：`make test-all`、`make smoke`、`make mcp-smoke`，并上传 `SMOKE_REPORT.md` 等制品。

（旧文档中的 `export-example-testout.ps1` **仓库中不存在**，已不收录。）
