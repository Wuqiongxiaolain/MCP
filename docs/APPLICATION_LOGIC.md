# graphmcp 应用运作逻辑详解

> 基于 graphmcp 1.1.0 | 2026-07-10

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
| AI 层 | Claude Code / Desktop | 理解意图，调用 MCP 工具 |
| 协议层 | JSON-RPC 2.0 over stdio | AI 与 MCP 服务器之间的通信协议 |
| 服务层 | graphmcp serve | 接收工具调用，分发到内部模块 |
| 逻辑层 | 6 个核心模块 | 解析、建模、校验、布局、导出、版本管理 |
| 存储层 | 文件系统 (graph-store/) | 图库持久化，JSON 文件存储 |

---

## 二、MCP 服务器设计

### 2.1 协议实现

| 特性 | 实现方式 |
|------|---------|
| 协议 | JSON-RPC 2.0（标准 RPC 协议） |
| 传输 | stdio（标准输入/输出流，行分隔） |
| 实现文件 | `src/mcp.hpp`（协议层，1325 行） |
| 启动方式 | `graphmcp serve` |
| 存储路径 | 环境变量 `GRAPHMCP_STORE`，默认 `./graph-store` |

### 2.2 MCP 协议握手流程

| 步骤 | 方向 | 消息类型 | 内容 |
|------|------|---------|------|
| 1 | AI → 服务器 | `initialize` 请求 | 客户端能力声明 |
| 2 | 服务器 → AI | `initialize` 响应 | 服务器能力 + 支持的协议版本 |
| 3 | AI → 服务器 | `initialized` 通知 | 握手完成确认 |
| 4 | AI → 服务器 | `tools/list` 请求 | 查询可用工具列表 |
| 5 | 服务器 → AI | `tools/list` 响应 | 返回 9 个工具的定义（名称、参数、描述） |
| 6 | AI → 服务器 | `tools/call` 请求 | 调用具体工具（携带参数） |
| 7 | 服务器 → AI | `tools/call` 响应 | 返回执行结果（或 isError: true） |

### 2.3 9 个 MCP 工具

| 工具名 | 参数 | 功能 | 对应模块 |
|--------|------|------|---------|
| `graph_create` | content, format?, type?, name? | 解析→校验→布局→入库 | parsers → model → layout → storage |
| `graph_convert` | content, format?, to | 一次性格式转换（不入库） | parsers → exporters |
| `graph_export` | id, to, path?, version? | 从图库导出为指定格式 | storage → exporters |
| `graph_open` | id, editor?, editorPath? | 调起外部编辑器 | 自动发现本机安装的 Draw.io/VS Code |
| `graph_import` | id, content?, format? | 编辑后重新导入入库 | parsers → storage（生成新版本） |
| `graph_validate` | content 或 id | 结构校验 | layout.hpp 校验逻辑 |
| `graph_list` | — | 列出库中所有图 | storage 索引读取 |
| `graph_history` | id | 查询版本历史 | storage 版本读取 |
| `graph_rollback` | id, version | 回退到指定版本 | storage 快照复制 |

---

## 三、存储层（数据库）设计

graphmcp 不使用传统数据库，而是**基于文件系统的 JSON 存储**。

### 3.1 目录结构

```
graph-store/                      ← 根目录（由 GRAPHMCP_STORE 指定）
├── index.json                    ← 全局索引：所有图的目录
└── <图ID>/                       ← 每张图一个子目录
    ├── HEAD                      ← 纯文本文件，记录当前版本号
    ├── latest.json               ← 当前版本的完整图模型
    ├── draft.json                ← 未提交的修改操作列表
    ├── stage.json                ← 已暂存待提交的操作
    ├── open.drawio / open.svg…   ← edit 命令生成的临时编辑文件
    └── versions/
        ├── v1.json               ← 不可变版本快照（含 model + savedAt + note）
        ├── v2.json
        └── ...
```

### 3.2 各文件职责

| 文件 | 格式 | 读写特征 | 说明 |
|------|------|---------|------|
| `index.json` | JSON 数组 | 频繁读写 | 记录每张图的 id、name、type、版本数、时间戳 |
| `HEAD` | 纯文本 | 读写 | 单行数字，指向当前版本号 |
| `latest.json` | JSON 对象 | 读写 | 完整 Graph 模型，是当前工作副本 |
| `draft.json` | JSON 数组 | 读写 | 记录自上次 commit 以来的所有修改操作 |
| `stage.json` | JSON 数组 | 读写 | 暂存区，用户通过 `version stage` 选择性暂存 |
| `versions/vN.json` | JSON 对象 | **只写不删** | 不可变快照，{version, savedAt, note, model} |

### 3.3 存储操作对照

| 操作 | CLI 命令 | 存储层行为 |
|------|---------|-----------|
| 创建图 | `create from-*` | 生成图 ID → 写入 `index.json` → 创建子目录 → 写入 `latest.json` + `versions/v1.json` + `HEAD` |
| 修改元素 | `graph update/insert/delete` | 原子操作写入 `draft.json`，不直接改模型 |
| 暂存 | `version stage` | 从 `draft.json` 迁移到 `stage.json` |
| 提交 | `version commit` | 应用 `stage.json` → 更新 `latest.json` → 写入新 `versions/vN.json` → 更新 `HEAD` → 清空 stage |
| 导出 | `export to-*` | 只读 `latest.json`（或指定版本），不写 |
| 回退 | `version checkout` | 移动 `HEAD` 指针，更新 `latest.json` |
| 删除 | `store delete` | 删除整个图子目录 + 从 `index.json` 移除条目 |

---

## 四、文件导入导出设计

### 4.1 设计原则

> **N 种输入 × M 种输出 ≠ N×M 个转换器**，而是通过统一图模型实现 **N + M** 的复杂度。

```
任意输入格式  ──parse──▶  统一图模型 (Graph)  ──export──▶  任意输出格式
    Mermaid        parsers.hpp    model.hpp        exporters.hpp     Drawio
    Markdown           ↓                             SVG / PNG / PDF
    CSV             5种解析器                         Excalidraw / URL
    XML                                              统一模型 JSON
    Excalidraw JSON
```

### 4.2 输入格式 → 解析器

| 输入格式 | 解析函数 | 关键解析能力 |
|---------|---------|-------------|
| **Mermaid** | `parseMermaid()` | 手写词法分析：节点形状括号 `[]{}()()`、8 种箭头 `-->` `--->` `-.->`、subgraph 嵌套栈 |
| **Markdown** | `parseMarkdown()` | 标题层级 `#` `##` → 树结构，列表缩进 → 父子关系 |
| **CSV 边表** | `parseCSV()` | `from,to,label` → 流程图；`id,label,parent` → 组织图（自动识别） |
| **XML** | `parseXML()` | 迷你 XML 解析器，支持嵌套节点、边、属性 |
| **Excalidraw** | `parseExcalidraw()` | 解析 Excalidraw JSON 的 elements 数组，提取矩形/菱形/箭头/文字，同时保留原始元素 |
| **Drawio** | `parseDrawio()` | 反向解析 Drawio XML，提取 mxCell 节点和边 |
| **统一模型** | `parseModel()` | 直接反序列化 Graph JSON |

### 4.3 输出格式 → 导出器

| 输出格式 | 导出函数 | 关键导出能力 |
|---------|---------|-------------|
| **Drawio** | `exportDrawio()` | 子节点相对父节点坐标计算，mxCell 结构生成 |
| **Mermaid** | `exportMermaid()` | 节点形状转义、subgraph 嵌套还原、边标签渲染 |
| **Excalidraw** | `exportExcalidraw()` | 绑定文本 + 箭头绑定，原始元素优先（无损往返），仿射变换，freedraw 轮廓 |
| **SVG** | `exportSVG()` | 边自动裁剪到节点边界，CSS 样式，字体内嵌 |
| **PNG** | `exportPNG()` | 先生成 SVG → 自动探测系统转换器（优先级见下） |
| **PDF** | `exportPDF()` | 同 PNG，先生成 SVG 再转换 |
| **URL** | `exportURL()` | Base64 编码 Mermaid 文本 → mermaid.live 链接 |
| **统一模型** | `exportModel()` | JSON 序列化完整 Graph 对象 |

### 4.4 PNG/PDF 渲染回退链

```
首选 inkscape → 备选 rsvg-convert → 备选 magick (ImageMagick)
→ 兜底 Chrome/Edge 无头模式 → 最终回退：输出 SVG 文件
```

| 转换器 | 可用性 | 说明 |
|--------|--------|------|
| Inkscape | 需安装 | 开源 SVG 编辑器，命令行模式 |
| rsvg-convert | 需安装 | librsvg 工具 |
| ImageMagick | 需安装 | 通用图像处理 |
| Chrome/Edge 无头 | Windows 预装 | 使用独立 `--user-data-dir` 避免和用户浏览器实例冲突 |
| 回退 | 始终可用 | 输出 `.svg` 替代，不报错 |

---

## 五、统一图模型——系统的心脏

### 5.1 Graph 数据结构

```
Graph {
  id          → "gthxud4py"        // 唯一标识
  name        → "系统交互架构"       // 可读名称
  type        → flowchart | architecture | er | orgchart | mindmap | whiteboard
  nodes[]     → [ {id, label, shape, parent, style, attrs[], x, y, w, h}, ... ]
  edges[]     → [ {id, from, to, label, style(solid|dashed|thick), arrow(arrow|none|both)}, ... ]
  elements[]  → [ ... ]             // Excalidraw 原始元素（白板场景无损保留）
  laidOut     → true/false          // 坐标是否已由布局引擎计算
}
```

### 5.2 节点类型与形状

| type | shape 取值 | 典型用途 |
|------|-----------|---------|
| flowchart | rect, diamond, round, ellipse, hexagon, parallelogram | 流程步骤、判断、起止 |
| architecture | rect, cylinder, cloud, component | 服务、数据库、云服务 |
| er | rect (实体) | 数据库表 |
| orgchart | rect | 人员/职位 |
| mindmap | rect, round | 主题/子主题 |
| whiteboard | rect, diamond, ellipse, arrow, text, line, draw | 自由绘制 |

### 5.3 层级结构

`node.parent` 一个字段承载三种语义：

| 语义 | parent 指向 | 导出表现 |
|------|------------|---------|
| 流程图 subgraph | 分组节点 ID | Mermaid `subgraph`，Drawio 容器 |
| 脑图/组织图树 | 父节点 ID | 树形连线布局 |
| 架构图容器 | 容器节点 ID | 子节点相对坐标，Drawio 嵌套 |

---

## 六、版本管理系统——类 Git 工作流

### 6.1 状态流转

```
                    graph update/insert/delete
    [HEAD]  ──────────────────────────────────▶  [Draft 草稿区]
                                                    │
                                             version stage
                                                    │
                                                    ▼
                                               [Stage 暂存区]
                                                    │
                                            version commit
                                                    │
                                                    ▼
                                               [新版本 Commit]
```

### 6.2 操作对照

| Git 操作 | graphmcp 操作 | 说明 |
|----------|-------------|------|
| 修改文件 | `graph update/insert/delete` | 修改累积在 `draft.json` |
| `git add` | `version stage` | 选择性暂存 |
| `git commit` | `version commit` | 生成不可变版本快照 |
| `git log` | `version log` | 查看版本历史 |
| `git diff` | `version diff` | 对比两个版本 |
| `git checkout` | `version checkout` | 切换 HEAD 到指定版本 |
| `git status` | `version status` | 查看当前状态（HEAD / Draft / Stage） |
| `git reset` | `version draft reset` | 丢弃草稿修改 |

---

## 七、用户 → AI → MCP 服务器 → 存储 完整交互流程

### 7.1 以"创建一张流程图并导出"为例

| 步骤 | 角色 | 动作 | 数据流向 |
|------|------|------|---------|
| 1 | **用户** | 说："帮我画一张登录流程图" | 自然语言 → AI |
| 2 | **AI 客户端** | 理解意图，决定调用 `graph_create` 工具 | — |
| 3 | **AI → MCP** | 发送 `tools/call`：工具名 `graph_create`，参数 `content="flowchart TD\nA-->B"`, `format="mermaid"` | JSON-RPC 请求 → stdio |
| 4 | **MCP 服务器** | `mcp.hpp` 解析 JSON-RPC，路由到 `handleGraphCreate()` | — |
| 5 | **解析器** | `parsers.hpp` 的 `parseMermaid()` 词法分析 Mermaid 文本 | 文本 → Graph 对象 |
| 6 | **校验器** | `layout.hpp` 检查重复 ID、悬空边、层级环、孤立节点 | Graph → errors/warnings |
| 7 | **布局引擎** | `layout.hpp` 根据图类型自动选择策略（layered/tree/grid），计算节点坐标 | Graph → Graph（含坐标） |
| 8 | **存储层** | `storage.hpp` 生成图 ID → 写入 `index.json` → 写入 `latest.json` + `versions/v1.json` + `HEAD` | Graph → 文件系统 |
| 9 | **MCP → AI** | 返回 `{ graphId: "g7abc", version: 1, nodes: 4, edges: 4 }` | JSON-RPC 响应 → stdio |
| 10 | **AI 客户端** | 告知用户："已创建，图 ID 为 g7abc" | — |
| 11 | **用户** | 说："导出为 SVG" | 自然语言 → AI |
| 12 | **AI → MCP** | 发送 `tools/call`：工具名 `graph_export`，参数 `id="g7abc"`, `to="svg"` | JSON-RPC 请求 → stdio |
| 13 | **MCP 服务器** | `mcp.hpp` 路由到 `handleGraphExport()` | — |
| 14 | **存储层** | 从 `versions/v1.json` 读取模型 | 文件系统 → Graph |
| 15 | **导出器** | `exporters.hpp` 的 `exportSVG()` 将 Graph 渲染为 SVG | Graph → SVG 字符串 |
| 16 | **MCP → AI** | 返回 SVG 内容 | JSON-RPC 响应 → stdio |
| 17 | **AI → 用户** | 展示 SVG 图片或保存文件路径 | — |

### 7.2 MCP 服务器内部模块协作

| 模块 | 文件 | 行数 | 职责 | 输入 | 输出 |
|------|------|------|------|------|------|
| JSON 解析 | `json.hpp` | 33+ | 递归下降解析，\uXXXX→UTF-8 | JSON 文本 | 键值树 |
| 解析器 | `parsers.hpp` | 245+ | 5 种格式→统一模型 | Mermaid/Markdown/CSV/XML/Excalidraw 文本 | Graph 对象 |
| 图模型 | `model.hpp` | 31+ | 统一数据结构定义 | — | — |
| 布局+校验 | `layout.hpp` | 37+ | 自动布局 + 结构校验 | Graph | Graph（含坐标）+ 校验结果 |
| 导出器 | `exporters.hpp` | 1364+ | 模型→输出格式 | Graph | Drawio/SVG/PNG/PDF/Excalidraw/URL 文本 |
| 存储 | `storage.hpp` | 53+ | 版本化文件存储 | Graph | 文件系统读写 |
| 游标 | `cursor_types.hpp` | 547+ | 元素级 CRUD 数据类型 | — | — |
| 版本管理 | `version_manager.hpp` | 717+ | Draft/Stage/Commit 工作流 | 操作序列 | 版本快照 |
| MCP 协议 | `mcp.hpp` | 1325+ | JSON-RPC 2.0 over stdio | JSON-RPC 请求 | JSON-RPC 响应 |
| CLI 入口 | `main.cpp` | 1643+ | 命令行分发，12 命令族 | argc/argv | 退出码 + 输出 |

### 7.3 关键设计决策

| 决策 | 做法 | 原因 |
|------|------|------|
| 统一图模型 | 所有格式先归一为 Graph | N+M 复杂度，避免 N×M 个转换器 |
| 零第三方依赖 | 手写 JSON/XML 解析器、Base64 编码 | 单文件可移植，无环境依赖 |
| 不可变版本快照 | versions/vN.json 只写不删 | 保证版本历史完整可回溯 |
| 文件系统存储 | JSON 文件而非数据库 | 零配置、可直接查看、易于备份 |
| Excalidraw 原始保留 | elements[] 原样保存 | 白板导出无损往返 |
| PNG/PDF 回退链 | 6 级降级 | 覆盖主流环境，不因缺少某工具而失败 |
| MCP 通知不回包 | 单向通知 | 符合 JSON-RPC 规范 |
| 独立 Chromium 用户目录 | `--user-data-dir` 隔离 | 避免与用户浏览器实例冲突 |
