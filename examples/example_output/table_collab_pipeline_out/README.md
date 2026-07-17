# 图&表协同流水线样例

复现：`bash scripts/export-table-collab-examples.sh`

导出 CSV 默认为 UTF-8 BOM + CRLF（Excel 可双击）；表 XML 仍为 graphmcp 方言，勿用 Excel 打开。

| 文件 | 对应工具 |
|------|----------|
| `01_rules.csv` | `table_rules_from_graph` / `table rules-from-graph` |
| `02_check_before.csv` | `table_check`（修复前） |
| `03_fixed.csv` | `table_fix_enums` 写回后的业务表 |
| `04_check_after.csv` | 修复后再 `table_check` |
| `05_anim_checklist.csv` | `table_derive` `animation_checklist` |
| `06_with_slug.csv` | `table_transform_column` `slug`（`name_slug_demo.csv`） |
| `07_sample_rows.csv` | `table_sample_rows` |
| `08_after_propose.csv` | `table_propose_rows`（追加一行） |

另导出到既有目录：

- `enemy_sample.csv_out/enemy_sample.anim_checklist.csv`
- `enemy_sample.csv_out/enemy_sample.with_slug.csv`（中文名多为 `col_N` 回退）

自然语言改表预览请用 MCP `table_update` 的 `dry_run`/`detail`（无单独产物文件）。
