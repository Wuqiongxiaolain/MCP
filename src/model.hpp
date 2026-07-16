// model.hpp - graphmcp 的统一图模型
// 一个模型同时表示流程图、架构图、ER 图、组织图、思维导图和白板自由场景。
#pragma once
#include "json.hpp"
#include <algorithm>
#include <ctime>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

namespace gm {

using gj::Json;

// 解析器/导出器/布局模块共同理解的图类型
// 可选值："flowchart" | "architecture" | "er" | "orgchart" | "mindmap" |
// "whiteboard"

// Layer: draw.io 图层 —— 同页内节点的垂直分组
// id 为 draw.io mxCell id，name 为图层展示名
struct Layer
{
    std::string id;
    std::string name;
    bool visible = true;
    bool locked  = false;
};

// Node: 图中的"节点"实体（命名上 n 表示 node，id 为机器唯一标识，label
// 为展示名）
struct Node
{
    std::string id;
    std::string label;
    std::string shape;  // 节点形状：rect | round | diamond | ellipse | circle |
                        // stadium | hexagon | group |
                        // process | document | cylinder | parallelogram |
                        // delay | manualInput | display | cloud |
                        // trapezoid | triangle | step | umlActor | note |
                        // cube | message
    std::string              parent;  // 层级关系：父节点 id（空字符串表示根层）
    std::string              layer;   // 所属图层名（空字符串表示默认图层）
    std::string style;  // 遗留/自由样式提示（非颜色；颜色用 fillColor/strokeColor）
    std::string fillColor;    // 填充色 (如 "#eef4ff"；空串用默认)
    std::string strokeColor;  // 描边色 (如 "#4a72b8"；空串用默认)
    std::vector<std::string> attrs;        // ER 属性列表：形如 "type name"
    double                   x = 0, y = 0, w = 0, h = 0;
};

// Edge: 图中的"边/连线"实体（命名上 from/to 表示起点和终点节点 id）
struct Edge
{
    std::string id;
    std::string from, to;
    std::string label;
    std::string style;  // 线型：solid | dashed | dotted | thick

    // 箭头装饰（取代单一 arrow 字段的粗糙分类）
    // 旧 arrow 字段保留为派生/兼容属性，toJson 和现有代码仍可使用
    std::string arrow     = "arrow";  // arrow | none | both (backward compat)
    std::string headStart = "none";   // none | arrow | open | cross
    std::string headEnd   = "arrow";  // none | arrow | open | cross
    std::string strokeColor;          // 描边色 (如 "#333"；空串使用默认)

    // 序列图 / gitGraph 专用
    int  seqNum  = 0;      // 消息序号
    bool isAsync = false;  // 异步消息（->>）

    // 边标签定位
    double labelX = 0;  // 标签 X 偏移（相对边中点）
    double labelY = 0;  // 标签 Y 偏移（相对边中点）
};

// Graph: 统一图模型容器（命名上 g 常用于 Graph 实例）
struct Graph
{
    std::string       id;
    std::string       name;
    std::string       type = "flowchart";
    std::string       rawMermaid;  // 不支持深度解析的 Mermaid 类型原始文本
    std::vector<Node>  nodes;
    std::vector<Edge>  edges;
    std::vector<Layer> layers;   // 图层列表（draw.io 多图层支持）
    std::vector<Graph> pages;   // 多页支持（首行为首页，pages 存储附加页）
    std::vector<Json>  elements;       // 原始白板元素（类似 excalidraw）
    Json files        = Json::obj();  // Excalidraw 顶层 files（image 附件）
    bool laidOut      = false;
    int  edgeCounter_ = 0;  // 自增边 ID 计数器
    int  nodeCounter_ = 0;  // 自增节点 ID 计数器

    // 类型特定的结构化数据（替代 rawMermaid 透传）
    // 每个深度解析的类型在此存入自己的 JSON 子对象，key 为类型名
    // 例如：properties.set("pie", pieData)
    Json properties = Json::obj();

    // 通过节点 id 查找可写节点指针；nid = node id
    Node* findNode(const std::string& nid)
    {
        for (auto& n : nodes)
            if (n.id == nid)
                return &n;
        return nullptr;
    }
    // 通过节点 id 查找只读节点指针；用于 const 场景避免误修改
    const Node* findNode(const std::string& nid) const
    {
        for (auto& n : nodes)
            if (n.id == nid)
                return &n;
        return nullptr;
    }
    // ensureNode: 若节点已存在则复用，不存在则创建，避免重复建点
    // 关键步骤：先查重 -> 再按需要补 label -> 最后创建默认 rect 节点
    Node& ensureNode(const std::string& nid, const std::string& label = "")
    {
        for (auto& n : nodes) {
            if (n.id == nid) {
                if (!label.empty() && (n.label.empty() || n.label == n.id))
                    n.label = label;
                return n;
            }
        }
        Node n;
        n.id    = nid;
        n.label = label.empty() ? nid : label;
        n.shape = "rect";
        nodes.push_back(n);
        return nodes.back();
    }
    // addEdge: 追加一条边，自动生成边 id（e1/e2...）
    // 参数命名：from/to 与节点 id 对齐，label/style 分别控制文案、线型
    // headStart/headEnd 控制两端箭头装饰，默认 from 端无箭头、to 端有箭头
    void addEdge(const std::string& from,
                 const std::string& to,
                 const std::string& label     = "",
                 const std::string& style     = "solid",
                 const std::string& arrow     = "arrow",
                 const std::string& headStart = "none",
                 const std::string& headEnd   = "arrow")
    {
        Edge e;
        e.id        = "e" + std::to_string(++edgeCounter_);
        e.from      = from;
        e.to        = to;
        e.label     = label;
        e.style     = style;
        e.arrow     = arrow;
        e.headStart = headStart;
        e.headEnd   = headEnd;
        edges.push_back(e);
    }

    // ---- 统一模型的 JSON 序列化/反序列化 ----
    // toJson: 将 Graph 序列化为 JSON，便于持久化和跨模块传输
    // 关键步骤：按 nodes/edges/elements 三块组织结构，避免信息丢失
    Json toJson() const
    {
        Json j = Json::obj();
        j.set("id", id);
        j.set("name", name);
        j.set("type", type);
        if (!rawMermaid.empty())
            j.set("rawMermaid", rawMermaid);
        j.set("laidOut", laidOut);
        Json ns = Json::arr();
        for (auto& n : nodes) {
            Json jn = Json::obj();
            jn.set("id", n.id);
            jn.set("label", n.label);
            jn.set("shape", n.shape);
            if (!n.parent.empty())
                jn.set("parent", n.parent);
            if (!n.layer.empty())
                jn.set("layer", n.layer);
            if (!n.style.empty())
                jn.set("style", n.style);
            if (!n.fillColor.empty())
                jn.set("fillColor", n.fillColor);
            if (!n.strokeColor.empty())
                jn.set("strokeColor", n.strokeColor);
            if (!n.attrs.empty()) {
                Json ja = Json::arr();
                for (auto& a : n.attrs)
                    ja.push(Json(a));
                jn.set("attrs", ja);
            }
            jn.set("x", n.x);
            jn.set("y", n.y);
            jn.set("w", n.w);
            jn.set("h", n.h);
            ns.push(jn);
        }
        j.set("nodes", ns);
        Json es = Json::arr();
        for (auto& e : edges) {
            Json je = Json::obj();
            je.set("id", e.id);
            je.set("from", e.from);
            je.set("to", e.to);
            if (!e.label.empty())
                je.set("label", e.label);
            je.set("style", e.style);
            je.set("arrow", e.arrow);
            // 扩展箭头信息：仅当与默认值不同时才序列化
            if (e.headStart != "none")
                je.set("headStart", e.headStart);
            if (e.headEnd != "arrow")
                je.set("headEnd", e.headEnd);
            if (e.seqNum != 0)
                je.set("seqNum", (double)e.seqNum);
            if (e.isAsync)
                je.set("isAsync", true);
            if (!e.strokeColor.empty())
                je.set("strokeColor", e.strokeColor);
            if (e.labelX != 0 || e.labelY != 0) {
                je.set("labelX", e.labelX);
                je.set("labelY", e.labelY);
            }
            es.push(je);
        }
        j.set("edges", es);
        if (!layers.empty()) {
            Json ls = Json::arr();
            for (auto& l : layers) {
                Json jl = Json::obj();
                jl.set("id", l.id);
                jl.set("name", l.name);
                jl.set("visible", l.visible);
                jl.set("locked", l.locked);
                ls.push(jl);
            }
            j.set("layers", ls);
        }
        if (!pages.empty()) {
            Json ps = Json::arr();
            for (auto& p : pages)
                ps.push(p.toJson());
            j.set("pages", ps);
        }
        if (!elements.empty()) {
            Json els = Json::arr();
            for (auto& el : elements)
                els.push(el);
            j.set("elements", els);
        }
        if (files.isObj())
            j.set("files", files);
        // 类型特定的结构化数据（替代 rawMermaid 透传）
        if (properties.isObj() && properties.o && !properties.o->empty())
            j.set("properties", properties);
        j.set("edgeCounter", (double)edgeCounter_);
        j.set("nodeCounter", (double)nodeCounter_);
        return j;
    }

    // fromJson: 从 JSON 还原 Graph；与 toJson 成对，保证双向转换
    // 关键步骤：先读图级字段 -> 再读 nodes/edges -> 最后恢复 whiteboard
    // elements
    static Graph fromJson(const Json& j)
    {
        Graph g;
        g.id           = j.str("id");
        g.name         = j.str("name");
        g.type         = j.str("type", "flowchart");
        g.rawMermaid   = j.str("rawMermaid");
        g.laidOut      = j.boolean("laidOut", false);
        g.edgeCounter_ = (int)j.num("edgeCounter", 0);
        g.nodeCounter_ = (int)j.num("nodeCounter", 0);
        if (const Json* ns = j.find("nodes")) {
            if (ns->isArr())
                for (auto& jn : *ns->a) {
                    Node n;
                    n.id          = jn.str("id");
                    n.label       = jn.str("label");
                    n.shape       = jn.str("shape", "rect");
                    n.parent      = jn.str("parent");
                    n.layer       = jn.str("layer");
                    n.style       = jn.str("style");
                    n.fillColor   = jn.str("fillColor");
                    n.strokeColor = jn.str("strokeColor");
                    if (const Json* ja = jn.find("attrs")) {
                        if (ja->isArr())
                            for (auto& a : *ja->a)
                                if (a.isStr())
                                    n.attrs.push_back(a.s);
                    }
                    n.x = jn.num("x");
                    n.y = jn.num("y");
                    n.w = jn.num("w");
                    n.h = jn.num("h");
                    g.nodes.push_back(n);
                }
        }
        if (const Json* es = j.find("edges")) {
            if (es->isArr())
                for (auto& je : *es->a) {
                    Edge e;
                    e.id    = je.str("id");
                    e.from  = je.str("from");
                    e.to    = je.str("to");
                    e.label = je.str("label");
                    e.style = je.str("style", "solid");
                    // 读 headStart/headEnd（优先），回退到旧 arrow 字段推导
                    e.headStart = je.str("headStart", "");
                    e.headEnd   = je.str("headEnd", "");
                    if (e.headStart.empty() && e.headEnd.empty()) {
                        // 仅含旧 arrow 字段，从 arrow 推导箭头
                        std::string ar = je.str("arrow", "arrow");
                        e.headStart = (ar == "both") ? "arrow" : "none";
                        e.headEnd   = (ar == "none") ? "none" : "arrow";
                        e.arrow     = ar;
                    } else {
                        // 已有 headStart/headEnd，从中计算兼容 arrow 字段
                        if (e.headStart == "none" && e.headEnd == "none")
                            e.arrow = "none";
                        else if (e.headStart != "none" && e.headEnd != "none")
                            e.arrow = "both";
                        else
                            e.arrow = "arrow";
                    }
                    e.seqNum      = (int)je.num("seqNum", 0);
                    e.isAsync     = je.boolean("isAsync", false);
                    e.strokeColor = je.str("strokeColor");
                    e.labelX      = je.num("labelX");
                    e.labelY      = je.num("labelY");
                    g.edges.push_back(e);
                }
        }
        if (const Json* ls = j.find("layers")) {
            if (ls->isArr())
                for (auto& jl : *ls->a) {
                    Layer l;
                    l.id      = jl.str("id");
                    l.name    = jl.str("name");
                    l.visible = jl.boolean("visible", true);
                    l.locked  = jl.boolean("locked", false);
                    g.layers.push_back(l);
                }
        }
        if (const Json* ps = j.find("pages")) {
            if (ps->isArr())
                for (auto& jp : *ps->a)
                    g.pages.push_back(Graph::fromJson(jp));
        }
        if (const Json* els = j.find("elements")) {
            if (els->isArr())
                for (auto& el : *els->a)
                    g.elements.push_back(el);
        }
        if (const Json* fs = j.find("files")) {
            if (fs->isObj())
                g.files = *fs;
        }
        // 类型特定的结构化数据
        if (const Json* p = j.find("properties")) {
            if (p->isObj())
                g.properties = *p;
        }
        return g;
    }
};

// stripUtf8Bom: 去掉文本开头的 UTF-8 BOM（EF BB BF），避免关键字识别失败
inline std::string stripUtf8Bom(std::string s)
{
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        return s.substr(3);
    return s;
}

// ---- 小型通用工具函数 ----

// trim: 去掉字符串首尾空白；命名直观表示"裁剪空白"
inline std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// splitLines: 按行拆分文本（兼容 \r\n），供解析器逐行处理
inline std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string              cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        }
        else if (c != '\r')
            cur += c;
    }
    if (!cur.empty())
        lines.push_back(cur);
    return lines;
}

// toLower: 统一转小写，便于做大小写不敏感匹配
inline std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return s;
}

// startsWith: 前缀判断，解析协议/关键字时常用
inline bool startsWith(const std::string& s, const std::string& p)
{ return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

// 生成简短且基本唯一的 id：g<epoch36><rand>
// genId: 生成短 id（prefix + 时间戳36进制 + 随机尾缀）
// 这样比纯随机更可读，也降低冲突概率
inline std::string genId(const std::string& prefix = "g")
{
    static const char* al = "0123456789abcdefghijklmnopqrstuvwxyz";
    // 进程内随机引擎（跨进程靠 pid 区分，避免同秒碰撞）
    static std::mt19937                       rng([]() {
        std::random_device rd;
        unsigned           seed = (unsigned)time(nullptr);
        for (int i = 0; i < 4; i++)
            seed ^= (unsigned)rd() << (i * 8);
#ifdef _WIN32
        seed ^= (unsigned)_getpid();
#else
        seed ^= (unsigned)getpid();
#endif
        return seed;
    }());
    static std::uniform_int_distribution<int> dist(0, 35);
    unsigned long long v  = (unsigned long long)time(nullptr);
    std::string        s;
    while (v) {
        s += al[v % 36];
        v /= 36;
    }
    std::reverse(s.begin(), s.end());
#ifdef _WIN32
    unsigned long pid = (unsigned long)_getpid();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    // 编码 pid（缩短）+ 更多随机尾缀，降低多进程同秒碰撞
    while (pid) {
        s += al[pid % 36];
        pid /= 36;
    }
    for (int i = 0; i < 4; i++)
        s += al[dist(rng)];
    return prefix + s;
}

// 根据 label 长度估算默认节点尺寸（近似 UTF-8：按码点数统计）
// defaultSize: 根据文本长度估算节点尺寸（UTF-8 场景下按码点近似）
// 关键步骤：估算字符宽度 -> 计算基础宽高 -> 针对形状/ER 属性做修正
inline void defaultSize(Node& n)
{
    size_t cps = 0;
    for (unsigned char c : n.label)
        if ((c & 0xC0) != 0x80)
            cps++;
    double wide = 0;
    for (unsigned char c : n.label)
        if (c >= 0x80) {
            wide = 1;
            break;
        }
    double perChar = wide ? 14.0 : 8.5;
    n.w            = std::max(100.0, cps * perChar + 32.0);
    n.h = 44.0;
    if (n.shape == "diamond")
        n.h = 60.0;
    else if (n.shape == "cylinder")
        n.h = 60.0;
    else if (n.shape == "umlActor")
        n.h = 80.0;
    else if (n.shape == "note")
        n.h = 50.0;
    if (n.shape == "circle") {
        n.w = std::max(n.w, n.h);
        n.h = n.w;
    }
    if (!n.attrs.empty())  // ER 实体：为属性行预留高度
        n.h = 30.0 + 22.0 * (double)n.attrs.size();
}

}  // namespace gm
