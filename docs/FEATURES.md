# 🎯 graphmcp：一份图，任意格式进出

> latest update: v0.1.1, 2026-07-10

不管你手上是 Mermaid、Markdown、CSV、XML、Excalidraw 还是 draw.io，丢给 graphmcp，它都能读懂；不管你要 SVG、PNG、PDF、drawio 还是 Excalidraw，它都能吐给你。中间那道「先转 A 再转 B」的手工活，从此不用你自己干。

**你可能正在遇到这些事**：

- ✍️ 写技术文档，Mermaid 流程图想导出成图片贴进去？
- 🏗️ draw.io 里画好的架构图，想转成 PDF 塞进设计评审文档？
- 🤖 用 Claude Code 写代码时，想让 AI 顺手把架构图也一起改了，不用自己再开一次 draw.io？

一句话：格式的事交给 graphmcp，你只管画。

<img src="images/pipeline.svg" alt="graphmcp 数据流水线" width="50%">

<sub>▲ 这张图本身就是 graphmcp 画的：</sub>

---

## 它能画这六种图

<table>
<tr>
<td align="center"><img src="images/type-flowchart.svg" width="200"><br><b>流程图</b></td>
<td align="center"><img src="images/type-architecture.svg" width="200"><br><b>架构图</b></td>
<td align="center"><img src="images/type-er.svg" width="200"><br><b>ER 图</b></td>
</tr>
<tr>
<td align="center"><img src="images/type-orgchart.svg" width="200"><br><b>组织架构图</b></td>
<td align="center"><img src="images/type-mindmap.svg" width="200"><br><b>思维导图</b></td>
<td align="center"><img src="images/type-whiteboard.svg" width="200"><br><b>白板</b></td>
</tr>
</table>

六种图共用同一套模型，格式之间随便转、不丢信息。

---

## 🚀 3 条命令，立刻跑起来

```bash
graphmcp create from-mermaid --file flow.mmd --name "登录流程"
graphmcp export to-svg --id <graph-id> -o output.svg
graphmcp serve                      # 作为 MCP 服务器接入 AI 客户端
```

第一条建图，第二条导出，第三条把它接进 Claude Code / Claude Desktop。完整参数见 [CLI &amp; MCP 指令参考](CLI_MCP_REFERENCE.md)。

---

## 🧰 核心能力：格式，你不用操心

原始诉求就是「别让我在多个工具之间手动倒腾文件」。

### 📥 丢进去就行，不用管它原来是什么格式

Mermaid、Markdown 大纲、CSV、XML、Excalidraw、draw.io——统统直接丢给 `from-input`，graphmcp 自己认格式、自己猜图类型：给它一个 `.mmd` 就出流程图，给它一个 `.csv` 就出组织架构图。这意味着你再也不用为「先把 A 转成 B、再转成 C」这种破事浪费时间。

<img src="images/cli-create.svg" alt="自动识别格式并入库" width="100%">

### 📤 一次建模，想要什么格式都有

同一张图，一条命令换一种格式吐出来：drawio、Mermaid、Excalidraw、SVG、PNG、PDF、JSON，外加一条 mermaid.live 在线编辑链接。SVG 永远可靠；PNG/PDF 依赖本机是否装了 inkscape/rsvg/magick 或 Chrome/Edge——都没装也不会失败退出，而是自动帮你留一份 SVG 兜底。

<img src="images/cli-export.svg" alt="一图多格式导出" width="100%">

### 📐 排版不用自己摆

新建的图不需要你手动拖节点——流程图自动分层，思维导图自动长成树，组织架构图自动垂直排布，`layout auto` 会照着图的类型自己选策略。

<img src="images/cli-layout.svg" alt="四种自动布局策略" width="100%">

### 🔄 GUI 里改完，一键存回来

不想在命令行改标签？`edit with-drawio` 直接调起 draw.io，你在 GUI 里随便改，改完 `import` 一声，graphmcp 帮你解析、校验、存成新版本——图库版本 +1，改动全程留痕。

<img src="images/cli-import.svg" alt="编辑闭环" width="100%">

### 🎨 白板转出转入，不失真

Excalidraw 手绘的白板——笔迹、图片、字体——原样往返，不是重新画一遍：原始笔迹数据整份保留，导出时优先吐回原始轨迹，连离线字体都内嵌在文件里，换台电脑照样能看。

<img src="images/type-whiteboard.svg" alt="Excalidraw 白板精确导出" height="200">

---

## 🎯 进阶能力：让「改图」这件事可控

多人协作、AI 代改的场景下，「改错了怎么办」比「能不能改」更重要——这组能力就是解决这个。

### ✅ 图哪里错了，一眼看出来

重复 ID、边指向不存在的节点、层级成环、孤立节点——四条规则一跑就知道。退出码可以直接接 CI：`0` 干净放行，`3` 有硬错误拦截，`--strict` 连警告也当错误处理。

<img src="images/cli-validate.svg" alt="结构校验与退出码" width="100%">

### 🗂 每一次修改都能回溯

跟 Git 一个逻辑：改动先进草稿（draft），确认了再暂存（stage）、提交（commit）。谁在什么时候改了什么、旧值新值分别是什么，`diff` 精确到字段级，随时能 `checkout` 回任意历史版本。

<img src="images/cli-version.svg" alt="版本控制工作流" width="100%">

### ✏️ 改一个节点，不用重写整份文件

想改一个标签、挪一个节点位置，不需要把整张图重新描述一遍——直接对着节点/边做增删改，还支持按属性批量改（比如把所有 `shape=rect` 一次性换成 `round`）。

<img src="images/cli-edit.svg" alt="Draft 模式图编辑" width="100%">

### 🎯 一个节点、一个节点地推进

游标语义搬到图上：打开、读当前项、走下一个、走上一个、关闭。特别适合 AI 客户端「读一个、判断一个、改一个」地啃一张大图，而不必把整张图都塞进上下文——状态存在磁盘上，跨进程也不丢。

<img src="images/cli-cursor.svg" alt="游标遍历" width="100%">

---

## 🔌 高级玩法：让 AI 直接帮你改图

### 🤖 MCP 服务器，25 个工具随时待命

`graphmcp serve` 把上面所有能力原样打包成 25 个 MCP 工具，通过 stdio 直接接进 Claude Code / Claude Desktop。你不用记命令、不用切窗口——跟 AI 说一句"把这个流程图里的审批节点删了"，剩下的事它自己调工具办完。

<img src="images/mcp-handshake.svg" alt="MCP 握手与工具列表" width="100%">

<!-- TODO(本地截图)：在 Claude Code 中实际调用 graph_create 的界面截图，存为 docs/images/mcp-claude-code.png 后替换下一行 -->

<!-- <img src="images/mcp-claude-code.png" alt="Claude Code 中调用 graphmcp" width="100%"> -->

---

## 🔧 技术规格（供技术选型参考）

> 技术规格版本以根目录 VERSION 为准

JSON/XML 解析器、Base64 编解码全部手写，一条 `g++` 命令构建完毕——不需要装 Node.js/npm，不需要 Python 运行时，也不用等 JVM 启动。Windows 下静态链接 libgcc/libstdc++，MCP 客户端用被裁剪的 PATH 拉起它也不会缺 DLL。

<img src="images/build.svg" alt="零依赖构建与测试" width="100%">

---

完整命令与参数见 [CLI &amp; MCP 指令参考](CLI_MCP_REFERENCE.md)，设计细节见 [应用运作逻辑](APPLICATION_LOGIC.md)。
