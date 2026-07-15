# MCP 协议版本 Bug 分析报告

> 背景:排查 `graphmcp` MCP server 连接失败问题时,发现 `main` 分支源码本身存在两个独立缺陷。本文基于 `origin/main` 当前真实代码 + 本地重新编译验证,记录分析过程与结论。

## 现象

`claude mcp list` 对 `graphmcp` server 报 `✘ Failed to connect`,但进程本身能正常启动、正常退出(`exit code 0`),表面上看不出原因。

用原始 JSON-RPC 直接模拟 MCP `initialize` 握手,发现服务端返回:

```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2026-7-10","capabilities":{"tools":{}},"serverInfo":{"name":"graphmcp","version":"unknown"}}}
```

`protocolVersion` 不是 MCP 规范定义的合法值(规范里应为 `"2024-11-05"` 这类固定日期字符串),客户端无法识别,直接判定握手失败——这才是连接失败的真正原因,和 MCP 配置作用域无关。

## 源码走读:`origin/main` 现在的真实代码

```cpp
inline const char* SERVER_NAME = "graphmcp";
inline const char* PROTOCOL_VERSION = "2026-7-10";   // 硬编码字符串字面量

// serverVersion: 从仓库根目录 VERSION 读取应用版本，作为单一版本来源（SSOT）
inline const std::string& serverVersion()
{
    static const std::string cached = []() {
        std::string v = ge::readFile("VERSION");   // 相对路径，相对的是"当前工作目录"
        ...
        return v.empty() ? std::string("unknown") : v;
    }();
    return cached;
}
```

这里是两个性质完全不同的问题:

### 问题一:`PROTOCOL_VERSION` 硬编码错误

写死的字符串常量,编译进二进制后固定不变,与运行目录、启动方式无关。只要是从当前 `main` 分支编译出来的 exe,这个值永远错误。

**来源**(`git log -p --all -- src/mcp.hpp` 追溯到的提交):

| 提交 | 时间 | 说明 |
|---|---|---|
| `08100e3` | 2026-07-10 14:16 | `docs: 文档分层重构 + 版本号对齐 v0.1.0` —— 把项目文档版本号对齐工作和 MCP 协议版本号搞混,`PROTOCOL_VERSION` 从 `"2024-11-05"` 被改成 `"2026-7-10"`(疑似把当天日期当成协议版本填了进去) |
| `6c21241` | 2026-07-10 18:36 | `feat(config): 以 VERSION 作为应用版本单一来源`(经 PR #48 `docs/version-ssot-v0.1.1` 合并入 main)—— 重构了 `SERVER_VERSION` 的读取方式,但没有连带修复已经错误的 `PROTOCOL_VERSION` |

这两个提交只存在于 `origin/main`,不在当前工作分支 `feature/docs` 上(`feature/docs` 落后 main 20 个提交,领先 0 个,单纯是分支久未同步,并非刻意规避)。**该 bug 目前仍存在于 `origin/main` 最新 tip,尚未修复。**

### 问题二:`serverVersion()` 依赖运行时工作目录

`ge::readFile("VERSION")` 用的是相对路径,读取的是进程启动时"当前工作目录"下的 `VERSION` 文件,不是 exe 自身所在目录,也不是仓库固定路径。这导致 `serverInfo.version` 的正确性完全取决于**从哪个目录启动这个 exe**。

## 验证:用隔离 worktree 现编现测

在临时 git worktree 中检出 `origin/main` 当前 tip(`78017cb`)并编译,用同一个 exe 切换三种工作目录分别测试握手响应:

| 启动时的工作目录 | protocolVersion | serverInfo.version |
|---|---|---|
| main worktree 自身目录(有 `VERSION` 文件,内容 `v0.1.1`) | `"2026-7-10"` ❌ | `"v0.1.1"` ✔ |
| `D:\MCP`(Claude Code 实际启动 MCP server 时使用的目录) | `"2026-7-10"` ❌ | `"unknown"` ❌ |
| `D:\ClaudeCodeTest`(exe 自身所在文件夹) | `"2026-7-10"` ❌ | `"unknown"` ❌ |

结论:
- `protocolVersion` 三种情况全部一致错误 → 证实是编译期硬编码,与运行环境无关。
- `version` 只有在"当前工作目录恰好是仓库根目录"时才正确,换到任何其它目录启动(包括作为全局工具跨项目使用的典型场景)必然是 `"unknown"`。这完整复现了最初在 `D:\ClaudeCodeTest\graphmcp.exe` 上观察到的现象。

## 关于 `D:\ClaudeCodeTest\graphmcp.exe` 的来源推测

`gh release list` 查到该仓库有正式 GitHub Release:

```
graphmcp v0.1.1-beta   Latest       2026-07-10 18:59  (windows-x64.zip,已下载 2 次)
v0.1.1-beta            Pre-release  2026-07-10 18:48
v0.1.0-beta            Pre-release  2026-07-10 10:37
```

`v0.1.1-beta` 附带的 Windows exe 发布于 7月10日 18:59,早于 `D:\ClaudeCodeTest\graphmcp.exe` 的文件时间(7月11日 09:03)约 14 小时,且已被下载 2 次。推测该文件是从这个官方 Release 下载解压而来,并非本地临时编译——CI 用 main 分支当前代码打的包,原样带上了上述两个 bug。

## 结论与后续

`D:\ClaudeCodeTest\graphmcp.exe` 现已用 `feature/docs` 分支的正确源码(`PROTOCOL_VERSION="2024-11-05"`, `SERVER_VERSION="1.1.0"`,硬编码常量、不依赖 VERSION 文件)重新编译覆盖,本地问题已解决。

`main` 分支本身的两个缺陷仍未修复,已记录为独立后续任务(修复 `PROTOCOL_VERSION` 硬编码值 + 让 `serverVersion()` 改用相对 exe 自身位置的绝对路径读取 `VERSION`,而非相对当前工作目录)。
