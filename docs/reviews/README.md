# 代码审查归档

重大改造的审查结论归档于此，便于 DevOps / 课程验收追溯。

## 命名（正式归档）

```
docs/reviews/YYYY-MM-DD-简短主题.md
```

示例：`2026-07-17-mcp-geometry-edit.md`

## 流程

1. 用 [TEMPLATE.md](TEMPLATE.md) 复制新文件并填写。
2. 关联 PR / commit；列出必改项与复测命令（优先已有 smoke / 单测）。
3. 合入前确认必改项已关闭或有明确延期说明。
4. 在 [ACCEPTANCE_DOD](../ACCEPTANCE_DOD.md) 审查项中引用本文件路径。

## Cursor Plan 归档副本

已确认归属本仓库的审查 Plan 副本在 [`cursor-plans/`](cursor-plans/README.md)，统一命名为：

```
代码审查报告_YYYY_MM_DD.md
代码审查报告_YYYY_MM_DD_02.md   # 同日多份时递增
```

正文一级标题格式：`代码审查报告_YYYY_MM_DD：<审查主题>`。

完整目录见 [cursor-plans/README.md](cursor-plans/README.md)（当前 **13** 份）。

## 索引（正式归档）

| 日期 | 主题 | 文件 |
|------|------|------|
| 2026-07-17 | CI Artifact docs-test-report-272 验收分析 | [2026-07-17-ci-artifact-docs-test-report-272.md](2026-07-17-ci-artifact-docs-test-report-272.md) |
| （其余可从 cursor-plans 结论摘录） | — | 见 [cursor-plans/](cursor-plans/README.md) |

## 不强制

- cursor-plans 中的 Plan 全文可作证据；正式签核仍建议按 TEMPLATE 提炼一页结论。
- 小修复可仅用 PR 描述；**跨模块 / 协议 / 存储**类改造应正式入库。
