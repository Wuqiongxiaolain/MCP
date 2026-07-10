# graphmcp 项目里程碑复盘

> 项目启动：2026-07-05 — 最后更新：2026-07-10
> 当前版本：v0.1.0 | 技术栈：C++17 · 零第三方依赖 · 单可执行文件

---

## 项目总览

graphmcp 是一个 C++17 图形设计与绘图 MCP 工具。所有格式（Mermaid / Markdown / CSV / XML / Excalidraw / Drawio）先归一到统一图模型，再从模型出发做校验、布局、导出。

### 当前能力

| 维度 | 值 |
|------|-----|
| 代码规模 | ~9,800 行（src/）+ ~2,100 行（tests/） |
| MCP 工具 | 25 个 |
| CLI 命令族 | 13 个 |
| 输入格式 | mermaid / markdown / csv / xml / excalidraw / drawio / model / auto |
| 输出格式 | drawio / mermaid / excalidraw / svg / png / pdf / url / model |
| 测试断言 | 366 单元 + 99 CLI 冒烟 + 10 MCP 冒烟 = 475 |
| 版本管理 | Draft-Stage-Commit 工作流 + Cursor 游标遍历 |

### 贡献者

| 贡献者 | 主要贡献领域 |
|--------|-------------|
| wldxiaobai | CI/CD、冒烟测试、clang-format、Excalidraw 导出、CLI 重构、MCP 三层测试 |
| wyQuQ | CLI 重构、版本管理、游标系统、代码审查修复 |
| kliang | 项目搭建、核心引擎（JSON/模型/解析器/导出器/存储/MCP/CLI） |
| yifengsun | 外部编辑器调起、drawio 解析回导、CI 冒烟完善 |

---

## 开发阶段划分

| 阶段 | 名称 | 日期 | 核心交付 |
|------|------|------|---------|
| P1 | 核心引擎搭建 | 07-05 | JSON/模型/5解析器/7导出器/存储/MCP/CLI |
| P2 | 加固与中文化 | 07-06 | 注释中文化、静态链接、gitignore |
| P3 | CI/CD 工程化 | 07-07 | GitHub Actions、clang-format、SonarQube、制品发布 |
| P4 | Excalidraw 白板体系 | 07-08 | 精确 SVG、freedraw 矢量化、内嵌字体、files 附件 |
| P5 | 架构重构 | 07-09 | CLI 12命令族、Draft-Stage-Commit、Cursor游标、MCP 8→24 |
| P6 | 外部编辑器闭环 | 07-09~10 | 编辑器自动发现、drawio 解析、import/export 闭环 |

---

## P1: 核心引擎搭建（2026-07-05）

### 提交概要

一天内 12 次迭代完成全栈骨架：

1. **项目骨架**（Makefile + CMake + .gitignore）
2. **手写 JSON 库**（~500 行递归下降解析器，`\uXXXX`→UTF-8，保持插入顺序）
3. **统一图模型**（Graph/Node/Edge + `toJson`/`fromJson`）
4. **5 种输入解析器**：Mermaid 手写词法、Markdown 大纲、CSV 边列表、手写 XML 解析器、Excalidraw JSON
5. **自动布局引擎**（Kahn 分层 + 环状回退 + 子树宽度树布局）+ 结构校验
6. **7 种输出导出器**（drawio/mermaid/excalidraw/SVG/PNG/PDF/URL）
7. **版本化 JSON 存储**（index.json + latest.json + versions/vN.json 快照）
8. **MCP stdio 服务**（8 工具）+ **CLI 入口**（9 子命令）
9. **测试体系**（121 断言）+ **文档体系**（README + 架构说明 + mindmap）
10. Chrome/Edge 无头栅格化 + CreateProcessW 浏览器启动 + 静态链接

### 关键决策

- **零依赖**：手写 JSON/XML/Base64，保持单文件可执行，降低 MCP 客户端集成复杂度
- **统一图模型居中**："N 种输入 → Graph → M 种输出"，新增格式只需一个函数对
- **Mermaid 子集**：仅 flowchart/mindmap/erDiagram，明确放弃时序图/甘特图
- **PNG/PDF 回退策略**：inkscape → rsvg-convert → magick → Chrome/Edge 无头 → SVG 兜底

### 遗留

- rough.js 近似渲染（P4 替换为自研精确 SVG 引擎）
- 无代码格式化（P3 引入 clang-format）
- Jenkins CI 为模板残留（P3 迁移至 GitHub Actions）

---

## P2: 加固与中文化（2026-07-06）

### 提交概要

6 次提交完成代码质量提升：源码注释英译中、MinGW 静态链接（`-static-libgcc -static-libstdc++`）、`.gitignore` 完善。团队统一中文注释标准。

---

## P3: CI/CD 工程化（2026-07-07）

### 提交概要

20 次提交建立完整的 CI/CD 体系：

1. **移除 Jenkins/Ansible** → 替换为 **GitHub Actions**（Ubuntu 构建→单元测试→冒烟→打包）
2. **引入 clang-format** 统一代码排版（WebKit 风格）
3. **cppcheck 静态检查** + SonarCFamily build-wrapper 集成
4. **Tag 触发多平台制品发布** + GitLab 镜像同步（可选流水线）
5. copilot-swe-agent 首次介入：自动修复空指针解引用守卫
6. 删除 Excalidraw 连线端点冗余计算

### 关键决策

- 统一到 GitHub Actions：团队已在 GitHub 协作，无需额外 CI 服务
- SonarQube 和 GitLab 镜像均为可选（仓库变量控制），不强制

---

## P4: Excalidraw 白板体系（2026-07-08）

### 提交概要

20 次提交完成 Excalidraw 导出从近似渲染到精确 SVG 的跃迁：

1. **移除 rough.js** → 自研精确 SVG 矢量化引擎（`toSVGExcalidraw`）
2. **freedraw 矢量笔迹**提取与渲染，支持压力感轮廓
3. **内嵌字体**（Excalifont）+ `model.hpp` 新增 `files` 附件保真字段
4. **MACOS 路径探测**（`_NSGetExecutablePath` + `realpath`）
5. **白板 SVG 冒烟**（fixture-regression 比对测试）
6. 示例文件重组为 `example_input` / `example_output` 分离目录

### 关键决策

- 放弃 rough.js：不支持 freedraw 和字体，自研引擎保证无损往返
- `GRAPHMCP_ASSETS` 环境变量允许用户覆盖资源路径

---

## P5: 架构重构（2026-07-09）

### 提交概要

项目历史上最大的重构，~30 次提交，净增 ~3,500 行：

1. **新增 `version_manager.hpp`**（717 行）：Draft-Stage-Commit 工作流（仿 Git）
2. **新增 `cursor_types.hpp`**（547 行）：持久化游标（open/get/move/close）
3. **`main.cpp` 完全重写**（+1467/-）：从 if-else 链改为 `<family> <subcommand>` 两级 dispatch，12 个命令族
4. **`mcp.hpp` 大幅扩展**（+1262/-）：MCP 工具 8→24（新增 graph_layout/graph_delete/graph_show/graph_diff/graph_status/graph_update/graph_insert/graph_delete_element/graph_draft/graph_stage/graph_commit/graph_checkout/graph_cursor_*）
5. **测试体系三层化**：单元测试 + 版本管理测试（383行）+ 游标测试（301行）+ CLI 冒烟（99步）+ MCP 冒烟（271行）
6. **文档体系完善**：用户指南（695行）+ CLI & MCP 指令参考（385行）
7. **服务版本号**：SERVER_VERSION 升级至 `0.1.0`，协议版本 `2026-7-10`

### 关键决策

- **Draft-Stage-Commit 模型**：借鉴 Git，实现图的增量编辑而非全量替换
- **Cursor 遍历**：持久化游标解决大图遍历状态管理
- **日志系统**：`GRAPHMCP_LOG` 环境变量控制 stderr 结构化日志（不影响 stdout JSON-RPC）

### 遗留

- `exporters.hpp` 膨胀至 2262 行，需拆分
- macOS/Linux 编辑器路径未在 CI 中覆盖

---

## P6: 外部编辑器闭环（2026-07-09 ~ 2026-07-10）

### 提交概要

14 次提交实现"AI 调起 → 用户编辑 → 回导 → AI 继续"完整闭环：

1. **编辑器自动发现**（`editorFromEnv`/`findExecutable`/`findDrawioDesktop`/`findVSCode`/`resolveEditor`）
2. **`openExternal()` 重写**：Windows ShellExecuteW + macOS app bundle 路径提取 + Linux xdg-open + 三平台降级策略
3. **`parseDrawio()`**（~185 行）：draw.io XML 反向解析为 Graph 模型
4. **MCP `graph_import`** + CLI `import`：编辑回导入口
5. **CI 冒烟**：graph_open 5 场景 + graph_import 3 场景，移除 `ENABLE_GRAPH_OPEN_TEST` Flag
6. **3 次 CI 修复**：工具数断言 24→25、mcp_smoke 断言对齐、availableEditors 空值兜底

### 关键决策

- MCP 工具名保持 `graph_open`（对齐规范），CLI 保持 `edit`
- `parseDrawio()` 复用已有 `detail::parseXmlDoc`，不引入新依赖
- `availableEditors` 空值写入 `""` 而非省略，保证 JSON 结构一致

---

## 跨阶段趋势

| 指标 | P1 | P3 | P5 | P6 |
|------|----|----|----|-----|
| 源码行数 | ~4,200 | ~4,300 | ~9,300 | ~9,800 |
| MCP 工具数 | 8 | 8 | 24 | 25 |
| CLI 命令族 | 9 | 9 | 12 | 13 |
| 输入格式数 | 5 | 5 | 5 | 6 |
| 单元测试断言 | 121 | ~200 | ~317 | 366 |
| CI Job 数 | 0 | 2 | 3 | 3 |

- **P1→P4**：核心功能密集期，P4 Excalidraw 导出体系完善
- **P5**：增长爆发点，CLI 重写 + 版本管理 + 游标一次性注入 ~3,500 行
- **P6**：收尾阶段，小规模增量打通 AI→人→AI 回路

---

## 技术债地图

### 超大文件

| 文件 | 行数 | 风险 |
|------|------|------|
| `src/exporters.hpp` | 2,262 | 🔴 集成了 7 种导出 + 编辑器发现 + 浏览器启动 + Base64 + XML 转义 + 文件 IO |
| `src/main.cpp` | 1,557 | 🟡 12 个 cmdXxx() 函数集中 |
| `src/mcp.hpp` | 1,476 | 🟡 25 个工具 handler |
| `tests/test_main.cpp` | 1,372 | 🟡 25 个测试函数 |

### 测试缺口

- 测试/源码比 0.22（偏低）
- macOS/Linux 编辑器路径未在 CI 验证（仅 Ubuntu 单平台）
- `parseDrawio()` ER 表格解析仅有基础往返测试
- `import` CLI 命令无专门的 CLI 冒烟（仅 MCP 层覆盖）

### 下阶段建议

1. **拆分 `exporters.hpp`**：最高优先级，拆为 `export_drawio.hpp` / `editor_launch.hpp` 等
2. **多平台 CI**：增加 macOS runner
3. **编辑器生态扩展**：Inkscape 支持、在线编辑器 URL、Linux Flatpak/Snap 路径
4. **`--watch` 模式**：`edit` 命令 mtime 轮询 + 自动 `import`
5. **Mermaid 时序图**：高频需求但需不同模型抽象

---

> 生成方式：基于 `git log --all` 140 条提交的结构化分析 | 最后更新：2026-07-10
