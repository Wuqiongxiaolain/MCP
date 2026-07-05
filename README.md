# graphmcp — 图形设计与绘图 MCP 工具

一个用 **C++17** 编写的图形设计与绘图工具，零第三方依赖，单可执行文件。
既可作为 **CLI 工具** 使用，也可作为 **MCP (Model Context Protocol) 服务器**
接入 Claude Code / Claude Desktop 等 AI 客户端。

## 功能总览

| 能力 | 说明 |
|---|---|
| 输入格式 | Mermaid（flowchart / mindmap / erDiagram）、Markdown 大纲、CSV（边表 / 层级表）、XML、Excalidraw JSON |
| 图形类型 | 流程图、架构图、ER 图、组织结构图、脑图、草图风格白板图 |
| 统一图模型 | 节点（形状/层级/ER属性/坐标）+ 连线（样式/箭头/标签）+ 白板原始元素 |
| 校验 | 重复 ID、悬空连线、层级环、孤立节点、空标签 |
| 布局 | 分层布局（流程/架构）、树布局（脑图横向 / 组织图纵向）、网格兜底 |
| 导出 | `.drawio`、Mermaid、Excalidraw JSON、SVG、PNG、PDF、浏览器 URL、统一模型 JSON |
| 编辑器调起 | mermaid.live 浏览器 URL、生成 .drawio/.excalidraw/.svg 并用系统默认程序打开 |
| 存储 | JSON 文件存储，含版本历史、历史查询、版本回溯 |
| MCP 接口 | 8 个工具：create / convert / export / open / validate / list / history / rollback |

## 构建

```sh
# Windows (MinGW)
mingw32-make all        # 或直接:
g++ -std=c++17 -O2 -Wall -Wextra -o bin/graphmcp.exe src/main.cpp

# Linux / CI
make all && make test
```

无第三方依赖：JSON 解析器、XML 解析器、Base64 编码均为内置实现。

## CLI 用法

```sh
# 1. 创建：解析 -> 校验 -> 自动布局 -> 存入版本库
graphmcp create --input examples/flowchart.mmd --name "登录流程" --id demo-flow

# 2. 一次性格式转换（不入库）
graphmcp convert --input examples/orgchart.csv --to mermaid
graphmcp convert --input examples/mindmap.md --to svg -o mind.svg

# 3. 从版本库导出
graphmcp export --id demo-flow --to drawio -o flow.drawio
graphmcp export --id demo-flow --to url          # 打印 mermaid.live 链接
graphmcp export --id demo-flow --to png -o flow.png   # 需要 inkscape/rsvg/magick，否则回退 SVG

# 4. 调起外部编辑器
graphmcp open --id demo-flow --editor browser    # 浏览器打开 mermaid.live
graphmcp open --id demo-flow --editor drawio     # 生成 .drawio 并用默认程序打开

# 5. 校验 / 版本管理
graphmcp validate --input examples/er.mmd
graphmcp history --id demo-flow
graphmcp rollback --id demo-flow --version 1

# 6. 启动 MCP 服务器（stdio）
graphmcp serve
```

存储目录默认 `./graph-store`，可用环境变量 `GRAPHMCP_STORE` 覆盖。

## 接入 MCP 客户端

Claude Code：

```sh
claude mcp add graphmcp -- D:/MCP/bin/graphmcp.exe serve
```

或在配置文件中（见 `mcp-config.example.json`）：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "D:/MCP/bin/graphmcp.exe",
      "args": ["serve"],
      "env": { "GRAPHMCP_STORE": "D:/MCP/graph-store" }
    }
  }
}
```

之后即可在对话中让 AI 调用 `graph_create`、`graph_export`、`graph_open` 等工具。

## 输入格式速览

**CSV 边表**（→ 流程图）: 表头 `from,to[,label]`；**CSV 层级表**（→ 组织结构图）: 表头 `id,label[,parent]`。

**XML**:

```xml
<graph type="architecture" name="系统架构">
  <node id="web" label="Web 前端"/>
  <node id="cluster" label="后端集群">
    <node id="api" label="API 服务" shape="round"/>
  </node>
  <edge from="web" to="api" label="http" style="dashed"/>
</graph>
```

Markdown 大纲（`#` 标题 + `-` 列表缩进）→ 脑图；Excalidraw JSON → 白板图（原始元素无损保留，可完整回写）。

## 目录结构

```
src/            核心源码（header-only 模块 + main.cpp）
  json.hpp        JSON 解析/序列化
  model.hpp       统一图模型
  parsers.hpp     5 种输入解析器 + 格式自动识别
  layout.hpp      布局引擎 + 结构校验
  exporters.hpp   6 种导出器 + URL 生成 + 编辑器调起
  storage.hpp     版本化 JSON 存储
  mcp.hpp         MCP JSON-RPC 服务器
tests/          单元测试（121 断言）
examples/       六种输入格式示例
ansible/        自动化部署 playbook
Jenkinsfile     CI/CD 流水线（构建→测试→Sonar→打包→部署）
sonar-project.properties  SonarQube 静态分析配置
docs/           架构说明、思维导图、工作记录
```

## DevOps 流水线

`Jenkinsfile` 定义的流水线阶段：

1. **Checkout** — 拉取代码
2. **Build** — `make all`
3. **Unit Tests** — `make test`（121 断言）
4. **Smoke Test** — CLI 全链路 + MCP stdio 会话脚本
5. **SonarQube Analysis + Quality Gate** — 静态分析，不达标即中断
6. **Package** — 产出 `graphmcp-<BUILD>.tar.gz` 制品
7. **Deploy** — `main` 分支触发 Ansible 部署到 `ansible/inventory.ini` 中的主机

## 许可

课程实践项目，仅供学习使用。
