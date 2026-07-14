# graphmcp 时间线与里程碑

> latest update: v0.2.0, 2026-07-14

> 项目启动：2026-07-05 — 覆盖至：2026-07-14（版本以根目录 VERSION 为准）  
> 早期阶段（P1–P6）为 07-10 初步收尾；**P7 起为扩展期**，对应 `c6e8009`（macOS CD 恢复）以来及并行合入的表协作 / Mermaid / OpenAPI 等交付。  
> 逐日详情见 [DEV_PROCESS.md](DEV_PROCESS.md)；需求与模块全景见 [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)。

| 阶段 | 名称 | 日期 | 核心交付 |
|------|------|------|---------|
| P1 | 核心引擎搭建 | 07-05 | JSON/模型/解析器/导出器/存储/MCP/CLI |
| P2 | 加固与中文化 | 07-06 | 注释中文化、gitignore |
| P3 | CI/CD 工程化 | 07-07 | GitHub Actions、clang-format、SonarQube、制品发布 |
| P4 | Excalidraw 白板体系 | 07-08 | 精确 SVG、freedraw、内嵌字体、files 附件 |
| P5 | 架构重构 | 07-09 | CLI 多命令族、Draft-Stage-Commit、Cursor、MCP 扩至约 24～25 |
| P6 | 外部编辑器闭环 | 07-09～10 | 编辑器发现、drawio 回导、文档分层；**暂禁** macOS CD（缺头文件） |
| **P7** | **macOS CD + 编辑打磨** | **07-11** | `c6e8009` 补全 macOS 头文件并恢复 CD 构建矩阵；edit/import 提示与导入错误改进（PR #62） |
| **P8** | **OpenAPI 契约** | **07-13** | `dump-tools` / `make docs-api`、OpenAPI 入库、CI 文档漂移校验（PR #65） |
| **P9** | **通用表协作** | **07-13～14** | Table/TableStore、`table_*` MCP/CLI、表 XML、图↔表投影与协同增强（PR #66/#70） |
| **P10** | **Mermaid 深解析 + 颜色** | **07-11～14** | 全类型深解析、`graph_property`、颜色全链路、状态图 `[*]` 校验、样例/冒烟对齐（PR #64 等） |

## 扩展期结果对照（截至 07-14）

| 维度 | 07-10 收口（历史） | 当前（扩展期后） |
|------|-------------------|------------------|
| MCP 工具 | ~25 | **46**（`toolList()` / OpenAPI） |
| CLI | 多命令族（尚未含完整 table 族） | **15** 族（含 `table` / `dump-tools`） |
| Mermaid | 以 flowchart/mindmap/er 为主 | class/state/sequence/pie 等深解析 + 颜色 |
| 表 | 无通用 Table | CSV / 表 XML + 图↔表协同 |
| CD | macOS 暂禁 | **macOS Runner 已恢复** |

## 尚未关闭（与进度相关）

| 项 | 说明 |
|----|------|
| draw.io 能力补齐 | 更完整互操作 / draw.io URL 等 |
| 导出观感过粗 | 布局与渲染打磨 |
| 性能与性能测试管线 | 大图/大表评估 + CI 性能基准 |
