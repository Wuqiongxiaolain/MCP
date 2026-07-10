# graphmcp 开发日志

> 倒序日记体，记录每日卡点与决策，不贴 Hash，不列统计。

---

## 2026-07-10

### 今日卡点

- **graph_open 无编辑器环境兜底**：CI（Ubuntu headless）无 draw.io/VS Code，`availableEditors` 字段被跳过不写，导致单测 `odj.find("availableEditors") != nullptr` 失败。修复为始终写入该字段（空则写 `""`）。
- **mcp_smoke 工具数断言**：新增 `graph_import` 后工具数 24→25，mcp_smoke.sh 硬编码 24 导致 CI 失败。已修复。
- **macOS CD 构建失败**：`exporters.hpp` 的 `#elif defined(__APPLE__)` 块缺少 `<dirent.h>` 和 `<unistd.h>`，8 个编译错误。暂时在 CD 矩阵中禁用 macOS，待修复头文件后恢复。
- **CD 分支测试**：新增 `workflow_dispatch` 手动触发 + `dry_run` 开关，允许在测试分支上构建 CD 制品而不创建 Release。测试通过后 `ah_feng_log` 合并至 main。

### 今日决策

- `openExternal()` 保持三平台 `editor` 参数为可选（默认 `""`），降级策略为：指定编辑器失败 → 系统默认关联重试。不引入新返回值类型，保持向后兼容。

### 明日计划

- 发布 `v0.1.0-beta2`：Linux + Windows 安装包（macOS 待头文件修复后恢复）。
- `exporters.hpp` 已达 2262 行，需拆分为 `editor_launch.hpp`。
- macOS `<dirent.h>`/`<unistd.h>` 补完 + 恢复 CD macOS 构建。

---

## 2026-07-09

### 今日卡点

- **合并冲突**：`ah_feng-editor-v2` 合并 main 时 5 文件冲突（exporters.hpp / main.cpp / mcp.hpp / storage.hpp / test_main.cpp）。main 在期间完成了 CLI 重构 + 版本管理 + 游标系统。逐个手动解决后，3 个测试函数被覆盖丢失，需从旧分支恢复。
- **mcp_smoke 断言死锁**：MCP 冒烟脚本用 `tools/list` 校验工具数，新增 `graph_import` 后从 24 变 25，但脚本中硬编码了 24。本地无法复现（缺 jq），CI 报错后才定位。

### 今日决策

- `graph_import` 插入在 `graph_open` 和 `graph_validate` 之间，保持工具编号连续。
- `parseDrawio()` 复用已有 `detail::parseXmlDoc` 内联解析器，不引入新 XML 库。

---

## 2026-07-08

### 编辑闭环实现

- 新增 6 个编辑器发现函数（`editorFromEnv` / `findExecutable` / `findDrawioDesktop` / `findVSCode` / `resolveEditor` / `readOpenFile`），全部位于 `exporters.hpp`。
- `parseDrawio()` 新增于 `parsers.hpp`，约 185 行：解析 draw.io XML（mxCell vertex/edge）回 Graph 模型。
- CLI 新增 `import` 命令族，MCP 新增 `graph_import` 工具。

### 踩坑记录

- **MinGW `_dupenv_s` 缺失**：旧代码 `#ifdef _WIN32` 使用 `_dupenv_s`，MinGW 不支持。改为 `#if defined(_WIN32) && !defined(__MINGW32__)` 回退到 `getenv`。
- **MCP 协议 stderr 干扰**：`GRAPHMCP_LOG=info` 启用日志后，确认日志写入 stderr 不污染 stdout JSON-RPC 通道。

---

## 2026-07-07 ~ 2026-07-05

> 项目启动与核心搭建期。详细记录见 `docs/PROJECT_SUMMARY_20260710.md`。

### 关键架构选择

- **零依赖**：手写 JSON/XML/Base64，拒绝 nlohmann/pugixml。决策原因：保持单文件可执行、避免 ABI 兼容问题、降低 MCP 客户端集成复杂度。
- **统一图模型**：N 种输入 → Graph → M 种输出。新增格式只需一个 `parse*` 或 `to*` 函数对。
- **单文件头模块**：每个模块一个 `.hpp`，全 inline 实现。编译为单个 `graphmcp` 可执行文件。
- **Jenkins → GitHub Actions**：P3 移除 Jenkins，统一到 GitHub Actions。原因：团队已在 GitHub 协作，无需额外 CI 服务。
- **rough.js → 自研 SVG**：P4 移除 rough.js 近似渲染，自研精确 SVG 矢量化。原因：rough.js 不支持 freedraw 压力感轮廓、Excalifont 字体内嵌。

### 已知坑

- `exporters.hpp` 从 1300 行膨胀至 2262 行，集成了 7 种导出 + 编辑器发现 + 浏览器启动，需拆分。
- macOS/Linux 编辑器路径探测未在 CI 中覆盖（仅 Ubuntu 单 runner）。
- Mermaid 不支持时序图（sequenceDiagram），这是高频需求但需不同模型抽象。
