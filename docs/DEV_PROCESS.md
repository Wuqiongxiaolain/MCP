# graphmcp 开发过程

> latest update: v0.1.1, 2026-07-10

> 本文档依据仓库全部提交记录（`git log --all`，截至 2026-07-10，约 **145** 条）事后整理，按日期还原实际演进，**不是**开发当日的实时日记。  
> 已并入原 `WORKLOG.md`（Keep a Changelog 式变更归档）、`CHANGELOG.md`、`DEV_LOG.md`。  
> 阶段编号与 [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md) 对齐；项目全景见 [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)。

---

## 总览

| 日期 | 约提交数 | 主线 |
|------|:--------:|------|
| 07-05 | 12 | P1 核心引擎：模型 / 解析 / 布局 / 导出 / 存储 / MCP+CLI / 初版 CI |
| 07-06 | 7 | P2 注释中文化、忽略规则、仓库合并整理 |
| 07-07 | 21 | P3 去掉 Jenkins/Ansible，接入 GitHub Actions、clang-format、发布流 |
| 07-08 | 37 | P4 白板精确导出；并行启动 CLI/版本/游标与编辑器闭环 |
| 07-09 | 45 | P5 架构合并落地；P6 编辑回导与三层测试补齐 |
| 07-10 | 23 | P6 收尾：编辑器字段兜底、文档分层、CD 与 macOS 构建调整 |

主要作者（按提交量）：`wldxiaobai`、`wyQuQ`、`kliang`、`yifengsun`（另有少量合并账号与 `copilot-swe-agent`）。

---

## 2026-07-05 — 核心引擎一日成型（P1）

当日由 `kliang` 连续提交，从脚手架到可运行 MCP 服务：

| 顺序 | 代表提交主题 | 产出 |
|------|--------------|------|
| 1 | `chore: project scaffolding` | Makefile、CMake、gitignore |
| 2 | `feat: minimal JSON library and unified graph model` | `json.hpp`、`model.hpp` |
| 3 | `feat: input parsers…` | Mermaid / Markdown / CSV / XML / Excalidraw |
| 4 | `feat: automatic layout engine and graph validation` | 布局 + 校验 |
| 5 | `feat: exporters…` | drawio / mermaid / excalidraw / SVG / URL / PNG·PDF |
| 6 | `feat: versioned JSON store…` | 索引 + latest + versions 快照 |
| 7 | `feat: MCP stdio server and CLI entry point` | 初版约 8 个 MCP 工具 + CLI |
| 8 | `test: unit tests (121 assertions)…` | 单测与示例输入 |
| 9 | `ci: Jenkins…` / `docs: README…` | 初版 Jenkins/Ansible/Sonar 与文档 |
| 10 | PNG/PDF 浏览器栅格化、`CreateProcessW`、静态链接 | Windows 导出可用性 |

### 变更摘要（原 WORKLOG）

| 类别 | 内容 |
|------|------|
| **新增** | 脚手架；零依赖核心库（`json` / `model` / `parsers` / `layout` / `exporters` / `storage` / `mcp` / `main`）；121 断言单测；多格式示例输入；Chrome/Edge 无头栅格化 PNG/PDF |
| **修复** | `writeFile` 前 `ensureParentDirs`；Windows UTF-8 命令行（`GetCommandLineW`）避免中文 `--name` 乱码 |
| **文档** | `ARCHITECTURE`、`MINDMAP`、README 初版；Jenkins / Ansible / Sonar 配置（后于 07-07 拆除 Jenkins/Ansible） |

**可从提交读出的决策**：零第三方依赖（手写 JSON）、统一 Graph 居中、stdio JSON-RPC 自实现、先用 Jenkins 满足课题要求。

---

## 2026-07-06 — 加固与中文化（P2）

| 代表提交 | 说明 |
|----------|------|
| `docs(src): 统一翻译源码英文注释`（PR #2） | 团队统一中文注释 |
| `chore: add out/ to .gitignore` | 忽略本地输出 |
| 远程 `main` 合并 / `Initial commit` 等 | 仓库镜像与分支对齐 |

### 变更摘要（原 WORKLOG）

| 类别 | 内容 |
|------|------|
| **文档** | 源码英文注释统一为中文 |
| **工程** | 合并 `main` 与远程初始分支；`.gitignore` 补充 `out/`、`docs/MiniTasks/` |

提交量少，以可读性与仓库卫生为主，功能面几乎未扩。

---

## 2026-07-07 — CI/CD 迁到 GitHub（P3）

以 `wldxiaobai` 的 `feature/github-actions-cicd` 为主（PR #10、#11）：

| 主题 | 代表提交 / 结果 |
|------|-----------------|
| 拆除旧链 | `chore(ci): 移除 Jenkins 与 Ansible 部署配置` |
| 新 CI | `ci: 添加 GitHub Actions…`、冒烟增强、GitLab 镜像可选同步 |
| 质量门 | clang-format、cppcheck、SonarCFamily build-wrapper |
| 发布 | Tag 触发多平台制品；修复 Windows Make、Tag 删除触发等 |
| 测试硬化 | MCP 冒烟 JSON 双形态、空指针守卫（含 copilot 辅助提交） |
| 小重构 | 删除 Excalidraw 连线端点冗余计算 |

### 变更摘要（原 WORKLOG）

| 类别 | 内容 |
|------|------|
| **新增** | GitHub Actions（构建 / 单测 / 冒烟 / 打包）；可选 SonarQube、GitLab 镜像；Tag 多平台发布 |
| **变更** | DevOps 主链 Jenkins/Ansible → GitHub Actions；README 精简，细节下沉 `docs/` |
| **修复** | MCP 冒烟 JSON 双格式；Sonar build-wrapper；cppcheck / 空指针；Tag 删除触发与发布前测试门禁 |
| **工程** | clang-format；持续 cppcheck |

**可从提交读出的决策**：协作已在 GitHub，CI 与仓库同站；Sonar / GitLab 镜像保持可选，不阻塞主路径。

---

## 2026-07-08 — 白板精化与架构分叉并行（P4 + P5/P6 启动）

当日提交最多之一，**三条线并行**：

### 线 A：Excalidraw 精确导出（`wldxiaobai`，后合入 PR #16）

- 修复箭头标签、白板 SVG 文本/箭头嵌字、freedraw 边界  
- `feat(model): 添加 Excalidraw files 附件保真字段`  
- 引入内嵌字体资源；`feat(exporters): 完善白板精确导出并移除近似 rough`  
- 样例改为 `example_input` / `example_output`；SVG fixture 冒烟、`GRAPHMCP_ASSETS`、macOS 可执行路径探测  

### 线 B：CLI / 版本 / 游标（`wyQuQ`，分支 `CLI`）

- `CLI重构 + Draft-Stage-Commit版本管理 + Cursor游标操作 + MCP工具补全`  
- 多轮 `fix`：非 Windows 编译、循环 include、测试节点 ID、代码质量项  
- `feat: 整合PR #15的游标持久化与草稿状态对比功能`  

### 线 C：编辑器闭环起步（`yifengsun`）

- `feat(editor): 增强外部编辑器调起与编辑闭环`  

### 变更摘要（原 WORKLOG，当日以白板为主）

| 类别 | 内容 |
|------|------|
| **新增** | Excalidraw `files` 保真；精确 SVG（含 angle/scale/crop）；freedraw 压力感轮廓；Virgil/Cascadia/Excalifont 内嵌；`GRAPHMCP_ASSETS` 路径链；样例 input/output 分离；whiteboard/architecture 冒烟与 `update-fixtures.sh` |
| **变更** | PNG/PDF 白板统一精确 `toSVG`→栅格化；箭头嵌字中点定位；XML 转义分层；字体 CSS 缓存策略；大 `files` 只序列化一次；`.gitattributes` `eol=lf` |
| **修复** | 文本定位、ER 校验、箭头语义；跨平台 `getEnvVar`；CI mermaid CRLF / SVG `DOTALL` 等 |
| **移除** | C++ rough 抖动叠加、`toExcalidrawRoughHtml` 及 rough.js 主路径（不入库） |
| **测试** | files/image、crop、matrix、字体 base64、`GRAPHMCP_ASSETS`、转义函数等 |

**过程特征**：功能增长与跨平台/冒烟修复交织；白板引擎与 CLI 大改尚未完全汇合，为次日合并冲突埋下伏笔。

---

## 2026-07-09 — 架构落地与编辑回导（P5 高潮 + P6）

当日提交量最高（约 45）。主线包括：

### CLI / 版本 / 游标合入

- `feat: CLI重构+版本管理+游标+白板导出+审查修复`（`wyQuQ`）  
- 合并 `main` 后修复白板导出编译、补 `Json` const 访问器、资产路径、`toSVGExcalidraw` 与 fixture 对齐  
- 文档：`docs: 新增CLI & MCP指令参考文档`（PR #21）  

### 测试与存储一致性（`wldxiaobai`，PR #24 / #26）

- 新版 CLI 冒烟链路、cppcheck 告警清理  
- `fix(storage): save 成功后同步写入 HEAD`（新旧版本命令互操作）  
- `feat(mcp): 增加 GRAPHMCP_LOG 调试日志`  
- MCP 三层测试与会话冒烟修正  

### 外部编辑器 + drawio 回导（`yifengsun`）

- `feat(editor): 外部编辑器调起 + drawio解析回导`  
- 合并 main 后恢复丢失的 drawio / `graph_import` 测试  
- `feat(test): 完善 MCP graph_open + graph_import 冒烟`；工具数断言 **24→25**  

### 变更摘要（据提交补记；原 WORKLOG 未单独成日）

| 类别 | 内容 |
|------|------|
| **新增** | Draft-Stage-Commit、Cursor 游标、CLI 多命令族、MCP 扩至约 24～25 工具、`graph_import` / `parseDrawio`、`GRAPHMCP_LOG`、CLI&MCP 参考文档 |
| **变更** | `Store::save` 同步 HEAD；白板 SVG 与 fixture 对齐 |
| **修复** | 跨分支合并丢测、冒烟断言、cppcheck 告警 |

**可从提交读出的决策**：Draft-Stage-Commit 仿 Git；MCP 与 CLI 对齐扩展；`parseDrawio` 复用既有 XML 解析；大合并后用测试捞回「丢测」。

---

## 2026-07-10 — 收尾、文档与 CD（P6 收口）

| 代表提交 | 说明 |
|----------|------|
| `fix(mcp): graph_open … 始终返回 availableEditors` | 无编辑器 CI 环境字段缺失导致断言失败 |
| PR #25 `ah_feng-editor-v2` 合入 | 编辑器闭环进 main |
| 文档：全景总结、应用逻辑、FEATURES、QUICK_START、文档分层与 v0.1.0 对齐 | 多 PR（#38–#47 一带） |
| `ci(cd): 暂时禁用 macOS 构建` | 缺 `dirent.h` / `unistd.h` 导致编译失败 |
| `ci(cd): workflow_dispatch + dry_run` | 分支上可测 CD 而不强制发 Release |

### 变更摘要（据提交补记）

| 类别 | 内容 |
|------|------|
| **修复** | `availableEditors` 空环境始终写出；mcp_smoke 工具数 25 |
| **工程** | macOS CD 暂禁；CD 手动触发 / dry_run |
| **文档** | 用户/维护者/开发者分层；v0.1.0 对齐 |

**过程特征**：功能以兜底与 CI 对齐为主；文档与发布并行。macOS CD「先禁用再修头文件」是提交中可见的权衡。

---

## 从提交可见的演进结果

| 指标 | 起点（07-05 末） | 收口（07-10） |
|------|------------------|---------------|
| MCP 工具 | ~8 | 25（含 `graph_import`） |
| CLI | 扁平子命令 | `<family> <subcommand>` 多命令族 |
| 导出 | 含 rough 近似白板 | 自研精确 SVG + files/字体 |
| CI | Jenkins 模板 | GitHub Actions + 冒烟 / 可选 Sonar / Tag 发布 |
| 版本 | 简单 versions 快照 | Draft → Stage → Commit + HEAD 同步 |

---

## 技术决策摘要

| 决策点 | 结论 |
|--------|------|
| 依赖策略 | 零第三方库；JSON/XML/Base64 内置 |
| 架构核心 | 统一图模型居中，N 输入 → Graph → M 输出 |
| 存储 | JSON 文件版本快照（非 SQLite） |
| PNG/PDF | 外部转换器 / 浏览器栅格化 + SVG 回退 |
| URL | mermaid.live `#base64:`（免 deflate） |
| MCP | stdio + JSON-RPC 2.0 |
| 白板导出 | 精确 SVG 栅格化；不追求 rough.js 手绘风对齐 |
| CI 主链 | GitHub Actions（Jenkins/Ansible 已移除） |
| 版本工作流 | Draft → Stage → Commit（仿 Git） |

---

## 遗留与展望

以下在提交与修复中反复出现，或原 WORKLOG 已列出、**尚未交付**：

1. **`exporters.hpp` 体量过大**（导出 + 编辑器发现 + 浏览器启动叠在同一头文件）  
2. **macOS CD**：补全 `dirent.h` / `unistd.h` 后恢复 runner  
3. **编辑器路径**：Linux/macOS 发现逻辑在 CI 覆盖不足  
4. **可选画布实时预览**（SVG + 本地 HTML 轮询 `latest.json`）  
5. **Mermaid 扩展**：classDiagram、stateDiagram；时序图需不同模型抽象  
6. **draw.io URL**（需 deflate，暂缓以保持零依赖）  
7. **可选 SQLite 后端**（大图检索）  
8. **分层布局** median 启发式减交叉  

---

## 说明

- 日期与主题以 **author date** 的 `git log` 为准；同一功能可能跨日、跨分支，本文按「首次明显落地日」归类。  
- 「变更摘要」保留原 WORKLOG 的新增/变更/修复分类，便于对照功能面；07-09/07-10 由提交补记。  
- 若需对外发版条目，可在 Tag 时另写简短 Release notes。  
- `README.md` 中指向 `WORKLOG.md` 的链接暂未改动，后续统一调整文档索引。
