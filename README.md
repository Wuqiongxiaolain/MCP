# graphmcp — 图形设计与绘图 MCP 工具

> latest update: v0.2.0, 2026-07-14

> Gitlab 为镜像备份仓，活跃项目管理见 GitHub：https://github.com/Wuqiongxiaolain/MCP

用 **C++17** 编写的图形设计与绘图工具：零第三方依赖、单可执行文件。既可作 **CLI**，也可作 **MCP** 服务器接入 Claude Code / Claude Desktop 等客户端。

---

## 文档导航

文档按「项目信息 → 功能概览 → 快速开始 → 原理说明 → 开发记录」组织，与课程文档总架构一致。

| 层级 | 文档 | 说明 |
|------|------|------|
| **项目信息** | [项目全景](docs/PROJECT_OVERVIEW.md) | 来源、目标、启动与开发流程（精简）、代码管理、下一阶段目标 |
| **流程详情** | [时间线](docs/PROJECT_TIMELINE.md) · [开发过程](docs/DEV_PROCESS.md) | 里程碑列表；按提交还原的逐日演进与变更摘要 |
| **功能概览** | [功能介绍](docs/FEATURES.md) | 六种图、格式进出、版本/编辑闭环、MCP 能力与技术规格 |
| **快速开始** | [Quick Start](docs/QUICK_START.md) | 下载 exe、配置 Claude / Cursor、验证 MCP |
| **原理说明** | [应用运作逻辑](docs/APPLICATION_LOGIC.md) | 总架构、MCP、存储、IO、统一图模型、版本管理、业务流程 |

### 附录与参考

| 文档 | 说明 |
|------|------|
| [用户手册](docs/USER_GUIDE.md) | 场景化教程与完整命令说明 |
| [CLI & MCP 指令参考](docs/CLI_MCP_REFERENCE.md) | 命令族与 46 个 MCP 工具速查表（以 `toolList()`/OpenAPI 为准） |
| [OpenAPI 契约](docs/api_reference/openapi.yaml) | 由 `make docs-api` 从 `toolList()` 生成，可供 Swagger 打开 |
| [项目思维导图](docs/MINDMAP.md) | 能力与 DevOps 全景（Mermaid / 大纲） |

示例输入见 [`examples/example_input/`](examples/example_input/)，导出基准见 [`examples/README.md`](examples/README.md)。MCP 配置模板：[`mcp-config.example.json`](mcp-config.example.json)。

---

## 构建

```sh
# Windows (MinGW)
mingw32-make all
# 或: g++ -std=c++17 -O2 -Wall -Wextra -o bin/graphmcp.exe src/main.cpp

# Linux / CI
make all && make test

# 从 toolList() 生成 OpenAPI（改 MCP schema 后执行并提交）
make docs-api
```

无第三方依赖：JSON / XML / Base64 均为内置实现。

---

## 许可

课程实践项目，仅供学习使用。
