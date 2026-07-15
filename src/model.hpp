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

namespace gm {

using gj::Json;

// 解析器/导出器/布局模块共同理解的图类型
// 可选值："flowchart" | "architecture" | "er" | "orgchart" | "mindmap" |
// "whiteboard"

// Node: 图中的"节点"实体（命名上 n 表示 node，id 为机器唯一标识，label
// 为展示名）
struct Node
{
    std::string id;
    std::string label;
    std::string shape;  // 节点形状：rect | round | diamond | ellipse | circle |
                        // stadium | group
    std::string              parent;  // 层级关系：父节点 id（空字符串表示根层）
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

    // 边路由路径点：布局阶段填充（虚拟节点在各中间层的坐标），导出阶段用于折线路由
    // 空 vector 表示未设置，导出器自行计算兜底路由
    std::vector<std::pair<double, double>> waypoints;

    // 边标签在画布上的绝对位置：布局阶段从 waypoints 中选出最佳直段计算，
    // 导出阶段供 draw.io 等格式定位标签偏移量
    double labelX = 0, labelY = 0;
};

// Graph: 统一图模型容器（命名上 g 常用于 Graph 实例）
struct Graph
{
    std::string       id;
    std::string       name;
    std::string       type = "flowchart";
    std::string       rawMermaid;  // 不支持深度解析的 Mermaid 类型原始文本
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Json> elements;       // 原始白板元素（类似 excalidraw）
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
            // 扩展箭头信息：仅当与默认值不同时才序列化，减少 JSON 冗余
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
            if (!e.waypoints.empty()) {
                Json wpArr = Json::arr();
                for (auto& wp : e.waypoints) {
                    Json wpObj = Json::obj();
                    wpObj.set("x", wp.first);
                    wpObj.set("y", wp.second);
                    wpArr.push(wpObj);
                }
                je.set("waypoints", wpArr);
            }
            if (e.labelX != 0 || e.labelY != 0) {
                je.set("labelX", e.labelX);
                je.set("labelY", e.labelY);
            }
            es.push(je);
        }
        j.set("edges", es);
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
                    e.arrow = je.str("arrow", "arrow");
                    // 扩展箭头信息：读新字段，若不存在则从 arrow 推导
                    e.headStart = je.str(
                        "headStart", (e.arrow == "both") ? "arrow" : "none");
                    e.headEnd = je.str("headEnd",
                                       (e.arrow == "none") ? "none" : "arrow");
                    e.seqNum      = (int)je.num("seqNum", 0);
                    e.isAsync     = je.boolean("isAsync", false);
                    e.strokeColor = je.str("strokeColor");
                    if (const Json* wp = je.find("waypoints")) {
                        if (wp->isArr())
                            for (auto& wpj : *wp->a)
                                e.waypoints.push_back(
                                    {wpj.num("x", 0.0), wpj.num("y", 0.0)});
                    }
                    e.labelX = je.num("labelX", 0.0);
                    e.labelY = je.num("labelY", 0.0);
                    g.edges.push_back(e);
                }
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

// stripUtf8Bom: 去掉文本开头的 UTF-8 BOM（EF BB BF），避免关键字识别失败
inline std::string stripUtf8Bom(std::string s)
{
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF &&
        (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    return s;
}

// splitLines: 按行拆分文本（兼容 \r\n），供解析器逐行处理
inline std::vector<std::string> splitLines(const std::string& text)
{
    // 关键步骤：先剥 BOM -> 再按换行切分
    std::string              src = stripUtf8Bom(text);
    std::vector<std::string> lines;
    std::string              cur;
    for (char c : src) {
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
    // 线程安全的随机引擎，以 time + rd 混合播种
    static std::mt19937                       rng([]() {
        std::random_device rd;
        unsigned           seed = (unsigned)time(nullptr);
        for (int i = 0; i < 4; i++)
            seed ^= (unsigned)rd() << (i * 8);
        return seed;
    }());
    static std::uniform_int_distribution<int> dist(0, 35);
    unsigned long long v = (unsigned long long)time(nullptr);
    std::string        s;
    while (v) {
        s += al[v % 36];
        v /= 36;
    }
    std::reverse(s.begin(), s.end());
    s += al[dist(rng)];
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
    n.h            = (n.shape == "diamond") ? 60.0 : 44.0;
    if (n.shape == "circle") {
        n.w = std::max(n.w, n.h);
        n.h = n.w;
    }
    if (!n.attrs.empty())  // ER 实体：为属性行预留高度
        n.h = 30.0 + 22.0 * (double)n.attrs.size();
}

}  // namespace gm
