# graphmcp 项目思维导图

> latest update: v0.1.1, 2026-07-10

> 本文件同时是本工具的合法输入：
> `graphmcp create --input docs/MINDMAP.md --name 项目思维导图`
> 即可把这份思维导图导入工具本身并导出为 SVG / drawio / Excalidraw。

## Mermaid 版（可贴入 mermaid.live 查看）

```mermaid
mindmap
  root((graphmcp 图形设计与绘图 MCP 工具))
    需求与目标
      多格式结构化输入
      六类图形生成
      统一图模型
      多格式导出
      编辑器调起
      版本管理与回溯
      MCP 协议接入
    技术选型
      C++17 零第三方依赖
      CLI 界面
      JSON 文件存储
      header-only 模块化
    核心架构
      解析层 parsers
        Mermaid flowchart/mindmap/er
        Markdown 大纲
        CSV 边表/层级表
        XML 图描述
        Excalidraw JSON
        格式自动识别
      统一图模型 model
        节点 形状/层级/ER属性/坐标
        连线 样式/箭头/标签
        白板原始元素无损保留
      处理层
        规则校验 重复ID/悬空边/层级环
        分层布局 Kahn拓扑
        树布局 脑图/组织图
        分组容器包围盒
      导出层 exporters
        drawio XML
        Mermaid 文本
        Excalidraw JSON
        SVG 原生渲染
        PNG/PDF 外部转换器
        mermaid.live URL
      存储层 storage
        index.json 目录
        latest.json 当前版
        versions/vN 不可变快照
        历史查询与回滚
      接口层
        CLI 九个子命令
        MCP JSON-RPC stdio
        八个 MCP 工具
        外部编辑器调起
    质量保障
      121 断言单元测试
      CLI 端到端冒烟
      MCP 会话脚本测试
      结构校验拒绝坏图
    DevOps
      Git 版本控制
      GitHub Actions
        构建 测试 冒烟
        SonarQube 质量门
        打包制品
      SonarQube 静态分析
```

## 大纲版（graphmcp 原生可解析）

# graphmcp 图形设计与绘图 MCP 工具
## 需求与目标
- 多格式结构化输入
- 六类图形生成
- 统一图模型
- 多格式导出
- 编辑器调起
- 版本管理与回溯
- MCP 协议接入
## 技术选型
- C++17 零第三方依赖
- CLI 界面
- JSON 文件存储
## 核心架构
- 解析层：5 种解析器 + 自动识别
- 统一图模型：节点/连线/层级/白板元素
- 处理层：校验 + 布局
- 导出层：6 种格式 + URL
- 存储层：版本化 JSON
- 接口层：CLI + MCP
## 质量保障
- 单元测试 121 断言
- 端到端冒烟测试
- MCP 会话测试
## DevOps
- Git 版本控制
- GitHub Actions 流水线
- SonarQube 静态分析
