# graphmcp — 图形设计与绘图 MCP 工具

> Gitlab为镜像备份仓，活跃项目管理见Github仓库https://github.com/Wuqiongxiaolain/MCP

一个用 **C++17** 编写的图形设计与绘图工具，零第三方依赖，单可执行文件。
既可作为 **CLI 工具** 使用，也可作为 **MCP (Model Context Protocol) 服务器**
接入 Claude Code / Claude Desktop 等 AI 客户端。

## 文档

功能、架构与使用方式的详细说明见 `docs/` 目录：

| 文档 | 说明 |
|------|------|
| [架构说明](docs/ARCHITECTURE.md) | 总体设计、统一图模型、各模块职责、MCP 工具清单、存储布局与错误处理 |
| [CLI & MCP 指令参考](docs/CLI_MCP_REFERENCE.md) | 完整 CLI 命令族与 MCP 工具接口文档，含示例 |
| [项目思维导图](docs/MINDMAP.md) | 能力总览、输入/输出格式、质量保障与 DevOps 全景 |
| [更新日志](docs/WORKLOG.md) | 自项目创建以来的功能新增、变更与修复记录 |

示例输入见 `examples/example_input/`，导出基准与目录说明见 `examples/README.md`；  
旧样例名已迁移，如 `mindmap.md -> outline.md`、  
`whiteboard.excalidraw -> whiteboard_freedraw.excalidraw`。MCP 配置模板见  
`mcp-config.example.json`。

**近期更新（2026-07-08）**：Excalidraw 白板精确导出（image/files 保真、仿射变换、freedraw 轮廓、离线字体内嵌）；PNG/PDF 统一精确 SVG 栅格化；详见[更新日志](docs/WORKLOG.md)。

## 构建

```sh
# Windows (MinGW)
mingw32-make all        # 或直接:
g++ -std=c++17 -O2 -Wall -Wextra -o bin/graphmcp.exe src/main.cpp

# Linux / CI
make all && make test
```

无第三方依赖：JSON 解析器、XML 解析器、Base64 编码均为内置实现。

## 许可

课程实践项目，仅供学习使用。
