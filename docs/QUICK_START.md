# Quick Start — 快速接入 graphmcp

> latest update: v0.1.1, 2026-07-10

> 下载 exe → 改一行配置 → Claude 就能画图。不需要 clone 仓库。

场景化教程见 [用户手册](USER_GUIDE.md)；命令与 MCP 工具速查见 [CLI & MCP 指令参考](CLI_MCP_REFERENCE.md)。

---

## 1. 下载

去 [GitHub Releases](https://github.com/Wuqiongxiaolain/MCP/releases) 下载最新 `graphmcp.exe`。

把 `graphmcp.exe` 放到任意目录，比如 `C:\tools\graphmcp.exe`。

> **不需要 clone 整个仓库。** `graphmcp.exe` 是单文件、零依赖、静态编译的，一个 exe 就是完整的 MCP 服务。

> **安装 MCP ≠ 安装 Skill。** 防误用黄金路径已写入 exe 内 `tools/list`。Release 中可选附带 `skills/graphmcp/`，仅对支持 Skill 的宿主为增益；无 Skill 的验收见 [NO_SKILL_HOST_CHECKLIST.md](NO_SKILL_HOST_CHECKLIST.md)。

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
