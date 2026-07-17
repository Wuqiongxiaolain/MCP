# 代码审查归档

重大改造的审查结论归档于此，便于 DevOps / 课程验收追溯。Cursor `.plan.md` 可作为起草材料，**不以 IDE 本地文件代替本目录**。

## 命名

```
docs/reviews/YYYY-MM-DD-简短主题.md
```

示例：`2026-07-17-mcp-geometry-edit.md`

## 流程

1. 用 [TEMPLATE.md](TEMPLATE.md) 复制新文件并填写。
2. 关联 PR / commit；列出必改项与复测命令（优先已有 smoke / 单测）。
3. 合入前确认必改项已关闭或有明确延期说明。
4. 在 [ACCEPTANCE_DOD](../ACCEPTANCE_DOD.md) 审查项中引用本文件路径。

## 索引

| 日期 | 主题 | 文件 |
|------|------|------|
| （尚无正式归档） | — | 使用 TEMPLATE 新增 |

## 不强制

- 不批量迁入历史 `.plan.md` 全文。
- 小修复可仅用 PR 描述；**跨模块 / 协议 / 存储**类改造应入库。
