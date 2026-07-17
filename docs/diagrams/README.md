# docs/diagrams — 文档制图版本库

本目录存放文档插图的 **graphmcp 图版本**（可复现导出）；对应 SVG 在 [`../images/`](../images/)。

| 子目录 / 图 | 用途 | 导出 SVG |
|-------------|------|----------|
| `timeline-s/` | 项目版本演进（S 形） | `images/version-evolution-s.svg` |
| `doc-figures/` | 架构 / 管道 / 里程碑等文档图 | 见下方 id |

## doc-figures 图 id

| id | 说明 |
|----|------|
| `arch-overview` | 整体架构总览 |
| `dual-pipeline` | Graph/Table 双管道与桥接 |
| `table-io` | 表输入输出 |
| `version-workflow` | Draft → Stage → Commit |
| `day1-pipeline` | 首日成果主链 |
| `dev-milestones` | 开发里程碑时间线 |
| `png-fallback` | PNG/PDF 渲染回退链 |
| `branch-strategy` | 分支策略示意 |
| `store-layout` | 存储目录结构 |

重导示例：

```powershell
.\bin\graphmcp.exe export to-svg --id arch-overview --store docs\diagrams\doc-figures -o docs\images\arch-overview.svg
```
