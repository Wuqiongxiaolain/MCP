// exporters.hpp - 统一图模型导出：drawio XML、Mermaid、Excalidraw JSON、
// SVG、浏览器 URL；PNG/PDF 在可用时通过外部转换器生成。
#pragma once
#include "layout.hpp"
#include "model.hpp"
#include <cstdlib>
#include <fstream>
#include <functional>

#ifdef _WIN32
#    include <direct.h>
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <sys/stat.h>
#endif

namespace ge {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

// ------------------------------------------------------------------ 工具函数
// --

// xmlEscape: XML 文本转义，避免标签字符破坏导出结构
inline std::string xmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

// base64Encode: URL/嵌入场景下的二进制安全编码
inline std::string base64Encode(const std::string& in)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t      i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) |
                     ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == in.size()) {
        unsigned v =
            ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// 创建文件路径的所有父目录（尽力而为）
// ensureParentDirs: 按路径逐级创建父目录（best-effort）
inline void ensureParentDirs(const std::string& path)
{
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/' || path[i] == '\\') {
            std::string dir = path.substr(0, i);
            if (dir.size() == 2 && dir[1] == ':')
                continue;  // Windows 盘符
#ifdef _WIN32
            _mkdir(dir.c_str());
#else
            mkdir(dir.c_str(), 0755);
#endif
        }
    }
}

// writeFile/readFile: 统一文件 IO 封装，供导出与存储模块复用
inline bool writeFile(const std::string& path, const std::string& content)
{
    ensureParentDirs(path);
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(content.data(), (std::streamsize)content.size());
    return f.good();
}

inline std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return "";
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

// ---------------------------------------------------------------- Mermaid --

// sanitizeMermaidId: Mermaid 标识符清洗，规避非法字符导致的渲染失败
inline std::string sanitizeMermaidId(const std::string& id)
{
    std::string out;
    for (char c : id)
        out += (isalnum((unsigned char)c) || c == '_') ? c : '_';
    if (out.empty())
        out = "n";
    return out;
}

// toMermaid: 统一模型导出为 Mermaid 文本
// 关键步骤：按图类型分支（mindmap/er/flowchart）-> 输出节点 -> 输出边
inline std::string toMermaid(const Graph& g)
{
    std::ostringstream os;
    if (g.type == "mindmap") {
        os << "mindmap\n";
        std::map<std::string, std::vector<const Node*>> children;
        std::vector<const Node*>                        roots;
        for (auto& n : g.nodes) {
            if (n.parent.empty())
                roots.push_back(&n);
            else
                children[n.parent].push_back(&n);
        }
        std::function<void(const Node*, int)> emit = [&](const Node* n,
                                                         int         depth) {
            os << std::string((size_t)(depth + 1) * 2, ' ');
            if (depth == 0)
                os << "root((" << n->label << "))";
            else
                os << n->label;
            os << "\n";
            for (auto* c : children[n->id])
                emit(c, depth + 1);
        };
        for (auto* r : roots)
            emit(r, 0);
        return os.str();
    }
    if (g.type == "er") {
        os << "erDiagram\n";
        for (auto& e : g.edges) {
            os << "    " << sanitizeMermaidId(e.from) << " ||--o{ "
               << sanitizeMermaidId(e.to) << " : \""
               << (e.label.empty() ? "relates" : e.label) << "\"\n";
        }
        for (auto& n : g.nodes) {
            if (n.attrs.empty())
                continue;
            os << "    " << sanitizeMermaidId(n.id) << " {\n";
            for (auto& a : n.attrs)
                os << "        " << a << "\n";
            os << "    }\n";
        }
        return os.str();
    }
    // flowchart / architecture / orgchart / whiteboard 统一导出为 flowchart TD
    os << "flowchart TD\n";
    std::map<std::string, std::vector<const Node*>> byGroup;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        byGroup[n.parent].push_back(&n);
    }
    auto emitNode = [&](const Node* n, int indent) {
        std::string id    = sanitizeMermaidId(n->id);
        std::string label = n->label.empty() ? n->id : n->label;
        // 处理会影响 mermaid 解析的标签字符
        std::string safe;
        for (char c : label) {
            if (c == '"')
                safe += "#quot;";
            else
                safe += c;
        }
        os << std::string((size_t)indent, ' ') << id;
        if (n->shape == "diamond")
            os << "{\"" << safe << "\"}";
        else if (n->shape == "round")
            os << "(\"" << safe << "\")";
        else if (n->shape == "circle")
            os << "((\"" << safe << "\"))";
        else if (n->shape == "stadium")
            os << "([\"" << safe << "\"])";
        else if (n->shape == "ellipse")
            os << "([\"" << safe << "\"])";
        else
            os << "[\"" << safe << "\"]";
        os << "\n";
    };
    // group 节点导出为 subgraph（单层）
    for (auto& n : g.nodes) {
        if (n.shape != "group")
            continue;
        os << "    subgraph " << sanitizeMermaidId(n.id) << " [\""
           << (n.label.empty() ? n.id : n.label) << "\"]\n";
        for (auto* c : byGroup[n.id])
            emitNode(c, 8);
        os << "    end\n";
    }
    // 未分组节点（以及 parent 不是 group 的节点）
    for (auto& kv : byGroup) {
        const Node* p = kv.first.empty() ? nullptr : g.findNode(kv.first);
        if (p && p->shape == "group")
            continue;
        for (auto* n : kv.second)
            emitNode(n, 4);
    }
    for (auto& e : g.edges) {
        os << "    " << sanitizeMermaidId(e.from);
        if (e.style == "dashed")
            os << " -.->";
        else if (e.style == "thick")
            os << " ==>";
        else if (e.arrow == "none")
            os << " ---";
        else if (e.arrow == "both")
            os << " <-->";
        else
            os << " -->";
        if (!e.label.empty())
            os << "|" << e.label << "|";
        os << " " << sanitizeMermaidId(e.to) << "\n";
    }
    return os.str();
}

// ----------------------------------------------------------------- draw.io --

// drawioStyle: 将统一 shape 映射为 draw.io 样式字符串
inline std::string drawioStyle(const Node& n)
{
    std::string base = "whiteSpace=wrap;html=1;";
    if (n.shape == "diamond")
        return "rhombus;" + base;
    if (n.shape == "ellipse" || n.shape == "circle")
        return "ellipse;" + base;
    if (n.shape == "round" || n.shape == "stadium")
        return "rounded=1;arcSize=" +
               std::string(n.shape == "stadium" ? "50" : "10") + ";" + base;
    if (n.shape == "hexagon")
        return "shape=hexagon;" + base;
    if (n.shape == "group")
        return "rounded=0;whiteSpace=wrap;html=1;verticalAlign=top;fillColor="
               "none;dashed=1;";
    if (!n.attrs.empty())
        return "shape=table;startSize=30;container=1;collapsible=0;" + base;
    return "rounded=0;" + base;
}

// toDrawio: 导出 draw.io XML
// 关键步骤：先 layout 保证有坐标 -> 先输出 group 再输出普通节点 -> 最后输出边
inline std::string toDrawio(Graph g)
{
    gl::layout(g);
    std::ostringstream os;
    os << "<mxfile host=\"graphmcp\" agent=\"graphmcp/1.0\" type=\"device\">\n";
    os << "  <diagram name=\"" << xmlEscape(g.name.empty() ? "Page-1" : g.name)
       << "\" id=\"" << xmlEscape(g.id.empty() ? "d1" : g.id) << "\">\n";
    os << "    <mxGraphModel dx=\"800\" dy=\"600\" grid=\"1\" gridSize=\"10\" "
          "guides=\"1\" tooltips=\"1\" connect=\"1\" arrows=\"1\" fold=\"1\" "
          "page=\"1\" pageScale=\"1\" pageWidth=\"1169\" pageHeight=\"826\" "
          "math=\"0\" shadow=\"0\">\n";
    os << "      <root>\n";
    os << "        <mxCell id=\"0\"/>\n";
    os << "        <mxCell id=\"1\" parent=\"0\"/>\n";
    // 先输出 group，确保子节点可引用其作为 parent
    std::vector<const Node*> ordered;
    for (auto& n : g.nodes)
        if (n.shape == "group")
            ordered.push_back(&n);
    for (auto& n : g.nodes)
        if (n.shape != "group")
            ordered.push_back(&n);
    for (auto* n : ordered) {
        std::string label = n->label;
        if (!n->attrs.empty()) {  // ER 实体：标题 + 属性行（HTML）
            label = "<b>" + n->label + "</b>";
            for (auto& a : n->attrs)
                label += "<br/>" + a;
        }
        const Node* p = n->parent.empty() ? nullptr : g.findNode(n->parent);
        bool        insideGroup = p && p->shape == "group";
        double      x = n->x, y = n->y;
        if (insideGroup) {
            x -= p->x;
            y -= p->y;
        }  // draw.io 子节点使用相对坐标
        os << "        <mxCell id=\"" << xmlEscape(n->id) << "\" value=\""
           << xmlEscape(label) << "\" style=\"" << drawioStyle(*n)
           << "\" vertex=\"1\" parent=\""
           << (insideGroup ? xmlEscape(n->parent) : "1") << "\">\n";
        os << "          <mxGeometry x=\"" << x << "\" y=\"" << y
           << "\" width=\"" << n->w << "\" height=\"" << n->h
           << "\" as=\"geometry\"/>\n";
        os << "        </mxCell>\n";
    }
    int ei = 0;
    for (auto& e : g.edges) {
        std::string style = "edgeStyle=orthogonalEdgeStyle;rounded=1;html=1;";
        if (e.style == "dashed")
            style += "dashed=1;";
        if (e.style == "thick")
            style += "strokeWidth=3;";
        if (e.arrow == "none")
            style += "endArrow=none;";
        if (e.arrow == "both")
            style += "startArrow=classic;";
        os << "        <mxCell id=\"edge" << ++ei << "\" value=\""
           << xmlEscape(e.label) << "\" style=\"" << style
           << "\" edge=\"1\" parent=\"1\" source=\"" << xmlEscape(e.from)
           << "\" target=\"" << xmlEscape(e.to) << "\">\n";
        os << "          <mxGeometry relative=\"1\" as=\"geometry\"/>\n";
        os << "        </mxCell>\n";
    }
    os << "      </root>\n";
    os << "    </mxGraphModel>\n";
    os << "  </diagram>\n";
    os << "</mxfile>\n";
    return os.str();
}

// ------------------------------------------------------------- Excalidraw --

// excalidrawBase: 生成 Excalidraw 元素公共字段模板
inline Json excalidrawBase(const std::string& id,
                           const std::string& type,
                           double             x,
                           double             y,
                           double             w,
                           double             h,
                           int                seed)
{
    Json el = Json::obj();
    el.set("id", id);
    el.set("type", type);
    el.set("x", x);
    el.set("y", y);
    el.set("width", w);
    el.set("height", h);
    el.set("angle", 0);
    el.set("strokeColor", "#1e1e1e");
    el.set("backgroundColor", "transparent");
    el.set("fillStyle", "solid");
    el.set("strokeWidth", 2);
    el.set("strokeStyle", "solid");
    el.set("roughness", 1);
    el.set("opacity", 100);
    el.set("groupIds", Json::arr());
    el.set("frameId", Json());
    el.set("seed", seed);
    el.set("version", 1);
    el.set("versionNonce", seed * 7 + 1);
    el.set("isDeleted", false);
    el.set("boundElements", Json());
    el.set("updated", 1);
    el.set("link", Json());
    el.set("locked", false);
    return el;
}

// toExcalidraw: 导出 Excalidraw JSON
// 关键步骤：whiteboard 直接透传；否则按节点/文本/连线分别生成元素
inline std::string toExcalidraw(Graph g)
{
    Json doc = Json::obj();
    doc.set("type", "excalidraw");
    doc.set("version", 2);
    doc.set("source", "graphmcp");
    Json els = Json::arr();
    if (!g.elements.empty()) {
        // 白板场景：无损透传原始 elements
        for (auto& el : g.elements)
            els.push(el);
    }
    else {
        gl::layout(g);
        int seed = 1000;
        for (auto& n : g.nodes) {
            std::string ty = "rectangle";
            if (n.shape == "ellipse" || n.shape == "circle" ||
                n.shape == "round" || n.shape == "stadium")
                ty = "ellipse";
            if (n.shape == "diamond")
                ty = "diamond";
            Json el = excalidrawBase(n.id, ty, n.x, n.y, n.w, n.h, ++seed);
            if (n.shape == "group") {
                el.set("backgroundColor", "transparent");
                el.set("strokeStyle", "dashed");
            }
            Json bound = Json::arr();
            Json bt    = Json::obj();
            bt.set("id", n.id + "_txt");
            bt.set("type", "text");
            bound.push(bt);
            el.set("boundElements", bound);
            els.push(el);
            // 绑定文本标签
            std::string label = n.label;
            for (auto& a : n.attrs)
                label += "\n" + a;
            Json txt = excalidrawBase(n.id + "_txt", "text", n.x + 10,
                                      n.y + n.h / 2 - 10, n.w - 20, 20, ++seed);
            txt.set("text", label);
            txt.set("originalText", label);
            txt.set("fontSize", 16);
            txt.set("fontFamily", 1);
            txt.set("textAlign", "center");
            txt.set("verticalAlign", n.attrs.empty() ? "middle" : "top");
            txt.set("containerId", n.id);
            txt.set("lineHeight", 1.25);
            els.push(txt);
        }
        for (auto& e : g.edges) {
            const Node* a = g.findNode(e.from);
            const Node* b = g.findNode(e.to);
            if (!a || !b)
                continue;
            double x1 = a->x + a->w / 2, y1 = a->y + a->h;
            double x2 = b->x + b->w / 2, y2 = b->y;
            if (y2 < y1) {
                y1 = a->y + a->h / 2;
                y2 = b->y + b->h / 2;
                x1 = x2 > a->x + a->w / 2 ? a->x + a->w : a->x;
            }
            Json el  = excalidrawBase(e.id, "arrow", x1, y1, x2 - x1, y2 - y1,
                                      5000 + (int)els.size());
            Json pts = Json::arr();
            Json p0  = Json::arr();
            p0.push(0.0);
            p0.push(0.0);
            Json p1 = Json::arr();
            p1.push(x2 - x1);
            p1.push(y2 - y1);
            pts.push(p0);
            pts.push(p1);
            el.set("points", pts);
            el.set("lastCommittedPoint", Json());
            Json sb = Json::obj();
            sb.set("elementId", e.from);
            sb.set("focus", 0);
            sb.set("gap", 1);
            Json eb = Json::obj();
            eb.set("elementId", e.to);
            eb.set("focus", 0);
            eb.set("gap", 1);
            el.set("startBinding", sb);
            el.set("endBinding", eb);
            el.set("startArrowhead",
                   e.arrow == "both" ? Json("arrow") : Json());
            el.set("endArrowhead", e.arrow == "none" ? Json() : Json("arrow"));
            if (e.style == "dashed")
                el.set("strokeStyle", "dashed");
            els.push(el);
        }
    }
    doc.set("elements", els);
    Json app = Json::obj();
    app.set("gridSize", Json());
    app.set("viewBackgroundColor", "#ffffff");
    doc.set("appState", app);
    doc.set("files", Json::obj());
    return doc.dump(2);
}

// -------------------------------------------------------------------- SVG --

// toSVG: 导出可视化 SVG
// 关键步骤：布局 -> 先画边后画节点 -> 根据形状选择图元并绘制标签
inline std::string toSVG(Graph g)
{
    gl::layout(g);
    double maxX = 200, maxY = 150;
    for (auto& n : g.nodes) {
        maxX = std::max(maxX, n.x + n.w);
        maxY = std::max(maxY, n.y + n.h);
    }
    std::ostringstream os;
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
       << (int)(maxX + 40) << "\" height=\"" << (int)(maxY + 40)
       << "\" viewBox=\"0 0 " << (int)(maxX + 40) << " " << (int)(maxY + 40)
       << "\">\n";
    os << "  <defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" "
          "refY=\"5\" "
          "markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\">"
          "<path d=\"M0,0 L10,5 L0,10 z\" fill=\"#333\"/></marker></defs>\n";
    os << "  <style>text{font-family:'Segoe "
          "UI',Arial,sans-serif;font-size:13px;}"
          ".lbl{fill:#222;}.elabel{fill:#555;font-size:11px;}</style>\n";
    // 边先绘制在底层
    for (auto& e : g.edges) {
        const Node* a = g.findNode(e.from);
        const Node* b = g.findNode(e.to);
        if (!a || !b)
            continue;
        double ax = a->x + a->w / 2, ay = a->y + a->h / 2;
        double bx = b->x + b->w / 2, by = b->y + b->h / 2;
        // 将连线端点裁剪到节点边界（近似、轴对齐）
        auto clip = [](double cx, double cy, double w, double h, double tx,
                       double ty, double& ox, double& oy) {
            double dx = tx - cx, dy = ty - cy;
            double sx = dx != 0 ? (w / 2) / std::fabs(dx) : 1e18;
            double sy = dy != 0 ? (h / 2) / std::fabs(dy) : 1e18;
            double s  = std::min(sx, sy);
            s         = std::min(s, 1.0);
            ox        = cx + dx * s;
            oy        = cy + dy * s;
        };
        double x1, y1, x2, y2;
        clip(ax, ay, a->w, a->h, bx, by, x1, y1);
        clip(bx, by, b->w, b->h, ax, ay, x2, y2);
        os << "  <line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2
           << "\" y2=\"" << y2 << "\" stroke=\"#333\" stroke-width=\""
           << (e.style == "thick" ? 3 : 1.5) << "\"";
        if (e.style == "dashed")
            os << " stroke-dasharray=\"6,4\"";
        if (e.arrow != "none")
            os << " marker-end=\"url(#arrow)\"";
        if (e.arrow == "both")
            os << " marker-start=\"url(#arrow)\"";
        os << "/>\n";
        if (!e.label.empty()) {
            os << "  <text class=\"elabel\" x=\"" << (x1 + x2) / 2 << "\" y=\""
               << (y1 + y2) / 2 - 4 << "\" text-anchor=\"middle\">"
               << xmlEscape(e.label) << "</text>\n";
        }
    }
    // 再绘制节点
    for (auto& n : g.nodes) {
        std::string fill   = n.shape == "group" ? "none" : "#eef4ff";
        std::string stroke = "#4a72b8";
        if (n.shape == "group") {
            os << "  <rect x=\"" << n.x << "\" y=\"" << n.y << "\" width=\""
               << n.w << "\" height=\"" << n.h
               << "\" fill=\"none\" stroke=\"#999\" "
                  "stroke-dasharray=\"5,4\" rx=\"6\"/>\n";
            os << "  <text class=\"lbl\" x=\"" << n.x + 8 << "\" y=\""
               << n.y + 18 << "\" fill=\"#777\">" << xmlEscape(n.label)
               << "</text>\n";
            continue;
        }
        double cx = n.x + n.w / 2, cy = n.y + n.h / 2;
        if (n.shape == "diamond") {
            os << "  <polygon points=\"" << cx << "," << n.y << " " << n.x + n.w
               << "," << cy << " " << cx << "," << n.y + n.h << " " << n.x
               << "," << cy << "\" fill=\"#fff7e0\" stroke=\"#c9a227\"/>\n";
        }
        else if (n.shape == "ellipse" || n.shape == "circle" ||
                 n.shape == "round" || n.shape == "stadium") {
            os << "  <ellipse cx=\"" << cx << "\" cy=\"" << cy << "\" rx=\""
               << n.w / 2 << "\" ry=\"" << n.h / 2
               << "\" fill=\"#e8f7ec\" stroke=\"#3d9155\"/>\n";
        }
        else {
            os << "  <rect x=\"" << n.x << "\" y=\"" << n.y << "\" width=\""
               << n.w << "\" height=\"" << n.h << "\" rx=\"4\" fill=\"" << fill
               << "\" stroke=\"" << stroke << "\"/>\n";
        }
        if (n.attrs.empty()) {
            os << "  <text class=\"lbl\" x=\"" << cx << "\" y=\"" << cy + 5
               << "\" text-anchor=\"middle\">" << xmlEscape(n.label)
               << "</text>\n";
        }
        else {  // ER 实体（包含属性行）
            os << "  <text class=\"lbl\" x=\"" << cx << "\" y=\"" << n.y + 20
               << "\" text-anchor=\"middle\" font-weight=\"bold\">"
               << xmlEscape(n.label) << "</text>\n";
            os << "  <line x1=\"" << n.x << "\" y1=\"" << n.y + 28 << "\" x2=\""
               << n.x + n.w << "\" y2=\"" << n.y + 28 << "\" stroke=\""
               << stroke << "\"/>\n";
            double ty = n.y + 46;
            for (auto& a : n.attrs) {
                os << "  <text class=\"lbl\" x=\"" << n.x + 10 << "\" y=\""
                   << ty << "\">" << xmlEscape(a) << "</text>\n";
                ty += 22;
            }
        }
    }
    os << "</svg>\n";
    return os.str();
}

// -------------------------------------------------------------------- URL --

// mermaid.live URL：使用普通 base64 载荷（无需压缩）
// toMermaidLiveUrl: 生成可直接打开的 mermaid.live 编辑链接
inline std::string toMermaidLiveUrl(const Graph& g)
{
    Json payload = Json::obj();
    payload.set("code", toMermaid(g));
    payload.set("mermaid", "{\"theme\":\"default\"}");
    payload.set("autoSync", true);
    payload.set("updateDiagram", true);
    return "https://mermaid.live/edit#base64:" + base64Encode(payload.dump());
}

// ------------------------------------------------------ PNG/PDF 工具导出 --

// 绝对路径（浏览器 file:// URL 需要）
// absPath: 取绝对路径，避免外部工具在不同工作目录下找不到文件
inline std::string absPath(const std::string& p)
{
#ifdef _WIN32
    char buf[1024];
    if (_fullpath(buf, p.c_str(), sizeof(buf)))
        return buf;
#else
    char buf[4096];
    if (realpath(p.c_str(), buf))
        return buf;
#endif
    return p;
}

// 由文件系统路径构建浏览器可用 file:// URL
// fileUrl: 将本地文件路径转换为浏览器可识别的 file:/// URL
inline std::string fileUrl(const std::string& path)
{
    std::string abs = absPath(path);
    for (char& c : abs)
        if (c == '\\')
            c = '/';
    return "file:///" + abs;
}

// 从 SVG 头部读取整型属性（width="..." / height="..."）
// svgDim: 从 SVG 头部读取宽高属性，为截图/PDF输出提供尺寸参数
inline int svgDim(const std::string& svg, const std::string& attr)
{
    size_t p = svg.find(attr + "=\"");
    if (p == std::string::npos)
        return 0;
    p += attr.size() + 2;
    int v = 0;
    while (p < svg.size() && isdigit((unsigned char)svg[p]))
        v = v * 10 + (svg[p++] - '0');
    return v;
}

// 按 PATH 名称或安装路径定位 Chromium 内核浏览器（Chrome/Edge）
// findBrowser: 按候选路径探测可用 Chromium 内核浏览器
inline std::string findBrowser()
{
    std::vector<std::string> cands;
#ifdef _WIN32
    const char* pf   = getenv("ProgramFiles");
    const char* pf86 = getenv("ProgramFiles(x86)");
    const char* lad  = getenv("LOCALAPPDATA");
    auto        add  = [&](const char* base, const char* rel) {
        if (base)
            cands.push_back(std::string(base) + rel);
    };
    add(pf, "\\Google\\Chrome\\Application\\chrome.exe");
    add(pf86, "\\Google\\Chrome\\Application\\chrome.exe");
    add(lad, "\\Google\\Chrome\\Application\\chrome.exe");
    add(pf, "\\Microsoft\\Edge\\Application\\msedge.exe");
    add(pf86, "\\Microsoft\\Edge\\Application\\msedge.exe");
    // 硬编码兜底：某些 shell（MSYS/Git Bash）会隐藏带括号的环境变量名，
    // 例如 ProgramFiles(x86)，导致 getenv 取不到。
    cands.push_back(
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe");
    cands.push_back(
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe");
    cands.push_back(
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe");
    cands.push_back(
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe");
#else
    cands = {"/usr/bin/google-chrome", "/usr/bin/chromium",
             "/usr/bin/chromium-browser", "/usr/bin/microsoft-edge"};
#endif
    for (auto& c : cands) {
        std::ifstream f(c, std::ios::binary);
        if (f.good())
            return c;
    }
    return "";
}

// 静默执行命令。Windows 下通过临时 .bat 规避 cmd.exe 在“带引号可执行路径
// + 重定向”场景下的引号剥离问题（该问题会让 std::system 悄悄失败）。
// runQuiet: 静默执行命令（Windows 下通过 .bat 规避 system 引号问题）
inline int runQuiet(const std::string& cmd, const std::string& tmpBase)
{
#ifdef _WIN32
    std::string bat = tmpBase + ".run.bat";
    writeFile(bat, "@echo off\r\n" + cmd + "\r\n");
    int rc = std::system(("\"" + bat + "\"").c_str());
    std::remove(bat.c_str());
    return rc;
#else
    (void)tmpBase;
    return std::system(cmd.c_str());
#endif
}

#ifdef _WIN32
inline std::wstring widen(const std::string& s)
{
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#endif

// 直接拉起浏览器（不经 shell）。Windows 使用 CreateProcessW，因此不依赖
// cmd.exe / COMSPEC / PATH。MCP 客户端常在精简环境中启动服务端，这点很关键。
// bInheritHandles 设为 FALSE，避免子进程写入 stdout（JSON-RPC 通道）。
// argstr 表示可执行文件之后的完整参数字符串（需提前处理好引号）。
// launchBrowser: 直接拉起浏览器子进程做渲染，避免依赖 shell 环境
inline int launchBrowser(const std::string& exe, const std::string& argstr)
{
#ifdef _WIN32
    std::string          cmdline = "\"" + exe + "\" " + argstr;
    std::wstring         wexe    = widen(exe);
    std::wstring         wcmd    = widen(cmdline);
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessW(wexe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok)
        return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
#else
    return std::system(
        ("\"" + exe + "\" " + argstr + " >/dev/null 2>&1").c_str());
#endif
}

// 尝试外部转换器；成功返回工具名，失败返回空字符串。
// 顺序：inkscape/rsvg/magick（若在 PATH）-> Chromium 浏览器（Chrome/Edge）。
// rasterize: 将 SVG 栅格化/打印为 png 或 pdf
// 关键步骤：优先本地工具链 -> 失败后回退 headless 浏览器 -> 校验输出文件
inline std::string rasterize(const std::string& svgPathIn,
                             const std::string& outPathIn,
                             const std::string& fmt)
{
    // 统一转绝对路径：浏览器会以自身工作目录解析 --print-to-pdf /
    // --screenshot， 若 outPath 是相对路径，可能静默写到错误位置。
    std::string svgPath = absPath(svgPathIn);
    std::string outPath = absPath(outPathIn);
#ifdef _WIN32
    std::string quiet = " >nul 2>nul";
#else
    std::string quiet = " >/dev/null 2>/dev/null";
#endif
    auto produced = [&]() {
        std::ifstream check(outPath, std::ios::binary);
        return check.good() &&
               check.peek() != std::ifstream::traits_type::eof();
    };

    std::vector<std::pair<std::string, std::string>> cands;  // {工具名, 命令}
    cands.push_back({"inkscape", "inkscape \"" + svgPath +
                                     "\" --export-filename=\"" + outPath +
                                     "\"" + quiet});
    cands.push_back({"rsvg-convert", "rsvg-convert -f " + fmt + " -o \"" +
                                         outPath + "\" \"" + svgPath + "\"" +
                                         quiet});
    cands.push_back(
        {"magick", "magick \"" + svgPath + "\" \"" + outPath + "\"" + quiet});
    for (auto& c : cands) {
        std::remove(outPath.c_str());
        if (runQuiet(c.second, outPath) == 0 && produced())
            return c.first;
    }

    // Chromium 兜底：将 SVG 包进紧凑尺寸 HTML，避免输出默认为整页 A4。
    std::string browser = findBrowser();
    if (!browser.empty()) {
        std::string svg = readFile(svgPath);
        int         w = svgDim(svg, "width"), h = svgDim(svg, "height");
        if (w <= 0)
            w = 1200;
        if (h <= 0)
            h = 800;
        std::string html =
            "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
            "@page{size:" +
            std::to_string(w) + "px " + std::to_string(h) +
            "px;margin:0}html,body{margin:0;padding:0;background:#fff}"
            "svg{display:block}</style></head><body>" +
            svg + "</body></html>";
        std::string htmlPath = svgPath + ".wrap.html";
        writeFile(htmlPath, html);
        std::string url = fileUrl(htmlPath);
        // 独立 user-data-dir：没有它时，新 headless
        // 进程可能附着到已运行浏览器， 导致任务被静默跳过。若 TEMP 不可用（精简
        // MCP 环境），回退到输出目录旁。
        const char* tmp = getenv("TEMP");
        if (!tmp)
            tmp = getenv("TMP");
        std::string profile =
            (tmp ? std::string(tmp) + "/graphmcp-chrome-profile" :
                   outPath + ".chromeprofile");
        std::string args = "--headless=new --disable-gpu --no-sandbox "
                           "--user-data-dir=\"" +
                           profile + "\" ";
        if (fmt == "pdf")
            args += "--no-pdf-header-footer --print-to-pdf=\"" + outPath +
                    "\" \"" + url + "\"";
        else
            args += "--force-device-scale-factor=2 --window-size=" +
                    std::to_string(w) + "," + std::to_string(h) +
                    " --screenshot=\"" + outPath + "\" \"" + url + "\"";
        std::remove(outPath.c_str());
        launchBrowser(browser, args);
        std::remove(htmlPath.c_str());
        if (produced()) {
#ifdef _WIN32
            return browser.find("msedge") != std::string::npos ? "edge" :
                                                                 "chrome";
#else
            return "chromium";
#endif
        }
    }
    return "";
}

// --------------------------------------------------------------- 分发入口 --

// ExportResult: 导出结果对象（ok/message/content/path 四元信息）
struct ExportResult
{
    bool        ok = false;
    std::string message;  // 人类可读状态信息
    std::string content;  // 内联内容（文本格式 / URL）
    std::string path;     // 写文件时的输出路径
};

// to 可选：drawio | mermaid | excalidraw | svg | png | pdf | url | model
// exportGraph: 统一导出分发入口
// 关键步骤：按目标格式生成内容 -> 可选写文件 -> 对 png/pdf 提供 SVG 回退
inline ExportResult
exportGraph(Graph g, const std::string& to, const std::string& outPath = "")
{
    ExportResult r;
    std::string  content;
    std::string  defExt = to;
    if (to == "drawio")
        content = toDrawio(g);
    else if (to == "mermaid") {
        content = toMermaid(g);
        defExt  = "mmd";
    }
    else if (to == "excalidraw")
        content = toExcalidraw(g);
    else if (to == "svg")
        content = toSVG(g);
    else if (to == "model" || to == "json") {
        gl::layout(g);
        content = g.toJson().dump(2);
        defExt  = "json";
    }
    else if (to == "url") {
        r.ok      = true;
        r.content = toMermaidLiveUrl(g);
        r.message = "browser URL generated (mermaid.live)";
        return r;
    }
    else if (to == "png" || to == "pdf") {
        std::string svg  = toSVG(g);
        std::string base = outPath.empty() ? ("graph_export." + to) : outPath;
        std::string svgPath = base + ".tmp.svg";
        if (!writeFile(svgPath, svg)) {
            r.message = "cannot write temp svg: " + svgPath;
            return r;
        }
        std::string tool = rasterize(svgPath, base, to);
        std::remove(svgPath.c_str());
        if (tool.empty()) {
            // 平滑兜底：在目标输出旁保留一份 SVG
            std::string fallback = base + ".svg";
            writeFile(fallback, svg);
            r.message =
                "no external converter found (tried inkscape/rsvg-convert/"
                "magick); wrote SVG fallback to " +
                fallback + " - convert it manually or install a converter";
            r.path = fallback;
            return r;
        }
        r.ok      = true;
        r.path    = base;
        r.message = to + " written via " + tool + ": " + base;
        return r;
    }
    else {
        r.message =
            "unknown export format: " + to +
            " (expected drawio|mermaid|excalidraw|svg|png|pdf|url|model)";
        return r;
    }

    if (!outPath.empty()) {
        if (!writeFile(outPath, content)) {
            r.message = "cannot write file: " + outPath;
            return r;
        }
        r.ok      = true;
        r.path    = outPath;
        r.message = to + " written: " + outPath;
    }
    else {
        r.ok      = true;
        r.content = content;
        r.message =
            to + " generated (" + std::to_string(content.size()) + " bytes)";
    }
    return r;
}

// ----------------------------------------------------- 外部编辑器打开 --

// 使用系统默认处理器/浏览器打开文件或 URL
// openExternal: 用系统默认处理器打开 URL 或文件
inline bool openExternal(const std::string& target)
{
#ifdef _WIN32
    std::string cmd = "start \"\" \"" + target + "\"";
#elif __APPLE__
    std::string cmd = "open \"" + target + "\"";
#else
    std::string cmd = "xdg-open \"" + target + "\"";
#endif
    return std::system(cmd.c_str()) == 0;
}

}  // namespace ge
