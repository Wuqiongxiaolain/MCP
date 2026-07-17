# 性能报告模板

> 数据源：`bin/bench_result.json`（相对 `tests/bench_baseline.json`）。  
> CI 通过 `make bench-ci` 比对后，由 `scripts/generate_perf_report.py` **组装**为 Artifact 中的 `docs/PERF_REPORT.md`（不入库）。本地可 `make docs-perf-report`。  
> IO/写盘敏感项（`store_save` / `mcp_graph_create` 等）比对用 **p50**，抑制 mean 长尾假退化；`memory_RSS_abs_*` 为绝对 RSS 信息项。

## 元信息

| 项 | 值 |
|----|----|
| VERSION | `{{VERSION}}` |
| commit | `{{COMMIT}}` |
| 生成时间 | `{{TIME}}` |
| 基线文件 | `tests/bench_baseline.json` |
| 结果文件 | `bin/bench_result.json` |

## 结论

| 项 | 值 |
|----|----|
| 总评 | `{{PERF_OVERALL}}`（PASS / WARN / FAIL） |
| 说明 | `{{PERF_NOTE}}` |

- **PASS**：未超过 CI 失败阈值（见 `scripts/bench_ci_retry.sh` / 环境变量放宽项）。
- **WARN**：共享 runner 抖动或本地/CI 偏差导致相对基线告警；bench 本身跑通。
- **FAIL**：连续重试后仍破门禁 → 阻断（除非已走「更新基线」流程）。

## 指标摘要（18）

| 指标 | value | unit | vs 基线 |
|------|-------|------|---------|
| … | | | 从 `bench_result.json` 填 |

## 何时允许刷新基线

1. 故意的性能优化或算法变更，且已在稳定环境复测。
2. 经人工确认后：GitHub Actions → **Update bench baseline**（`workflow_dispatch`，须勾选确认）。
3. **禁止**在日常 CI 中自动写回基线。

## 相关

- [验收清单 · 性能项](../ACCEPTANCE_DOD.md)
- CI Artifact 中的 `bin/bench_result.json` 与测试报告「微基准」分节
