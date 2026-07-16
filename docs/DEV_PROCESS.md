# graphmcp 开发过程

> latest update: v0.2.6-beta, 2026-07-16

> 本文档依据仓库提交记录事后整理，按日期还原实际演进，**不是**开发当日的实时日记。  
> 已并入原 `WORKLOG.md` / `CHANGELOG.md` / `DEV_LOG.md`。  
> **P1–P6**（至 2026-07-10）为初版收口叙事；**P7 起**（自 `c6e8009` / 2026-07-11 起）为扩展期，与 [PROJECT_TIMELINE.md](PROJECT_TIMELINE.md)、[PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) 对齐；**P13** 为 v0.2.6 布局增强。

> 口径说明：文中「当日」数字为历史值。**当前能力**以 `src/main.cpp` / `src/mcp.hpp::toolList()` / OpenAPI 为准（15 个 CLI 命令族、**47** 个 MCP 工具）。

---

## 总览

| 日期 | 约提交数 | 主线 |
|------|:--------:|------|
| 07-05 | 12 | P1 核心引擎：模型 / 解析 / 布局 / 导出 / 存储 / MCP+CLI / 初版 CI |
| 07-06 | 7 | P2 注释中文化、忽略规则、仓库合并整理 |
| 07-07 | 21 | P3 去掉 Jenkins/Ansible，接入 GitHub Actions、clang-format、发布流 |
| 07-08 | 37 | P4 白板精确导出；并行启动 CLI/版本/游标与编辑器闭环 |
| 07-09 | 45 | P5 架构合并落地；P6 编辑回导与三层测试补齐 |
| 07-10 | 23 | P6 收尾：编辑器字段兜底、文档分层；**暂禁** macOS CD；新需求开始涌现 |
| **07-11** | **~4+** | **P7**：macOS 头文件 + 恢复 CD 矩阵（`c6e8009`）；edit/import 改进；Mermaid 全类型支持起步 |
| **07-13** | **~44** | **P8–P10 高峰**：OpenAPI/`dump-tools`；Table 全链路；Mermaid 深解析与表 XML |
| **07-14** | **~45** | **P9–P10 收口**：颜色全链路/BOM/`[*]` 校验；Benchmark CI 套件；CI 策略重构（基线仅比对、workflow_dispatch）；v0.2.2 / v0.2.3-beta；drawio 多图层/多页起步 |
| **07-15** | **~30** | **P11**：MCP 性能重构（存储一致性/写放大/跨平台回归）；drawio 兼容合入（形状/图层/页/边标签）；Jenkins 本地 DevOps 链（Docker/Ansible）；v0.2.4-beta；**P13 起步**：分层布局增强（层平衡/减交叉/waypoint） |
| **07-16** | **~12** | **P12**：Ansible Runner 替代 Semaphore；Docker 固化；Jenkins→Ansible→nginx 发布链；v0.2.5-beta；**P13**：布局增强合入（PR #78）+ v0.2.6-beta |

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
| **新增** | Draft-Stage-Commit、Cursor 游标、CLI 多命令族、MCP 扩至约 24～25 工具（当日口径，当前已扩展）、`graph_import` / `parseDrawio`、`GRAPHMCP_LOG`、CLI&MCP 参考文档 |
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
| **修复** | `availableEditors` 空环境始终写出；mcp_smoke 工具数 25（当日口径） |
| **工程** | macOS CD **暂禁**（为次日头文件修复埋下）；CD 手动触发 / dry_run |
| **文档** | 用户/维护者/开发者分层；v0.1.0 对齐 |

**过程特征**：功能以兜底与 CI 对齐为主；文档与发布并行。当日起明确出现后续扩展期需求：**通用表、Mermaid 类型扩展、恢复 macOS CD**。

---

## 2026-07-11 — macOS CD 恢复与扩展起步（P7 / P10 启动）

以 `c6e8009` 为扩展期标志提交：

| 代表提交 / PR | 说明 |
|---------------|------|
| `c6e8009` `fix(build): 补全 macOS 头文件 + 恢复 CD macOS 构建矩阵` | 关闭 07-10「暂禁 macOS」问题；CD 重新纳入 macos Runner |
| `2c2b483` / PR #62 | edit/import 覆盖提示、版本显示、导入错误消息改进 |
| `30d72dc` `feat: add support for all Mermaid diagram types` | Mermaid 全类型支持起步（后续 07-13～14 深解析补全） |

### 变更摘要

| 类别 | 内容 |
|------|------|
| **工程** | macOS CD **已恢复**（与「尚未解决：macOS CD」条目脱钩） |
| **功能** | Mermaid 扩展线启动 |
| **体验** | 编辑回导提示与错误信息可观测性增强 |

---

## 2026-07-13 — OpenAPI、表协作与 Mermaid 深解析（P8 / P9 / P10）

扩展期提交高峰（author date 约 44 条）。三条主线并行合入：

### 线 A：OpenAPI 契约（PR #65）

- `feat(mcp): 新增 dump-tools 从 toolList 导出 OpenAPI`  
- `chore(build): 接入 docs-api 目标与 CI 文档漂移校验`  
- `docs/api_reference/openapi.yaml` 入库；兼容说明写入 toolList  

### 线 B：通用表协作（PR #66 一带，后续 #70 增强）

- `feat(table): 新增通用 Table 模型与 TableStore`  
- `feat(table): 图与表有损投影及对齐校验`；`feat(mcp): 暴露 table 工具族与 CLI`  
- 表 XML（模式 A）、原子写、兼容环境变量、`table_smoke` / 样例导出脚本  
- 文档同步表协作语义与维护/抽离约定  

### 线 C：Mermaid 深解析 + graph_property

- `feat: complete deep parsing for all 19 Mermaid diagram types`  
- `docs(api): regenerate OpenAPI spec with graph_property tool`  
- 测试与 `detectFormat` 加固；冒烟工具数断言阶段性上调  

### 变更摘要

| 类别 | 内容 |
|------|------|
| **新增** | `dump-tools` / OpenAPI；Table 子系统；Mermaid 多类型深解析；`graph_property` |
| **变更** | MCP 工具面从「图中心」扩为「图 + 表」；CLI 增加 `table` 族与契约导出 |
| **测试** | 表单测 / MCP 扩展断言 / 样例与 fixture 脚本 |
| **工程** | CI 校验 OpenAPI 与代码 schema 不漂移 |

---

## 2026-07-14 — 颜色全链路、Benchmark CI 与 CI 策略重构（P9/P10 收口 + P11 启动）

当日提交量极大（约 45 条），可划分为早晚两个半场：

### 上半场：颜色全链路与 BOM 修复（PR #71 一带）

| 代表提交 | 说明 |
|----------|------|
| `547302f` / `6ac9420` / `dcbafc4` | 颜色全链路：`fillColor`/`strokeColor` 模型字段、`classDef`/`linkStyle` 解析、Mermaid 导出颜色指令顺序校正 |
| `52e59b0` | `feat(cursor): 接通节点与边的颜色 Draft 读写` |
| `ec25ca3` | `fix(parsers): 扩展 linkStyle 语法并剥离 UTF-8 BOM` |
| `79a19f6` | `fix(layout): 校验认可状态图起始/终止标记 [*]` |
| `ee8b5bb` | `test(examples): 同步颜色与 Mermaid 样例导出快照` |

### 下半场：Benchmark CI 套件与策略重构

| 代表提交 | 说明 |
|----------|------|
| `ea8f62c` | `feat: add micro-benchmark suite and CI performance regression detection` |
| `db41a2b` | `feat(bench): add Table model micro-benchmarks (7 categories, 18 metrics)` |
| `d9784d2` | `ci: 取消 main 自动回写性能基线，改为仅比对` |
| `6548e5e` | `feat(ci): 新增按需刷新性能基线的 workflow_dispatch` |
| `2f5fdce` | `feat(ci): 新增按需写回 VERSION 与 OpenAPI 的 workflow_dispatch` |
| `91a9fa3` | `ci:更改了CI触发策略`（feat/\*\* / fix/\*\* 纳入 push 触发） |
| `bed238f` | `chore(release): 将 VERSION 与 OpenAPI 升至 v0.2.2` |

### drawio 兼容扩展起步（v0.2.3-beta）

| 代表提交 | 说明 |
|----------|------|
| `1c47ad2` | `feat(drawio): 扩展形状支持、多图层、多页及边标签定位` |
| `0fc03ce` | `chore(release): bump version to v0.2.3-beta` |
| `12e545a` | `ci(cd): Release 正文改用 tag message 并保留自动 Full Changelog` |

### 变更摘要

| 类别 | 内容 |
|------|------|
| **新增** | 颜色一等字段（`fillColor`/`strokeColor`）+ 多格式往返；Cursor 颜色 Draft 读写；`linkStyle` 解析增强 / UTF-8 BOM 剥离；微基准测试套件（含 Table 7 类 18 指标）；`workflow_dispatch` 按需写回基线与 VERSION/OpenAPI；drawio 多图层/多页/形状扩展 |
| **变更** | CI 基线策略：main 自动写回 → 仅比对 + 手动 dispatch；CD Release 正文改用 tag message |
| **修复** | linkStyle 解析健壮性；颜色指令顺序导致 Mermaid 导出异常；状态图 `[*]` 校验误拒绝；BOM 导致解析失败 |
| **测试** | 颜色解析往返/Draft 更新/冒烟覆盖新类型与 linkStyle；Bench CI 性能回归检测；fixture 对齐 |

---

## 2026-07-15 — MCP 性能重构、drawio 合入与 Jenkins DevOps（P11 高峰）

当日约 30 条提交，三条主线并行：

### 线 A：MCP 性能全面重构（PR #79，由 `wldxiaobai` 主导）

这是一次 **MCP 服务器四维改造**：存储一致性、热路径压缩、超时/错误语义、跨平台性能回归验证。

| 代表提交 | 说明 |
|----------|------|
| `4415685` | `perf(mcp): 提升存储一致性并压缩热路径开销` |
| `d8fe6b7` | `perf(mcp): 以指针化 latest 削减写放大` |
| `c2508fe` | `fix(mcp): 补齐审查中的存储与超时正确性缺口` |
| `be4a804` | `feat(mcp): 强化 tools/list 契约并新增 graph_apply` |
| `a7b9943` | `fix(mcp): 补齐 commit 崩溃恢复与 apply 错误语义` |
| `d6d105a` | `fix(mcp): Job Assign 失败时正确降级终止策略` |
| `85d0888` | `test(mcp): 增加跨平台性能回归验证` |
| `34a54fb` / `4845de9` | `test(mcp): 扩展性能 smoke 覆盖审查回归点` + `扩展四维改造验收 smoke` |
| `7fde9ba` | `docs(mcp): 记录 Agent 性能与 UX 分析` |
| `9af25d5` | `chore(mcp): 可选 Skill 包装入 Release` |
| `17f832d` / `518d858` | 内存稳定性基准：从相对阈值改为绝对阈值 |

### 线 B：drawio 兼容合入与修复（PR #80）

| 代表提交 | 说明 |
|----------|------|
| `0694ab5` | `merge: 合并 main (v0.2.3-beta) — fillColor/strokeColor/箭头扩展/BOM 处理` |
| `5edfed9` | `fix: 修复审查发现的 4 个问题` |

### 线 C：Jenkins 本地 DevOps 链（PR #84）

| 代表提交 | 说明 |
|----------|------|
| `690f99e` | `chore(ci): 添加本地 Jenkins Pipeline 定义` |
| `479abc4` | `feat(docker): 自定义 Jenkins 镜像固化 CI 运行时依赖` |
| `4da65ee` | `fix(docker): 预装 librsvg2-bin 以通过 smoke 栅格导出` |
| `504540a` | `feat(ansible): 添加 Semaphore 管理 Jenkins 容器工具的示例` |
| `f35fd07` | `feat(ci): Jenkins 构建后经 Ansible 发布制品到 nginx 下载站` |
| `14de6b4` | `fix(ci): 本地 Jenkins Bench 仅告警不阻断 CI` |
| 多轮 Groovy 转义/解析修复 | `1e05c81`、`b3fa921`、`56d7535` |

### v0.2.4-beta 发布

`ec5ddf5` `chore(release): bump version to v0.2.4-beta` + 基线更新。

### 变更摘要

| 类别 | 内容 |
|------|------|
| **新增** | MCP 四维性能改造（存储一致性、写放大削减、超时语义、跨平台回归）；`graph_apply` 工具；Jenkins Pipeline + Docker 镜像（Jenkins/Ansible）；nginx 下载站发布链；MCP Agent 性能/UX 分析文档 |
| **变更** | 内存基准从相对阈值切换为绝对阈值；CI 触发策略扩展 featur/fix 分支 |
| **修复** | drawio 审查 4 问题；commit 崩溃恢复语义；Job Assign 降级；Groovy 转义与 sh 字符串解析；Docker 预装 librsvg2-bin |
| **测试** | 四维改造验收 smoke；跨平台性能回归覆盖；内存稳定性固定图验证 |
| **工程** | 本地 Jenkins DevOps 链（Docker→Jenkins→Ansible→nginx）；`__pycache__` 入 `.gitignore` |

---

## 2026-07-16 — Ansible Runner 替代与发布链收口（P12）

约 8 条提交，聚焦 DevOps 工具链切换与最终交付：

| 代表提交 | 说明 |
|----------|------|
| `c873ee9` | `feat(docker): 添加 ansible-runner 运行容器` |
| `3629268` | `refactor(ci): Jenkins 发布改由 ansible-runner 执行` |
| `4cafa0c` | `fix(ansible): 下载站首页改用 UTF-8 英文避免乱码` |
| `d95215f` | `docs(ansible): 补充 nginx 下载站发布说明` |
| `639869c` | `chore(release): bump version to v0.2.5-beta` |

### 变更摘要

| 类别 | 内容 |
|------|------|
| **新增** | ansible-runner Docker 容器，替代 Semaphore 执行 Ansible 发布 |
| **变更** | CD 发布链：Jenkins → ansible-runner（原 Semaphore）→ nginx 下载站 |
| **修复** | nginx 下载站首页 UTF-8 英文避免乱码 |
| **文档** | nginx 下载站发布说明 |

**过程特征**：DevOps 工具链趋于稳定——Semaphore UI → ansible-runner 命令行容器；发布完全自动化。

---

## 2026-07-15～16 — 分层布局增强（P13 / v0.2.6-beta）

布局引擎在基础 Kahn / 树 / 网格之上增强分层策略（`feature/layout-crossing-minimization`，PR #78），并发布 **v0.2.6-beta**。能力已可用，但复杂图观感仍不完善。

| 代表提交 | 说明 |
|----------|------|
| `26eaa00` | `feat(layout): layer balancing, enhanced crossing minimization, and waypoint-based edge routing` |
| `8c22255` | `feat(layout): compute edge label position from longest waypoint segment` |
| `0eaf4df` | `fix(layout): sync edge waypoints and labels with node position normalization` |
| `2f77df1` | `Merge pull request #78`（布局增强合入 main） |
| `a9c9175` | `chore(release): bump version to v0.2.6-beta` |

### 变更摘要

| 类别 | 内容 |
|------|------|
| **新增** | 分层布局：层平衡（限制单层节点数并下沉超额节点）；barycenter 启发式交叉最小化（上下扫 + 局部交换）；长边虚拟节点 → `Edge.waypoints` 折线路由；边标签按最长折线段落中定位 |
| **修复** | 节点坐标归一化（dx/dy）时同步平移 waypoints 与 `labelX`/`labelY`，避免折线落入负坐标 |
| **导出** | SVG 等路径可消费 waypoints 做折线边 |
| **边界** | 尚不完善：密集交叉、特殊图类型间距与整体观感仍需继续打磨 |

**过程特征**：布局从「能排开」走向「可减交叉 + 折线路由」；与导出侧 waypoint 消费联动，但未宣称达到生产级美观。

---

## 从提交可见的演进结果

### 历史快照（截至 2026-07-10）

| 指标 | 起点（07-05 末） | 收口（07-10） |
|------|------------------|---------------|
| MCP 工具 | ~8 | 25（含 `graph_import`） |
| CLI | 扁平子命令 | `<family> <subcommand>` 多命令族 |
| 导出 | 含 rough 近似白板 | 自研精确 SVG + files/字体 |
| CI | Jenkins 模板 | GitHub Actions + 冒烟 / 可选 Sonar / Tag 发布 |
| 版本 | 简单 versions 快照 | Draft → Stage → Commit + HEAD 同步 |
| macOS CD | — | **暂禁** |

### 扩展期后（截至 2026-07-16，当前）

| 指标 | 当前值 |
|------|--------|
| MCP 工具 | **47**（图 + 表 + property；以 `toolList()`/OpenAPI 为准） |
| CLI 命令族 | **15**（含 `table` / `dump-tools` / `import`） |
| Mermaid | 19 种深解析 + 颜色全链路（`classDef`/`linkStyle`/BOM）；坏样例硬/软失败语义明确 |
| 通用表 | TableStore + CSV/表 XML + 图↔表协同增强（rules/check/fix/derive/transform/sample/propose） |
| Drawio | 多图层/多页/形状扩展/边标签定位 |
| 布局 | 分层增强（层平衡 / barycenter 减交叉 / waypoint 折线路由，v0.2.6，尚不完善） |
| 性能 | 微基准套件（18 指标）+ MCP 热路径优化（写放大/存储一致性/超时语义）+ CI 性能回归检测 |
| CI/CD | GitHub Actions（构建/单测/冒烟/bench/OpenAPI 校验）+ 本地 Jenkins DevOps + Ansible 发布到 nginx |
| CD | **含 macOS** 构建矩阵（已恢复） |
| 契约 | OpenAPI 自动生成 + CI 漂移校验；`workflow_dispatch` 按需写回 VERSION/基线 |

---

## 技术决策摘要

| 决策点 | 结论 |
|--------|------|
| 依赖策略 | 零第三方库；JSON/XML/Base64 内置 |
| 架构核心 | 统一图模型居中；**表为并列一等对象**（非边表冒充业务宽表） |
| 存储 | JSON 文件版本快照（图与 `tables/` 分目录） |
| PNG/PDF | 外部转换器 / 浏览器栅格化 + SVG 回退 |
| URL | mermaid.live `#base64:`（免 deflate） |
| MCP | stdio + JSON-RPC 2.0；**代码即文档**（`toolList` → OpenAPI） |
| 白板导出 | 精确 SVG 栅格化；不追求 rough.js 手绘风对齐 |
| CI 主链 | GitHub Actions + 本地 Jenkins DevOps（Ansible 发布）；CD 含 macOS |
| 版本工作流 | Draft → Stage → Commit（仿 Git）；操作序列式（非快照式） |
| 性能 | 微基准 + CI 仅比对基线（不自动写回）；热路径指针化削减写放大 |
| 发布 | Tag 触发多平台 Release + Jenkins → Ansible Runner → nginx |

---

## 遗留与展望（对照当前事实）

### 扩展期已关闭

1. **macOS CD**：`c6e8009` 已补头文件并恢复 Runner  
2. **Mermaid 类型扩展**：19 种子类型深解析已落地（可持续打磨质量）  
3. **通用表格支持**：Table + MCP/CLI + 协同增强已落地  
4. **颜色全链路**：`fillColor`/`strokeColor` 一等字段 + 多格式往返已落地  
5. **性能回归检测**：Benchmark CI 套件 + 基线比对已落地  
6. **本地 DevOps**：Jenkins + Docker + Ansible Runner 发布链已落地  

### 尚未交付 / 需继续跟进

1. **`exporters.hpp` 体量过大**（导出 + 编辑器发现 + 浏览器启动叠在同一头文件，~3300 行）  
2. **编辑器路径**：Linux/macOS 发现逻辑在 CI 覆盖仍不足  
3. **可选画布实时预览**（SVG + 本地 HTML 轮询 `latest.json`）       
4. **可选 SQLite 后端**（大图检索替代文件系统）    

---

## 说明

- 日期与主题以 **author date** 的 `git log` 为准；同一功能可能跨日、跨分支，本文按「首次明显落地日 / 合入日」归类。  
- 「变更摘要」保留原 WORKLOG 的新增/变更/修复分类；07-09/07-10 由提交补记；**07-11～07-16 按 `c6e8009` 以来提交与合入 PR 补记**。  
- 若需对外发版条目，可在 Tag 时另写简短 Release notes。  
