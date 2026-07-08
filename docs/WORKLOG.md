# 更新日志

项目：**graphmcp** — 图形设计与绘图 MCP 工具

本文件记录自项目创建以来的功能与修复变更，按日期归档。格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)：按 **新增 / 变更 / 修复 / 移除 / 文档 / 工程** 归类。

---

## [未发布] — 2026-07-08

### 新增

- **Excalidraw `files` 附件保真**：`Graph.files` 贯穿解析、模型往返、SVG 嵌入与存储，image 元素的 `dataURL` 不再丢失。
- **白板精确 SVG 导出**：rectangle / ellipse / diamond / arrow / line / freedraw / text / image 按几何精确渲染；支持 `angle` / `scale` 仿射变换与 image `crop` 内层裁剪。
- **freedraw 压力感轮廓**：由折线改为可变宽度闭合 path，viewBox 计入笔宽避免裁切。
- **离线字体内嵌**：引入 Virgil、Cascadia、Excalifont（`third_party/excalidraw-assets/`），SVG 内嵌 `@font-face` base64。
- **资源路径探测**：`GRAPHMCP_ASSETS` → 可执行文件旁 `third_party` → CWD；支持 Windows / Linux / macOS（含路径缓冲重试）。
- **样例体系**：`examples/example_input/` 与 `example_output/` 分离；新增 `whiteboard_freedraw` 等多格式基准输出。
- **CI 冒烟增强**：workflow / whiteboard / architecture 的 mermaid 全文比对；whiteboard SVG 剥离 `<style>` 后几何比对；`scripts/update-fixtures.sh` 一键重生 fixture。

### 变更

- **PNG/PDF 白板路径**：统一为精确 `toSVG` → 栅格化，不再使用近似 rough 叠加或 HTML+rough.js 截图。
- **箭头嵌字**：折线标签按路径长度中点定位；仅 `startArrowhead` 时交换逻辑边方向。
- **XML 转义分层**：`xmlTextEscape`（style 文本）、`xmlAttrEscape`（双引号属性）、`xmlEscape`（通用），避免字体 CSS 单引号被 `&#39;` 破坏。
- **字体 CSS 缓存**：仅 Virgil + Excalifont 主片齐备时永久缓存，部分失败可重试。
- **`Store::save`**：大 `files` 白板只序列化一次 `toJson()`。
- **`.gitattributes`**：文本类样例与配置统一 `eol=lf`，配合 CI `--strip-trailing-cr`。

### 修复

- 白板文本定位、ER 校验覆盖、箭头语义与导出分支遗漏。
- 跨平台 `getEnvVar` 替代废弃 `getenv`；Windows UTF-8 命令行与父目录创建等历史问题保持可用。
- CI mermaid 期望输出与 CRLF 差异；whiteboard 图像边缺失；SVG style 剥离改用 Python `DOTALL` 防换行误报。

### 移除

- C++ 静态 rough 抖动叠加、`toExcalidrawRoughHtml` 及 rough.js 导出主路径依赖（`rough.js` 不入库）。

### 测试

- 扩充 Excalidraw 单测：files/image、crop、matrix 镜像、字体 base64、startOnly marker、`GRAPHMCP_ASSETS`、转义函数等。

---

## 2026-07-07

### 新增

- **GitHub Actions CI**：构建、单元测试、端到端冒烟、制品打包。
- **可选流水线**：SonarQube 静态分析（`SONAR_ENABLED`）；GitLab 镜像同步（`GITLAB_MIRROR_ENABLED`）。
- **Tag 发布流程**：多平台制品构建与发布（含 Windows Make 兼容修复）。

### 变更

- DevOps 主链路由 Jenkins/Ansible 迁移至 GitHub Actions。
- README 精简，详细说明下沉至 `docs/`。

### 修复

- MCP 冒烟日志 JSON 双格式兼容；Sonar CFamily build-wrapper 生成；cppcheck 报错与测试空指针解引用。
- 发布流程 Tag 删除触发与发布前测试门禁。

### 工程

- 启用 **clang-format** 统一 C++ 排版；持续 **cppcheck** 清理。

---

## 2026-07-06

### 文档

- 源码英文注释统一为中文说明（`docs(src)`）。

### 工程

- 合并 `main` 与远程初始分支；`.gitignore` 补充 `out/`、`docs/MiniTasks/`。

---

## 2026-07-05 — 初始版本

### 新增

- **项目脚手架**：Makefile、CMakeLists.txt、`.gitignore`。
- **核心库（零第三方依赖）**：
  - `json.hpp` — 手写 JSON 解析/序列化（保序、UTF-8、`\u` 代理对）。
  - `model.hpp` — 统一图模型 Graph/Node/Edge，支持 flowchart / architecture / er / orgchart / mindmap / whiteboard。
  - `parsers.hpp` — Mermaid、Markdown 大纲、CSV、XML、Excalidraw 解析与 `detectFormat` 自动识别。
  - `layout.hpp` — 图校验（重复 ID、悬空边、层级环等）与分层/树形/网格布局。
  - `exporters.hpp` — drawio、mermaid、excalidraw、SVG、mermaid.live URL、PNG/PDF（外部转换器链 + SVG 回退）。
  - `storage.hpp` — JSON 文件版本化存储（index + latest + 不可变快照 + 回滚）。
  - `mcp.hpp` — MCP stdio JSON-RPC 2.0，8 个工具。
  - `main.cpp` — 9 个 CLI 子命令 + `serve`。
- **单元测试**：`tests/test_main.cpp` 覆盖解析、布局、导出、存储、MCP 协议。
- **示例输入**：flowchart、orgchart、outline、er、architecture、excalidraw 等。
- **浏览器栅格化**：自动探测 Chrome/Edge，经无头模式生成 PNG/PDF（独立 user-data-dir、绝对路径、`.bat` 启动兼容）。

### 修复

- `writeFile` 前 `ensureParentDirs`，避免 `-o` 含子目录时写入失败。
- Windows 命令行 UTF-8：`GetCommandLineW` 重取 argv，修复中文 `--name` 乱码。

### 文档

- `docs/ARCHITECTURE.md`、`docs/MINDMAP.md`、README 初版。
- Jenkins 流水线、Ansible 部署、SonarQube 配置（后于 07-07 移除 Jenkins/Ansible）。

---

## 技术决策摘要（常读）

| 决策点 | 结论 |
|--------|------|
| 依赖策略 | 零第三方库；JSON/XML/Base64 内置 |
| 架构核心 | 统一图模型居中，N 输入 × M 输出 |
| 存储 | JSON 文件版本快照（非 SQLite） |
| PNG/PDF | 外部转换器 / 浏览器栅格化 + SVG 回退 |
| URL | mermaid.live `#base64:`（免 deflate） |
| MCP | stdio + JSON-RPC 2.0（2024-11-05） |
| 白板导出 | 精确 SVG 栅格化；不追求 rough.js 手绘风对齐 |

## 遗留与展望

- [ ] 可选画布实时预览（SVG + 本地 HTML 轮询 `latest.json`）。
- [ ] Mermaid：classDiagram、stateDiagram。
- [ ] draw.io URL（需 deflate，暂缓以保持零依赖）。
- [ ] 大图检索场景下的可选 SQLite 后端。
- [ ] 分层布局 median 启发式减交叉。
