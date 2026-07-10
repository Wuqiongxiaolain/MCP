# graphmcp 项目全景总结


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

| 需求功能点 | 状态 | 实现方式 |
|-----------|:--:|---------|
| 接收 XML / CSV / Mermaid / Markdown / Excalidraw JSON | ✅ | 5 种解析器 + 格式自动识别 |
| 流程图 / 架构图 / ER 图 / 组织图 / 脑图 / 白板图 | ✅ | 6 种图类型 |
| 统一图模型管理 + 节点/连线/层级/白板元素 | ✅ | `model.hpp` 统一 Graph 结构 |
| 生成浏览器 URL + 调起外部编辑器 | ✅ | mermaid.live URL + Draw.io/VS Code 自动发现 |
| 图结构校验 + 基础布局 | ✅ | Kahn 分层 / 树布局 / 网格 + 4 项校验规则 |
| 导出 .drawio / Mermaid / Excalidraw / PNG / SVG / PDF / URL | ✅ | 8 种输出格式 |
| 图定义保存 + 历史版本管理 + 回溯 | ✅ | 类 Git 工作流（Draft→Stage→Commit→Checkout） |
| MCP 接口：创建 / 转换 / 打开 / 导出 | ✅ | 9 个 MCP 工具 |
| 可选：实时画布预览 | ❎ | 列为后续目标 |

---

## 三、启动方式

项目于 2026-07-05 启动，采用**自底向上、逐层构建**的策略，首日即完成了全部核心模块的开发。

### 3.1 启动路径

启动步骤如下：

| 顺序 | 步骤 | 产出 | 说明 |
|------|------|------|------|
| 1 | **项目脚手架** | Makefile、CMakeLists.txt、.gitignore | 确定构建体系与项目结构 |
| 2 | **JSON 库 + 图模型** | `json.hpp`、`model.hpp` | 自研 JSON 解析器（递归下降、\uXXXX→UTF-8、保序）；定义 Graph/Node/Edge 统一数据结构，确定 6 种图类型 |
| 3 | **5 种输入解析器** | `parsers.hpp` | 手写 Mermaid 词法分析（节点形状括号、8 种箭头、subgraph 栈）、Markdown 标题层级解析、CSV 边表/层级表自动识别、迷你 XML 解析器、Excalidraw JSON 解析，含 `detectFormat` 自动格式识别 |
| 4 | **校验 + 布局引擎** | `layout.hpp` | 4 项校验规则（重复 ID、悬空边、层级环、孤立节点）；3 种布局策略（Kahn 分层、递归子树树布局、网格） |
| 5 | **8 种输出导出器** | `exporters.hpp` | Drawio XML（子节点相对坐标）、Mermaid 文本（subgraph 嵌套还原）、Excalidraw JSON（原始元素无损保留）、SVG（边裁剪到节点边界）、PNG/PDF（外部转换器链 + SVG 回退）、mermaid.live URL（Base64 编码） |
| 6 | **版本化存储** | `storage.hpp` | 文件系统 JSON 存储：`index.json` 全局索引 + `latest.json` 工作副本 + `versions/vN.json` 不可变快照 + 回滚机制 |
| 7 | **MCP 服务 + CLI 入口** | `mcp.hpp`、`main.cpp` | JSON-RPC 2.0 over stdio，8 个 MCP 工具；9 个 CLI 子命令 + `serve` |
| 8 | **测试 + 示例** | `tests/test_main.cpp`、`examples/` | 121 条断言覆盖解析/布局/导出/存储/MCP 协议；多格式示例输入 |
| 9 | **CI + 文档** | Jenkinsfile、`docs/` | Jenkins + Ansible + SonarQube（初版 CI）；README、架构说明、思维导图、工作日志 |

### 3.2 关键启动决策

项目在启动时做出的核心设计选择，贯穿了整个开发过程：

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 依赖策略 | **零第三方库** | JSON/XML/Base64 全部手写内置，保证单文件可移植、零环境依赖 |
| 架构核心 | **统一图模型居中** | 所有输入归一为 Graph，所有输出从 Graph 导出，N+M 复杂度而非 N×M |
| 存储方案 | **JSON 文件而非 SQLite** | 零配置、可直接查看、易于备份，虽然需求允许 SQLite |
| MCP 协议 | **自实现 JSON-RPC 2.0** | 不引入外部 RPC 框架，stdio 行分隔，简洁可控 |
| 构建系统 | **Make + CMake 双支持** | Make 用于快速开发，CMake 用于 IDE 集成和跨平台 |

### 3.3 首日成果

在 07-05 单日内，项目从零完成了**一条完整的处理链路**：

```
Mermaid/Markdown/CSV/XML 文本输入
    → 解析为统一 Graph
    → 校验 + 自动布局
    → 导出为 Drawio/SVG/PNG/PDF/URL
    → 写入文件系统版本存储
    → 通过 MCP 协议暴露为 AI 可调用的工具
```

这意味着在第一天结束时，项目已经是一个**可编译、可运行、可通过 MCP 协议被 AI 调用的完整工具**。后续 5 天的开发都是在这个骨架之上进行增强、重构和完善。

---

## 四、开发流程

### 4.1 时间线与里程碑

```
2026-07-05                          2026-07-07                    2026-07-10
    │                                    │                             │
    ▼                                    ▼                             ▼
┌────────────┐  ┌──────────────────┐   ┌──────────────┐   ┌──────────────────┐
│ Day 1      │  │ Day 2            │   │ Day 3-4      │   │ Day 5-6          │
│ 核心引擎    │─▶│ CI/CD + 工程化   │─▶│ CLI 重构      │─▶│ 编辑器 + 白板精化 │
│ 全部模块    │  │ 中文注释 + 文档   │   │ 版本管理      │   │ 测试补全 + 文档   │
└────────────┘  └──────────────────┘   └──────────────┘   └──────────────────┘
```

| 阶段 | 日期 | 关键产出 |
|------|------|---------|
| **启动** | 07-05 | 项目脚手架、6 个核心模块（解析/模型/布局/导出/存储/MCP）、121 条单元测试 |
| **工程化** | 07-06 | 源码英文→中文注释翻译 |
| **CI/CD 迁移** | 07-07 | Jenkins/Ansible → GitHub Actions、clang-format/cppcheck 接入、SonarQube 可选 |
| **功能增强** | 07-07 ~ 07-08 | Excalidraw 白板精确导出、离线字体内嵌、Excalidraw files 保真 |
| **架构升级** | 07-08 ~ 07-09 | CLI 重构（9 命令→12 命令族）、版本管理（Draft/Stage/Commit）、游标操作、drawio 解析回导 |
| **收尾完善** | 07-10 | 编辑器自动发现、MCP 协议补全、冒烟测试增强、应用原理文档 |

### 4.2 开发节奏

| 指标 | 数值 |
|------|------|
| 总提交数 | 40+ |
| 总 PR 数 | 26 |
| 开发天数 | 6 天 |
| 核心模块 | 10 个 C++ 头文件 |
| 代码规模 | 合计约 7000+ 行 |

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
| 冒烟测试 | `smoke_test.sh`（12 命令族全量） | `make smoke` + CI |
| MCP 协议测试 | `mcp_smoke.sh` | CI |
| 样例导出矩阵 | `export-example-testout.sh` | `make export-testout` |

### 5.4 DevOps 演进

| 阶段 | 工具链 | 说明 |
|------|--------|------|
| 初版 | Jenkins + Ansible | 07-05 搭建，遵循需求文档 |
| 迁移 | GitHub Actions | 07-07 迁移，更适合 GitHub 生态 |
| 现状 | GitHub Actions CI + 可选 SonarQube + GitLab 镜像 | 详见 `.github/workflows/` |

---

## 六、完成初步开发后的下一步目标

### 6.1 功能扩展

| 目标 | 说明 |
|------|------|
| **实时画布预览** | 通过 SVG + 本地 HTML 轮询 `latest.json` 实现实时预览 |
| **更多 Mermaid 类型** | classDiagram（类图）、stateDiagram（状态图） |
| **draw.io URL** | 需要 deflate 压缩，暂以零依赖为由缓做 |
| **分层布局优化** | 引入 median 启发式减少连线交叉 |

### 6.2 工程提升

| 目标 | 说明 |
|------|------|
| **SQLite 可选后端** | 大图检索场景下替代 JSON 文件存储 |
| **贡献指南** | 开源协作规范文档（`CONTRIBUTING.md`） |
| **Release 制品** | Tag 触发多平台二进制发布（Windows/Linux/macOS） |

---

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
| 8 | MCP 接口：创建 / 转换 / 打开 / 导出 | ✅ |
| 9 | 可选：实时画布 | ❎ |
| 10 | C++ / CLI / JSON 存储 / Git | ✅ |
