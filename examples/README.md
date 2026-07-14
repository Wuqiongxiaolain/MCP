# examples 目录说明

> latest update: v0.1.2, 2026-07-14

本目录按“输入样例 / 导出结果”拆分：

- `example_input/`：所有导入源文件
- `example_output/<原文件名>_out/`：对应输入文件的导出结果
- `example_testout/`：本地全量 convert 试跑产物（`scripts/export-example-testout.sh`，一般不提交）

迁移说明：

- `mindmap.md` 已迁移为 `example_input/outline.md`
- `whiteboard.excalidraw` 已迁移为 `example_input/whiteboard_freedraw.excalidraw`
- `er.mmd` 为仅用于 `validate` 的输入样例，当前不提供对应 `example_output/er.mmd_out/`

## 输入类型

### 图源文件 → 图导出

导入格式覆盖：

- `.drawio`
- `Mermaid`
- `Markdown 大纲`
- `Excalidraw JSON（含 freedraw）`
- `XML`
- **边表 / 层级表 CSV**（如 `orgchart.csv`）→ 可直接 `convert` / `create from-csv` 成图

导出格式覆盖：

- `.drawio` / Mermaid / Excalidraw / SVG / PNG / PDF / `.url.txt`

示例：`example_input/flowchart.mmd` → `example_output/flowchart.mmd_out/`  
带颜色：`example_input/flowchart_colors.mmd`（`classDef` / `linkStyle` → 模型 `fillColor`/`strokeColor`）

### 通用表 CSV / 表 XML → table 产物

`enemy_sample.csv`、`enemy_sample.xml`、`skill_relations.csv` 是一等公民 **Table**，不是盲目 `convert to-*` 的图源。

表交换格式（与表 XML 互通）：`csv` / `model`（JSON）/ `xml`。宽表另可导出 `check_report.csv`。

| 输入 | 输出目录 | 产物 |
|------|----------|------|
| `enemy_sample.csv` | `example_output/enemy_sample.csv_out/` | `*.csv`、`*.model.json`、`*.xml`、`*.check_report.csv`；协同增强另有 `*.anim_checklist.csv`、`*.with_slug.csv` |
| `enemy_sample.xml` | `example_output/enemy_sample.xml_out/` | 同上（`format=xml` 导入后导出，验证与 CSV 路径互通） |
| `enemy_sample_bad.csv` | `example_output/enemy_sample_bad.csv_out/` | 非法枚举样例：`*.check_report.csv`、`*.fixed.csv`、`*.fix_enums.log` |
| `enemy_spec.md` | `example_output/enemy_spec.md_out/` | 思维导图规范 → `*.rules.csv`、`*.mmd` |
| `name_slug_demo.csv` | （见 pipeline） | slug 稳定键演示输入 |
| `skill_relations.csv` | `example_output/skill_relations.csv_out/` | 表：`*.csv`、`*.model.json`、`*.xml`；图：`*.mmd` / `*.drawio` / `*.excalidraw` / `*.svg` / `*.url.txt`；`*.png` / `*.pdf` 尽力生成（缺省见下） |

图&表协同流水线汇总目录：`example_output/table_collab_pipeline_out/`（`01`…`08` 逐步产物，见该目录 `README.md`）。

复现导出：

```bash
make export-table-examples
make export-table-collab-examples
# 或: bash scripts/export-table-collab-examples.sh ./bin/graphmcp.exe
```

说明：

- `png` / `pdf` 依赖本机浏览器/渲染链路；失败时脚本会 SKIP，不阻塞，也不强制提交这两类文件。
- `export-example-testout.sh` 对上述通用表会 **SKIP** 盲目 graph convert，避免误报 FAIL。
- `table_update` 的 `dry_run`/`detail` 为 MCP 审计预览，无单独静态产物；CLI：`table update … --dry-run`。

CLI 最小示例见 [`docs/CLI_MCP_REFERENCE.md`](../docs/CLI_MCP_REFERENCE.md)「最小示例」表中的 `table create` / `table from-table` / 协同编排行。
