# graphmcp 验收清单（Definition of Done）

> latest update: v0.2.9-beta, 2026-07-17

本清单是 DevOps / 课程验收的**签核入口**。一次发布或合并到 `main` 前，应能对照下列项给出证据（CI Artifact、报告文件或链接）。

## 报告职责边界

| 产物 | 职责 | 获取方式 |
|------|------|----------|
| `docs/TEST_REPORT.md`（**仅 CI Artifact，不入库**） | **验收用**汇总：单元 / 冒烟 / bench / 导出回归 + **GO/NO-GO** | [GitHub Actions](https://github.com/Wuqiongxiaolain/MCP/actions) Artifact `docs-test-report-*` 或 Jenkins Artifacts |
| [`examples/example_testout/TEST_REPORT.md`](../examples/example_testout/TEST_REPORT.md) | **样例导出明细**（单文件×格式 PASS/SKIP；本地/CI 可生成） | 同上 Artifact 内嵌，或本地 `scripts/export-example-testout.sh` |
| [`docs/templates/QUALITY_GATE_REPORT.md`](templates/QUALITY_GATE_REPORT.md) → Artifact `QUALITY_GATE_REPORT.md` | 静态质量门（cppcheck 必过；Sonar 可选） | CI Artifact |
| [`docs/templates/PERF_REPORT.md`](templates/PERF_REPORT.md) → Artifact `PERF_REPORT.md` | 性能结论（bench vs 基线） | CI Artifact（与 `bin/bench_result.json` 同包） |
| [`docs/templates/DEPLOY_RELEASE_REPORT.md`](templates/DEPLOY_RELEASE_REPORT.md) → Artifact | 发布/部署结果 | CD / Jenkins Deploy |
| [`docs/api_reference/openapi.yaml`](api_reference/openapi.yaml) | MCP 契约（**生成物**；真源为 `toolList()`） | 仓库内；CI `git diff` 校验 |

## 勾选表（发布 / 合入 main）

填写时在「结果」列写 `PASS` / `FAIL` / `SKIPPED`，并附证据路径或 Run URL。

| # | 检查项 | 结果 | 证据 |
|---|--------|:----:|------|
| 1 | 单元测试（main / version / cursor）全绿 | | `docs/TEST_REPORT.md` 套件一览 |
| 2 | CLI 冒烟 + MCP 冒烟 + table 冒烟通过 | | 同上；`docs/SMOKE_REPORT.md` |
| 3 | 样例全量导出回归无意外 FAIL | | `examples/example_testout/TEST_REPORT.*` |
| 4 | OpenAPI 与 `toolList()` 无漂移（`make docs-api` 后无 diff） | | CI step「校验 OpenAPI」 |
| 5 | 性能 bench 相对基线未破门禁（`PERF_REPORT` FAIL / `bench.exit≠0` → 测试报告套件 FAIL → **NO-GO**） | | `docs/PERF_REPORT.md` + `docs/TEST_REPORT.md` |
| 6 | 质量门：cppcheck 无 error；若有 warning 则报告为 WARN（可合入，须审阅；**不单独决定 GO**） | | `docs/QUALITY_GATE_REPORT.md` |
| 7 | 质量门：SonarQube/SonarCloud（有配置则 PASS；未配置则 SKIPPED） | | 同上；勿将 SKIPPED 记为 PASS |
| 8 | `VERSION` 与 README / 关键文档页眉版本一致 | | 根目录 `VERSION` |
| 9 | 制品可运行（`--help` 或等价健康检查） | | 部署报告或本地验证 |
| 10 | Breaking change（MCP 工具增删改、必填字段）已写入 CHANGELOG / 发布说明 | | [CHANGELOG.md](../CHANGELOG.md)、Release notes |
| 11 | 重大改造已有审查归档（或 PR 审查结论） | | [docs/reviews/](reviews/) |
| 12 | 本地 DevOps 发版路径可复现（若本次走 Jenkins） | | [RUNBOOK.md](RUNBOOK.md) |

### GO / NO-GO

| 结论 | 条件 |
|------|------|
| **GO** | 上表 1–6、8–9 均为 PASS；7 为 PASS 或明确 SKIPPED；10–12 按变更范围适用 |
| **NO-GO** | 任一项 1–6、8–9 为 FAIL；或 7 已启用 Sonar 但质量门失败 |

测试报告首屏的 **GO/NO-GO** 由套件汇总推导（见 `scripts/generate_docs_test_report.py`）；**完整签核**仍以本表为准（含质量门与文档一致性）。

## 维护约定

- 每完成一个可发布版本：更新 [CHANGELOG.md](../CHANGELOG.md) 对应章节（可从 tag message / [DEV_PROCESS.md](DEV_PROCESS.md) 摘录）。
- OpenAPI：**禁止手改** `openapi.yaml`；改 `toolList()` 后执行 `make docs-api` 并提交。
- 审查：`.plan.md` 可作为起草材料；结论须归档到 `docs/reviews/YYYY-MM-DD-主题.md`。

## 相关导航

- [运维 Runbook](RUNBOOK.md)
- [项目全景](PROJECT_OVERVIEW.md)
- [技术选型分析](ANALYSIS_WEB_VS_SINGLE_EXE.md) · [零依赖](ANALYSIS_VCPKG_VS_ZERO_DEP.md) · [JSON vs SQLite](ANALYSIS_JSON_VS_SQLITE.md)
