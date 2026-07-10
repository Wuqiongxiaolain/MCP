# graphmcp 功能概览

> **v1.1.0** · C++17 · 零第三方依赖 · 单可执行文件
> 一个把「任意结构化输入」变成「任意图形输出」的绘图引擎，既是 CLI，也是 MCP 服务器。

**核心思想：所有格式 → 统一图模型 → 所有输出格式。** N 个解析器 × M 个导出器，只需 N+M 个适配层，而不是 N×M 个转换器。

<img src="images/pipeline.svg" alt="graphmcp 数据流水线" width="100%">



<sub>▲ 这张图本身就是 graphmcp 画的：</sub>

---

## 🧩 六类图形，一套模型

流程图、架构图、ER 图、组织架构图、思维导图、白板 —— 六种图共用一个 `Graph` 结构。秘诀是 `node.parent` 一字段三用：树层级、子图分组、容器嵌套。类型字符串随模型存储，往返转换不丢失。

|        流程图        |       架构图       |     ER 图     |
| :------------------: | :----------------: | :------------: |
|                      |                    |                |
| **组织架构图** | **思维导图** | **白板** |
|                      |                    |                |

---

## 🔍 六种输入格式，自动识别

Mermaid、Markdown 大纲、CSV、XML、Excalidraw、draw.io —— 丢给 `from-input` 即可，`detectFormat()` 自己认。图类型也一并推断：`.mmd` 出流程图，`.md` 出思维导图，`.csv` 出组织架构图。Mermaid 解析器是手写词法器，不依赖正则。

<img src="images/cli-create.svg" alt="自动识别格式并入库" width="100%">

---

## 📤 八种输出格式，一图多投

一次建模，导出 drawio / mermaid / excalidraw / svg / png / pdf / model(JSON)，外加一条 mermaid.live 在线编辑链接。

> ⚠️ **PNG / PDF 需要外部转换器**（inkscape、rsvg-convert、magick，或 Chrome/Edge headless）。都找不到时不会失败退出，而是自动回退，在目标路径旁写一份 SVG —— 下图末尾就是真实的回退输出。

<img src="images/cli-export.svg" alt="一图多格式导出" width="100%">

---

## 📐 自动布局，四种策略

流程图走 Kahn 拓扑分层（带成环兜底），思维导图走水平树，组织架构图走垂直树，另有网格布局。`layout auto` 按图类型自选策略。分组容器的包围盒在布局后自动回填。

<img src="images/cli-layout.svg" alt="四种自动布局策略" width="100%">

---

## ✅ 结构校验，可接入 CI

四条规则：重复节点 ID、悬空边（指向不存在的节点）、层级成环、孤立节点。退出码可直接做 CI 闸门 —— `0` 干净、`3` 有 error、`1` 表示 `--strict` 把 warning 提升成了 error。

<img src="images/cli-validate.svg" alt="结构校验与退出码" width="100%">

---

## 🗂 Git 式版本控制

`draft → stage → commit` 三段式工作流，配 `log` / `diff` / `checkout`。每次提交冻结一份不可变快照 `versions/vN.json`。版本对比精确到字段级：谁被增、谁被改、旧值新值一目了然。

<img src="images/cli-version.svg" alt="版本控制工作流" width="100%">

---

## ✏️ Draft 模式图编辑

不必重写整份源文件 —— 直接对节点和边做增删改，变更先落到 `draft.json`，提交后才成为新版本。支持 `--selector` 按属性批量改（如把所有 `shape=rect` 改成 `round`），删节点时级联删除关联边。

<img src="images/cli-edit.svg" alt="Draft 模式图编辑" width="100%">

---

## 🎯 游标遍历

数据库游标语义搬到图上：`open / get / next / prev / update / insert / delete / close`。游标状态持久化到磁盘，跨进程存活 —— 特别适合 AI 客户端「读一个节点、改一个节点」地逐步推进，而不必一次性吞下整张图。

<img src="images/cli-cursor.svg" alt="游标遍历" width="100%">

---

## 🔄 编辑闭环：导出 → 外部编辑 → 回导

`edit with-drawio` 调起 draw.io（自动发现编辑器路径），你在 GUI 里随手改，`import` 再把它解析、校验、存成新版本。draw.io、Excalidraw、SVG、浏览器四种编辑器均可，改完图库版本 +1。

<img src="images/cli-import.svg" alt="编辑闭环" width="100%">

---

## 🤖 MCP 服务器 · 25 个工具

`graphmcp serve` 通过 stdio 讲 JSON-RPC 2.0，向 Claude Code / Claude Desktop 暴露 25 个工具，覆盖生命周期、查看、编辑、版本、游标五大族。日志只写 stderr，绝不污染 stdout 的协议流。

<img src="images/mcp-handshake.svg" alt="MCP 握手与工具列表" width="100%">

<!-- TODO(本地截图)：在 Claude Code 中实际调用 graph_create 的界面截图，存为 docs/images/mcp-claude-code.png 后替换下一行 -->

<!-- <img src="images/mcp-claude-code.png" alt="Claude Code 中调用 graphmcp" width="100%"> -->

---

## 🎨 Excalidraw 白板无损往返

白板的原始 `elements[]` 与派生的逻辑节点并排保存，导出时优先吐回原数组 —— 手绘轨迹、仿射变换、图片与字体全部保真，字体以 base64 内嵌，离线可读。上方「白板」示例图即为真实导出结果。

<img src="images/type-whiteboard.svg" alt="Excalidraw 白板精确导出" height="260">

---

## 📦 零依赖 · 单文件 · 静态链接

JSON 解析器、XML 解析器、Base64 编解码全部手写。除 `main.cpp` 外皆为 header-only，一条 `g++` 命令构建完毕，没有链接步骤，没有第三方库。Windows 下静态链接 libgcc/libstdc++ —— MCP 客户端用被裁剪的 PATH 拉起它也不会缺 DLL。

<img src="images/build.svg" alt="零依赖构建与测试" width="100%">

---

## 快速上手

```bash
graphmcp create from-mermaid --file flow.mmd --name "登录流程"
graphmcp store list
graphmcp export to-svg --id <graph-id> -o output.svg
graphmcp serve                      # 作为 MCP 服务器接入 AI 客户端
```

完整命令与参数见 [CLI &amp; MCP 指令参考](CLI_MCP_REFERENCE.md)，设计细节见 [架构说明](ARCHITECTURE.md)。
