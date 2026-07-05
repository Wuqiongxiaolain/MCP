// parsers.hpp - structured input parsers -> unified graph model
// Supported inputs: Mermaid (flowchart / mindmap / erDiagram), Markdown
// outline, CSV (edge list or hierarchy), simple XML, Excalidraw JSON.
#pragma once
#include "model.hpp"
#include <stdexcept>
#include <functional>
#include <cstring>
#include <cctype>

namespace gp {

using gm::Graph;
using gm::Node;
using gm::trim;
using gm::splitLines;
using gm::toLower;
using gm::startsWith;
using gj::Json;

struct ParseError : std::runtime_error {
    explicit ParseError(const std::string& m) : std::runtime_error(m) {}
};

// ---------------------------------------------------------------- mermaid --

namespace detail {

inline bool isIdentChar(char c) {
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

// read `A`, then optional shape bracket: [..] (..) ((..)) {..} ([..])
inline bool readNodeRef(const std::string& s, size_t& pos,
                        std::string& id, std::string& label, std::string& shape) {
    while (pos < s.size() && isspace((unsigned char)s[pos])) pos++;
    size_t start = pos;
    while (pos < s.size() && isIdentChar(s[pos])) pos++;
    if (pos == start) return false;
    id = s.substr(start, pos - start);
    label.clear(); shape.clear();
    if (pos >= s.size()) return true;
    char open = s[pos];
    auto grab = [&](const std::string& openTok, const std::string& closeTok,
                    const std::string& shp) -> bool {
        if (s.compare(pos, openTok.size(), openTok) != 0) return false;
        size_t st = pos + openTok.size();
        size_t end = s.find(closeTok, st);
        if (end == std::string::npos) return false;
        label = trim(s.substr(st, end - st));
        // strip surrounding quotes
        if (label.size() >= 2 && label.front() == '"' && label.back() == '"')
            label = label.substr(1, label.size() - 2);
        shape = shp;
        pos = end + closeTok.size();
        return true;
    };
    if (open == '(') {
        if (grab("((", "))", "circle")) return true;
        if (grab("([", "])", "stadium")) return true;
        if (grab("(", ")", "round")) return true;
    } else if (open == '[') {
        if (grab("[[", "]]", "rect")) return true;
        if (grab("[", "]", "rect")) return true;
    } else if (open == '{') {
        if (grab("{{", "}}", "hexagon")) return true;
        if (grab("{", "}", "diamond")) return true;
    }
    return true;
}

// read an arrow token; returns style/arrow or empty
inline bool readArrow(const std::string& s, size_t& pos,
                      std::string& style, std::string& arrow) {
    while (pos < s.size() && isspace((unsigned char)s[pos])) pos++;
    static const struct { const char* tok; const char* style; const char* arr; } arrows[] = {
        {"-.->", "dashed", "arrow"}, {"-.-",  "dashed", "none"},
        {"==>",  "thick",  "arrow"}, {"===",  "thick",  "none"},
        {"<-->", "solid",  "both"},  {"-->",  "solid",  "arrow"},
        {"---",  "solid",  "none"},  {"--",   "solid",  "none"},
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

} // namespace detail

inline Graph parseMermaidFlowchart(const std::vector<std::string>& lines, size_t first) {
    Graph g;
    g.type = "flowchart";
    std::vector<std::string> groupStack; // subgraph nesting
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%")) continue;
        if (startsWith(line, "classDef") || startsWith(line, "class ") ||
            startsWith(line, "style ") || startsWith(line, "linkStyle")) continue;
        if (startsWith(line, "subgraph")) {
            std::string rest = trim(line.substr(8));
            std::string gid = rest, glabel = rest;
            size_t br = rest.find('[');
            if (br != std::string::npos) {
                gid = trim(rest.substr(0, br));
                size_t close = rest.find(']', br);
                glabel = trim(rest.substr(br + 1, close == std::string::npos
                                                  ? std::string::npos : close - br - 1));
            }
            if (gid.empty()) gid = "sg" + std::to_string(g.nodes.size());
            Node& n = g.ensureNode(gid, glabel);
            n.shape = "group";
            if (!groupStack.empty()) n.parent = groupStack.back();
            groupStack.push_back(gid);
            continue;
        }
        if (line == "end") {
            if (!groupStack.empty()) groupStack.pop_back();
            continue;
        }
        // parse: node (arrow |label| node)*
        size_t pos = 0;
        std::string id, label, shape;
        if (!detail::readNodeRef(line, pos, id, label, shape)) continue;
        {
            Node& n = g.ensureNode(id, label);
            if (!shape.empty()) n.shape = shape;
            if (n.parent.empty() && !groupStack.empty()) n.parent = groupStack.back();
        }
        std::string prev = id;
        while (true) {
            std::string style, arrow;
            if (!detail::readArrow(line, pos, style, arrow)) break;
            // optional |label|
            while (pos < line.size() && isspace((unsigned char)line[pos])) pos++;
            std::string elabel;
            if (pos < line.size() && line[pos] == '|') {
                size_t end = line.find('|', pos + 1);
                if (end != std::string::npos) {
                    elabel = trim(line.substr(pos + 1, end - pos - 1));
                    pos = end + 1;
                }
            }
            std::string id2, label2, shape2;
            if (!detail::readNodeRef(line, pos, id2, label2, shape2)) break;
            Node& n2 = g.ensureNode(id2, label2);
            if (!shape2.empty()) n2.shape = shape2;
            if (n2.parent.empty() && !groupStack.empty()) n2.parent = groupStack.back();
            g.addEdge(prev, id2, elabel, style, arrow);
            prev = id2;
        }
    }
    return g;
}

inline Graph parseMermaidMindmap(const std::vector<std::string>& lines, size_t first) {
    Graph g;
    g.type = "mindmap";
    std::vector<std::pair<int, std::string>> stack; // (indent, nodeId)
    int seq = 0;
    for (size_t li = first; li < lines.size(); li++) {
        const std::string& raw = lines[li];
        std::string t = trim(raw);
        if (t.empty() || startsWith(t, "%%")) continue;
        int indent = 0;
        for (char c : raw) { if (c == ' ') indent++; else if (c == '\t') indent += 4; else break; }
        // strip mermaid mindmap decorations root((x)) / (x) / [x]
        std::string label = t;
        auto stripWrap = [&](const std::string& o, const std::string& c) {
            size_t op = label.find(o);
            if (op != std::string::npos && label.size() > op + o.size() &&
                label.rfind(c) == label.size() - c.size()) {
                std::string inner = label.substr(op + o.size(),
                    label.size() - c.size() - op - o.size());
                label = trim(inner);
                return true;
            }
            return false;
        };
        if (!stripWrap("((", "))")) if (!stripWrap("([", "])"))
            if (!stripWrap("[", "]")) stripWrap("(", ")");
        std::string nid = "m" + std::to_string(++seq);
        Node& n = g.ensureNode(nid, label);
        n.shape = stack.empty() ? "circle" : "round";
        while (!stack.empty() && stack.back().first >= indent) stack.pop_back();
        if (!stack.empty()) {
            n.parent = stack.back().second;
            g.addEdge(stack.back().second, nid, "", "solid", "none");
        }
        stack.emplace_back(indent, nid);
    }
    return g;
}

inline Graph parseMermaidER(const std::vector<std::string>& lines, size_t first) {
    Graph g;
    g.type = "er";
    std::string curEntity;
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%")) continue;
        if (!curEntity.empty()) {
            if (line == "}") { curEntity.clear(); continue; }
            g.ensureNode(curEntity).attrs.push_back(line);
            continue;
        }
        // entity block start: NAME {
        if (line.size() > 1 && line.back() == '{') {
            curEntity = trim(line.substr(0, line.size() - 1));
            Node& n = g.ensureNode(curEntity);
            n.shape = "rect";
            continue;
        }
        // relationship: A ||--o{ B : label
        size_t dd = line.find("--");
        size_t colon = line.find(':');
        if (dd != std::string::npos) {
            size_t ls = dd;
            while (ls > 0 && !isspace((unsigned char)line[ls - 1])) ls--;
            std::string left = trim(line.substr(0, ls));
            size_t re = dd + 2;
            while (re < line.size() && !isspace((unsigned char)line[re])) re++;
            std::string rest = trim(line.substr(re,
                colon == std::string::npos ? std::string::npos : colon - re));
            std::string label = colon == std::string::npos ? "" : trim(line.substr(colon + 1));
            if (label.size() >= 2 && label.front() == '"' && label.back() == '"')
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

inline Graph parseMermaid(const std::string& text) {
    auto lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = toLower(trim(lines[i]));
        if (t.empty() || startsWith(t, "%%")) continue;
        if (startsWith(t, "graph") || startsWith(t, "flowchart"))
            return parseMermaidFlowchart(lines, i + 1);
        if (startsWith(t, "mindmap"))
            return parseMermaidMindmap(lines, i + 1);
        if (startsWith(t, "erdiagram"))
            return parseMermaidER(lines, i + 1);
        throw ParseError("unsupported mermaid diagram type: " + t.substr(0, 20));
    }
    throw ParseError("empty mermaid input");
}

// --------------------------------------------------------- markdown outline --

inline Graph parseMarkdownOutline(const std::string& text) {
    Graph g;
    g.type = "mindmap";
    std::vector<std::pair<int, std::string>> stack; // (level, nodeId)
    int seq = 0;
    int lastHeadingLevel = 0;
    for (auto& raw : splitLines(text)) {
        std::string t = trim(raw);
        if (t.empty()) continue;
        int level;
        std::string label;
        if (t[0] == '#') {
            size_t h = 0;
            while (h < t.size() && t[h] == '#') h++;
            level = (int)h;
            label = trim(t.substr(h));
            lastHeadingLevel = level;
        } else if (t[0] == '-' || t[0] == '*' || t[0] == '+') {
            int indent = 0;
            for (char c : raw) { if (c == ' ') indent++; else if (c == '\t') indent += 4; else break; }
            level = lastHeadingLevel + 1 + indent / 2;
            label = trim(t.substr(1));
        } else {
            continue; // plain paragraph text is ignored in outline mode
        }
        if (label.empty()) continue;
        std::string nid = "m" + std::to_string(++seq);
        Node& n = g.ensureNode(nid, label);
        while (!stack.empty() && stack.back().first >= level) stack.pop_back();
        if (!stack.empty()) {
            n.parent = stack.back().second;
            g.addEdge(stack.back().second, nid, "", "solid", "none");
            n.shape = "round";
        } else {
            n.shape = "circle";
        }
        stack.emplace_back(level, nid);
    }
    if (g.nodes.empty()) throw ParseError("no outline items found in markdown");
    return g;
}

// ------------------------------------------------------------------- csv --

inline std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQ) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; i++; }
                else inQ = false;
            } else cur += c;
        } else {
            if (c == '"') inQ = true;
            else if (c == ',') { out.push_back(trim(cur)); cur.clear(); }
            else cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

// Two CSV schemas:
//   edge list:  from,to[,label]          -> flowchart
//   hierarchy:  id,label[,parent]        -> orgchart
inline Graph parseCSV(const std::string& text) {
    auto lines = splitLines(text);
    if (lines.empty()) throw ParseError("empty csv input");
    auto header = splitCsvLine(lines[0]);
    std::map<std::string, int> col;
    for (size_t i = 0; i < header.size(); i++) col[toLower(header[i])] = (int)i;
    auto has = [&](const std::string& k) { return col.count(k) > 0; };

    Graph g;
    bool edgeList = (has("from") && has("to")) || (has("source") && has("target"));
    bool tree = has("id") && (has("parent") || has("label"));
    if (!edgeList && !tree)
        throw ParseError("csv header must contain from,to[,label] or id,label[,parent]");

    int cFrom = has("from") ? col["from"] : (has("source") ? col["source"] : -1);
    int cTo   = has("to")   ? col["to"]   : (has("target") ? col["target"] : -1);
    int cLbl  = has("label") ? col["label"] : -1;
    int cId   = has("id") ? col["id"] : -1;
    int cPar  = has("parent") ? col["parent"] : -1;

    g.type = edgeList ? "flowchart" : "orgchart";
    for (size_t li = 1; li < lines.size(); li++) {
        if (trim(lines[li]).empty()) continue;
        auto f = splitCsvLine(lines[li]);
        auto cell = [&](int c) -> std::string {
            return (c >= 0 && c < (int)f.size()) ? f[(size_t)c] : "";
        };
        if (edgeList) {
            std::string from = cell(cFrom), to = cell(cTo);
            if (from.empty() || to.empty()) continue;
            g.ensureNode(from);
            g.ensureNode(to);
            g.addEdge(from, to, cell(cLbl));
        } else {
            std::string id = cell(cId);
            if (id.empty()) continue;
            Node& n = g.ensureNode(id, cell(cLbl));
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

// ------------------------------------------------------------------- xml --

namespace detail {

struct XmlNode {
    std::string tag;
    std::map<std::string, std::string> attrs;
    std::vector<XmlNode> children;
    std::string text;
};

// minimal XML parser: elements, attributes, text; skips comments/decl/cdata
inline XmlNode parseXmlDoc(const std::string& src) {
    size_t pos = 0;
    auto skipMisc = [&]() {
        while (pos < src.size()) {
            while (pos < src.size() && isspace((unsigned char)src[pos])) pos++;
            if (src.compare(pos, 4, "<!--") == 0) {
                size_t e = src.find("-->", pos);
                pos = (e == std::string::npos) ? src.size() : e + 3;
            } else if (src.compare(pos, 2, "<?") == 0) {
                size_t e = src.find("?>", pos);
                pos = (e == std::string::npos) ? src.size() : e + 2;
            } else if (src.compare(pos, 2, "<!") == 0) {
                size_t e = src.find(">", pos);
                pos = (e == std::string::npos) ? src.size() : e + 1;
            } else break;
        }
    };
    auto decodeEntities = [](std::string s) {
        struct { const char* e; const char* r; } ents[] = {
            {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"},
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
               src[pos] != '>' && src[pos] != '/') pos++;
        node.tag = src.substr(st, pos - st);
        // attributes
        while (pos < src.size()) {
            while (pos < src.size() && isspace((unsigned char)src[pos])) pos++;
            if (pos < src.size() && (src[pos] == '>' || src[pos] == '/')) break;
            size_t as = pos;
            while (pos < src.size() && src[pos] != '=' &&
                   !isspace((unsigned char)src[pos]) && src[pos] != '>') pos++;
            std::string aname = src.substr(as, pos - as);
            while (pos < src.size() && isspace((unsigned char)src[pos])) pos++;
            std::string aval;
            if (pos < src.size() && src[pos] == '=') {
                pos++;
                while (pos < src.size() && isspace((unsigned char)src[pos])) pos++;
                if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
                    char q = src[pos++];
                    size_t vs = pos;
                    while (pos < src.size() && src[pos] != q) pos++;
                    aval = src.substr(vs, pos - vs);
                    if (pos < src.size()) pos++;
                }
            }
            if (!aname.empty()) node.attrs[aname] = decodeEntities(aval);
        }
        if (pos < src.size() && src[pos] == '/') { // self-closing
            pos++;
            if (pos < src.size() && src[pos] == '>') pos++;
            return node;
        }
        if (pos < src.size() && src[pos] == '>') pos++;
        // children / text
        while (pos < src.size()) {
            size_t ts = pos;
            while (pos < src.size() && src[pos] != '<') pos++;
            std::string txt = trim(src.substr(ts, pos - ts));
            if (!txt.empty()) node.text += decodeEntities(txt);
            if (pos >= src.size()) break;
            if (src.compare(pos, 2, "</") == 0) {
                size_t e = src.find('>', pos);
                pos = (e == std::string::npos) ? src.size() : e + 1;
                return node;
            }
            if (src.compare(pos, 4, "<!--") == 0) {
                size_t e = src.find("-->", pos);
                pos = (e == std::string::npos) ? src.size() : e + 3;
                continue;
            }
            node.children.push_back(parseElem());
        }
        return node;
    };
    skipMisc();
    if (pos >= src.size()) throw ParseError("xml: empty document");
    return parseElem();
}

} // namespace detail

// XML schema:
// <graph type="flowchart" name="...">
//   <node id="a" label="Start" shape="round"/>
//   <node id="b" label="Group"> <node id="c" label="Child"/> </node>
//   <edge from="a" to="b" label="ok" style="dashed"/>
// </graph>
inline Graph parseXML(const std::string& text) {
    detail::XmlNode root = detail::parseXmlDoc(text);
    if (root.tag != "graph")
        throw ParseError("xml: root element must be <graph>, got <" + root.tag + ">");
    Graph g;
    if (root.attrs.count("type")) g.type = root.attrs["type"];
    if (root.attrs.count("name")) g.name = root.attrs["name"];

    std::function<void(const detail::XmlNode&, const std::string&)> walk =
        [&](const detail::XmlNode& xn, const std::string& parent) {
        for (auto& c : xn.children) {
            if (c.tag == "node") {
                std::string id = c.attrs.count("id") ? c.attrs.at("id")
                                : "n" + std::to_string(g.nodes.size() + 1);
                std::string label = c.attrs.count("label") ? c.attrs.at("label")
                                   : (!c.text.empty() ? c.text : id);
                Node& n = g.ensureNode(id, label);
                if (c.attrs.count("shape")) n.shape = c.attrs.at("shape");
                if (c.attrs.count("style")) n.style = c.attrs.at("style");
                n.parent = parent;
                if (!c.children.empty()) {
                    if (n.shape == "rect") n.shape = "group";
                    walk(c, id);
                }
            } else if (c.tag == "edge") {
                std::string from = c.attrs.count("from") ? c.attrs.at("from") : "";
                std::string to   = c.attrs.count("to")   ? c.attrs.at("to")   : "";
                if (from.empty() || to.empty())
                    throw ParseError("xml: <edge> requires from and to attributes");
                std::string label = c.attrs.count("label") ? c.attrs.at("label") : "";
                std::string style = c.attrs.count("style") ? c.attrs.at("style") : "solid";
                g.addEdge(from, to, label, style);
            } else if (c.tag == "attr") {
                // ER attribute inside a <node>
            }
        }
    };
    walk(root, "");
    return g;
}

// ------------------------------------------------------------- excalidraw --

inline Graph parseExcalidraw(const std::string& text) {
    std::string err;
    Json j = Json::parse(text, &err);
    if (!err.empty()) throw ParseError("excalidraw: invalid JSON: " + err);
    const Json* els = j.find("elements");
    if (!els || !els->isArr())
        throw ParseError("excalidraw: missing 'elements' array");
    Graph g;
    g.type = "whiteboard";
    // keep raw elements for lossless round-trip
    for (auto& el : *els->a) {
        if (el.boolean("isDeleted", false)) continue;
        g.elements.push_back(el);
    }
    // derive logical nodes from shapes, labels from bound/contained text
    std::map<std::string, std::string> textByContainer;
    for (auto& el : g.elements) {
        if (el.str("type") == "text" && !el.str("containerId").empty())
            textByContainer[el.str("containerId")] = el.str("text");
    }
    for (auto& el : g.elements) {
        std::string ty = el.str("type");
        if (ty == "rectangle" || ty == "ellipse" || ty == "diamond") {
            Node n;
            n.id = el.str("id");
            n.label = textByContainer.count(n.id) ? textByContainer[n.id] : "";
            n.shape = ty == "rectangle" ? "rect" : (ty == "ellipse" ? "ellipse" : "diamond");
            n.x = el.num("x"); n.y = el.num("y");
            n.w = el.num("width"); n.h = el.num("height");
            g.nodes.push_back(n);
        } else if (ty == "arrow") {
            std::string from, to;
            if (const Json* sb = el.find("startBinding"))
                if (sb->isObj()) from = sb->str("elementId");
            if (const Json* eb = el.find("endBinding"))
                if (eb->isObj()) to = eb->str("elementId");
            if (!from.empty() && !to.empty())
                g.addEdge(from, to);
        }
    }
    g.laidOut = true; // whiteboard scenes carry their own coordinates
    return g;
}

// --------------------------------------------------------------- dispatch --

// format: mermaid | markdown | csv | xml | excalidraw | model | auto
inline std::string detectFormat(const std::string& text) {
    std::string t = trim(text);
    if (t.empty()) return "auto";
    if (t[0] == '<') return "xml";
    if (t[0] == '{') {
        std::string err;
        Json j = Json::parse(t, &err);
        if (err.empty()) {
            if (j.str("type") == "excalidraw" || j.find("elements")) return "excalidraw";
            if (j.find("nodes")) return "model";
        }
        return "excalidraw";
    }
    std::string low = toLower(t);
    for (auto& l : splitLines(low)) {
        std::string s = trim(l);
        if (s.empty() || startsWith(s, "%%")) continue;
        if (startsWith(s, "graph ") || startsWith(s, "flowchart") ||
            startsWith(s, "mindmap") || startsWith(s, "erdiagram"))
            return "mermaid";
        break;
    }
    if (t[0] == '#' || t[0] == '-' || t[0] == '*') return "markdown";
    // csv heuristic: first line contains comma and known headers
    auto lines = splitLines(low);
    if (!lines.empty() && lines[0].find(',') != std::string::npos) {
        if (lines[0].find("from") != std::string::npos ||
            lines[0].find("source") != std::string::npos ||
            lines[0].find("id") != std::string::npos)
            return "csv";
    }
    return "markdown";
}

inline Graph parseAny(const std::string& text, std::string format = "auto",
                      std::string diagramType = "") {
    if (format.empty() || format == "auto") format = detectFormat(text);
    Graph g;
    if      (format == "mermaid")    g = parseMermaid(text);
    else if (format == "markdown")   g = parseMarkdownOutline(text);
    else if (format == "csv")        g = parseCSV(text);
    else if (format == "xml")        g = parseXML(text);
    else if (format == "excalidraw") g = parseExcalidraw(text);
    else if (format == "model") {
        std::string err;
        Json j = Json::parse(text, &err);
        if (!err.empty()) throw ParseError("model: invalid JSON: " + err);
        g = Graph::fromJson(j);
    }
    else throw ParseError("unknown input format: " + format);
    if (!diagramType.empty() && diagramType != "auto") g.type = diagramType;
    return g;
}

} // namespace gp
