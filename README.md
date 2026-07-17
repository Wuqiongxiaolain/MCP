# graphmcp — 图形设计与绘图 MCP 工具

> latest update: v0.2.9-beta, 2026-07-17

> Gitlab 为镜像备份仓，活跃项目管理见 GitHub：https://github.com/Wuqiongxiaolain/MCP

用 **C++17** 编写的图形设计与绘图工具：零第三方依赖、单可执行文件。既可作 **CLI**，也可作 **MCP** 服务器接入 Claude Code / Claude Desktop 等客户端。

当前能力摘要：15 个 CLI 命令族、**51** 个 MCP 工具；分层布局（层平衡 / 减交叉 / waypoint）；Agent 宜优先 `graph_apply` / `graph_set_edge_route` / `graph_nudge_node` 等**原子改图**。整图 `export model` → 手改 → `import` **可以**，但是下下策，仅当原子工具无法表达该修改时再用。

---

## 文档导航

文档按「项目信息 → 功能概览 → 快速开始 → 原理说明 → 开发记录」组织，与课程文档总架构一致。

| 层级 | 文档 | 说明 |
|------|------|------|
| **项目信息** | [项目全景](docs/PROJECT_OVERVIEW.md) | 来源、目标、启动与开发流程、代码管理、下一阶段目标 |
| **流程详情** | [时间线](docs/PROJECT_TIMELINE.md) · [开发过程](docs/DEV_PROCESS.md) | P1–P13 里程碑与扩展期；按提交还原的逐日演进与变更摘要 |
| **功能概览** | [功能介绍](docs/FEATURES.md) | 六种图、19 种 Mermaid、格式进出、颜色/图层/多页、布局/几何编辑、版本/编辑闭环、MCP 与 Table 能力 |
| **快速开始** | [Quick Start](docs/QUICK_START.md) | 下载 exe、配置 Claude Code / Desktop、验证 MCP、调试 |
| **用户手册** | [用户手册](docs/USER_GUIDE.md) | 场景化教程、完整命令示例、输入输出格式速查、FAQ |
| **原理说明** | [应用运作逻辑](docs/APPLICATION_LOGIC.md) | 总架构、双一等模型、存储、IO 管道、版本管理、桥接协作 |

### 附录与参考

| 文档 | 说明 |
|------|------|
| [CLI & MCP 指令参考](docs/CLI_MCP_REFERENCE.md) | 15 个命令族与 **51** 个 MCP 工具完整参数速查（以 `toolList()`/OpenAPI 为准） |
| [OpenAPI 契约](docs/api_reference/openapi.yaml) | 由 `make docs-api` 从 `toolList()` 生成，可供 Swagger 打开；**真源为 `toolList()`，勿手改 YAML** |
| [测试报告（CI Artifact）](https://github.com/Wuqiongxiaolain/MCP/actions) | CI 汇总 Artifact（`docs-test-report-*`，含 GO/NO-GO）；与 `examples/example_testout/TEST_REPORT`（导出明细）职责不同；`docs/TEST_REPORT.md` **不入库** |
| [变更日志](CHANGELOG.md) | 按版本摘要；发版时维护 |
| [项目思维导图](docs/MINDMAP.md) | 能力与 DevOps 全景（Mermaid / 大纲） |
| [文档插图版本库](docs/diagrams/README.md) | graphmcp 可复现制图目录；SVG 在 `docs/images/` |

### DevOps 验收与运维

| 文档 | 说明 |
|------|------|
| [验收清单 DoD](docs/ACCEPTANCE_DOD.md) | 合入/发版签核勾选表与 GO/NO-GO |
| [运维 Runbook](docs/RUNBOOK.md) | Jenkins / Ansible / nginx 启停、发版、排查、回滚 |
| [质量门报告模板](docs/templates/QUALITY_GATE_REPORT.md) | cppcheck 必过；Sonar 可选（SKIPPED 须明示） |
| [发布/部署报告模板](docs/templates/DEPLOY_RELEASE_REPORT.md) | 版本、制品哈希、健康检查、回滚 |
| [性能报告模板](docs/templates/PERF_REPORT.md) | bench 18 指标 vs 基线 |
| [代码审查归档](docs/reviews/README.md) | 重大改造审查结论入库约定 |

### 技术选型分析

| 文档 | 说明 |
|------|------|
| [Web 前后端分离 vs 单可执行文件](docs/ANALYSIS_WEB_VS_SINGLE_EXE.md) | 部署与交互形态对比；说明为何采用单 exe + CLI/MCP |
| [vcpkg vs 零第三方依赖](docs/ANALYSIS_VCPKG_VS_ZERO_DEP.md) | 包管理与自研实现对比；说明为何坚持零链接依赖 |
| [JSON 持久化 vs SQLite](docs/ANALYSIS_JSON_VS_SQLITE.md) | 存储引擎对比；说明为何采用文件系统 JSON 快照 |

### 课程原始需求（MiniTasks）

| 文档 | 说明 |
|------|------|
| [课题需求 brief](docs/MiniTasks/basic.md) | 课程下达的图形 MCP 工具功能与工程约束原文 |
| [C++ 脚手架参考](docs/MiniTasks/toolpacks.md) | 课程附带的 vcpkg / clang-format / cppcheck 脚手架说明（历史参考；本项目实际为零依赖） |

示例输入见 [`examples/example_input/`](examples/example_input/)，导出基准见 [`examples/README.md`](examples/README.md)。MCP 配置模板：[`mcp-config.example.json`](mcp-config.example.json)。Skill 定义：[`skills/graphmcp/SKILL.md`](skills/graphmcp/SKILL.md)。

> **文档树约定**：上表为以本 README 为根的导航入口；`docs/` 下 Markdown 均应能从本页（或经一跳子页）到达。可用 `python scripts/check_docs_links.py` 校验可达性与断链。

---

## 构建

```sh
# Windows (MinGW)
mingw32-make all
# 或: g++ -std=c++17 -O2 -Wall -Wextra -static -o bin/graphmcp.exe src/main.cpp

# Linux / macOS
make all && make test

# 从 toolList() 生成 OpenAPI（改 MCP schema 后执行并提交）
make docs-api
```

### 常用 make 目标

| 目标 | 说明 |
|------|------|
| `make all` | 编译主程序 + 测试 |
| `make test` / `make test-all` | 单元测试（模型/版本/游标） |
| `make bench` / `make bench-ci` | 性能基准（18 指标）/ CI 基线比对 |
| `make smoke` / `make mcp-smoke` | 冒烟测试 / MCP 协议测试 |
| `make table-smoke` | 表与图↔表协作冒烟 |
| `make docs-api` | `dump-tools` → OpenAPI YAML |
| `make docs-test-report` | 从 `docs/ci_results/` **组装**报告（不重跑；CI 用） |
| `make docs-test-report-local` | 本地完整重跑后生成报告（调试用） |
| `make docs-quality-gate` | cppcheck + Sonar 状态 → 质量门报告 |
| `make docs-deploy-report` | 本地组装发布/部署报告骨架 |

### 版本 / 基线 / 发布（GitHub Actions + Jenkins）

- **日常 CI**：借助 GitHub Actions 用于快速验证、快捷落地、敏捷迭代；部署于本地 Docker 镜像的 Jenkins+Ansible 用于最终验证、独立部署。
- **刷新性能基线**：Actions → `Update bench baseline`（`workflow_dispatch`，须勾选确认）。
- **写回 VERSION + OpenAPI**：Actions → `Bump version`（传入完整版本号并确认；**不**自动打 tag）。
- **发布制品**：推送 `v*` tag 触发多平台 CD Release（Windows/Linux/macOS）。
- **本地 DevOps**：Jenkins（Docker 镜像）→ Ansible Runner → nginx 下载站（见 `Jenkinsfile`、`ansible/`、`docker/`）。

无第三方依赖：JSON / XML / Base64 均为内置实现。

---

## 许可

课程实践项目，仅供学习使用。
