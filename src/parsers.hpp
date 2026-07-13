// parsers.hpp - 结构化输入解析器 -> 统一图模型
// 支持输入：Mermaid（flowchart / mindmap / erDiagram）、Markdown 大纲、
// CSV（边列表或层级）、简化 XML、Excalidraw JSON。
#pragma once
#include "csv_util.hpp"
#include "model.hpp"
#include <cctype>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace gp {

using gj::Json;
using gm::Graph;
using gm::Node;
using gm::splitLines;
using gm::startsWith;
using gm::toLower;
using gm::trim;

// ParseError: 统一解析异常类型，便于上层区分“输入格式错误”与其他异常
struct ParseError : std::runtime_error
{
    explicit ParseError(const std::string& m) : std::runtime_error(m) {}
};

// ---------------------------------------------------------------- Mermaid --

namespace detail {

    // isIdentChar: Mermaid 标识符字符判断（支持下划线和 UTF-8 非 ASCII 字节）
    inline bool isIdentChar(char c)
    {
        return isalnum((unsigned char)c) || c == '_' ||
               (unsigned char)c >= 0x80;
    }

    // 读取节点引用 `A`，并支持可选形状括号：[..] (..) ((..)) {..} ([..])
    // readNodeRef: 从当前游标读取一个节点引用（id + 可选形状包装标签）
    // 参数命名：id=节点标识，label=展示文本，shape=图形类型
    // 关键步骤：跳空白 -> 读 id -> 识别括号语法 -> 回填 label/shape
    inline bool readNodeRef(const std::string& s,
                            size_t&            pos,
                            std::string&       id,
                            std::string&       label,
                            std::string&       shape)
    {
        while (pos < s.size() && isspace((unsigned char)s[pos]))
            pos++;
        size_t start = pos;
        while (pos < s.size() && isIdentChar(s[pos]))
            pos++;
        if (pos == start)
            return false;
        id = s.substr(start, pos - start);
        label.clear();
        shape.clear();
        if (pos >= s.size())
            return true;
        char open = s[pos];
        auto grab = [&](const std::string& openTok, const std::string& closeTok,
                        const std::string& shp) -> bool {
            if (s.compare(pos, openTok.size(), openTok) != 0)
                return false;
            size_t st  = pos + openTok.size();
            size_t end = s.find(closeTok, st);
            if (end == std::string::npos)
                return false;
            label = trim(s.substr(st, end - st));
            // 去掉包裹在两端的引号
            if (label.size() >= 2 && label.front() == '"' &&
                label.back() == '"')
                label = label.substr(1, label.size() - 2);
            shape = shp;
            pos   = end + closeTok.size();
            return true;
        };
        if (open == '(') {
            if (grab("((", "))", "circle"))
                return true;
            if (grab("([", "])", "stadium"))
                return true;
            if (grab("(", ")", "round"))
                return true;
        }
        else if (open == '[') {
            if (grab("[[", "]]", "rect"))
                return true;
            if (grab("[", "]", "rect"))
                return true;
        }
        else if (open == '{') {
            if (grab("{{", "}}", "hexagon"))
                return true;
            if (grab("{", "}", "diamond"))
                return true;
        }
        return true;
    }

    // 读取连线 token；成功时返回 style/arrow，失败返回空
    // readArrow: 读取 Mermaid 连线 token，并映射到统一 style/arrow 语义
    inline bool readArrow(const std::string& s,
                          size_t&            pos,
                          std::string&       style,
                          std::string&       arrow)
    {
        while (pos < s.size() && isspace((unsigned char)s[pos]))
            pos++;
        static const struct
        {
            const char* tok;
            const char* style;
            const char* arr;
        } arrows[] = {
            {"-.->", "dashed", "arrow"}, {"-.-", "dashed", "none"},
            {"==>", "thick", "arrow"},   {"===", "thick", "none"},
            {"<-->", "solid", "both"},   {"-->", "solid", "arrow"},
            {"---", "solid", "none"},    {"--", "solid", "none"},
        };
        for (auto& a : arrows) {
            size_t len = strlen(a.tok);
            if (s.compare(pos, len, a.tok) == 0) {
                pos += len;
                style = a.style;
                arrow = a.arr;
                return true;
            }
        }
        return false;
    }

}  // namespace detail

// parseMermaidFlowchart: 解析 flowchart 子语法
// 关键步骤：处理 subgraph 层级 -> 解析节点链路 -> 生成边并绑定分组父节点
inline Graph parseMermaidFlowchart(const std::vector<std::string>& lines,
                                   size_t                          first)
{
    Graph g;
    g.type = "flowchart";
    std::vector<std::string> groupStack;  // subgraph 嵌套栈
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        if (startsWith(line, "classDef") || startsWith(line, "class ") ||
            startsWith(line, "style ") || startsWith(line, "linkStyle"))
            continue;
        if (startsWith(line, "subgraph")) {
            std::string rest = trim(line.substr(8));
            std::string gid = rest, glabel = rest;
            size_t      br = rest.find('[');
            if (br != std::string::npos) {
                gid          = trim(rest.substr(0, br));
                size_t close = rest.find(']', br);
                glabel = trim(rest.substr(br + 1, close == std::string::npos ?
                                                      std::string::npos :
                                                      close - br - 1));
            }
            if (gid.empty())
                gid = "sg" + std::to_string(g.nodes.size());
            Node& n = g.ensureNode(gid, glabel);
            n.shape = "group";
            if (!groupStack.empty())
                n.parent = groupStack.back();
            groupStack.push_back(gid);
            continue;
        }
        if (line == "end") {
            if (!groupStack.empty())
                groupStack.pop_back();
            continue;
        }
        // 解析模式：node (arrow |label| node)*
        size_t      pos = 0;
        std::string id, label, shape;
        if (!detail::readNodeRef(line, pos, id, label, shape))
            continue;
        {
            Node& n = g.ensureNode(id, label);
            if (!shape.empty())
                n.shape = shape;
            if (n.parent.empty() && !groupStack.empty())
                n.parent = groupStack.back();
        }
        std::string prev = id;
        while (true) {
            std::string style, arrow;
            if (!detail::readArrow(line, pos, style, arrow))
                break;
            // 可选边标签 |label|
            while (pos < line.size() && isspace((unsigned char)line[pos]))
                pos++;
            std::string elabel;
            if (pos < line.size() && line[pos] == '|') {
                size_t end = line.find('|', pos + 1);
                if (end != std::string::npos) {
                    elabel = trim(line.substr(pos + 1, end - pos - 1));
                    pos    = end + 1;
                }
            }
            std::string id2, label2, shape2;
            if (!detail::readNodeRef(line, pos, id2, label2, shape2))
                break;
            Node& n2 = g.ensureNode(id2, label2);
            if (!shape2.empty())
                n2.shape = shape2;
            if (n2.parent.empty() && !groupStack.empty())
                n2.parent = groupStack.back();
            g.addEdge(prev, id2, elabel, style, arrow);
            prev = id2;
        }
    }
    return g;
}

// parseMermaidMindmap: 解析 mindmap 缩进结构
// 关键步骤：缩进决定父子关系 -> 生成树边（无箭头）-> 根节点使用 circle 形状
inline Graph parseMermaidMindmap(const std::vector<std::string>& lines,
                                 size_t                          first)
{
    Graph g;
    g.type = "mindmap";
    std::vector<std::pair<int, std::string>> stack;  // (缩进, nodeId)
    int                                      seq = 0;
    for (size_t li = first; li < lines.size(); li++) {
        const std::string& raw = lines[li];
        std::string        t   = trim(raw);
        if (t.empty() || startsWith(t, "%%"))
            continue;
        int indent = 0;
        for (char c : raw) {
            if (c == ' ')
                indent++;
            else if (c == '\t')
                indent += 4;
            else
                break;
        }
        // 去掉 mermaid mindmap 的包裹标记 root((x)) / (x) / [x]
        std::string label = t;
        auto stripWrap    = [&](const std::string& o, const std::string& c) {
            size_t op = label.find(o);
            if (op != std::string::npos && label.size() > op + o.size() &&
                label.rfind(c) == label.size() - c.size()) {
                std::string inner = label.substr(
                    op + o.size(), label.size() - c.size() - op - o.size());
                label = trim(inner);
                return true;
            }
            return false;
        };
        if (!stripWrap("((", "))"))
            if (!stripWrap("([", "])"))
                if (!stripWrap("[", "]"))
                    stripWrap("(", ")");
        std::string nid = "m" + std::to_string(++seq);
        Node&       n   = g.ensureNode(nid, label);
        n.shape         = stack.empty() ? "circle" : "round";
        while (!stack.empty() && stack.back().first >= indent)
            stack.pop_back();
        if (!stack.empty()) {
            n.parent = stack.back().second;
            g.addEdge(stack.back().second, nid, "", "solid", "none");
        }
        stack.emplace_back(indent, nid);
    }
    return g;
}

// parseMermaidER: 解析 ER 图（实体块 + 关系语句）
// 关键步骤：读取实体属性块 -> 解析 A -- B 关系 -> 写入 attrs 与 edges
inline Graph parseMermaidER(const std::vector<std::string>& lines, size_t first)
{
    Graph g;
    g.type = "er";
    std::string curEntity;
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        if (!curEntity.empty()) {
            if (line == "}") {
                curEntity.clear();
                continue;
            }
            g.ensureNode(curEntity).attrs.push_back(line);
            continue;
        }
        // 实体块起始：NAME {
        if (line.size() > 1 && line.back() == '{') {
            curEntity = trim(line.substr(0, line.size() - 1));
            Node& n   = g.ensureNode(curEntity);
            n.shape   = "rect";
            continue;
        }
        // 关系语句：A ||--o{ B : label
        size_t dd    = line.find("--");
        size_t colon = line.find(':');
        if (dd != std::string::npos) {
            size_t ls = dd;
            while (ls > 0 && !isspace((unsigned char)line[ls - 1]))
                ls--;
            std::string left = trim(line.substr(0, ls));
            size_t      re   = dd + 2;
            while (re < line.size() && !isspace((unsigned char)line[re]))
                re++;
            std::string rest = trim(line.substr(re, colon == std::string::npos ?
                                                        std::string::npos :
                                                        colon - re));
            std::string label =
                colon == std::string::npos ? "" : trim(line.substr(colon + 1));
            if (label.size() >= 2 && label.front() == '"' &&
                label.back() == '"')
                label = label.substr(1, label.size() - 2);
            if (!left.empty() && !rest.empty()) {
                g.ensureNode(left).shape = "rect";
                g.ensureNode(rest).shape = "rect";
                g.addEdge(left, rest, label, "solid", "none");
            }
        }
    }
    return g;
}

// parseMermaid: Mermaid 分发入口，根据首个有效指令选择具体子解析器
inline Graph parseMermaid(const std::string& text)
{
    auto lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = toLower(trim(lines[i]));
        if (t.empty() || startsWith(t, "%%"))
            continue;
        if (startsWith(t, "graph") || startsWith(t, "flowchart"))
            return parseMermaidFlowchart(lines, i + 1);
        if (startsWith(t, "mindmap"))
            return parseMermaidMindmap(lines, i + 1);
        if (startsWith(t, "erdiagram"))
            return parseMermaidER(lines, i + 1);
        throw ParseError("unsupported mermaid diagram type: " +
                         t.substr(0, 20));
    }
    throw ParseError("empty mermaid input");
}

// --------------------------------------------------------- Markdown 大纲 --

// parseMarkdownOutline: 将 Markdown 标题/列表解析为层级图（mindmap）
// 关键步骤：识别层级 -> 建立 parent 关系 -> 同步补一条树边
inline Graph parseMarkdownOutline(const std::string& text)
{
    Graph g;
    g.type = "mindmap";
    std::vector<std::pair<int, std::string>> stack;  // (层级, nodeId)
    int                                      seq              = 0;
    int                                      lastHeadingLevel = 0;
    for (auto& raw : splitLines(text)) {
        std::string t = trim(raw);
        if (t.empty())
            continue;
        int         level;
        std::string label;
        if (t[0] == '#') {
            size_t h = 0;
            while (h < t.size() && t[h] == '#')
                h++;
            level            = (int)h;
            label            = trim(t.substr(h));
            lastHeadingLevel = level;
        }
        else if (t[0] == '-' || t[0] == '*' || t[0] == '+') {
            int indent = 0;
            for (char c : raw) {
                if (c == ' ')
                    indent++;
                else if (c == '\t')
                    indent += 4;
                else
                    break;
            }
            level = lastHeadingLevel + 1 + indent / 2;
            label = trim(t.substr(1));
        }
        else {
            continue;  // 在大纲模式下忽略普通段落文本
        }
        if (label.empty())
            continue;
        std::string nid = "m" + std::to_string(++seq);
        Node&       n   = g.ensureNode(nid, label);
        while (!stack.empty() && stack.back().first >= level)
            stack.pop_back();
        if (!stack.empty()) {
            n.parent = stack.back().second;
            g.addEdge(stack.back().second, nid, "", "solid", "none");
            n.shape = "round";
        }
        else {
            n.shape = "circle";
        }
        stack.emplace_back(level, nid);
    }
    if (g.nodes.empty())
        throw ParseError("no outline items found in markdown");
    return g;
}

// ------------------------------------------------------------------- CSV --

using gcsv::splitCsvLine;

// 两种 CSV 模式：
//   边列表：from,to[,label]        -> flowchart
//   层级表：id,label[,parent]      -> orgchart
// parseCSV: 解析 CSV（边列表 或 层级表两种模式）
// 关键步骤：先读表头判模式 -> 按列映射取值 -> 逐行建点建边
inline Graph parseCSV(const std::string& text)
{
    auto lines = splitLines(text);
    if (lines.empty())
        throw ParseError("empty csv input");
    auto                       header = splitCsvLine(lines[0]);
    std::map<std::string, int> col;
    for (size_t i = 0; i < header.size(); i++)
        col[toLower(header[i])] = (int)i;
    auto has = [&](const std::string& k) { return col.count(k) > 0; };

    Graph g;
    bool  edgeList =
        (has("from") && has("to")) || (has("source") && has("target"));
    bool tree = has("id") && (has("parent") || has("label"));
    if (!edgeList && !tree)
        throw ParseError(
            "csv header must contain from,to[,label] or id,label[,parent]");

    int cFrom =
        has("from") ? col["from"] : (has("source") ? col["source"] : -1);
    int cTo  = has("to") ? col["to"] : (has("target") ? col["target"] : -1);
    int cLbl = has("label") ? col["label"] : -1;
    int cId  = has("id") ? col["id"] : -1;
    int cPar = has("parent") ? col["parent"] : -1;

    g.type = edgeList ? "flowchart" : "orgchart";
    for (size_t li = 1; li < lines.size(); li++) {
        if (trim(lines[li]).empty())
            continue;
        auto f    = splitCsvLine(lines[li]);
        auto cell = [&](int c) -> std::string {
            return (c >= 0 && c < (int)f.size()) ? f[(size_t)c] : "";
        };
        if (edgeList) {
            std::string from = cell(cFrom), to = cell(cTo);
            if (from.empty() || to.empty())
                continue;
            g.ensureNode(from);
            g.ensureNode(to);
            g.addEdge(from, to, cell(cLbl));
        }
        else {
            std::string id = cell(cId);
            if (id.empty())
                continue;
            Node&       n      = g.ensureNode(id, cell(cLbl));
            std::string parent = cell(cPar);
            if (!parent.empty()) {
                n.parent = parent;
                g.ensureNode(parent);
                g.addEdge(parent, id, "", "solid", "none");
            }
        }
    }
    return g;
}

// ------------------------------------------------------------------- XML --

namespace detail {

    // XmlNode: 轻量 XML 中间结构（tag/attrs/children/text）
    // attr_order: 属性首次出现顺序（与 attrs 并行；map 本身无序）
    struct XmlNode
    {
        std::string                        tag;
        std::map<std::string, std::string> attrs;
        std::vector<std::string>           attr_order;
        std::vector<XmlNode>               children;
        std::string                        text;
    };

    // 最小 XML 解析器：支持元素、属性、文本；跳过注释/声明/cdata
    // parseXmlDoc: 最小可用 XML 解析器（面向本项目输入子集）
    // 关键步骤：跳过声明/注释 -> 递归解析元素 -> 解码实体字符
    inline XmlNode parseXmlDoc(const std::string& src)
    {
        size_t pos      = 0;
        auto   skipMisc = [&]() {
            while (pos < src.size()) {
                while (pos < src.size() && isspace((unsigned char)src[pos]))
                    pos++;
                if (src.compare(pos, 4, "<!--") == 0) {
                    size_t e = src.find("-->", pos);
                    pos      = (e == std::string::npos) ? src.size() : e + 3;
                }
                else if (src.compare(pos, 2, "<?") == 0) {
                    size_t e = src.find("?>", pos);
                    pos      = (e == std::string::npos) ? src.size() : e + 2;
                }
                else if (src.compare(pos, 2, "<!") == 0) {
                    size_t e = src.find(">", pos);
                    pos      = (e == std::string::npos) ? src.size() : e + 1;
                }
                else
                    break;
            }
        };
        auto decodeEntities = [](std::string s) {
            struct
            {
                const char* e;
                const char* r;
            } ents[] = {
                {"&lt;", "<"},    {"&gt;", ">"},   {"&amp;", "&"},
                {"&quot;", "\""}, {"&apos;", "'"},
            };
            for (auto& en : ents) {
                size_t p = 0;
                while ((p = s.find(en.e, p)) != std::string::npos) {
                    s.replace(p, strlen(en.e), en.r);
                    p += strlen(en.r);
                }
            }
            return s;
        };
        std::function<XmlNode()> parseElem = [&]() -> XmlNode {
            XmlNode node;
            if (pos >= src.size() || src[pos] != '<')
                throw ParseError("xml: expected '<'");
            pos++;
            size_t st = pos;
            while (pos < src.size() && !isspace((unsigned char)src[pos]) &&
                   src[pos] != '>' && src[pos] != '/')
                pos++;
            node.tag = src.substr(st, pos - st);
            // 解析属性
            while (pos < src.size()) {
                while (pos < src.size() && isspace((unsigned char)src[pos]))
                    pos++;
                if (pos < src.size() && (src[pos] == '>' || src[pos] == '/'))
                    break;
                size_t as = pos;
                while (pos < src.size() && src[pos] != '=' &&
                       !isspace((unsigned char)src[pos]) && src[pos] != '>')
                    pos++;
                std::string aname = src.substr(as, pos - as);
                while (pos < src.size() && isspace((unsigned char)src[pos]))
                    pos++;
                std::string aval;
                if (pos < src.size() && src[pos] == '=') {
                    pos++;
                    while (pos < src.size() && isspace((unsigned char)src[pos]))
                        pos++;
                    if (pos < src.size() &&
                        (src[pos] == '"' || src[pos] == '\'')) {
                        char   q  = src[pos++];
                        size_t vs = pos;
                        while (pos < src.size() && src[pos] != q)
                            pos++;
                        aval = src.substr(vs, pos - vs);
                        if (pos < src.size())
                            pos++;
                    }
                }
                if (!aname.empty()) {
                    if (!node.attrs.count(aname))
                        node.attr_order.push_back(aname);
                    node.attrs[aname] = decodeEntities(aval);
                }
            }
            if (pos < src.size() && src[pos] == '/') {  // 自闭合标签
                pos++;
                if (pos < src.size() && src[pos] == '>')
                    pos++;
                return node;
            }
            if (pos < src.size() && src[pos] == '>')
                pos++;
            // 解析子节点/文本
            while (pos < src.size()) {
                size_t ts = pos;
                while (pos < src.size() && src[pos] != '<')
                    pos++;
                std::string txt = trim(src.substr(ts, pos - ts));
                if (!txt.empty())
                    node.text += decodeEntities(txt);
                if (pos >= src.size())
                    break;
                if (src.compare(pos, 2, "</") == 0) {
                    size_t e = src.find('>', pos);
                    pos      = (e == std::string::npos) ? src.size() : e + 1;
                    return node;
                }
                if (src.compare(pos, 4, "<!--") == 0) {
                    size_t e = src.find("-->", pos);
                    pos      = (e == std::string::npos) ? src.size() : e + 3;
                    continue;
                }
                node.children.push_back(parseElem());
            }
            return node;
        };
        skipMisc();
        if (pos >= src.size())
            throw ParseError("xml: empty document");
        return parseElem();
    }

}  // namespace detail

// XML 输入约定：
// <graph type="flowchart" name="...">
//   <node id="a" label="Start" shape="round"/>
//   <node id="b" label="Group"> <node id="c" label="Child"/> </node>
//   <edge from="a" to="b" label="ok" style="dashed"/>
// </graph>
// parseXML: 将约定 XML 模式映射为统一 Graph
// 关键步骤：校验根节点 -> DFS 递归 node/edge -> 维护 parent 层级
inline Graph parseXML(const std::string& text)
{
    detail::XmlNode root = detail::parseXmlDoc(text);
    if (root.tag != "graph")
        throw ParseError("xml: root element must be <graph>, got <" + root.tag +
                         ">");
    Graph g;
    if (root.attrs.count("type"))
        g.type = root.attrs["type"];
    if (root.attrs.count("name"))
        g.name = root.attrs["name"];

    std::function<void(const detail::XmlNode&, const std::string&)> walk =
        [&](const detail::XmlNode& xn, const std::string& parent) {
            for (auto& c : xn.children) {
                if (c.tag == "node") {
                    std::string id =
                        c.attrs.count("id") ?
                            c.attrs.at("id") :
                            "n" + std::to_string(g.nodes.size() + 1);
                    std::string label = c.attrs.count("label") ?
                                            c.attrs.at("label") :
                                            (!c.text.empty() ? c.text : id);
                    Node&       n     = g.ensureNode(id, label);
                    if (c.attrs.count("shape"))
                        n.shape = c.attrs.at("shape");
                    if (c.attrs.count("style"))
                        n.style = c.attrs.at("style");
                    n.parent = parent;
                    if (!c.children.empty()) {
                        if (n.shape == "rect")
                            n.shape = "group";
                        walk(c, id);
                    }
                }
                else if (c.tag == "edge") {
                    std::string from =
                        c.attrs.count("from") ? c.attrs.at("from") : "";
                    std::string to =
                        c.attrs.count("to") ? c.attrs.at("to") : "";
                    if (from.empty() || to.empty())
                        throw ParseError(
                            "xml: <edge> requires from and to attributes");
                    std::string label =
                        c.attrs.count("label") ? c.attrs.at("label") : "";
                    std::string style =
                        c.attrs.count("style") ? c.attrs.at("style") : "solid";
                    g.addEdge(from, to, label, style);
                }
                else if (c.tag == "attr") {
                    // <node> 内部的 ER 属性（当前保留扩展位）
                }
            }
        };
    walk(root, "");
    return g;
}

// --------------------------------------------------------------- drawio --

// parseDrawio: 解析 draw.io XML (.drawio) 回统一 Graph 模型
// 关键步骤：提取 mxCell vertex → 节点(含坐标/形状/层级) +
//           提取 mxCell edge → 边(含箭头/线型)
//           映射 draw.io 样式回统一模型形状
inline Graph parseDrawio(const std::string& text)
{
    detail::XmlNode mxfile = detail::parseXmlDoc(text);
    if (mxfile.tag != "mxfile")
        throw ParseError("drawio: root element must be <mxfile>, got <" +
                         mxfile.tag + ">");
    const detail::XmlNode* diagram = nullptr;
    for (auto& c : mxfile.children)
        if (c.tag == "diagram") { diagram = &c; break; }
    if (!diagram)
        throw ParseError("drawio: <mxfile> must contain a <diagram>");
    const detail::XmlNode* model = nullptr;
    for (auto& c : diagram->children)
        if (c.tag == "mxGraphModel") { model = &c; break; }
    if (!model)
        throw ParseError("drawio: <diagram> must contain an <mxGraphModel>");
    const detail::XmlNode* root = nullptr;
    for (auto& c : model->children)
        if (c.tag == "root") { root = &c; break; }
    if (!root)
        throw ParseError("drawio: <mxGraphModel> must contain a <root>");

    Graph g;
    if (diagram->attrs.count("name"))
        g.name = diagram->attrs.at("name");
    g.type = "flowchart";

    struct Cell
    {
        std::string id, value, style, parent;
        std::string source, target;
        bool        vertex = false, edge = false;
        double      x = 0, y = 0, w = 140, h = 80;
        bool        hasSourcePoint = false;
    };
    std::vector<Cell> cells;

    for (auto& c : root->children) {
        if (c.tag != "mxCell")
            continue;
        Cell cell;
        auto ga = [&](const char* k, std::string& out) {
            if (c.attrs.count(k))
                out = c.attrs.at(k);
        };
        ga("id", cell.id);
        ga("value", cell.value);
        ga("style", cell.style);
        ga("parent", cell.parent);
        ga("source", cell.source);
        ga("target", cell.target);
        if (c.attrs.count("vertex") && c.attrs.at("vertex") == "1")
            cell.vertex = true;
        if (c.attrs.count("edge") && c.attrs.at("edge") == "1")
            cell.edge = true;
        for (auto& gc : c.children) {
            if (gc.tag == "mxGeometry") {
                auto gd = [&](const char* k, double& out) {
                    if (gc.attrs.count(k))
                        out = std::stod(gc.attrs.at(k));
                };
                gd("x", cell.x);
                gd("y", cell.y);
                gd("width", cell.w);
                gd("height", cell.h);
                for (auto& pt : gc.children) {
                    if (pt.tag == "mxPoint") {
                        auto it = pt.attrs.find("as");
                        if (it != pt.attrs.end() && it->second == "sourcePoint")
                            cell.hasSourcePoint = true;
                    }
                }
            }
        }
        cells.push_back(cell);
    }

    auto skipCell = [&](const Cell& cell) -> bool {
        if (cell.id == "0" || cell.id == "1")
            return true;
        if (cell.edge && cell.hasSourcePoint && cell.source.empty())
            return true;
        return false;
    };
    auto styleHas = [](const std::string& s, const std::string& key) {
        return s.find(key) != std::string::npos;
    };

    for (auto& cell : cells) {
        if (skipCell(cell) || !cell.vertex)
            continue;

        std::string              label = cell.value;
        std::vector<std::string> attrs;
        if (!label.empty() && label.find("<b>") != std::string::npos) {
            std::string title;
            size_t      bp = label.find("<b>");
            size_t      be = label.find("</b>", bp);
            if (bp != std::string::npos && be != std::string::npos)
                title = label.substr(bp + 3, be - bp - 3);
            else
                title = label;
            size_t pos = 0;
            while (pos < label.size()) {
                size_t      brp = label.find("<br/>", pos);
                std::string part;
                if (brp != std::string::npos) {
                    part = label.substr(pos, brp - pos);
                    pos  = brp + 5;
                }
                else {
                    part = label.substr(pos);
                    pos  = label.size();
                }
                std::string cleaned;
                for (size_t i = 0; i < part.size();) {
                    if (part.compare(i, 4, "</b>") == 0)
                        i += 4;
                    else if (part.compare(i, 3, "<b>") == 0)
                        i += 3;
                    else
                        cleaned += part[i++];
                }
                std::string t = gm::trim(cleaned);
                if (!t.empty())
                    attrs.push_back(t);
            }
            if (!attrs.empty() && !title.empty()) {
                label = title;
                if (attrs[0] == title)
                    attrs.erase(attrs.begin());
            }
        }
        if (label.empty())
            label = cell.id;

        std::string shape = "rect";
        if (styleHas(cell.style, "rhombus"))
            shape = "diamond";
        else if (styleHas(cell.style, "ellipse"))
            shape = "ellipse";
        else if (styleHas(cell.style, "shape=hexagon"))
            shape = "hexagon";
        else if (styleHas(cell.style, "arcSize=50"))
            shape = "stadium";
        else if (styleHas(cell.style, "rounded=1"))
            shape = "round";
        else if (styleHas(cell.style, "shape=table"))
            shape = "rect";

        bool isGroup = false;
        if (styleHas(cell.style, "fillColor=none") &&
            styleHas(cell.style, "dashed=1"))
            isGroup = true;
        for (auto& oc : cells)
            if (oc.parent == cell.id && (oc.vertex || oc.edge))
                isGroup = true;
        if (isGroup)
            shape = "group";

        Node& n = g.ensureNode(cell.id, label);
        n.shape = shape;
        n.x     = cell.x;
        n.y     = cell.y;
        n.w     = cell.w;
        n.h     = cell.h;
        if (!attrs.empty())
            n.attrs = attrs;
        if (!cell.parent.empty() && cell.parent != "1" && cell.parent != "0")
            n.parent = cell.parent;
    }

    for (auto& cell : cells) {
        if (skipCell(cell) || !cell.edge)
            continue;
        if (cell.source.empty() || cell.target.empty())
            continue;

        std::string label     = cell.value;
        std::string edgeStyle = "solid";
        if (styleHas(cell.style, "dashed=1"))
            edgeStyle = "dashed";
        if (styleHas(cell.style, "strokeWidth=3") ||
            styleHas(cell.style, "strokeWidth=4"))
            edgeStyle = "thick";

        std::string arrow  = "arrow";
        bool        noEnd  = styleHas(cell.style, "endArrow=none");
        bool        hasStart =
            styleHas(cell.style, "startArrow=classic") ||
            styleHas(cell.style, "startArrow=block");
        if (noEnd && hasStart)
            arrow = "both";
        else if (noEnd)
            arrow = "none";
        else if (hasStart)
            arrow = "both";

        g.addEdge(cell.source, cell.target, label, edgeStyle, arrow);
    }

    return g;
}

// ------------------------------------------------------------- Excalidraw --

// parseExcalidraw: 解析 Excalidraw JSON
// 关键步骤：保留原始 elements 以便无损回写 -> 从几何元素提取节点 ->
// 从箭头绑定提取边
inline Graph parseExcalidraw(const std::string& text)
{
    std::string err;
    Json        j = Json::parse(text, &err);
    if (!err.empty())
        throw ParseError("excalidraw: invalid JSON: " + err);
    const Json* els = j.find("elements");
    if (!els || !els->isArr())
        throw ParseError("excalidraw: missing 'elements' array");
    Graph g;
    g.type = "whiteboard";
    if (const Json* fs = j.find("files")) {
        if (fs->isObj())
            g.files = *fs;
    }
    // 保留原始 elements，支持无损往返转换
    for (auto& el : *els->a) {
        if (el.boolean("isDeleted", false))
            continue;
        g.elements.push_back(el);
    }
    // 从图形元素提取逻辑节点，标签优先取绑定/容器文本
    std::map<std::string, std::string> textByContainer;
    for (auto& el : g.elements) {
        if (el.str("type") == "text" && !el.str("containerId").empty())
            textByContainer[el.str("containerId")] = el.str("text");
    }
    for (auto& el : g.elements) {
        std::string ty = el.str("type");
        if (ty == "rectangle" || ty == "ellipse" || ty == "diamond") {
            Node n;
            n.id    = el.str("id");
            n.label = textByContainer.count(n.id) ? textByContainer[n.id] : "";
            n.shape = ty == "rectangle" ?
                          "rect" :
                          (ty == "ellipse" ? "ellipse" : "diamond");
            n.x     = el.num("x");
            n.y     = el.num("y");
            n.w     = el.num("width");
            n.h     = el.num("height");
            g.nodes.push_back(n);
        }
        else if (ty == "arrow") {
            std::string from, to;
            if (const Json* sb = el.find("startBinding"))
                if (sb->isObj())
                    from = sb->str("elementId");
            if (const Json* eb = el.find("endBinding"))
                if (eb->isObj())
                    to = eb->str("elementId");
            if (!from.empty() && !to.empty()) {
                // 箭头嵌入文字：text.containerId 指向 arrow.id
                std::string label    = textByContainer.count(el.str("id")) ?
                                           textByContainer[el.str("id")] :
                                           "";
                std::string style    = el.str("strokeStyle", "solid");
                std::string arrow    = "arrow";
                bool        hasEnd   = !el.str("endArrowhead").empty() &&
                                       el.str("endArrowhead") != "null";
                bool        hasStart = !el.str("startArrowhead").empty() &&
                                       el.str("startArrowhead") != "null";
                if (!hasEnd && !hasStart)
                    arrow = "none";
                else if (hasEnd && hasStart)
                    arrow = "both";
                else if (hasStart && !hasEnd)
                    std::swap(from, to);
                g.addEdge(from, to, label, style, arrow);
            }
        }
    }
    g.laidOut = true;  // 白板场景自带坐标
    return g;
}

// --------------------------------------------------------------- 分发入口 --

// format 可选：mermaid | markdown | csv | xml | excalidraw | model | auto
// detectFormat: 自动格式识别（命名体现“检测而非解析”）
// 识别顺序：xml/json-mermaid-markdown-csv，尽量减少误判成本
inline std::string detectFormat(const std::string& text)
{
    std::string t = trim(text);
    if (t.empty())
        return "auto";
    if (t[0] == '<') {
        if (t.find("<mxfile") != std::string::npos)
            return "drawio";
        return "xml";
    }
    if (t[0] == '{') {
        std::string err;
        Json        j = Json::parse(t, &err);
        if (err.empty()) {
            if (j.str("type") == "excalidraw" || j.find("elements"))
                return "excalidraw";
            if (j.find("nodes"))
                return "model";
        }
        return "excalidraw";
    }
    std::string low = toLower(t);
    for (auto& l : splitLines(low)) {
        std::string s = trim(l);
        if (s.empty() || startsWith(s, "%%"))
            continue;
        if (startsWith(s, "graph ") || startsWith(s, "flowchart") ||
            startsWith(s, "mindmap") || startsWith(s, "erdiagram"))
            return "mermaid";
        break;
    }
    if (t[0] == '#' || t[0] == '-' || t[0] == '*')
        return "markdown";
    // CSV 启发式判断：首行包含逗号且出现已知表头
    auto lines = splitLines(low);
    if (!lines.empty() && lines[0].find(',') != std::string::npos) {
        if (lines[0].find("from") != std::string::npos ||
            lines[0].find("source") != std::string::npos ||
            lines[0].find("id") != std::string::npos)
            return "csv";
    }
    return "markdown";
}

// parseAny: 统一解析入口；支持 format=auto 自动检测
// 关键步骤：确定格式 -> 调用对应解析器 -> 按需覆盖图类型
inline Graph parseAny(const std::string& text,
                      std::string        format      = "auto",
                      const std::string& diagramType = "")
{
    if (format.empty() || format == "auto")
        format = detectFormat(text);
    Graph g;
    if (format == "mermaid")
        g = parseMermaid(text);
    else if (format == "markdown")
        g = parseMarkdownOutline(text);
    else if (format == "csv")
        g = parseCSV(text);
    else if (format == "xml")
        g = parseXML(text);
    else if (format == "excalidraw")
        g = parseExcalidraw(text);
    else if (format == "drawio")
        g = parseDrawio(text);
    else if (format == "model") {
        std::string err;
        Json        j = Json::parse(text, &err);
        if (!err.empty())
            throw ParseError("model: invalid JSON: " + err);
        g = Graph::fromJson(j);
    }
    else
        throw ParseError("unknown input format: " + format);
    if (!diagramType.empty() && diagramType != "auto")
        g.type = diagramType;
    return g;
}

}  // namespace gp
