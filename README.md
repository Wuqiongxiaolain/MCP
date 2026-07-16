# graphmcp — 图形设计与绘图 MCP 工具

> latest update: v0.2.5-beta, 2026-07-16

> Gitlab 为镜像备份仓，活跃项目管理见 GitHub：https://github.com/Wuqiongxiaolain/MCP

用 **C++17** 编写的图形设计与绘图工具：零第三方依赖、单可执行文件。既可作 **CLI**，也可作 **MCP** 服务器接入 Claude Code / Claude Desktop 等客户端。

---

## 文档导航

文档按「项目信息 → 功能概览 → 快速开始 → 原理说明 → 开发记录」组织，与课程文档总架构一致。

| 层级 | 文档 | 说明 |
|------|------|------|
| **项目信息** | [项目全景](docs/PROJECT_OVERVIEW.md) | 来源、目标、启动与开发流程、代码管理、下一阶段目标 |
| **流程详情** | [时间线](docs/PROJECT_TIMELINE.md) · [开发过程](docs/DEV_PROCESS.md) | P1–P12 里程碑列表（07-05→07-16）；按提交还原的逐日演进与变更摘要 |
| **功能概览** | [功能介绍](docs/FEATURES.md) | 六种图、19 种 Mermaid、格式进出、颜色/图层/多页、版本/编辑闭环、MCP 与 Table 能力 |
| **快速开始** | [Quick Start](docs/QUICK_START.md) | 下载 exe、配置 Claude Code / Desktop、验证 MCP、调试 |
| **用户手册** | [用户手册](docs/USER_GUIDE.md) | 场景化教程、完整命令示例、输入输出格式速查、FAQ |
| **原理说明** | [应用运作逻辑](docs/APPLICATION_LOGIC.md) | 总架构、双一等模型、存储、IO 管道、版本管理、桥接协作 |

### 附录与参考

| 文档 | 说明 |
|------|------|
| [CLI & MCP 指令参考](docs/CLI_MCP_REFERENCE.md) | 15 个命令族与 46 个 MCP 工具完整参数速查（以 `toolList()`/OpenAPI 为准） |
| [OpenAPI 契约](docs/api_reference/openapi.yaml) | 由 `make docs-api` 从 `toolList()` 生成，可供 Swagger 打开 |
| [项目思维导图](docs/MINDMAP.md) | 能力与 DevOps 全景（Mermaid / 大纲） |

示例输入见 [`examples/example_input/`](examples/example_input/)，导出基准见 [`examples/README.md`](examples/README.md)。MCP 配置模板：[`mcp-config.example.json`](mcp-config.example.json)。Skill 定义：[`skills/graphmcp/SKILL.md`](skills/graphmcp/SKILL.md)。

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

### 版本 / 基线 / 发布（GitHub Actions + Jenkins）

- **日常 CI**：借助GitHub服务器的Github Acitons用于快速验证、快捷落地、敏捷迭代；部署于本地Docker镜像的Jenkins+Ansible用于最终验证、独立部署。
- **刷新性能基线**：Actions → `Update bench baseline`（`workflow_dispatch`，须勾选确认）。
- **写回 VERSION + OpenAPI**：Actions → `Bump version`（传入完整版本号并确认；**不**自动打 tag）。
- **发布制品**：推送 `v*` tag 触发多平台 CD Release（Windows/Linux/macOS）。
- **本地 DevOps**：Jenkins（Docker 镜像）→ Ansible Runner → nginx 下载站（见 `Jenkinsfile`、`ansible/`、`docker/`）。

无第三方依赖：JSON / XML / Base64 均为内置实现。

---

## 许可

课程实践项目，仅供学习使用。
