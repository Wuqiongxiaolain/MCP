# Changelog

本文件按版本记录对用户/验收可见的变更摘要。过程性开发日记见 [`docs/DEV_PROCESS.md`](docs/DEV_PROCESS.md)。

## 维护约定

1. **发版前**更新本节（与根目录 `VERSION`、annotated tag message 对齐）。
2. MCP / CLI **Breaking change**（工具增删改、必填字段变化）必须单独列出，并同步 DoD / 发布报告。
3. OpenAPI 为 `toolList()` 生成物：契约变更先改代码，再 `make docs-api`。
4. 可从 GitHub Release 自动 notes 或 `DEV_PROCESS` 摘录后精炼写入此处。

---

## [v0.2.9-beta] - 2026-07-17

### 新增

- MCP 几何原子编辑：`graph_set_edge_route` / `graph_nudge_node` / `graph_set_edge_heads` 等；优先 `graph_apply` 原子改图。
- DevOps 验收文档：`docs/ACCEPTANCE_DOD.md`、`docs/RUNBOOK.md`、质量门/部署/性能报告模板与 `docs/reviews/` 审查归档约定。
- CI：测试报告首屏 GO/NO-GO；cppcheck 质量门报告 Artifact；CD 部署报告 Artifact。

### 变更

- 技术选型分析文首统一「决策摘要」框。
- CLI/MCP 参考明确：契约真源为 `toolList()`，OpenAPI 为生成物。

### Breaking

- 无相对上一公开发布的强制不兼容项（以本仓库 tag 历史为准；若有工具参数变更请在发版时补记）。

### 修复

- （发版时按需从 Release notes 回填）

---

## 更早版本

历史里程碑与逐日演进见 [docs/PROJECT_TIMELINE.md](docs/PROJECT_TIMELINE.md) 与 [docs/DEV_PROCESS.md](docs/DEV_PROCESS.md)。
