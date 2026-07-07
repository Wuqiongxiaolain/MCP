# 工作记录（WORKLOG）

项目：graphmcp — 图形设计与绘图 MCP 工具
日期：2026-07-05
环境：Windows 11 + MinGW-w64 g++ 8.1（C++17）+ Git 2.53

---

## 1. 需求分析与技术决策（开工前）

| 决策点 | 结论 | 理由 |
|---|---|---|
| 依赖策略 | 零第三方库，JSON/XML/Base64 全部手写 | 课程环境不可控（无 CMake、无包管理器）；单文件 g++ 即可构建，CI 最简 |
| 模块组织 | header-only + 单翻译单元 | 编译命令一条即可；模块边界仍靠命名空间清晰划分 |
| 数据存储 | JSON 文件（弃 SQLite） | 版本快照天然是文档型数据；避免引入 sqlite3 依赖破坏零依赖原则 |
| 架构核心 | 统一图模型居中，"N 输入 × M 输出" | 避免写 N×M 个格式转换器，新增格式只加一个函数 |
| PNG/PDF | 委托外部转换器 + SVG 回退 | 纯 C++ 手写光栅化/PDF 成本过高且偏离课程重点；回退策略保证命令不"硬失败" |
| URL 生成 | mermaid.live `#base64:`（免压缩） | draw.io URL 需要 deflate 压缩（要 zlib），mermaid.live 支持纯 base64，零依赖可达 |
| MCP 传输 | stdio + 换行分隔 JSON-RPC 2.0 | MCP 标准 stdio 传输方式，协议版本 2024-11-05 |

## 2. 开发过程记录

### 阶段一：环境确认
- 探测到 MinGW g++ 8.1、mingw32-make、git 2.53；**无 CMake** → 本地用 g++/Makefile 构建，CMakeLists.txt 保留给 CI/IDE。
- 创建 `D:\MCP` 项目根目录。

### 阶段二：核心库（自底向上）
1. `src/json.hpp` — 递归下降 JSON 解析器 + 序列化。要点：对象用
   `vector<pair>` 保持键插入顺序（保证导出文件 diff 友好）；`\uXXXX`
   含代理对正确解码为 UTF-8。
2. `src/model.hpp` — 统一图模型 Graph/Node/Edge + JSON 互转；
   `parent` 字段一字段三用（树/分组/容器）；按 label 的 CJK 宽度估算默认节点尺寸。
3. `src/parsers.hpp` — 5 个解析器：
   - Mermaid：手写词法（不用正则），支持 6 种节点形状括号、8 种箭头、
     边标签 `|x|`、`subgraph…end` 栈式分组；mindmap 按缩进建树；
     erDiagram 解析关系行与实体属性块。
   - Markdown：`#` 级别 + 列表缩进混合建树。
   - CSV：表头驱动双模式（from/to 边表→流程图；id/label/parent→组织图），带引号转义。
   - XML：手写迷你解析器（标签/属性/文本/实体），`<node>` 嵌套即层级。
   - Excalidraw：原始元素无损保留 + 派生逻辑节点（containerId 文本绑定、箭头 start/endBinding）。
   - `detectFormat` 首字符/首行启发式自动识别。
4. `src/layout.hpp` — 校验（重复 ID、悬空边、层级环、孤立点、空标签）+
   三种布局（Kahn 分层含环兜底 / 递归子树宽度树布局 / 网格兜底），
   分组容器最后按成员包围盒回填。
5. `src/exporters.hpp` — drawio mxGraphModel（子节点相对坐标）、Mermaid
   回写（label 转义）、Excalidraw（绑定文本+箭头绑定）、SVG（边裁剪到
   节点边框、菱形/椭圆/分组虚线框、ER 属性表）、mermaid.live URL、
   PNG/PDF 外部转换器链（inkscape→rsvg-convert→magick）+ SVG 回退。
6. `src/storage.hpp` — index.json 目录 + latest.json + 不可变版本快照；
   回滚实现为"旧快照另存新版本"（保留全部历史，非破坏）。
7. `src/mcp.hpp` — JSON-RPC 分发（initialize/tools/list/tools/call/ping、
   通知不回包）+ 8 个工具的 schema 与实现。
8. `src/main.cpp` — 9 个 CLI 子命令 + `serve`。

### 阶段三：构建与测试
- `tests/test_main.cpp`：14 组测试、121 条断言，覆盖 JSON 往返、5 种解析器、
  格式识别、校验（悬空边/层级环）、布局（层序/树向）、4 种导出器、Base64
  边界、存储版本/回滚/损坏处理、MCP 协议握手与工具调用。
- **首次编译即通过（-Wall -Wextra 零警告），121/121 断言通过。**

### 阶段四：端到端验证与缺陷修复
用 6 个示例文件全链路冒烟，发现并修复 2 个缺陷：

| # | 缺陷 | 根因 | 修复 |
|---|---|---|---|
| 1 | `export -o out\flow.svg` 报 cannot write file | `ofstream` 不创建父目录 | `writeFile` 前增加 `ensureParentDirs`（逐级 mkdir，跳过盘符） |
| 2 | `--name 登录流程` 入库后乱码 | Windows argv 是 ANSI(GBK) 编码，而模型按 UTF-8 存储 | `GetCommandLineW + WideCharToMultiByte(CP_UTF8)` 重取 UTF-8 argv；`SetConsoleOutputCP(CP_UTF8)` |

修复后回归：121/121 通过；重跑冒烟全绿。

### 阶段五补：PNG/PDF 导出增强（接入 Cherry Studio 验证时发现）

用户在 Cherry Studio 里让模型生成 PDF，收到"未安装 PDF/PNG 转换工具"提示。
排查确认这不是客户端或模型问题，而是本机没装 inkscape/rsvg/magick 光栅化器，
graphmcp 按设计回退成 SVG。由于用户机器已装 Microsoft Edge（Chromium 内核），
决定让工具自动复用它，避免额外下载。为此改进 `exporters.hpp::rasterize`，
过程中连续排掉 4 个坑：

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 3 | 只找 `chrome` 命令，找不到已装的 Edge | 检测名单太窄 | `findBrowser()` 探测 Chrome/Edge 的标准安装路径 + PATH |
| 4 | `std::system` 调 Edge 无输出 | Windows `cmd /c` 对"带引号 exe 路径 + 重定向"的组合会错误剥离引号 | 把命令写入临时 `.bat` 再执行（`runQuiet`） |
| 5 | 相对路径 outPath 时浏览器不产出文件 | 浏览器按自身工作目录解析 `--print-to-pdf`/`--screenshot` | `rasterize` 开头把 svg/out 路径统一转绝对路径（`absPath`） |
| 6 | 系统已开着 Edge 时无头命令直接返回、跳过任务 | 新无头实例附加到已运行实例 | 加独立 `--user-data-dir`（临时 profile 目录）隔离 |

外加健壮性：`getenv("ProgramFiles(x86)")` 在 MSYS/Git Bash 下取不到（括号名被屏蔽），
补充硬编码绝对路径兜底。SVG 用 HTML `@page` 包装使 PDF/PNG 尺寸贴合图形。

验证（Windows 正常环境，等同 Cherry Studio 启动环境）：
- CLI `export --to pdf/png`：经 Edge 生成，`%PDF-` 头正确、PNG 魔数正确 ✔
- MCP `graph_export to=pdf`：返回 "pdf written via edge"，67KB 有效 PDF ✔
- 121/121 单元测试仍全绿 ✔

### 阶段五：验证结果实录
```
create  ：6 种格式（mmd/csv/md/er.mmd/xml/excalidraw）全部入库 ✔
export  ：drawio/svg/excalidraw/mermaid/url 全部成功 ✔
          png 无转换器 → 按设计回退写出 flow.png.svg 并提示 ✔
history ：v1(created) → v2(第二版) → v3(rollback to v1) ✔
validate：er.mmd → valid: no issues found ✔
MCP     ：initialize 握手 → tools/list 返回 8 工具 →
          graph_create 中文脑图入库返回 {"status":"created"} ✔
```

### 阶段六：DevOps 配置
- **Git**：`.gitignore`（bin/dist/store 等产物），分阶段提交。
- **GitHub Actions**：`.github/workflows/ci.yml`，构建 → 单元测试 → 冒烟测试
  → 打包制品；可选 SonarQube 静态分析与质量门禁（`SONAR_ENABLED=true`）。
- **SonarQube**：`sonar-project.properties`（sources/tests 划分、产物目录排除、
  cfamily build-wrapper 配置）。

## 3. 工时分布（估算）

| 阶段 | 占比 |
|---|---|
| 需求分析与技术决策 | 10% |
| 核心库编码（模型/解析/布局/导出/存储/MCP） | 50% |
| 测试编写与端到端验证 | 20% |
| 缺陷修复（目录创建、Windows 编码） | 5% |
| DevOps 配置（GitHub Actions/Sonar/Git） | 10% |
| 文档（README/架构/思维导图/工作记录） | 5% |

## 4. 遗留与展望

- [ ] 可选功能"专属画布实时绘制"：可基于 SVG 导出 + 本地 HTML 页面
  （WebSocket 或轮询 latest.json）实现局部刷新预览。
- [ ] Mermaid 支持面扩展：classDiagram、stateDiagram。
- [ ] draw.io URL 直开：引入 miniz 单头文件库做 deflate 后即可生成
  `app.diagrams.net/#R…` 链接（会破坏零依赖，故暂缓）。
- [ ] SQLite 存储后端：当图数量大、需要按类型/时间检索时切换。
- [ ] 布局质量：分层布局可加 median 启发式减少交叉。
