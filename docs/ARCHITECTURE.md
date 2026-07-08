# graphmcp 架构说明

## 总体设计

```
                    ┌─────────────────────────────────────────────┐
  输入内容           │                graphmcp (单可执行文件)         │        输出
──────────────►     │                                             │  ──────────────►
 Mermaid            │  parsers.hpp ──► model.hpp ──► exporters.hpp │   .drawio
 Markdown 大纲       │  (5种解析器)     (统一图模型)     (6种导出器)     │   Mermaid
 CSV                │                    │                        │   Excalidraw JSON
 XML                │        ┌───────────┼───────────┐            │   SVG / PNG / PDF
 Excalidraw JSON    │   layout.hpp   validate    storage.hpp      │   mermaid.live URL
                    │   (自动布局)    (规则校验)    (版本化存储)       │   外部编辑器调起
                    │                                             │
                    │  mcp.hpp: JSON-RPC 2.0 over stdio (MCP协议)  │
                    │  main.cpp: CLI 命令分发                       │
                    └─────────────────────────────────────────────┘
```

设计原则：**所有格式都先归一到统一图模型，再从模型出发做校验、布局、导出**。
新增一种输入格式只需写一个 `parse* -> Graph` 函数；新增一种输出格式只需写
一个 `Graph -> string` 函数，N 种输入 × M 种输出不需要 N×M 个转换器。

## 统一图模型（model.hpp）

```
Graph {
  id, name, type            # flowchart|architecture|er|orgchart|mindmap|whiteboard
  nodes[]: { id, label, shape, parent, style, attrs[], x, y, w, h }
  edges[]: { id, from, to, label, style(solid|dashed|thick), arrow(arrow|none|both) }
  elements[]: 原始 Excalidraw 元素（白板场景无损保留）
  laidOut: 坐标是否已生效
}
```

- **层级结构**：`node.parent` 同时服务于三种语义——脑图/组织图的树、
  流程图的 subgraph 分组、架构图的容器嵌套。
- **ER 属性**：`node.attrs` 存放 `"类型 字段名"` 行，导出时渲染为表格。
- **白板元素**：Excalidraw 输入的 `elements` 原样保留（isDeleted 除外），
  同时**派生**出逻辑节点/连线供转换到其它格式；导出回 Excalidraw 时优先
  使用原始元素，保证无损往返。

## 模块职责

| 模块 | 职责 | 关键点 |
|---|---|---|
| json.hpp | JSON 解析/序列化 | 递归下降解析，\uXXXX 代理对 → UTF-8，对象保持插入顺序（输出确定性） |
| parsers.hpp | 输入 → 模型 | 手写 Mermaid 词法（节点形状括号、8 种箭头、subgraph 栈）；迷你 XML 解析器；CSV 引号转义；格式自动识别 |
| layout.hpp | 校验 + 布局 | 校验：重复 ID/悬空边/层级环/孤立点。布局：Kahn 分层（含环兜底）、递归子树宽度树布局、分组容器包围盒回填 |
| exporters.hpp | 模型 → 输出 | drawio 子节点相对坐标；Excalidraw 绑定文本 + 箭头绑定；SVG 边裁剪到节点边界；PNG/PDF 自动探测转换器（inkscape/rsvg/magick，或已装的 Chrome/Edge 无头模式），均无则回退 SVG |
| storage.hpp | 版本化存储 | `index.json` 目录 + 每图 `latest.json` + 不可变 `versions/vN.json` 快照；回滚 = 旧快照另存为新版本（非破坏） |
| mcp.hpp | MCP 协议 | JSON-RPC 2.0，stdio 换行分隔；initialize 握手、tools/list、tools/call；通知不回包 |
| main.cpp | CLI | 9 个子命令；Windows 下 argv 转 UTF-8（GetCommandLineW）避免中文乱码 |

## 存储布局

```
graph-store/
  index.json                  # 图目录：id/name/type/versions/时间戳
  <graphId>/
    latest.json               # 当前版本模型
    versions/v1.json …        # 不可变历史快照 {version, savedAt, note, model}
    open.drawio / open.svg…   # open 命令生成的临时编辑文件
```

## MCP 工具清单

| 工具 | 参数 | 功能 |
|---|---|---|
| graph_create | content, format?, type?, name?, id? | 解析+校验+布局+入库；若提供已有 id 则更新（升版本） |
| graph_convert | content, format?, to | 一次性转换（不入库） |
| graph_export | id, to, path?, version? | 导出文件或内联内容 |
| graph_open | id, editor?, editorPath? | 生成 URL / 文件并调起外部编辑器；editorPath 指定编辑器路径 |
| graph_validate | content \| id | 结构校验，返回 errors/warnings |
| graph_list | — | 列出库中所有图 |
| graph_history | id | 版本历史 |
| graph_rollback | id, version | 回溯到指定版本 |

## 错误处理约定

- 解析失败抛 `ParseError`，CLI 捕获后退出码 2，MCP 返回 `isError:true` 的工具结果。
- 校验存在 error 级问题时 create 拒绝入库（退出码 3 / status:"rejected"）。
- PNG/PDF 无转换器时不视为失败中断，写出 `.svg` 回退文件并明确提示。

## 已知边界

- Mermaid 支持常用子集（flowchart / mindmap / erDiagram），不支持
  sequenceDiagram、gantt 等时序类图（它们不属于"图"模型范畴）。
- ER 关系基数（||--o{ 等）统一渲染为无箭头连线，基数标注保留在 label 中。
- PNG/PDF 依赖外部转换器，属于刻意取舍：保持零依赖、单文件可移植。
  转换器优先级 inkscape → rsvg-convert → magick → Chrome/Edge 无头模式；
  由于 Windows 大多预装 Edge，实际几乎总能直接出图，无需额外安装。
  注意：无头 Chromium 会用独立 `--user-data-dir`，避免和用户正在运行的
  浏览器实例冲突（否则新命令会附加到已有实例并跳过转换任务）。
