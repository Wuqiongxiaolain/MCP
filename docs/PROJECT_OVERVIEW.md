# graphmcp 项目全景总结

> latest update: v0.2.0, 2026-07-14

> 说明：本文包含大量“立项/阶段过程”信息（历史视角）。涉及当前能力的口径，请以 `src/main.cpp`、`src/mcp.hpp::toolList()` 与 `docs/api_reference/openapi.yaml` 为准。

## 当前状态快照（截至本次更新）

- CLI 命令族已扩展到 15 个（含 `table`、`dump-tools`、`import`）。
- MCP 工具总数为 47（图 + 表协作 + 规则/修复/派生链路）。
- Mermaid 已支持 class/state/sequence/pie 等扩展类型，不再仅限初期子集。
- 通用表（CSV / 表 XML）与图↔表协同链路已落地；CD 已恢复 macOS 构建矩阵。
- OpenAPI 由 `dump-tools` / `make docs-api` 从 `toolList()` 自动生成并由 CI 校验漂移。

## 一、项目来源

本项目来自**课程实践要求**——"图形设计与绘图 MCP 工具"，是一份完整的软件开发综合训练课题。项目通过接收结构化图形描述内容，实现多种图形类型的生成、编辑、导出与版本管理，并通过 MCP 协议接入 AI 客户端，使用户能以自然语言驱动图形设计。

---

## 二、原始目标

### 2.1 需求文档核心要求

| 维度 | 规定 | 实际实现 |
|------|------|---------|
| 编程语言 | C++ | C++17 |
| 界面形式 | CLI | CLI + MCP 双模式 |
| 数据存储 | JSON 或 SQLite | JSON 文件系统（零依赖） |
| 版本控制 | Git | Git + GitHub |
| 持续集成 | Jenkins + Ansible | GitHub Actions（已迁移） |
| 代码质量 | SonarQube | SonarQube + cppcheck + clang-format |

### 2.2 功能需求完成情况

> 本表对照当前实现（`src/` + [`docs/api_reference/openapi.yaml`](api_reference/openapi.yaml)），状态以代码事实为准。

| 需求功能点 | 状态 | 实现方式（当前） |
|-----------|:--:|---------|
| 接收 XML / CSV / Mermaid / Markdown / Excalidraw / draw.io | ✅ | `parsers.hpp`：多格式解析 + `detectFormat` 自动识别；图 CSV 为边表/层级表 |
| 流程图 / 架构图 / ER 图 / 组织图 / 脑图 / 白板图 | ✅ | 统一 Graph 的 6 类业务图类型（`model.hpp`） |
| Mermaid 扩展类型（class / state / sequence / pie / …） | ✅ | `parseMermaid*` 深解析多类型；未知类型报错或走 `rawMermaid` 透传 |
| 统一图模型 + 节点/连线/层级/白板元素 + 颜色字段 | ✅ | `Node`/`Edge` 含 `fillColor`/`strokeColor`；白板保留 `elements`/`files` |
| 生成浏览器 URL + 调起外部编辑器 | ✅ | mermaid.live URL；`edit`/`graph_open` 调起 Draw.io / Excalidraw / SVG / 浏览器 |
| 图结构校验 + 基础布局 | ✅ | `layout.hpp`：重复 ID / 悬空边 / 层级环 / 孤立点；`auto`/`layered`/`tree-*`/`grid`；状态图认可 `[*]` 端点 |
| 导出 draw.io / Mermaid / Excalidraw / PNG / SVG / PDF / URL / model | ✅ | `exporters.hpp` 统一分发；PNG/PDF 走外部转换链，失败回退 SVG |
| 图版本保存 / 草稿暂存提交 / 回溯 | ✅ | Draft→Stage→Commit；`checkout` 移 HEAD；`rollback` 另存新版本 |
| 游标遍历与细粒度改图 | ✅ | `cursor_*` + `graph_update`/`insert`/`delete_element`/`graph_property` |
| MCP 接口（创建 / 转换 / 打开 / 导出及扩展） | ✅ | **47** 个工具（`toolList()` / OpenAPI）；另有 CLI **15** 命令族 + `dump-tools` |
| **通用表格 + 图↔表协作**（07-10 后扩展） | ✅ | `table_*` / `graph_from_table`：CSV 与表 XML、规则校验修复、派生与样例提案行 |
| 可选：实时画布预览 | ❎ | 列为后续目标 |
| draw.io URL / 导出观感打磨 / 性能与性能测试管线 | ❎ | 列为下一阶段目标（见 §六） |

能力与目标的思维导图总览见 [MINDMAP.md](MINDMAP.md)。

---

## 三、启动方式

项目于 **2026-07-05** 启动，采用**自底向上、逐层构建**：首日打通「解析→模型→校验布局→导出→存储→MCP」主链路；其后在同一骨架上扩展版本/游标、表协作、Mermaid 深解析与工程化能力。

### 3.1 启动日构建路径（07-05，历史）

| 顺序 | 步骤 | 当时产出 | 说明 |
|------|------|----------|------|
| 1 | 项目脚手架 | Makefile、CMakeLists.txt、.gitignore | 确定构建体系 |
| 2 | JSON + 图模型 | `json.hpp`、`model.hpp` | 自研 JSON；统一 Graph/Node/Edge |
| 3 | 输入解析器 | `parsers.hpp` | Mermaid / Markdown / CSV / XML / Excalidraw（首日以 flowchart 等为主） |
| 4 | 校验 + 布局 | `layout.hpp` | 校验规则 + Kahn / 树 / 网格布局 |
| 5 | 导出器 | `exporters.hpp` | draw.io / Mermaid / Excalidraw / SVG / PNG·PDF / URL |
| 6 | 版本化存储 | `storage.hpp` | index + latest + versions 快照 |
| 7 | MCP + CLI | `mcp.hpp`、`main.cpp` | JSON-RPC over stdio；首日约 8 工具 + 扁平 CLI |
| 8 | 测试 + 示例 | `tests/`、`examples/` | 首日单测与多格式样例输入 |
| 9 | CI + 文档 | Jenkins 初版、`docs/` | 后迁 GitHub Actions |

### 3.2 当前模块与能力全景（对照源码）

| 层级 | 文件 / 入口 | 当前职责 |
|------|-------------|---------|
| 模型 | `model.hpp` | Graph/Node/Edge；颜色、扩展箭头字段、`properties`、白板 `elements` |
| 解析 | `parsers.hpp` | 多格式图输入；Mermaid 多类型深解析；draw.io / Excalidraw |
| 校验布局 | `layout.hpp` | 结构校验（含 stateDiagram `[*]`）；多种布局策略 |
| 导出 | `exporters.hpp` | 多格式导出、栅格化回退、编辑器发现与调起 |
| 图存储 / 版本 / 游标 | `storage.hpp`、`version_*.hpp`、`cursor_types.hpp` | 图库快照、Draft/Stage/Commit、游标持久化 |
| 通用表 | `table_model.hpp`、`table_storage.hpp`、`table_bridge.hpp`、`table_xml.hpp`、`csv_util.hpp` | 表模型、版本存储、图↔表投影、表 XML |
| MCP | `mcp.hpp`、`mcp_table_tools.hpp` | **47** 工具；OpenAPI 由 `dump-tools` 导出 |
| CLI | `main.cpp` | **15** 命令族：`create`/`convert`/`export`/`edit`/`import`/`layout`/`validate`/`store`/`table`/`version`/`graph`/`cursor`/`draft`/`serve`/`dump-tools` |
| 契约 | `docs/api_reference/openapi.yaml` | `make docs-api` 从 `toolList()` 生成，CI 防漂移 |

### 3.3 关键设计决策

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 依赖策略 | **零第三方库** | JSON/XML/Base64 内置，单文件可移植 |
| 架构核心 | **统一图模型居中** | N 输入 → Graph → M 输出；表为并列一等对象 |
| 存储方案 | **JSON 文件而非 SQLite** | 零配置、可直接查看；表与图分目录 |
| MCP 协议 | **自实现 JSON-RPC 2.0** | stdio 行分隔，schema 与 OpenAPI 同源 |
| 构建系统 | **Make + CMake** | 快速开发与跨平台 CI/CD（含 macOS） |
| 契约维护 | **代码即文档** | `toolList()` → `dump-tools` → OpenAPI |

### 3.4 首日成果与后续演进

在 07-05 单日内已打通：

```
文本/结构化输入 → 统一 Graph → 校验/布局 → 多格式导出
               → 文件系统版本存储 → MCP/CLI 可调用
```

这保证项目从第一天起即可编译、运行并被 AI 客户端调用。之后在同一主链上叠加：CLI 命令族与版本游标（07-08～07-10）、**表协作 + Mermaid 深解析 + 颜色链路 + macOS CD + OpenAPI**（07-11～07-14，见 §四）。

## 四、开发流程

### 4.1 时间线与里程碑

```
2026-07-05          2026-07-07        2026-07-10           2026-07-11 ~ 07-14
    │                    │                 │                        │
    ▼                    ▼                 ▼                        ▼
┌──────────┐  ┌────────────────┐  ┌────────────────┐  ┌──────────────────────────┐
│ Day 1    │  │ Day 2~4        │  │ Day 5~6        │  │ 扩展期                   │
│ 核心引擎  │─▶│ CI/CD + CLI    │─▶│ 编辑器 + 文档   │─▶│ 表协作 / Mermaid /       │
│ 全部模块  │  │ 版本与白板     │  │ 初步收尾        │  │ macOS CD / 颜色 / OpenAPI│
└──────────┘  └────────────────┘  └────────────────┘  └──────────────────────────┘
```

| 阶段 | 日期 | 关键产出 |
|------|------|---------|
| **启动** | 07-05 | 项目脚手架、6 个核心模块（解析/模型/布局/导出/存储/MCP）、121 条单元测试 |
| **工程化** | 07-06 | 源码英文→中文注释翻译 |
| **CI/CD 迁移** | 07-07 | Jenkins/Ansible → GitHub Actions、clang-format/cppcheck 接入、SonarQube 可选 |
| **功能增强** | 07-07 ~ 07-08 | Excalidraw 白板精确导出、离线字体内嵌、Excalidraw files 保真 |
| **架构升级** | 07-08 ~ 07-09 | CLI 重构（9 命令→多命令族）、版本管理（Draft/Stage/Commit）、游标操作、drawio 解析回导 |
| **初步收尾** | 07-10 | 编辑器自动发现、MCP 协议补全、冒烟测试增强、应用原理文档；同时开始涌现**表协作 / Mermaid 扩展 / macOS CD**等新需求 |
| **平台扩展** | 07-11 ~ 07-14 | 通用 Table/MCP 工具族与图表协同；Mermaid 全类型深解析与颜色全链路；补全 macOS 头文件并恢复 CD 构建矩阵；OpenAPI/`dump-tools` 落地 |

按阶段编号（P1–P6）的早期里程碑见 [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md)。  
按提交记录还原的逐日演进见 [开发过程](DEV_PROCESS.md)。

自 `c6e8009`（`fix(build): 补全 macOS 头文件 + 恢复 CD macOS 构建矩阵`，2026-07-11）起至当前，仓库累计约 **70+** 次功能/测试/文档提交，重点落在表协作、Mermaid 扩展与工程契约对齐。

### 4.2 开发节奏

| 指标 | 数值（约） |
|------|------|
| 总提交数 | 200+（含扩展期） |
| 开发跨度 | 07-05 启动；07-10 初步收尾后进入扩展期 |
| 核心模块 | 图核心 + 表协作模块（`table_*.hpp` / `mcp_table_tools.hpp` 等） |
| MCP 工具 | 47（以 `toolList()` 为准） |
| CLI 命令族 | 15（含 `table` / `dump-tools`） |

### 4.3 07-10 以来的需求落地（已解决）

07-10 初步收尾后，新涌现并已交付的能力如下：

| 需求 | 状态 | 说明 |
|------|:--:|------|
| **通用表格支持** | ✅ | Table 模型 / TableStore、CSV 与表 XML 互通、图↔表投影与协同增强（规则/校验/修复/派生等） |
| **Mermaid 支持扩展** | ✅ | 覆盖 class/state/sequence/pie 等更多类型的深解析路径；坏样例区分硬失败/软失败；颜色（`classDef`/`linkStyle`）全链路 |
| **macOS CD 支持** | ✅ | 补全 macOS 头文件依赖，恢复 CD 构建矩阵中的 macOS Runner |

---

## 五、代码管理方式

### 5.1 分支策略

```
main ────────────────────────────────────────────────────────▶ (主线)
  │
  ├── feature/github-actions-cicd    → PR #10, #11       (CI/CD)
  ├── feature/excalidraw-export-...  → PR #16            (白板导出)
  ├── docs/changelog                 → PR #19, #21, #22
  ├── docs/cli-mcp-reference         → PR #21            (接口文档)
  ├── feature/cli-test-pipeline      → PR #24            (CLI 测试)
  ├── ah_feng-editor-v2              → PR #25            (编辑器)
  ├── feature/mcp-three-layer-tests  → PR #26            (MCP 测试)
  ├── CLI                            → PR #13            (CLI 重构+版本+游标)
  └── docs/app-logic-explanation     → PR #41            (原理说明)
```    

### 5.2 PR 工作流

| 环节 | 做法 |
|------|------|
| 分支命名 | `feature/*` / `docs/*` / `fix/*` 前缀 |
| PR 合并 | 每个 PR 一个独立主题，通过 GitHub PR 页面合并 |
| 代码审查 | PR 内审查（review ） |
| 提交规范 | `feat(*): ` / `fix(*): ` / `docs(*): ` / `test(*): ` / `chore(*): ` / `style(*): ` 前缀 |

### 5.3 质量保障

| 层次 | 工具 | 触发方式 |
|------|------|---------|
| 代码风格 | clang-format | 手动执行 |
| 静态分析 | cppcheck + SonarQube（可选） | CI / 手动 |
| 单元测试 | `test_main.cpp` + `test_version.cpp` + `test_cursor.cpp` | `make test-all` |
| 冒烟测试 | `smoke_test.sh`（命令族全量 + fixture 回归） | `make smoke` + CI |
| MCP 协议测试 | `mcp_smoke.sh` | CI |
| 表协作冒烟 | `table_smoke.sh` | `make table-smoke` |
| 样例导出矩阵 | `export-example-testout.sh` / 表样例导出脚本 | `make export-testout` 等 |
| OpenAPI 漂移校验 | `dump-tools` / `make docs-api` | CI |

### 5.4 DevOps 演进

| 阶段 | 工具链 | 说明 |
|------|--------|------|
| 初版 | Jenkins + Ansible | 07-05 搭建，遵循需求文档 |
| 迁移 | GitHub Actions | 07-07 迁移，更适合 GitHub 生态 |
| 现状 | GitHub Actions CI + CD（含 **macOS** 构建矩阵）+ 可选 SonarQube + GitLab 镜像 | 详见 `.github/workflows/` |

---

## 六、下一阶段目标（尚未解决）

### 6.1 已关闭的早期目标（对照）

以下在 07-10 后的扩展期中已完成，**不再**列为待办：

| 目标 | 状态 | 结果 |
|------|:--:|------|
| 通用表格支持 | ✅ | Table + MCP/CLI 工具族与协同链路 |
| Mermaid 类型扩展 | ✅ | 深解析覆盖 class/state/sequence/pie 等；颜色链路可用 |
| macOS CD | ✅ | CD 矩阵恢复 macOS Runner |

### 6.2 功能扩展（待做）

| 目标 | 说明 |
|------|------|
| **draw.io 能力补齐** | 现有 draw.io 往返仍有缺口（如更完整的互操作 / draw.io URL 等）；需在保持零依赖前提下继续补齐 |
| **导出图质量过粗** | 当前部分导出结果视觉上偏粗糙（布局、渲染与样式一致化），需要系统性打磨 |
| **实时画布预览** | 通过 SVG + 本地 HTML 轮询 `latest.json` 实现实时预览（可选） |

### 6.3 工程与性能（待做）

| 目标 | 说明 |
|------|------|
| **潜在性能问题** | 大图/大表场景下的解析、布局与导出开销尚未系统评估 |
| **性能测试管线** | 缺少可重复的性能基准与 CI 性能冒烟；需建立可对比、可回归的性能测试流水线 |
| **SQLite 可选后端** | 大图检索场景下可评估替代 JSON 文件存储 |
| **贡献指南** | 开源协作规范文档（`CONTRIBUTING.md`） |

## 附录：需求对照检查清单

| # | 需求项 | 完成 |
|---|--------|:--:|
| 1 | 接收 XML / CSV / Mermaid / Markdown / Excalidraw JSON | ✅ |
| 2 | 流程图 / 架构图 / ER 图 / 组织图 / 脑图 / 白板图 | ✅ |
| 3 | 统一图模型 + 节点/连线/层级/白板元素 | ✅ |
| 4 | 生成 URL + 调起外部编辑器 | ✅ |
| 5 | 图结构校验 + 基础布局 | ✅ |
| 6 | 导出 .drawio / Mermaid / Excalidraw / PNG / SVG / PDF / URL | ✅ |
| 7 | 图定义保存 + 历史版本管理 + 回溯 | ✅ |
| 8 | MCP 接口：创建 / 转换 / 打开 / 导出（现含表协作等扩展） | ✅ |
| 9 | 通用表格支持 + 图↔表协同 | ✅ |
| 10 | Mermaid 类型扩展（class/state/sequence/pie 等） | ✅ |
| 11 | macOS CD 构建矩阵 | ✅ |
| 12 | C++ / CLI / JSON 存储 / Git | ✅ |
| 13 | 可选：实时画布 | ❎ |
| 14 | draw.io 能力补齐 / 导出质量打磨 / 性能与性能测试管线 | ❎ |
