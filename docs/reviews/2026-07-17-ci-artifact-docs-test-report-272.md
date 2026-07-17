# 审查记录：CI Artifact docs-test-report-272 验收分析

> 日期：2026-07-17  
> 审查人：Cursor Agent  
> 关联：GitHub Actions run [`29590940328`](https://github.com/Wuqiongxiaolain/MCP/actions/runs/29590940328) / commit `22c91a5`  
> 产物路径：本机 `Docs/docs-test-report-272`（Artifact `docs-test-report-272`）

## 1. 范围

- 改动模块 / 文件：不直接改产品代码；审查对象为该次 CI 汇总产物：
  - `docs/TEST_REPORT.md` / `.json`
  - `docs/PERF_REPORT.md` / `.json`
  - `docs/QUALITY_GATE_REPORT.md` / `.json`
  - `docs/SMOKE_REPORT.md`
  - `docs/ci_results/*.exit|*.log`
  - `bin/bench_result.json`
  - `examples/example_testout/TEST_REPORT.*`
- 目标（解决什么问题）：从验收视角判断「还有哪些问题」，区分真失败、假失败、报告口径与流水线编排缺陷。
- 不在本次范围：逐文件修产品逻辑（除已另提交的 bench p50/RSS 修复）；不重新跑完整 CI。

## 2. 风险

| 风险 | 等级（高/中/低） | 缓解 |
|------|:----------------:|------|
| bench 用 mean 比对被写盘长尾抬爆 → CI 误阻断 | 高 | 已合入：IO 敏感项改 p50；单位量纲统一（见 `174d199` 一带） |
| TEST_REPORT 将 bench exit=2 降为 WARN 仍给 GO，与 PERF/CI 硬失败口径不一致 | 高 | 统一 GO 与 bench 门禁，或首屏明示「GO 不含性能」 |
| bench 失败后 `export-testout` 被跳过，样例导出证据缺失却显示 SKIP | 高 | export 步骤加 `if: always()`，或拆独立 job |
| `mcp_smoke` 与 export 共用 `example_testout/TEST_REPORT.json`，截断后语义混淆 | 中 | 拆分报告路径 / 文件名 |
| cppcheck warning：`exporters.hpp:2800` 死代码分支 | 中 | 修布局边端口分配条件 |
| `memory_RSS_*` 全 0、无绝对 RSS → 无法区分「无泄漏」与「未采样」 | 中 | 已合入：`memory_RSS_abs_*` + Windows 采样 |
| Sonar SKIPPED；branch=unknown；套件耗时全 0.0s | 低 | DoD 明示；回填元数据/耗时 |

## 3. 结论分类

### 必改（合并前 / 下次 CI 前）

- [x] **bench 假 FAIL（IO mean 长尾）**：`store_save` / `mcp_graph_create` / `tableStore_save_n200` 的 p50 实际优于基线，mean 被毛刺拉高；量纲 bug 导致报告 p50>p95。→ 已在后续提交修复，**须用新 CI 复验**。
- [x] **GO/NO-GO 与 bench 硬门禁对齐**：bench/compare 失败记套件 **FAIL** → **NO-GO**；首屏文案已写明。
- [x] **bench 失败不得吞掉样例导出**：GHA `if: !cancelled()`；Jenkins `catchError` 后继续 Export。

### 建议（可跟进）

- [x] 修复 `src/exporters.hpp:2800` cppcheck **warning**（去掉不同层分支内不可达的同层代码）。
- [x] 拆分 MCP / export 的 TEST_REPORT 路径（mcp → `docs/ci_results/mcp-protocol/`）。
- [x] `--from-ci` 回填耗时（`.duration`）与 branch（`run_meta.env`）。
- [ ] 若 DoD 要求 Sonar：配置 `SONAR_ENABLED` + Token，避免长期 SKIPPED 冒充「质量门已过」。
- [ ] style 级 cppcheck（约 365 条）分批降噪，非阻断。

### 已确认无问题

- 单元测试（主 1071 / 版本 97 / 游标 57）：**PASS**
- CLI 冒烟（109）、MCP 协议冒烟（13）、表协作冒烟、MCP 性能冒烟：**PASS**
- cppcheck：**0 error**（exit 门禁用 = 0）；唯一 warning 不阻断（按现行策略）
- Sonar：**SKIPPED**（未在本 job 启用，符合「不得记 PASS」约定）
- 大量 CPU 型 bench 指标相对基线为 **IMPR**（解析/JSON/导出等），说明并非整机性能塌陷

## 4. 复测项

| 项 | 命令 / 用例 | 结果 |
|----|-------------|:----:|
| 单元 | Artifact：`unit-*.exit=0` | PASS |
| CLI 冒烟 | `smoke.exit=0`；`SMOKE_REPORT.md` ALL PASSED | PASS |
| MCP 冒烟 | `mcp-smoke.exit=0` | PASS |
| 表 / 性能冒烟 | `table-smoke` / `perf-smoke` exit=0 | PASS |
| 微基准比对 | `bench.exit=2`；PERF 总评 FAIL（旧逻辑） | FAIL（假阳性，已修待复验） |
| 样例全量导出 | 无 `export-testout` 捕获文件 | **未执行** |
| 质量门 | QUALITY_GATE 总体 WARN；cppcheck warning=1 | WARN |

### 4.1 退出码一览（`docs/ci_results`）

| 名称 | exit |
|------|:----:|
| unit-main / version / cursor | 0 |
| smoke / mcp-smoke / table-smoke / perf-smoke | 0 |
| cppcheck | 0 |
| **bench** | **2** |
| export-testout | （缺失） |

### 4.2 性能失败项（旧比对，mean）

| 指标 | 基线 → 报告 mean | 说明 |
|------|------------------|------|
| `mcp_graph_create` | 409µs → 3.90ms（9.54×） | 三次重试形态不一，典型 IO 抖动 |
| `store_save` | 435µs → 18.06ms（41.53×） | 还原后 p50≈256µs，优于基线 |
| `tableStore_save_n200` | 590µs → 2.96ms（5.01×） | 同左；读路径 `store_load` 仍 IMPR |

### 4.3 质量门摘要

- **warning（1）**：`exporters.hpp:2800` Opposite inner if → dead code  
- **style（365）**：报告中截断列出；不阻断  
- **Sonar**：`SONAR_STATUS=SKIPPED`（本 job 未跑）

## 5. 后续

- 对应测试是否已补充：bench 单位统一、IO 用 p50、RSS `abs_*` 与 Windows 采样、冒烟用例已在分支后续提交中补充；**本 Artifact 仍为修复前快照**。
- 是否需更新 OpenAPI（`make docs-api`）：否。
- 是否需 CHANGELOG / Breaking 说明：建议在 CHANGELOG 记一条「bench 比对改用 p50（IO 敏感项）」；非 Breaking。
- 建议下一步：
  1. 推送含 `fix(bench): …` 的分支并重跑 CI，确认 PERF/GO 与 export 产物齐全；
  2. 改 CI：export 不依赖 bench 成功；
  3. 对齐 GO 文案或判定；
  4. 修 `exporters.hpp:2800` warning。

---

## 附录 A：验收摘要对照

| 报告 | 总评 | 备注 |
|------|------|------|
| `TEST_REPORT` | WARN / **GO（有 WARN）** | bench 被软化为 WARN |
| `PERF_REPORT` | **FAIL** | 与 CI bench 阻断一致 |
| `QUALITY_GATE_REPORT` | **WARN** | cppcheck warning + Sonar SKIPPED |

## 附录 B：根因一句话

| 现象 | 根因 |
|------|------|
| 3 个 bench FAIL | 写盘长尾抬高 **mean**；旧脚本按 mean 门禁 |
| RSS 全 0 | 测增量且暖机后无增长；当时无绝对 RSS 字段 |
| 导出套件 SKIP | bench 失败截断后续步骤 + 共用 TEST_REPORT 仅剩 mcp 段 |
| 仍显示 GO | 测试报告策略刻意不把 bench 比对失败升为 NO-GO |
