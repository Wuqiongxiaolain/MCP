# 🎨 graphmcp 精选样例集

由 **graphmcp v0.2.4-beta** 生成的 10 个幽默风趣的流程图样例，覆盖多种图表结构模式。

## 📂 目录结构

每个样例目录包含：

| 文件 | 格式 | 用途 |
|------|------|------|
| `source.mmd` | Mermaid | 📝 源文件（可编辑） |
| `*.drawio` | draw.io XML | 🖊️ Diagrams.net 桌面编辑 |
| `*.excalidraw` | Excalidraw JSON | ✏️ 手绘白板风格 |
| `*.svg` | SVG | 📐 矢量图，可嵌入网页/文档 |
| `*.png` | PNG | 🖼️ 位图，直接预览分享 |
| `*.pdf` | PDF | 📄 矢量文档，适合打印 |
| `*.mmd` | Mermaid | 🔄 回导出的 Mermaid（含布局坐标） |
| `*.model.json` | Model JSON | 🔧 统一模型，可二次处理 |
| `*.url.txt` | URL | 🔗 mermaid.live 在线分享链接 |

## 🎯 样例列表

| # | 名称 | 主题 | 结构类型 | 节点/边 |
|---|------|------|----------|---------|
| 01 | **V我50作战计划** | 如何优雅地向别人要到50块钱 | 策略分支型 | 8/10 |
| 02 | **哈吉米猫猫升职记** | 一只网红猫的晋级之路 | 线性递进型 | 11/10 |
| 03 | **海贼王剧情速通** | 从东海到最终之岛的路线图 | 主干+支线型 | 19/18 |
| 04 | **股票从入门到放弃** | 韭菜的完整心路历程 | 循环回退型 | 13/14 |
| 05 | **打工人摸鱼决策树** | 根据老板位置动态切换摸鱼策略 | 条件分支型 | 12/15 |
| 06 | **奶茶点单灵魂拷问** | 糖度/配料/品牌的终极选择 | 多层决策型 | 11/14 |
| 07 | **周末生存指南** | 躺平 vs 内卷的博弈 | 对比分支型 | 13/13 |
| 08 | **假装很懂AI话术包** | 不同场景下的装逼关键词 | 场景分类型 | 15/14 |
| 09 | **游戏菜鸟进化论** | 从被虐到虐人的成长路径 | 阶段递进型 | 14/14 |
| 10 | **减肥Flag生命周期** | 从立flag到放弃的完整闭环 | 循环型 | 11/12 |

## 🔄 重新生成

如需修改源文件后重新导出，运行：

```powershell
# 单个样例
D:\MCP\bin\graphmcp create from-mermaid --file source.mmd --name "name"
D:\MCP\bin\graphmcp export --id <id> --to drawio -o name.drawio
D:\MCP\bin\graphmcp export --id <id> --to mermaid -o name.mmd
D:\MCP\bin\graphmcp export --id <id> --to excalidraw -o name.excalidraw
D:\MCP\bin\graphmcp export --id <id> --to svg -o name.svg
D:\MCP\bin\graphmcp export --id <id> --to png -o name.png
D:\MCP\bin\graphmcp export --id <id> --to pdf -o name.pdf
D:\MCP\bin\graphmcp export --id <id> --to model -o name.model.json
D:\MCP\bin\graphmcp export --id <id> --to url > name.url.txt
```

## 💡 布局说明

所有样例使用 `flowchart TD`（自上而下分层布局），经过以下优化确保图面清晰：

- **Sugiyama 分层算法** — Kahn 拓扑排序 + 层间均衡
- **重心交叉最小化** — 贪心相邻交换减少边交叉
- **分支节点 ≤ 3 出口** — 避免局部过度拥挤
- **控制节点标签长度** — 中文标签简洁，必要时换行

> 如需调整布局效果，可以修改 `source.mmd` 中的图结构后重新生成。
