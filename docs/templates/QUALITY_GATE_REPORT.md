# 质量门报告模板

> 由 CI 填充为 `docs/QUALITY_GATE_REPORT.md`（gitignore / Artifact）。本地可参考本模板手工填写。

## 元信息

| 项 | 值 |
|----|----|
| VERSION | `{{VERSION}}` |
| branch | `{{BRANCH}}` |
| commit | `{{COMMIT}}` |
| 生成时间 | `{{TIME}}` |
| CI run | `{{RUN_URL}}` |

## 总结论

| 门禁 | 状态 | 说明 |
|------|:----:|------|
| **总体** | `{{OVERALL}}` | PASS / FAIL / PARTIAL |
| cppcheck（必过） | `{{CPPCHECK_STATUS}}` | error 级 >0 → FAIL |
| SonarQube / SonarCloud（可选） | `{{SONAR_STATUS}}` | PASS / FAIL / **SKIPPED** |

> **规则**：未配置 Sonar（`SONAR_ENABLED` 未开或缺少 Token/URL）必须写 **SKIPPED**，不得记为 PASS。  
> C++ 官方分析依赖 SonarCloud 或商业版 SonarQube；社区版自建不一定可用。

## cppcheck

| 项 | 值 |
|----|----|
| 命令 | `cppcheck --enable=warning,style,performance,portability --inline-suppr -q src`（门禁仅看 error） |
| exit | `{{CPPCHECK_EXIT}}` |
| error 数 | `{{CPPCHECK_ERRORS}}` |
| warning 数 | `{{CPPCHECK_WARNINGS}}` |

### 关键问题摘要

```
{{CPPCHECK_SUMMARY}}
```

## Sonar

| 项 | 值 |
|----|----|
| Host | `{{SONAR_HOST}}` |
| Project Key | `graphmcp`（见 `sonar-project.properties`） |
| Quality Gate | `{{SONAR_QG}}` |
| 仪表盘 | `{{SONAR_DASHBOARD_URL}}` |

跳过原因（若 SKIPPED）：`{{SONAR_SKIP_REASON}}`

## 阻断规则（本项目）

1. cppcheck **error** 级问题 → 阻断合并/发版。
2. Sonar 已启用且 Quality Gate 失败 → 阻断（仅 push main/dev 且配置完整时）。
3. Sonar SKIPPED → 不阻断，但须在 [ACCEPTANCE_DOD](../ACCEPTANCE_DOD.md) 中明示。

## 相关

- [验收清单](../ACCEPTANCE_DOD.md)
- [运维 Runbook · SonarCloud](../RUNBOOK.md)
