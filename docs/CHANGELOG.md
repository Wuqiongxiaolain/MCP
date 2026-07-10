# Changelog

> graphmcp 版本更新记录。最新改动在前。用户视角，不列提交 Hash。

---

## v1.1.0 (2026-07-09~10) — 外部编辑器闭环

### Added
- 编辑器自动发现：`resolveEditor()` 按优先级探测 draw.io Desktop / VS Code 安装路径
- `openExternal()` 支持显式编辑器参数，三平台降级策略（Windows ShellExecuteW / macOS open -a / Linux xdg-open）
- `parseDrawio()`：draw.io XML 反向解析为统一 Graph 模型
- MCP `graph_import` 工具：重新导入外部编辑后的文件，自动解析→校验→布局→入库
- CLI `import` 命令族：编辑回导入口，支持 `--id` / `--file` / `--content` / `--format`
- `graph_open` 响应新增 `editor`、`editorPath`、`availableEditors` 字段

### Changed
- `graph_open` 工具新增 `editorPath` 参数
- `edit` 命令新增 `--editor-path` 参数
- MCP 工具总数：24 → 25
- CLI 命令族：12 → 13

### Fixed
- MinGW 编译兼容（`_dupenv_s` → `getenv` 回退）
- macOS `open -a` 参数修复（提取 `.app` bundle 路径）
- `graph_open` 无编辑器环境始终返回 `availableEditors` 字段

---

## v1.0.0 (2026-07-05~09) — 核心引擎与架构重构

### Added
- JSON 解析/序列化、Base64 编码（手写内置，零第三方依赖）
- 统一图模型（Graph/Node/Edge）+ 5 种输入解析器（Mermaid/Markdown/CSV/XML/Excalidraw）
- 7 种输出导出器（drawio/mermaid/excalidraw/SVG/PNG/PDF/URL）
- 版本化 JSON 存储（index.json + latest.json + versions/vN.json）
- MCP stdio 服务（8 工具）→ 重构为 24 工具
- Draft-Stage-Commit 版本管理工作流
- Cursor 游标遍历系统
- Excalidraw 精确 SVG 导出（移除 rough.js 近似渲染）
- GitHub Actions CI + SonarQube 静态分析
- CLI `create`/`convert`/`export`/`edit`/`layout`/`validate`/`store`/`version`/`graph`/`cursor`/`draft`/`serve` 12 命令族

### Changed
- 输入格式新增 `drawio`
- CLI 从简单 `--flag` 格式重构为 `<family> <subcommand>` 格式
- 架构文档、用户指南、CLI & MCP 指令参考文档

---
