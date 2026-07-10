# Quick Start — 快速接入 graphmcp

> 下载 exe → 改一行配置 → Claude 就能画图。不需要 clone 仓库。

---

## 1. 下载

去 [GitHub Releases](https://github.com/Wuqiongxiaolain/MCP/releases) 下载最新 `graphmcp.exe`。

把 `graphmcp.exe` 放到任意目录，比如 `C:\tools\graphmcp.exe`。

> **不需要 clone 整个仓库。** `graphmcp.exe` 是单文件、零依赖、静态编译的，一个 exe 就是完整的 MCP 服务。qui

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
