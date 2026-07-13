# examples 目录说明

> latest update: v0.1.1, 2026-07-10

本目录按“输入样例 / 导出结果”拆分：

- `example_input/`：所有导入源文件
- `example_output/<原文件名>_out/`：对应输入文件的导出结果

迁移说明：

- `mindmap.md` 已迁移为 `example_input/outline.md`
- `whiteboard.excalidraw` 已迁移为 `example_input/whiteboard_freedraw.excalidraw`
- `er.mmd` 为仅用于 `validate` 的输入样例，当前不提供对应 `example_output/er.mmd_out/`

导入格式覆盖：

- `.drawio`
- `Mermaid`
- `Markdown 大纲`
- `Excalidraw JSON（含 freedraw）`
- `XML`
- `CSV`（边表/层级表 → 图；另见通用表 `enemy_sample.csv` / `skill_relations.csv`）

导出格式覆盖：

- `.drawio`
- `Mermaid`
- `Excalidraw JSON`
- `SVG`
- `PNG`
- `PDF`
- `浏览器 URL（保存为 .url.txt）`

示例：

- 输入：`example_input/flowchart.mmd`
- 输出目录：`example_output/flowchart.mmd_out/`
