# Quick Start — 快速接入 graphmcp

> latest update: v0.2.6-beta, 2026-07-16

> 下载 exe → 改一行配置 → Claude 就能画图（和管表）。不需要 clone 仓库。

场景化教程见 [用户手册](USER_GUIDE.md)；命令与 MCP 工具速查见 [CLI & MCP 指令参考](CLI_MCP_REFERENCE.md)。

---

## 1. 下载

去 [GitHub Releases](https://github.com/Wuqiongxiaolain/MCP/releases) 下载最新版本：

- **Windows**：`graphmcp.exe`
- **Linux**：`graphmcp`
- **macOS**：`graphmcp`

把可执行文件放到任意目录，比如 `C:\tools\graphmcp.exe`（Windows）或 `/usr/local/bin/graphmcp`（Linux/macOS）。

> Release 制品由 Tag 推送触发多平台构建，含 Windows/Linux/macOS。

> **不需要 clone 整个仓库。** `graphmcp.exe`（Windows）或 `graphmcp`（Linux/macOS）是单文件、零依赖、静态编译的，一个可执行文件就是完整的 MCP 服务。

---

## 2. 配置

根据你用的客户端，选一个配置文件：

### Claude Code CLI

编辑 `%USERPROFILE%\.claude\settings.json`：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "C:/tools/graphmcp.exe",
      "args": ["serve"]
    }
  }
}
```

### Claude Desktop

编辑 `%APPDATA%\Claude\claude_desktop_config.json`：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "C:/tools/graphmcp.exe",
      "args": ["serve"]
    }
  }
}
```

> 两个客户端配置格式一样，但**文件路径不同**。`command` 改成你实际放 exe 的路径，正斜杠 `/`。

---

## 3. 验证

重启 Claude Code / Claude Desktop，输入：

```
/mcp list
```

看到 `graphmcp` 即成功。试试效果：

> 画一张用户登录流程图：输入账号 → 验证密码 → 进入主页 / 提示错误

**调试**：若连接失败，可加 `GRAPHMCP_LOG=debug` 查看 stderr 日志：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "C:/tools/graphmcp.exe",
      "args": ["serve"],
      "env": {
        "GRAPHMCP_LOG": "debug"
      }
    }
  }
}
```

> 日志仅写 stderr，不影响 MCP 协议通信（stdio 上的 JSON-RPC）。

---


## 配置速查

| 客户端                    | 配置文件                                                                |
| ------------------------- | ----------------------------------------------------------------------- |
| Claude Code CLI（全局）   | `C:\Users\<用户名>\.claude\settings.json`                             |
| Claude Code CLI（单项目） | `<项目根>\.mcp.json`                                                  |
| Claude Desktop            | `C:\Users\<用户名>\AppData\Roaming\Claude\claude_desktop_config.json` |

统一格式：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "你的路径/graphmcp.exe",
      "args": ["serve"]
    }
  }
}
```

---

## 附：固定图存储位置

graphmcp 默认在运行目录下创建 `graph-store\`。想固定到指定目录：

```json
{
  "mcpServers": {
    "graphmcp": {
      "command": "C:/tools/graphmcp.exe",
      "args": ["serve"],
      "env": {
        "GRAPHMCP_STORE": "D:/my-graphs"
      }
    }
  }
}
```

> 仓库根目录 [`mcp-config.example.json`](../mcp-config.example.json) 提供了完整配置模板，可直接参考修改。

---

## 附：不只是画图——通用表协作

graphmcp 内置**通用表（Table）**为并列一等模型。AI 可帮你：

- 从思维导图抽出校验规则 → 检查业务表 → 自动修复非法枚举
- 技能关系 CSV → 一键生成关系图
- 派生动画清单、生成 slug 键、占位行提案

```text
> 把 enemies.csv 导入成表，用 mindmap 里的分类规则检查，不合法的自动修
```

详情见 [用户手册 · 通用表](USER_GUIDE.md) 与 [CLI & MCP 参考](CLI_MCP_REFERENCE.md)。
