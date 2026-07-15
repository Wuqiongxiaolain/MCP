// parsers.hpp - 结构化输入解析器 -> 统一图模型
// 支持输入：Mermaid（flowchart / mindmap / erDiagram）、Markdown 大纲、
// CSV（边列表或层级）、简化 XML、Excalidraw JSON。
#pragma once
#include "csv_util.hpp"
#include "model.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace gp {

using gj::Json;
using gm::Graph;
using gm::Node;
using gm::splitLines;
using gm::startsWith;
using gm::stripUtf8Bom;
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
    std::vector<std::string>                                   groupStack;
    std::map<std::string, std::pair<std::string, std::string>> classDefs;
    // pendingLinkStyles: 边可能尚未全部创建时先缓存，结束时按索引回写
    std::vector<std::pair<size_t, std::string>> pendingLinkStyles;
    auto parseStyleColors =
        [](const std::string& s) -> std::pair<std::string, std::string> {
        std::string fill, stroke;
        // extract: 支持 #hex / 命名色，以及 rgb()/rgba()/hsl()/hsla() 括号值
        auto extract = [&](const std::string& key) -> std::string {
            size_t p = s.find(key + ":");
            if (p == std::string::npos)
                return "";
            p += key.size() + 1;
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t'))
                p++;
            auto startsFunc = [&](const char* name) {
                size_t n = std::strlen(name);
                return p + n <= s.size() && s.compare(p, n, name) == 0;
            };
            if (startsFunc("rgb(") || startsFunc("rgba(") ||
                startsFunc("hsl(") || startsFunc("hsla(")) {
                size_t open  = s.find('(', p);
                size_t close = (open == std::string::npos) ?
                                   std::string::npos :
                                   s.find(')', open + 1);
                if (close != std::string::npos)
                    return s.substr(p, close - p + 1);
            }
            size_t e = s.find_first_of(",; \t\r\n", p);
            if (e == std::string::npos)
                return s.substr(p);
            return s.substr(p, e - p);
        };
        fill   = extract("fill");
        stroke = extract("stroke");
        if (stroke.empty())
            stroke = extract("color");
        return {fill, stroke};
    };
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        // linkStyle: 支持单索引 / 逗号列表 / 区间 / default
        // 例：linkStyle 0 stroke:#f00
        //     linkStyle 0,1,2 stroke:#f00
        //     linkStyle 1-3 stroke:#f00
        //     linkStyle default stroke:#f00
        if (startsWith(line, "linkStyle")) {
            std::string rest = trim(line.substr(9));
            // 步骤1：定位样式段（stroke:/fill:/color:）
            size_t stylePos = std::string::npos;
            for (const char* key : {"stroke:", "fill:", "color:"}) {
                size_t p = rest.find(key);
                if (p != std::string::npos &&
                    (stylePos == std::string::npos || p < stylePos))
                    stylePos = p;
            }
            std::string indexPart =
                stylePos == std::string::npos ? rest : trim(rest.substr(0, stylePos));
            std::string style =
                stylePos == std::string::npos ? "" : trim(rest.substr(stylePos));
            auto colors = parseStyleColors(style);
            if (!colors.second.empty() && !indexPart.empty()) {
                // 步骤2：解析索引集合（default=全部边，占位 SIZE_MAX）
                if (toLower(indexPart) == "default") {
                    pendingLinkStyles.push_back(
                        {static_cast<size_t>(-1), colors.second});
                }
                else {
                    size_t pos = 0;
                    while (pos < indexPart.size()) {
                        while (pos < indexPart.size() &&
                               (indexPart[pos] == ' ' || indexPart[pos] == ','))
                            pos++;
                        if (pos >= indexPart.size())
                            break;
                        size_t numStart = pos;
                        while (pos < indexPart.size() &&
                               isdigit((unsigned char)indexPart[pos]))
                            pos++;
                        if (pos == numStart)
                            break;
                        size_t a = (size_t)std::strtoul(
                            indexPart.substr(numStart, pos - numStart).c_str(),
                            nullptr, 10);
                        // 区间 a-b
                        if (pos < indexPart.size() && indexPart[pos] == '-') {
                            pos++;
                            size_t numStart2 = pos;
                            while (pos < indexPart.size() &&
                                   isdigit((unsigned char)indexPart[pos]))
                                pos++;
                            if (pos > numStart2) {
                                size_t b = (size_t)std::strtoul(
                                    indexPart
                                        .substr(numStart2, pos - numStart2)
                                        .c_str(),
                                    nullptr, 10);
                                if (a > b)
                                    std::swap(a, b);
                                for (size_t k = a; k <= b; ++k)
                                    pendingLinkStyles.push_back(
                                        {k, colors.second});
                            }
                        }
                        else {
                            pendingLinkStyles.push_back({a, colors.second});
                        }
                    }
                }
            }
            continue;
        }
        // classDef 定义：存储 className → 颜色对，后续 class 引用时应用
        if (startsWith(line, "classDef ")) {
            std::string rest = trim(line.substr(9));
            size_t      sp   = rest.find(' ');
            if (sp != std::string::npos) {
                std::string name  = trim(rest.substr(0, sp));
                std::string style = rest.substr(sp + 1);
                classDefs[name]   = parseStyleColors(style);
            }
            continue;
        }
        // class 引用：将 classDef 的颜色应用到匹配节点
        if (startsWith(line, "class ")) {
            std::string rest = trim(line.substr(6));
            size_t      sp   = rest.find_last_of(' ');
            if (sp != std::string::npos) {
                std::string className = trim(rest.substr(sp + 1));
                auto        it        = classDefs.find(className);
                if (it != classDefs.end()) {
                    std::string ids = trim(rest.substr(0, sp));
                    // 空格/逗号分割的节点列表
                    size_t pos = 0;
                    while (pos < ids.size()) {
                        while (pos < ids.size() &&
                               (ids[pos] == ' ' || ids[pos] == ','))
                            pos++;
                        size_t end = pos;
                        while (end < ids.size() && ids[end] != ' ' &&
                               ids[end] != ',')
                            end++;
                        if (end > pos) {
                            std::string nid = ids.substr(pos, end - pos);
                            Node&       n   = g.ensureNode(nid);
                            if (!it->second.first.empty())
                                n.fillColor = it->second.first;
                            if (!it->second.second.empty())
                                n.strokeColor = it->second.second;
                        }
                        pos = end;
                    }
                }
            }
            continue;
        }
        // style 指令：直接对指定节点设置颜色
        if (startsWith(line, "style ")) {
            std::string rest = trim(line.substr(6));
            size_t      sp   = rest.find(' ');
            if (sp != std::string::npos) {
                std::string nid    = trim(rest.substr(0, sp));
                std::string style  = rest.substr(sp + 1);
                auto        colors = parseStyleColors(style);
                Node&       n      = g.ensureNode(nid);
                if (!colors.first.empty())
                    n.fillColor = colors.first;
                if (!colors.second.empty())
                    n.strokeColor = colors.second;
            }
            continue;
        }
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
            // 先处理 -- "label" --> 或 -- "label" --- 语法
            std::string elabel;
            {
                size_t tmpPos = pos;
                while (tmpPos < line.size() &&
                       isspace((unsigned char)line[tmpPos]))
                    tmpPos++;
                if (tmpPos + 3 < line.size() &&
                    line.compare(tmpPos, 3, "-- ") == 0 &&
                    line[tmpPos + 3] == '"') {
                    size_t closeQ = line.find('"', tmpPos + 4);
                    if (closeQ != std::string::npos) {
                        elabel =
                            line.substr(tmpPos + 4, closeQ - tmpPos - 4);
                        pos = closeQ + 1;  // 跳过 -- "label"
                    }
                }
            }
            std::string style, arrow;
            if (!detail::readArrow(line, pos, style, arrow))
                break;
            // 可选边标签 |label|（若上面没提取到 -- "label" 标签）
            while (pos < line.size() && isspace((unsigned char)line[pos]))
                pos++;
            if (elabel.empty() && pos < line.size() && line[pos] == '|') {
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
    // 回写 linkStyle 缓存的边描边色（SIZE_MAX 表示 default → 全部边）
    for (auto& ps : pendingLinkStyles) {
        if (ps.first == static_cast<size_t>(-1)) {
            for (auto& e : g.edges)
                e.strokeColor = ps.second;
        }
        else if (ps.first < g.edges.size()) {
            g.edges[ps.first].strokeColor = ps.second;
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

// parseMermaidClass: 解析 classDiagram 语法 -> 统一 Graph 模型
// 关键步骤：解析 class 块提取属性和方法 -> 解析关系箭头（继承/组合/聚合/关联）
// -> 节点存 attrs，边存关系类型标签
inline Graph parseMermaidClass(const std::vector<std::string>& lines,
                               size_t                          first)
{
    Graph g;
    g.type = "classDiagram";
    std::string curClass;
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        // 注解：<<interface>> / <<abstract>>
        if (startsWith(line, "<<") && line.find(">>") != std::string::npos) {
            if (!curClass.empty()) {
                Node* n = g.findNode(curClass);
                if (n)
                    n->attrs.push_back(line);
            }
            continue;
        }
        // class 块起始
        if (startsWith(line, "class ")) {
            std::string rest = trim(line.substr(6));
            // 去掉末尾的 {
            if (!rest.empty() && rest.back() == '{')
                rest = trim(rest.substr(0, rest.size() - 1));
            curClass                               = rest;
            g.ensureNode(curClass, curClass).shape = "rect";
            continue;
        }
        // class 块结束
        if (line == "}") {
            curClass.clear();
            continue;
        }
        // 成员/方法（在 class 块内）
        if (!curClass.empty()) {
            Node& n = g.ensureNode(curClass);
            n.attrs.push_back(line);
            continue;
        }
        // 关系语句：支持 <|-- *-- o-- --> -- ..> ..|> <-->
        std::string relLabel;
        size_t      colon = line.find(':');
        if (colon != std::string::npos) {
            relLabel = trim(line.substr(colon + 1));
            if (relLabel.size() >= 2 && relLabel.front() == '"' &&
                relLabel.back() == '"')
                relLabel = relLabel.substr(1, relLabel.size() - 2);
            line = trim(line.substr(0, colon));
        }
        // 尝试匹配各种关系箭头
        struct RelArrow
        {
            const char* tok;
            const char* name;
        };
        static const RelArrow arrows[] = {
            {"<|--", "inheritance"},
            {"--|>", "inheritance"},
            {"*--", "composition"},
            {"--*", "composition"},
            {"o--", "aggregation"},
            {"--o", "aggregation"},
            {"<-->", "bidirectional"},
            {"-->", "association"},
            {"--", "link"},
            {"..|>", "realization"},
            {"<|..", "realization"},
            {"..>", "dependency"},
            {"<..", "dependency"},
            {"..", "dotted"},
        };
        for (auto& ar : arrows) {
            size_t ap = line.find(ar.tok);
            if (ap != std::string::npos) {
                std::string left  = trim(line.substr(0, ap));
                size_t      after = ap + strlen(ar.tok);
                std::string right = trim(line.substr(after));
                if (!left.empty() && !right.empty()) {
                    g.ensureNode(left).shape  = "rect";
                    g.ensureNode(right).shape = "rect";
                    std::string label =
                        relLabel.empty() ? std::string(ar.name) : relLabel;
                    g.addEdge(left, right, label, "solid",
                              std::string(ar.name) == "bidirectional" ?
                                  "both" :
                                  "arrow");
                }
                break;
            }
        }
    }
    return g;
}

// parseMermaidState: 解析 stateDiagram-v2 语法 -> 统一 Graph 模型
// 关键步骤：解析 state 块 -> 处理复合状态 -> 解析转移/守卫/动作
inline Graph parseMermaidState(const std::vector<std::string>& lines,
                               size_t                          first)
{
    Graph g;
    g.type = "stateDiagram";
    std::vector<std::string> stateStack;  // 复合状态嵌套栈
    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        // 复合状态: state "Name" as id {
        if (startsWith(line, "state ")) {
            std::string rest = trim(line.substr(6));
            std::string sid, slabel;
            // 检查是否有 { 结尾
            bool hasBlock = !rest.empty() && rest.back() == '{';
            if (hasBlock)
                rest = trim(rest.substr(0, rest.size() - 1));
            // 检查 "label" as id 语法
            size_t asPos = rest.find(" as ");
            if (asPos != std::string::npos) {
                slabel = trim(rest.substr(0, asPos));
                if (slabel.size() >= 2 && slabel.front() == '"' &&
                    slabel.back() == '"')
                    slabel = slabel.substr(1, slabel.size() - 2);
                sid = trim(rest.substr(asPos + 4));
            }
            else {
                sid    = rest;
                slabel = rest;
                if (slabel.size() >= 2 && slabel.front() == '"' &&
                    slabel.back() == '"')
                    slabel = slabel.substr(1, slabel.size() - 2);
            }
            if (sid.empty()) {
                sid = "s" + std::to_string(g.nodes.size() + 1);
            }
            Node& n = g.ensureNode(sid, slabel);
            n.shape = hasBlock ? "group" : "round";
            if (!stateStack.empty())
                n.parent = stateStack.back();
            if (hasBlock)
                stateStack.push_back(sid);
            continue;
        }
        // 复合状态结束
        if (line == "}" && !stateStack.empty()) {
            stateStack.pop_back();
            continue;
        }
        // 状态描述（note-like）
        if (startsWith(line, "note ") || startsWith(line, "--")) {
            continue;
        }
        // 转移: [*] --> state  或  state --> [*]  或  state --> state
        size_t arrowPos = line.find("-->");
        if (arrowPos == std::string::npos)
            arrowPos = line.find("->");
        if (arrowPos != std::string::npos) {
            int alen =
                (arrowPos + 3 < line.size() && line[arrowPos + 2] == '>') ? 3 :
                                                                            2;
            std::string left  = trim(line.substr(0, arrowPos));
            std::string right = trim(line.substr(arrowPos + alen));
            std::string elabel;
            // 提取 : label 部分
            size_t colonPos = right.find(':');
            if (colonPos != std::string::npos) {
                elabel = trim(right.substr(colonPos + 1));
                right  = trim(right.substr(0, colonPos));
            }
            if (elabel.size() >= 2 && elabel.front() == '"' &&
                elabel.back() == '"')
                elabel = elabel.substr(1, elabel.size() - 2);
            if (!left.empty() && !right.empty()) {
                // [*] 是特殊起始/终止状态
                if (left != "[*]") {
                    Node& n = g.ensureNode(left);
                    if (n.shape.empty() || n.shape == "rect")
                        n.shape = "round";
                    if (!stateStack.empty() && n.parent.empty())
                        n.parent = stateStack.back();
                }
                if (right != "[*]") {
                    Node& n = g.ensureNode(right);
                    if (n.shape.empty() || n.shape == "rect")
                        n.shape = "round";
                    if (!stateStack.empty() && n.parent.empty())
                        n.parent = stateStack.back();
                }
                g.addEdge(left, right, elabel, "solid", "arrow");
            }
        }
    }
    return g;
}

// parseMermaidRequirement: 解析 requirementDiagram
// 关键步骤：解析 requirement/element 定义 -> 节点 -> 解析关系 -> edges
// 同时将完整结构存入 Graph.properties["requirementDiagram"]
inline Graph parseMermaidRequirement(const std::vector<std::string>& lines,
                                     size_t                          first)
{
    Graph g;
    g.type = "requirementDiagram";

    Json rd   = Json::obj();
    Json els  = Json::arr();
    Json rels = Json::arr();

    std::string curBlockId;    // 当前正在解析的元素 id
    std::string curBlockType;  // requirement | element 等
    Json        curBlock = Json::obj();

    auto flushBlock = [&]() {
        if (!curBlockId.empty()) {
            curBlock.set("id", curBlockId);
            curBlock.set("type", curBlockType);
            els.push(curBlock);
            curBlockId.clear();
            curBlock = Json::obj();
        }
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明行
        if (startsWith(lower, "requirementdiagram"))
            continue;

        // 检测关系行：需要包含 " - " 和 " -> "
        // 形如：req1 - traces -> req2 或  elem1 <- satisfies - req1
        bool hasArrow = (line.find(" -> ") != std::string::npos ||
                         line.find(" -&gt; ") != std::string::npos);
        bool hasDash  = (line.find(" - ") != std::string::npos);
        if (hasDash && hasArrow) {
            flushBlock();

            // 解析关系
            std::string left, right, relType;

            // 检测逆序：dest <- type - src
            size_t larr = line.find(" <- ");
            size_t rarr = line.find(" -&gt; ");
            if (rarr == std::string::npos)
                rarr = line.find(" -> ");
            if (larr != std::string::npos) {
                // 逆序: dest <- relType - src
                size_t dashAfter = line.find(" - ", larr + 4);
                if (dashAfter != std::string::npos) {
                    right   = trim(line.substr(0, larr));
                    relType = trim(line.substr(larr + 4, dashAfter - larr - 4));
                    left    = trim(line.substr(dashAfter + 3));
                }
            }
            else if (rarr != std::string::npos) {
                // 正序: src - relType -> dest
                size_t dashBefore = line.rfind(" - ", rarr);
                if (dashBefore != std::string::npos) {
                    left    = trim(line.substr(0, dashBefore));
                    relType = trim(
                        line.substr(dashBefore + 3, rarr - dashBefore - 3));
                    right = trim(line.substr(rarr + 4));
                }
            }

            if (!left.empty() && !right.empty() && !relType.empty()) {
                // 创建节点（如果还不存在）-> 用于 tools 编辑
                g.ensureNode(left, left);
                g.ensureNode(right, right);

                // 存入 properties
                Json rel = Json::obj();
                rel.set("from", left);
                rel.set("to", right);
                rel.set("type", relType);
                rels.push(rel);

                // 同步到 edges（关系类型存入 label）
                g.addEdge(left, right, relType, "solid", "arrow");
            }
            continue;
        }

        // 检测 requirement/element 定义行：type name { 或 type name {
        // requirementDiagram 中 requirement/element 关键字开始一个新块
        bool        isReqDecl = false;
        std::string blockType;
        size_t      declEnd = 0;

        // 检查所有已知的需求/元素类型关键字
        static const std::vector<std::string> reqKeywords = {
            "requirement ",
            "functionalrequirement ",
            "performancerequirement ",
            "interfacerequirement ",
            "physicalrequirement ",
            "designconstraint ",
            "element "};
        for (auto& kw : reqKeywords) {
            if (startsWith(lower, kw)) {
                // 找到原始大小写的关键字
                size_t      kwLen  = kw.size();
                std::string origKw = line.substr(0, kwLen - 1);  // 去掉末尾空格
                blockType          = origKw;
                isReqDecl          = true;
                declEnd            = kwLen;
                break;
            }
        }

        if (isReqDecl) {
            flushBlock();
            curBlockType = blockType;

            // 提取名称：在关键字之后、{ 之前
            std::string rest  = trim(line.substr(declEnd));
            size_t      brace = rest.find('{');
            if (brace != std::string::npos) {
                curBlockId = trim(rest.substr(0, brace));
            }
            else {
                curBlockId = rest;  // 可能是无块体的声明
                if (!curBlockId.empty()) {
                    // 无块体时立即创建
                    curBlock.set("id", curBlockId);
                    curBlock.set("type", curBlockType);
                    els.push(curBlock);
                    curBlockId.clear();
                    curBlock = Json::obj();
                }
            }
            continue;
        }

        // 块体内部属性行
        if (!curBlockId.empty()) {
            if (line == "}") {
                flushBlock();
                continue;
            }

            // 解析属性：key: value 或 key: "value"
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string val = trim(line.substr(colon + 1));
                // 去掉引号
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                    val = val.substr(1, val.size() - 2);
                curBlock.set(key, val);
            }
        }
    }
    flushBlock();  // 处理最后一个块

    rd.set("elements", els);
    rd.set("relations", rels);
    g.properties.set("requirementDiagram", rd);

    return g;
}

// parseMermaidSankey: 解析 sankey-beta 加权流图
// 关键步骤：逐行解析 CSV "source,target,value" -> flows -> 节点自动推导
// 同时将完整结构存入 Graph.properties["sankey"]
inline Graph parseMermaidSankey(const std::vector<std::string>& lines,
                                size_t                          first)
{
    Graph g;
    g.type = "sankey";

    Json                  sk         = Json::obj();
    Json                  flows      = Json::arr();
    bool                  showValues = false;
    std::set<std::string> nodeSet;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明行
        if (startsWith(lower, "sankey-beta") || startsWith(lower, "sankey"))
            continue;

        // 配置行
        if (lower == "showvalues") {
            showValues = true;
            continue;
        }
        if (startsWith(lower, "linkcolor") ||
            startsWith(lower, "nodealignment"))
            continue;  // 渲染配置，跳过

        // 数据行: source,target,value
        // 简单分割：按逗号拆分为 3 部分
        size_t c1 = line.find(',');
        if (c1 == std::string::npos)
            continue;
        size_t c2 = line.find(',', c1 + 1);
        if (c2 == std::string::npos) {
            // 可能只有两列？sankey 必须是三列
            continue;
        }

        std::string source = trim(line.substr(0, c1));
        std::string target = trim(line.substr(c1 + 1, c2 - c1 - 1));
        std::string valStr = trim(line.substr(c2 + 1));
        double      value  = 0;
        try {
            value = std::stod(valStr);
        }
        catch (...) {
            continue;
        }

        if (source.empty() || target.empty())
            continue;

        // 记录节点
        nodeSet.insert(source);
        nodeSet.insert(target);

        // 存入 properties
        Json flow = Json::obj();
        flow.set("from", source);
        flow.set("to", target);
        flow.set("value", value);
        flows.push(flow);

        // 同步到 Graph（加权边 + 自动节点）
        g.ensureNode(source, source);
        g.ensureNode(target, target);
        // label 存为 value 的字符串形式，方便编辑工具展示
        g.addEdge(source, target, valStr, "solid", "arrow");
    }

    sk.set("showValues", showValues);
    sk.set("flows", flows);
    g.properties.set("sankey", sk);

    return g;
}

// parseMermaidSequence: 解析 sequenceDiagram
// 关键步骤：participant/actor -> 节点, 消息 -> edges, loop/alt 等片段 -> group
// 节点 将完整结构化数据存入 Graph.properties["sequence"]
inline Graph parseMermaidSequence(const std::vector<std::string>& lines,
                                  size_t                          first)
{
    Graph g;
    g.type = "sequenceDiagram";

    Json seq        = Json::obj();
    Json parts      = Json::arr();  // participants
    Json msgs       = Json::arr();  // messages
    Json frags      = Json::arr();  // fragments (loop/alt/opt/par)
    Json notes      = Json::arr();  // notes
    bool autonumber = false;

    // 消息箭头类型解析
    auto parseArrow = [](const std::string& arrow, bool& isAsync,
                         bool& isReturn, std::string& headEnd) {
        isAsync  = false;
        isReturn = false;
        headEnd  = "arrow";
        // ->> 同步实心箭头
        // -->> 异步实心箭头
        // ->  同步无激活
        // --> 异步无激活
        // -x  带叉
        // --x 带叉虚线
        // -)  带圆
        // --) 带圆异步
        if (arrow.find(">>") != std::string::npos) {
            isAsync = true;
            headEnd = "arrow";
        }
        else if (arrow.find("->") != std::string::npos) {
            headEnd = "arrow";
        }
        else if (arrow.find("-x") != std::string::npos) {
            headEnd = "cross";
        }
        else if (arrow.find("-)") != std::string::npos) {
            headEnd = "open";
        }
        else {
            headEnd = "arrow";
        }
        if (arrow.find("--") == 0)
            isReturn = true;
        if (arrow.find(">>") == arrow.size() - 2)
            isAsync = true;
    };

    int seqNum = 0;
    // 片段栈：存储当前嵌套的 fragment
    struct FragState
    {
        std::string type;
        std::string label;
        Json        innerMsgs = Json::arr();
    };
    std::vector<FragState> fragStack;

    auto flushFrag = [&](FragState& fs) {
        Json frag = Json::obj();
        frag.set("type", fs.type);
        frag.set("label", fs.label);
        frag.set("messages", fs.innerMsgs);
        frags.push(frag);
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明
        if (startsWith(lower, "sequencediagram"))
            continue;

        // autonumber
        if (lower == "autonumber") {
            autonumber = true;
            continue;
        }

        // participant / actor 声明
        // 格式：participant/actor Name [as Alias]
        if (startsWith(lower, "participant ") || startsWith(lower, "actor ")) {
            bool        isActor = startsWith(lower, "actor ");
            std::string rest    = trim(line.substr(isActor ? 6 : 12));

            // 解析 "Name as Alias" 或 just "Name"
            std::string id, label;
            size_t      asPos = toLower(rest).find(" as ");
            if (asPos != std::string::npos) {
                label = trim(rest.substr(0, asPos));
                id    = trim(rest.substr(asPos + 4));
            }
            else {
                id = label = rest;
            }

            // 去掉引号
            if (id.size() >= 2 && id.front() == '"' && id.back() == '"')
                id = id.substr(1, id.size() - 2);
            if (label.size() >= 2 && label.front() == '"' &&
                label.back() == '"')
                label = label.substr(1, label.size() - 2);

            Json p = Json::obj();
            p.set("id", id);
            p.set("label", label);
            p.set("type", isActor ? "actor" : "participant");
            p.set("order", (double)(parts.a ? parts.a->size() : 0));
            parts.push(p);
            continue;
        }

        // Note 声明
        // Note left of|right of|over id1[,id2]: text
        if (startsWith(lower, "note ")) {
            std::string rest = trim(line.substr(5));
            std::string placement, targets;
            size_t      colonPos = rest.find(':');
            std::string noteText;
            if (colonPos != std::string::npos) {
                noteText = trim(rest.substr(colonPos + 1));
                rest     = trim(rest.substr(0, colonPos));
            }
            // "left of Alice", "right of Bob", "over Alice,Bob"
            size_t ofPos = toLower(rest).find(" of ");
            if (ofPos != std::string::npos) {
                placement = trim(rest.substr(0, ofPos));
                targets   = trim(rest.substr(ofPos + 4));
            }

            Json nt = Json::obj();
            nt.set("placement", placement);
            nt.set("text", noteText);
            // 多个目标用逗号分隔
            Json tgtArr = Json::arr();
            if (!targets.empty()) {
                size_t pos = 0;
                while (pos < targets.size()) {
                    size_t      comma = targets.find(',', pos);
                    std::string t     = trim(targets.substr(
                        pos, comma == std::string::npos ? std::string::npos :
                                                          comma - pos));
                    if (!t.empty())
                        tgtArr.push(Json(t));
                    if (comma == std::string::npos)
                        break;
                    pos = comma + 1;
                }
            }
            nt.set("targets", tgtArr);
            notes.push(nt);
            continue;
        }

        // activate / deactivate
        if (startsWith(lower, "activate ") ||
            startsWith(lower, "deactivate ")) {
            // 简单跳过：这些会在导出时保留在 rawMermaid 或作为消息属性
            // 当前只做基本存储
            continue;
        }

        // Fragment 开始：loop, alt, opt, par, critical, break, rect
        bool        isFragStart = false;
        std::string fragType;
        std::string fragLabel;
        for (auto& kw : {"loop ", "alt ", "opt ", "par ", "critical ", "break ",
                         "rect ", "else ", "and ", "option "}) {
            if (startsWith(lower, kw)) {
                isFragStart = true;
                std::string kwStr(kw);
                fragType  = trim(kwStr);  // 去掉末尾空格
                fragLabel = trim(line.substr(strlen(kw) - 1));
                break;
            }
        }

        if (isFragStart) {
            // 注意：else/and/option 是现有片段的延续
            if (fragType == "else" || fragType == "and" ||
                fragType == "option") {
                // 结束当前子片段，开始新的子片段
                if (!fragStack.empty()) {
                    flushFrag(fragStack.back());
                    fragStack.back().innerMsgs = Json::arr();
                    fragStack.back().label =
                        fragStack.back().label +
                        (fragStack.back().label.empty() ? "" : ", ") +
                        fragType + " " + fragLabel;
                }
                continue;
            }
            // 新片段
            FragState fs;
            fs.type  = fragType;
            fs.label = fragLabel;
            fragStack.push_back(fs);
            continue;
        }

        // Fragment 结束
        if (lower == "end") {
            if (!fragStack.empty()) {
                // 检查是否是嵌套的 rect/rgb 等
                if (fragStack.size() > 1) {
                    FragState inner = fragStack.back();
                    fragStack.pop_back();
                    Json frag = Json::obj();
                    frag.set("type", inner.type);
                    frag.set("label", inner.label);
                    frag.set("messages", inner.innerMsgs);
                    fragStack.back().innerMsgs.push(frag);
                }
                else {
                    FragState fs = fragStack.back();
                    fragStack.pop_back();
                    flushFrag(fs);
                }
            }
            continue;
        }

        // 消息行：检测箭头模式
        // 模式：Actor Arrow Actor : Label
        // Arrow: ->>, -->>, ->, -->, -x, --x, -), --)
        std::string arrowPattern;
        size_t      arrowPos = std::string::npos;
        // 完整箭头列表：按长度降序排列避免短前缀误匹配长箭头
        for (auto& pat : {">>", "->", "-x", "-)"}) {
            // 先尝试双横线版本
            std::string dbl = std::string("--") + pat;
            size_t      p   = line.find(dbl);
            if (p != std::string::npos) {
                arrowPos     = p;
                arrowPattern = dbl;
                break;
            }
            // 再尝试单横线版本
            std::string sgl = std::string("-") + pat;
            p               = line.find(sgl);
            if (p != std::string::npos) {
                arrowPos     = p;
                arrowPattern = sgl;
                break;
            }
        }

        if (arrowPos != std::string::npos && !arrowPattern.empty()) {
            // 提取消息标签（在冒号之后）
            std::string msgLabel;
            size_t colonPos = line.find(':', arrowPos + arrowPattern.size());
            if (colonPos != std::string::npos)
                msgLabel = trim(line.substr(colonPos + 1));

            // 提取 from 和 to
            std::string fromId = trim(line.substr(0, arrowPos));
            std::string toId   = trim(
                line.substr(arrowPos + arrowPattern.size(),
                            colonPos == std::string::npos ?
                                std::string::npos :
                                colonPos - arrowPos - arrowPattern.size()));

            // 去掉引号
            auto unquote = [](std::string& s) {
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                    s = s.substr(1, s.size() - 2);
            };
            unquote(fromId);
            unquote(toId);
            unquote(msgLabel);

            // 解析箭头语义
            bool        isAsync = false, isReturn = false;
            std::string headEnd = "arrow";
            parseArrow(arrowPattern, isAsync, isReturn, headEnd);

            seqNum++;
            Json msg = Json::obj();
            msg.set("from", fromId);
            msg.set("to", toId);
            msg.set("label", msgLabel);
            msg.set("type", isAsync ? "async" : (isReturn ? "return" : "sync"));
            msg.set("seqNum", (double)seqNum);
            msg.set("headEnd", headEnd);
            msg.set("isReturn", isReturn);

            if (fragStack.empty()) {
                msgs.push(msg);
            }
            else {
                fragStack.back().innerMsgs.push(msg);
            }
        }
    }
    // 处理未关闭的片段
    while (!fragStack.empty()) {
        FragState fs = fragStack.back();
        fragStack.pop_back();
        if (!fragStack.empty()) {
            Json frag = Json::obj();
            frag.set("type", fs.type);
            frag.set("label", fs.label);
            frag.set("messages", fs.innerMsgs);
            fragStack.back().innerMsgs.push(frag);
        }
        else {
            flushFrag(fs);
        }
    }

    seq.set("autonumber", autonumber);
    seq.set("participants", parts);
    seq.set("messages", msgs);
    seq.set("fragments", frags);
    if (notes.a && !notes.a->empty())
        seq.set("notes", notes);
    g.properties.set("sequence", seq);

    // 不填充 nodes/edges（序列图不是图模型）

    return g;
}

// parseMermaidGantt: 解析 gantt 甘特图
// 关键步骤：section/task -> patterns -> Graph.properties["gantt"]
inline Graph parseMermaidGantt(const std::vector<std::string>& lines,
                               size_t                          first)
{
    Graph g;
    g.type = "gantt";

    Json        gantt      = Json::obj();
    Json        sections   = Json::arr();
    Json        curSection = Json::obj();
    Json        curTasks   = Json::arr();
    std::string curSectionName;
    std::string title, dateFormat = "YYYY-MM-DD";

    auto flushSection = [&]() {
        if (!curSectionName.empty() || (curTasks.a && curTasks.a->size() > 0)) {
            curSection.set("name", curSectionName);
            curSection.set("tasks", curTasks);
            sections.push(curSection);
            curSection = Json::obj();
            curTasks   = Json::arr();
        }
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明
        if (lower == "gantt")
            continue;

        // 配置指令
        if (startsWith(lower, "dateformat ")) {
            dateFormat = trim(line.substr(11));
            continue;
        }
        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        if (startsWith(lower, "axisformat ") ||
            startsWith(lower, "excludes ") || startsWith(lower, "weekend ") ||
            startsWith(lower, "tickinterval ")) {
            continue;  // 渲染配置，暂不解析
        }

        // section 行
        if (startsWith(lower, "section ")) {
            flushSection();
            curSectionName = trim(line.substr(8));
            curTasks       = Json::arr();
            continue;
        }

        // task 行：label : [tags,] [id,] startSpec, endSpec
        size_t colon = line.find(':');
        if (colon != std::string::npos && curSection.o) {
            std::string label = trim(line.substr(0, colon));
            std::string rest  = trim(line.substr(colon + 1));

            Json task = Json::obj();
            task.set("label", label);

            // 分割剩余的逗号分隔字段
            std::vector<std::string> parts;
            {
                size_t pos = 0;
                while (pos < rest.size()) {
                    size_t      comma = rest.find(',', pos);
                    std::string part  = trim(rest.substr(
                        pos, comma == std::string::npos ? std::string::npos :
                                                          comma - pos));
                    if (!part.empty())
                        parts.push_back(part);
                    if (comma == std::string::npos)
                        break;
                    pos = comma + 1;
                }
            }

            size_t pi = 0;
            // 检测 tags（done, active, crit, milestone）
            if (pi < parts.size()) {
                std::string pLower = toLower(parts[pi]);
                if (pLower == "done" || pLower == "active" ||
                    pLower == "crit" || pLower == "milestone") {
                    task.set("status", pLower);
                    pi++;
                }
            }
            // 检测 id（不是日期格式、不是 after/until、不是 duration）
            if (pi < parts.size()) {
                std::string& p = parts[pi];
                // id 通常是简短单词，不是日期或时间段
                if (!p.empty() &&
                    p.find_first_of("0123456789") == std::string::npos &&
                    !startsWith(toLower(p), "after ") &&
                    !startsWith(toLower(p), "until ")) {
                    task.set("id", p);
                    pi++;
                }
            }
            // start
            if (pi < parts.size()) {
                task.set("start", parts[pi]);
                pi++;
            }
            // end/duration
            if (pi < parts.size()) {
                task.set("end", parts[pi]);
                pi++;
            }
            // after/until 依赖
            for (size_t k = 0; k < parts.size(); k++) {
                std::string lowerP = toLower(parts[k]);
                if (startsWith(lowerP, "after ")) {
                    task.set("after", trim(parts[k].substr(6)));
                }
            }

            curTasks.push(task);
        }
    }
    flushSection();

    gantt.set("title", title);
    gantt.set("dateFormat", dateFormat);
    gantt.set("sections", sections);
    g.properties.set("gantt", gantt);

    return g;
}

// parseMermaidPie: 解析 pie 饼图
// 关键步骤：title/数据行 -> entries -> Graph.properties["pie"]
inline Graph parseMermaidPie(const std::vector<std::string>& lines,
                             size_t                          first)
{
    Graph g;
    g.type = "pie";

    Json        pie     = Json::obj();
    Json        entries = Json::arr();
    std::string title;
    bool        showData = false;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明（单纯的 "pie" 行）
        if (lower == "pie")
            continue;
        if (startsWith(lower, "pie title ")) {
            title = trim(line.substr(10));
            continue;
        }
        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        if (lower == "showdata") {
            showData = true;
            continue;
        }

        // 数据行："Label" : value  或  "Label" : value
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string label  = trim(line.substr(0, colon));
            std::string valStr = trim(line.substr(colon + 1));

            // 去掉引号
            if (label.size() >= 2 && label.front() == '"' &&
                label.back() == '"')
                label = label.substr(1, label.size() - 2);

            double value = 0;
            try {
                value = std::stod(valStr);
            }
            catch (...) {
                continue;
            }

            Json entry = Json::obj();
            entry.set("label", label);
            entry.set("value", value);
            entries.push(entry);
        }
    }

    if (!title.empty())
        pie.set("title", title);
    pie.set("showData", showData);
    pie.set("entries", entries);
    g.properties.set("pie", pie);

    return g;
}

// parseMermaidKanban: 解析 kanban 看板
inline Graph parseMermaidKanban(const std::vector<std::string>& lines,
                                size_t                          first)
{
    Graph g;
    g.type               = "kanban";
    Json        kb       = Json::obj();
    Json        cols     = Json::arr();
    Json        curCol   = Json::obj();
    Json        curCards = Json::arr();
    std::string curColId, curColTitle;

    auto flushCol = [&]() {
        if (!curColId.empty()) {
            curCol.set("id", curColId);
            curCol.set("title", curColTitle);
            curCol.set("cards", curCards);
            cols.push(curCol);
            curCol   = Json::obj();
            curCards = Json::arr();
        }
    };

    // 计算每行的前导空白长度
    auto indentLen = [](const std::string& s) -> int {
        int n = 0;
        for (char c : s) {
            if (c == ' ')
                n++;
            else if (c == '\t')
                n += 2;
            else
                break;
        }
        return n;
    };

    int colIndent = -1;

    for (size_t li = first; li < lines.size(); li++) {
        std::string rawLine = lines[li];
        std::string line    = trim(rawLine);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (lower == "kanban")
            continue;

        // 配置行
        if (startsWith(lower, "ticketbaseurl "))
            continue;

        // 列定义：id[Title]  或  [Title]
        size_t brack      = line.find('[');
        size_t closeBrack = line.find(']');
        int    indent     = indentLen(rawLine);

        // 列：缩进较浅或有括号且非明显卡片缩进
        if (brack != std::string::npos && closeBrack != std::string::npos) {
            if (colIndent < 0)
                colIndent = indent;
            bool isCard = (colIndent >= 0 && indent > colIndent);

            if (!isCard) {
                flushCol();
                curColId    = trim(line.substr(0, brack));
                curColTitle = line.substr(brack + 1, closeBrack - brack - 1);
                curCards    = Json::arr();
                colIndent   = indent;
                continue;
            }
        }

        // 卡片行
        if (brack != std::string::npos && closeBrack != std::string::npos) {
            std::string tline = trim(line);
            brack             = tline.find('[');
            closeBrack        = tline.find(']');
            if (brack != std::string::npos && closeBrack != std::string::npos) {
                Json        card = Json::obj();
                std::string cid  = trim(tline.substr(0, brack));
                std::string ctitle =
                    tline.substr(brack + 1, closeBrack - brack - 1);
                card.set("id", cid);
                card.set("title", ctitle);

                // 解析元数据：@{ key: 'value', ... }
                size_t atPos = tline.find("@{");
                if (atPos != std::string::npos) {
                    size_t endPos = tline.find('}', atPos);
                    if (endPos != std::string::npos) {
                        std::string meta =
                            tline.substr(atPos + 2, endPos - atPos - 2);
                        // 简单分割逗号分隔的 key: 'value' 对
                        size_t mp = 0;
                        while (mp < meta.size()) {
                            size_t      comma = meta.find(',', mp);
                            std::string pair  = trim(
                                meta.substr(mp, comma == std::string::npos ?
                                                    std::string::npos :
                                                    comma - mp));
                            size_t kvColon = pair.find(':');
                            if (kvColon != std::string::npos) {
                                std::string k = trim(pair.substr(0, kvColon));
                                std::string v = trim(pair.substr(kvColon + 1));
                                if (v.size() >= 2 && v.front() == '\'' &&
                                    v.back() == '\'')
                                    v = v.substr(1, v.size() - 2);
                                if (v.size() >= 2 && v.front() == '"' &&
                                    v.back() == '"')
                                    v = v.substr(1, v.size() - 2);
                                card.set(k, v);
                            }
                            if (comma == std::string::npos)
                                break;
                            mp = comma + 1;
                        }
                    }
                }
                curCards.push(card);
            }
        }
    }
    flushCol();
    kb.set("columns", cols);
    g.properties.set("kanban", kb);
    return g;
}

// parseMermaidGit: 解析 gitGraph
inline Graph parseMermaidGit(const std::vector<std::string>& lines,
                             size_t                          first)
{
    Graph g;
    g.type                          = "gitGraph";
    Json                  gg        = Json::obj();
    Json                  commits   = Json::arr();
    Json                  branches  = Json::arr();
    std::string           curBranch = "main";
    int                   order     = 0;
    std::set<std::string> branchSet;
    branchSet.insert("main");

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (lower == "gitgraph")
            continue;

        // branch 声明
        if (startsWith(lower, "branch ")) {
            std::string bn = trim(line.substr(7));
            curBranch      = bn;
            if (branchSet.insert(bn).second) {
                Json br = Json::obj();
                br.set("name", bn);
                br.set("order", (double)branches.a->size());
                branches.push(br);
            }
            continue;
        }
        // checkout
        if (startsWith(lower, "checkout ")) {
            curBranch = trim(line.substr(9));
            if (branchSet.insert(curBranch).second) {
                Json br = Json::obj();
                br.set("name", curBranch);
                br.set("order", (double)branches.a->size());
                branches.push(br);
            }
            continue;
        }
        // merge
        if (startsWith(lower, "merge ")) {
            std::string rest = trim(line.substr(6));
            Json        cm   = Json::obj();
            cm.set("branch", curBranch);
            cm.set("type", "MERGE");
            cm.set("label", rest);
            cm.set("order", (double)(++order));
            commits.push(cm);
            continue;
        }
        // cherry-pick
        if (startsWith(lower, "cherry-pick ")) {
            std::string rest = trim(line.substr(12));
            Json        cm   = Json::obj();
            cm.set("branch", curBranch);
            cm.set("type", "CHERRY_PICK");
            cm.set("label", rest);
            cm.set("order", (double)(++order));
            commits.push(cm);
            continue;
        }
        // commit 行：精确匹配 "commit" 开头（避免误匹配 committed 等）
        if (lower == "commit" || startsWith(lower, "commit ")) {
            std::string rest = trim(line.substr(6));
            Json        cm   = Json::obj();
            cm.set("branch", curBranch);
            cm.set("type", "NORMAL");
            cm.set("order", (double)(++order));

            // 解析可选字段：id:"xxx" tag:"xxx" type:HIGHLIGHT
            auto extractQuoted = [](const std::string& s,
                                    const std::string& key) -> std::string {
                size_t p = toLower(s).find(key + ":");
                if (p == std::string::npos)
                    return "";
                p += key.size() + 1;
                if (p >= s.size())
                    return "";
                if (s[p] == '"') {
                    size_t e = s.find('"', p + 1);
                    if (e != std::string::npos)
                        return s.substr(p + 1, e - p - 1);
                }
                // unquoted value
                size_t e = s.find(' ', p);
                return s.substr(p, e == std::string::npos ? std::string::npos :
                                                            e - p);
            };

            std::string cid = extractQuoted(rest, "id");
            std::string tag = extractQuoted(rest, "tag");
            if (!cid.empty())
                cm.set("id", cid);
            if (!tag.empty())
                cm.set("tag", tag);

            std::string label = rest;
            // 去掉已解析的 id/tag
            if (!cid.empty()) {
                size_t pos = lower.find("id:");
                if (pos != std::string::npos)
                    label = trim(rest.substr(0, pos));
            }
            cm.set("label", label);
            commits.push(cm);
            continue;
        }
    }

    gg.set("commits", commits);
    gg.set("branches", branches);
    g.properties.set("gitGraph", gg);
    return g;
}

// parseMermaidJourney: 解析 journey 用户旅程
inline Graph parseMermaidJourney(const std::vector<std::string>& lines,
                                 size_t                          first)
{
    Graph g;
    g.type               = "journey";
    Json        jn       = Json::obj();
    Json        sections = Json::arr();
    Json        curTasks = Json::arr();
    std::string curSection, title;

    auto flushSection = [&]() {
        if (!curSection.empty() || (curTasks.a && curTasks.a->size() > 0)) {
            Json sec = Json::obj();
            sec.set("name", curSection);
            sec.set("tasks", curTasks);
            sections.push(sec);
            curTasks = Json::arr();
        }
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (lower == "journey")
            continue;

        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        if (startsWith(lower, "section ")) {
            flushSection();
            curSection = trim(line.substr(8));
            curTasks   = Json::arr();
            continue;
        }

        // task 行：Task Name: score: actor1, actor2
        size_t c1 = line.find(':');
        if (c1 != std::string::npos) {
            std::string              taskName = trim(line.substr(0, c1));
            std::string              rest     = trim(line.substr(c1 + 1));
            size_t                   c2       = rest.find(':');
            int                      score    = 0;
            std::vector<std::string> actors;
            if (c2 != std::string::npos) {
                try {
                    score = std::stoi(trim(rest.substr(0, c2)));
                }
                catch (...) {
                    score = 0;
                }
                std::string actorStr = trim(rest.substr(c2 + 1));
                // 分割逗号
                size_t ap = 0;
                while (ap < actorStr.size()) {
                    size_t      comma = actorStr.find(',', ap);
                    std::string a     = trim(actorStr.substr(
                        ap, comma == std::string::npos ? std::string::npos :
                                                         comma - ap));
                    if (!a.empty())
                        actors.push_back(a);
                    if (comma == std::string::npos)
                        break;
                    ap = comma + 1;
                }
            }

            Json task = Json::obj();
            task.set("label", taskName);
            task.set("score", (double)score);
            Json actArr = Json::arr();
            for (auto& a : actors)
                actArr.push(Json(a));
            task.set("actors", actArr);
            curTasks.push(task);
        }
    }
    flushSection();

    if (!title.empty())
        jn.set("title", title);
    jn.set("sections", sections);
    g.properties.set("journey", jn);
    return g;
}

// parseMermaidTimeline: 解析 timeline 时间线
inline Graph parseMermaidTimeline(const std::vector<std::string>& lines,
                                  size_t                          first)
{
    Graph g;
    g.type                = "timeline";
    Json        tl        = Json::obj();
    Json        sections  = Json::arr();
    Json        curEvents = Json::arr();
    std::string curSection, title;

    auto flushSection = [&]() {
        if (!curSection.empty() || (curEvents.a && curEvents.a->size() > 0)) {
            Json sec = Json::obj();
            sec.set("name", curSection);
            sec.set("events", curEvents);
            sections.push(sec);
            curEvents = Json::arr();
        }
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (lower == "timeline")
            continue;

        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        if (startsWith(lower, "section ")) {
            flushSection();
            curSection = trim(line.substr(8));
            curEvents  = Json::arr();
            continue;
        }

        // 时间段行：period : event1 [: event2 ...]
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            Json        evt       = Json::obj();
            std::string period    = trim(line.substr(0, colon));
            std::string eventsStr = trim(line.substr(colon + 1));

            evt.set("period", period);
            Json evtList = Json::arr();
            // 分割冒号分隔的多个事件
            size_t ep = 0;
            while (ep < eventsStr.size()) {
                size_t nextColon = eventsStr.find(':', ep);
                // 注意：冒号可能被引号包裹
                std::string e = trim(eventsStr.substr(
                    ep, nextColon == std::string::npos ? std::string::npos :
                                                         nextColon - ep));
                if (!e.empty())
                    evtList.push(Json(e));
                if (nextColon == std::string::npos)
                    break;
                ep = nextColon + 1;
            }
            evt.set("events", evtList);
            curEvents.push(evt);
        }
    }
    flushSection();

    if (!title.empty())
        tl.set("title", title);
    tl.set("sections", sections);
    g.properties.set("timeline", tl);
    return g;
}

// parseMermaidQuadrant: 解析 quadrantChart
inline Graph parseMermaidQuadrant(const std::vector<std::string>& lines,
                                  size_t                          first)
{
    Graph g;
    g.type             = "quadrantChart";
    Json        qc     = Json::obj();
    Json        points = Json::arr();
    std::string title, xLeft, xRight, yBottom, yTop, q1, q2, q3, q4;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);

        if (startsWith(lower, "quadrantchart"))
            continue;
        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        if (startsWith(lower, "x-axis ")) {
            std::string rest  = trim(line.substr(7));
            size_t      arrow = rest.find(" --> ");
            if (arrow == std::string::npos)
                arrow = rest.find(" --&gt; ");
            if (arrow != std::string::npos) {
                xLeft  = trim(rest.substr(0, arrow));
                xRight = trim(rest.substr(
                    arrow +
                    (rest.find("--&gt;") != std::string::npos ? 6 : 5)));
            }
            continue;
        }
        if (startsWith(lower, "y-axis ")) {
            std::string rest  = trim(line.substr(7));
            size_t      arrow = rest.find(" --> ");
            if (arrow == std::string::npos)
                arrow = rest.find(" --&gt; ");
            if (arrow != std::string::npos) {
                yBottom = trim(rest.substr(0, arrow));
                yTop    = trim(rest.substr(
                    arrow +
                    (rest.find("--&gt;") != std::string::npos ? 6 : 5)));
            }
            continue;
        }
        if (startsWith(lower, "quadrant-1 ")) {
            q1 = trim(line.substr(11));
            continue;
        }
        if (startsWith(lower, "quadrant-2 ")) {
            q2 = trim(line.substr(11));
            continue;
        }
        if (startsWith(lower, "quadrant-3 ")) {
            q3 = trim(line.substr(11));
            continue;
        }
        if (startsWith(lower, "quadrant-4 ")) {
            q4 = trim(line.substr(11));
            continue;
        }

        // 数据点：Name: [x, y]
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string pname  = trim(line.substr(0, colon));
            std::string coords = trim(line.substr(colon + 1));
            // 解析 [x, y]
            size_t lb = coords.find('[');
            size_t rb = coords.find(']');
            if (lb != std::string::npos && rb != std::string::npos) {
                std::string inner = coords.substr(lb + 1, rb - lb - 1);
                size_t      comma = inner.find(',');
                if (comma != std::string::npos) {
                    double x = 0, y = 0;
                    try {
                        x = std::stod(trim(inner.substr(0, comma)));
                    }
                    catch (...) {
                    }
                    try {
                        y = std::stod(trim(inner.substr(comma + 1)));
                    }
                    catch (...) {
                    }
                    Json pt = Json::obj();
                    pt.set("label", pname);
                    pt.set("x", x);
                    pt.set("y", y);
                    points.push(pt);
                }
            }
        }
    }

    if (!title.empty())
        qc.set("title", title);
    qc.set("xLeft", xLeft);
    qc.set("xRight", xRight);
    qc.set("yBottom", yBottom);
    qc.set("yTop", yTop);
    if (!q1.empty())
        qc.set("q1", q1);
    if (!q2.empty())
        qc.set("q2", q2);
    if (!q3.empty())
        qc.set("q3", q3);
    if (!q4.empty())
        qc.set("q4", q4);
    qc.set("points", points);
    g.properties.set("quadrantChart", qc);
    return g;
}

// parseMermaidXychart: 解析 xychart-beta
inline Graph parseMermaidXychart(const std::vector<std::string>& lines,
                                 size_t                          first)
{
    Graph g;
    g.type             = "xychart";
    Json        xy     = Json::obj();
    Json        series = Json::arr();
    Json        xAxis  = Json::obj();
    Json        yAxis  = Json::obj();
    std::string title;
    bool        isHorizontal = false;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);

        if (startsWith(lower, "xychart-beta") || lower == "xychart")
            continue;
        if (lower == "horizontal") {
            isHorizontal = true;
            continue;
        }
        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }
        // x-axis
        if (startsWith(lower, "x-axis ")) {
            std::string rest = trim(line.substr(7));
            // categorical: [cat1, cat2, ...] 或 numeric: label min --> max
            if (rest.find('[') != std::string::npos) {
                xAxis.set("type", "categorical");
                Json   cats = Json::arr();
                size_t lb   = rest.find('[');
                size_t rb   = rest.find(']');
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string inner = rest.substr(lb + 1, rb - lb - 1);
                    size_t      cp    = 0;
                    while (cp < inner.size()) {
                        size_t      comma = inner.find(',', cp);
                        std::string cat   = trim(inner.substr(
                            cp, comma == std::string::npos ? std::string::npos :
                                                             comma - cp));
                        if (cat.size() >= 2 && cat.front() == '"' &&
                            cat.back() == '"')
                            cat = cat.substr(1, cat.size() - 2);
                        if (!cat.empty())
                            cats.push(Json(cat));
                        if (comma == std::string::npos)
                            break;
                        cp = comma + 1;
                    }
                }
                xAxis.set("categories", cats);
            }
            else {
                xAxis.set("type", "numeric");
                xAxis.set("label", rest);
            }
            continue;
        }
        // y-axis
        if (startsWith(lower, "y-axis ")) {
            std::string rest = trim(line.substr(7));
            yAxis.set("label", rest);
            continue;
        }
        // bar / line series
        if (startsWith(lower, "bar ") || startsWith(lower, "line ")) {
            bool        isLine = startsWith(lower, "line ");
            std::string rest   = trim(line.substr(isLine ? 5 : 4));
            std::string sname;
            // "name" [values] 或 [values]
            if (!rest.empty() && rest.front() == '"') {
                size_t eq = rest.find('"', 1);
                if (eq != std::string::npos) {
                    sname = rest.substr(1, eq - 1);
                    rest  = trim(rest.substr(eq + 1));
                }
            }
            Json s = Json::obj();
            s.set("type", isLine ? "line" : "bar");
            s.set("label", sname);
            Json   vals = Json::arr();
            size_t lb   = rest.find('[');
            size_t rb   = rest.find(']');
            if (lb != std::string::npos && rb != std::string::npos) {
                std::string inner = rest.substr(lb + 1, rb - lb - 1);
                size_t      vp    = 0;
                while (vp < inner.size()) {
                    size_t      comma = inner.find(',', vp);
                    std::string vs    = trim(inner.substr(
                        vp, comma == std::string::npos ? std::string::npos :
                                                         comma - vp));
                    if (!vs.empty()) {
                        try {
                            vals.push(Json(std::stod(vs)));
                        }
                        catch (...) {
                        }
                    }
                    if (comma == std::string::npos)
                        break;
                    vp = comma + 1;
                }
            }
            s.set("data", vals);
            series.push(s);
            continue;
        }
    }

    if (!title.empty())
        xy.set("title", title);
    if (isHorizontal)
        xy.set("horizontal", true);
    xy.set("xAxis", xAxis);
    xy.set("yAxis", yAxis);
    xy.set("series", series);
    g.properties.set("xychart", xy);
    return g;
}

// parseMermaidBlock: 解析 block-beta
inline Graph parseMermaidBlock(const std::vector<std::string>& lines,
                               size_t                          first)
{
    Graph g;
    g.type       = "block";
    Json bl      = Json::obj();
    Json blocks  = Json::arr();
    Json blEdges = Json::arr();
    int  columns = 0;
    int  col = 0, row = 0;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (startsWith(lower, "block-beta") || lower == "block")
            continue;

        // columns N
        if (startsWith(lower, "columns ")) {
            try {
                columns = std::stoi(trim(line.substr(8)));
            }
            catch (...) {
            }
            continue;
        }
        // style / classDef / class 指令
        if (startsWith(lower, "style ") || startsWith(lower, "classdef ") ||
            startsWith(lower, "class "))
            continue;

        // 边：A --> B 或 A --- B 或 A -- "label" --> B
        if (line.find("--") != std::string::npos &&
            line.find(':') == std::string::npos) {
            // 简单边解析
            bool        directed = (line.find("-->") != std::string::npos ||
                                    line.find("--&gt;") != std::string::npos);
            std::string sep      = directed ? "-->" : "---";
            size_t      sepPos   = line.find(sep);
            if (sepPos == std::string::npos) {
                if (line.find("--&gt;") != std::string::npos) {
                    sep    = "--&gt;";
                    sepPos = line.find(sep);
                }
            }
            if (sepPos != std::string::npos) {
                Json e = Json::obj();
                e.set("from", trim(line.substr(0, sepPos)));
                e.set("to", trim(line.substr(sepPos + sep.size())));
                e.set("directed", directed);
                blEdges.push(e);
            }
            continue;
        }

        // block 定义
        std::string bid, blabel, shape;
        size_t      brack = line.find('[');
        size_t      paren = line.find('(');
        size_t      brace = line.find('{');

        // 提取 block id（到第一个括号或空格之前）
        size_t sp  = line.find(' ');
        size_t br  = line.find_first_of("[({");
        size_t end = std::min(sp, br);
        if (end == std::string::npos)
            end = std::max(sp, br);
        if (end != std::string::npos) {
            bid = trim(line.substr(0, end));
        }
        else {
            bid = line;
        }

        // 提取形状和标签
        if (brack != std::string::npos) {
            size_t cb = line.find(']', brack);
            if (cb != std::string::npos)
                blabel = line.substr(brack + 1, cb - brack - 1);
            shape = "rect";
        }
        if (paren != std::string::npos) {
            size_t cp = line.find(')', paren);
            shape     = "round";
            if (brack == std::string::npos && cp != std::string::npos)
                blabel = line.substr(paren + 1, cp - paren - 1);
        }
        if (brace != std::string::npos) {
            size_t cb2 = line.find('}', brace);
            shape      = "diamond";
            if (brack == std::string::npos && paren == std::string::npos &&
                cb2 != std::string::npos)
                blabel = line.substr(brace + 1, cb2 - brace - 1);
        }

        // 检测 colSpan :N
        int    colSpan = 1;
        size_t csPos   = line.find(":N");
        if (csPos == std::string::npos)
            csPos = line.rfind(':');
        if (csPos != std::string::npos && csPos > 0 &&
            (csPos + 1 < line.size()) &&
            isdigit((unsigned char)line[csPos + 1])) {
            try {
                colSpan = std::stoi(line.substr(csPos + 1));
            }
            catch (...) {
            }
        }

        Json bk = Json::obj();
        bk.set("id", bid);
        bk.set("label", blabel);
        if (!shape.empty())
            bk.set("shape", shape);
        bk.set("col", (double)col);
        bk.set("row", (double)row);
        if (colSpan > 1)
            bk.set("colSpan", (double)colSpan);
        blocks.push(bk);

        col += colSpan;
        if (columns > 0 && col >= columns) {
            col = 0;
            row++;
        }
    }

    bl.set("columns", (double)columns);
    bl.set("blocks", blocks);
    bl.set("edges", blEdges);
    g.properties.set("block", bl);
    return g;
}

// parseMermaidPacket: 解析 packet-beta
inline Graph parseMermaidPacket(const std::vector<std::string>& lines,
                                size_t                          first)
{
    Graph g;
    g.type             = "packet";
    Json        pk     = Json::obj();
    Json        fields = Json::arr();
    std::string title;

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;
        std::string lower = toLower(line);
        if (startsWith(lower, "packet-beta") || lower == "packet")
            continue;

        if (startsWith(lower, "title ")) {
            title = trim(line.substr(6));
            continue;
        }

        // 字段行：bit_range: "label"
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string range = trim(line.substr(0, colon));
            std::string label = trim(line.substr(colon + 1));
            if (label.size() >= 2 && label.front() == '"' &&
                label.back() == '"')
                label = label.substr(1, label.size() - 2);

            Json f = Json::obj();
            f.set("label", label);

            // 解析位范围：N-M 或 Nbits 或 +N (相对) 或 N
            if (range.find('-') != std::string::npos && range[0] != '+') {
                size_t dash = range.find('-');
                try {
                    f.set("start", std::stoi(trim(range.substr(0, dash))));
                    f.set("end", std::stoi(trim(range.substr(dash + 1))));
                }
                catch (...) {
                }
            }
            else if (startsWith(range, "+")) {
                try {
                    f.set("bits", std::stoi(trim(range.substr(1))));
                }
                catch (...) {
                }
            }
            else {
                // Nbits 或 N
                size_t      bpos = toLower(range).find("bit");
                std::string num =
                    (bpos != std::string::npos) ? range.substr(0, bpos) : range;
                try {
                    f.set("bits", std::stoi(trim(num)));
                }
                catch (...) {
                }
            }
            fields.push(f);
        }
    }

    if (!title.empty())
        pk.set("title", title);
    pk.set("fields", fields);
    g.properties.set("packet", pk);
    return g;
}

// parseMermaidArchitecture: 解析 architecture-beta
// 关键步骤：解析 group/service/junction -> 节点（含 icon/groupId）
//         解析边（含端口方向）-> edges + properties
inline Graph parseMermaidArchitecture(const std::vector<std::string>& lines,
                                      size_t                          first)
{
    Graph g;
    g.type = "architecture-beta";

    Json arch   = Json::obj();
    Json groups = Json::arr();
    Json svcs   = Json::arr();
    Json juncs  = Json::arr();
    Json aEdges = Json::arr();

    // 辅助：从 "(icon)[label]" 或 "[label]" 提取 icon 和 label
    auto parseIconLabel = [](const std::string& s, std::string& icon,
                             std::string& label) {
        icon.clear();
        label.clear();
        size_t paren      = s.find('(');
        size_t closeParen = s.find(')');
        if (paren != std::string::npos && closeParen != std::string::npos) {
            icon = s.substr(paren + 1, closeParen - paren - 1);
        }
        size_t brack      = s.find('[');
        size_t closeBrack = s.find(']');
        if (brack != std::string::npos && closeBrack != std::string::npos) {
            label = s.substr(brack + 1, closeBrack - brack - 1);
        }
        else {
            // 没有 [label]，使用括号后的文本作为 label
            if (closeParen != std::string::npos)
                label = trim(s.substr(closeParen + 1));
            else
                label = trim(s);
        }
    };

    for (size_t li = first; li < lines.size(); li++) {
        std::string line = trim(lines[li]);
        if (line.empty() || startsWith(line, "%%"))
            continue;

        std::string lower = toLower(line);

        // 跳过 diagram 声明
        if (startsWith(lower, "architecture-beta") ||
            startsWith(lower, "architecture"))
            continue;

        // group 声明：group id(icon)[label] [in parentId]
        if (startsWith(lower, "group ")) {
            std::string rest = trim(line.substr(6));
            // 提取 id: 第一个单词（在括号之前）
            size_t      spaceOrParen = rest.find_first_of(" (");
            std::string grpId        = (spaceOrParen != std::string::npos) ?
                                           rest.substr(0, spaceOrParen) :
                                           rest;

            std::string icon, label;
            parseIconLabel(rest, icon, label);

            // 检测 "in parentId"
            std::string parentId;
            size_t      inPos = toLower(rest).find(" in ");
            if (inPos != std::string::npos) {
                parentId = trim(rest.substr(inPos + 4));
            }

            Json grp = Json::obj();
            grp.set("id", grpId);
            grp.set("label", label);
            if (!icon.empty())
                grp.set("icon", icon);
            if (!parentId.empty())
                grp.set("groupId", parentId);
            groups.push(grp);

            // 同步到 Graph
            Node& n = g.ensureNode(grpId, label.empty() ? grpId : label);
            n.shape = "group";
            if (!parentId.empty())
                n.parent = parentId;
            if (!icon.empty())
                n.style = icon;  // icon 存入 style
            continue;
        }

        // service 声明：service id(icon)[label] [in parentId]
        if (startsWith(lower, "service ")) {
            std::string rest         = trim(line.substr(8));
            size_t      spaceOrParen = rest.find_first_of(" (");
            std::string svcId        = (spaceOrParen != std::string::npos) ?
                                           rest.substr(0, spaceOrParen) :
                                           rest;

            std::string icon, label;
            parseIconLabel(rest, icon, label);

            std::string parentId;
            size_t      inPos = toLower(rest).find(" in ");
            if (inPos != std::string::npos)
                parentId = trim(rest.substr(inPos + 4));

            Json svc = Json::obj();
            svc.set("id", svcId);
            svc.set("label", label);
            if (!icon.empty())
                svc.set("icon", icon);
            if (!parentId.empty())
                svc.set("groupId", parentId);
            svcs.push(svc);

            Node& n = g.ensureNode(svcId, label.empty() ? svcId : label);
            n.shape = "rect";
            if (!parentId.empty())
                n.parent = parentId;
            if (!icon.empty())
                n.style = icon;
            continue;
        }

        // junction 声明：junction id [in parentId]
        if (startsWith(lower, "junction ")) {
            std::string rest = trim(line.substr(9));
            size_t      sp   = rest.find(' ');
            std::string jid =
                (sp != std::string::npos) ? rest.substr(0, sp) : rest;

            std::string parentId;
            size_t      inPos = toLower(rest).find(" in ");
            if (inPos != std::string::npos)
                parentId = trim(rest.substr(inPos + 4));

            Json jn = Json::obj();
            jn.set("id", jid);
            if (!parentId.empty())
                jn.set("groupId", parentId);
            juncs.push(jn);

            Node& n = g.ensureNode(jid, jid);
            n.shape = "circle";
            if (!parentId.empty())
                n.parent = parentId;
            continue;
        }

        // 边声明：srcId:port --|--> port:dstId
        // 格式示例：db:R -- L:auth  或  auth:T --> B:j1
        if (line.find(':') != std::string::npos &&
            (line.find("--") != std::string::npos)) {
            bool directed = (line.find("-->") != std::string::npos ||
                             line.find("--&gt;") != std::string::npos);
            bool bidi     = (line.find("<-->") != std::string::npos ||
                             line.find("&lt;--&gt;") != std::string::npos);

            // 分割左右两部分
            std::string sep    = directed ? "-->" : (bidi ? "<-->" : "--");
            size_t      sepPos = line.find(sep);
            if (sepPos == std::string::npos) {
                // 尝试 --&gt; / &lt;--&gt;
                if (line.find("--&gt;") != std::string::npos) {
                    sep    = "--&gt;";
                    sepPos = line.find(sep);
                }
                else if (line.find("&lt;--&gt;") != std::string::npos) {
                    sep    = "&lt;--&gt;";
                    sepPos = line.find(sep);
                }
            }

            if (sepPos != std::string::npos) {
                std::string leftPart  = trim(line.substr(0, sepPos));
                std::string rightPart = trim(line.substr(sepPos + sep.size()));

                // 解析 leftPart: srcId:port
                std::string srcId, srcPort;
                size_t      lColon = leftPart.rfind(':');
                if (lColon != std::string::npos) {
                    srcPort = trim(leftPart.substr(lColon + 1));
                    srcId   = trim(leftPart.substr(0, lColon));
                }
                else {
                    srcId = leftPart;
                }

                // 解析 rightPart: port:dstId
                std::string dstId, dstPort;
                size_t      rColon = rightPart.find(':');
                if (rColon != std::string::npos) {
                    dstPort = trim(rightPart.substr(0, rColon));
                    dstId   = trim(rightPart.substr(rColon + 1));
                }
                else {
                    dstId = rightPart;
                }

                if (!srcId.empty() && !dstId.empty()) {
                    Json ae = Json::obj();
                    ae.set("from", srcId);
                    ae.set("to", dstId);
                    if (!srcPort.empty())
                        ae.set("fromPort", srcPort);
                    if (!dstPort.empty())
                        ae.set("toPort", dstPort);
                    ae.set("directed", directed || bidi);
                    if (bidi)
                        ae.set("bidi", true);
                    aEdges.push(ae);

                    // 同步到 edges
                    g.ensureNode(srcId, srcId);
                    g.ensureNode(dstId, dstId);
                    g.addEdge(srcId, dstId, "", "solid",
                              (directed || bidi) ? "arrow" : "none", "none",
                              (directed || bidi) ? "arrow" : "none");
                }
            }
        }
    }

    arch.set("groups", groups);
    arch.set("services", svcs);
    arch.set("junctions", juncs);
    arch.set("edges", aEdges);
    g.properties.set("architecture", arch);

    return g;
}

// parseMermaid: Mermaid 分发入口，根据首个有效指令选择具体子解析器
// mermaidTypeFromFirstLine: 从首行关键字推断 Mermaid 类型名
inline std::string mermaidTypeFromFirstLine(const std::string& lowerLine)
{
    if (startsWith(lowerLine, "graph") || startsWith(lowerLine, "flowchart"))
        return "flowchart";
    if (startsWith(lowerLine, "mindmap"))
        return "mindmap";
    if (startsWith(lowerLine, "erdiagram"))
        return "er";
    if (startsWith(lowerLine, "sequencediagram"))
        return "sequenceDiagram";
    if (startsWith(lowerLine, "classdiagram") ||
        startsWith(lowerLine, "classdiagram-v2"))
        return "classDiagram";
    if (startsWith(lowerLine, "statediagram"))
        return "stateDiagram";
    if (startsWith(lowerLine, "gantt"))
        return "gantt";
    if (startsWith(lowerLine, "pie"))
        return "pie";
    if (startsWith(lowerLine, "gitgraph"))
        return "gitGraph";
    if (startsWith(lowerLine, "journey"))
        return "journey";
    if (startsWith(lowerLine, "timeline"))
        return "timeline";
    if (startsWith(lowerLine, "kanban"))
        return "kanban";
    if (startsWith(lowerLine, "quadrantchart"))
        return "quadrantChart";
    if (startsWith(lowerLine, "xychart-beta"))
        return "xychart";
    if (startsWith(lowerLine, "architecture-beta"))
        return "architecture-beta";
    if (startsWith(lowerLine, "packet-beta"))
        return "packet";
    if (startsWith(lowerLine, "block-beta"))
        return "block";
    if (startsWith(lowerLine, "sankey-beta"))
        return "sankey";
    if (startsWith(lowerLine, "requirementdiagram"))
        return "requirement";
    return "";
}

// parseMermaid: Mermaid 分发入口，根据首个有效指令选择具体子解析器
// 对于能深度解析的类型（flowchart/mindmap/er）走专用解析器；
// 其余类型走 rawMermaid 透传模式，保留原始文本供 Mermaid/URL 导出。
inline Graph parseMermaid(const std::string& text)
{
    auto lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string t = toLower(trim(lines[i]));
        if (t.empty() || startsWith(t, "%%"))
            continue;
        // 跳过 YAML 前置元数据（--- ... --- 或 --- ... ...）
        if (trim(lines[i]) == "---") {
            for (i++; i < lines.size(); i++) {
                std::string inner = trim(lines[i]);
                if (inner == "---" || inner == "...")
                    break;
            }
            continue;
        }
        if (startsWith(t, "graph") || startsWith(t, "flowchart"))
            return parseMermaidFlowchart(lines, i + 1);
        if (startsWith(t, "mindmap"))
            return parseMermaidMindmap(lines, i + 1);
        if (startsWith(t, "erdiagram"))
            return parseMermaidER(lines, i + 1);
        if (startsWith(t, "classdiagram"))
            return parseMermaidClass(lines, i + 1);
        if (startsWith(t, "statediagram"))
            return parseMermaidState(lines, i + 1);
        if (startsWith(t, "requirementdiagram"))
            return parseMermaidRequirement(lines, i + 1);
        if (startsWith(t, "sankey-beta"))
            return parseMermaidSankey(lines, i + 1);
        if (startsWith(t, "sequencediagram"))
            return parseMermaidSequence(lines, i + 1);
        if (startsWith(t, "pie"))
            return parseMermaidPie(lines,
                                   i);  // 不跳过首行：pie title 可能在同一行
        if (startsWith(t, "gantt"))
            return parseMermaidGantt(lines, i + 1);
        if (startsWith(t, "architecture-beta"))
            return parseMermaidArchitecture(lines, i + 1);
        if (startsWith(t, "kanban"))
            return parseMermaidKanban(lines, i + 1);
        if (startsWith(t, "gitgraph"))
            return parseMermaidGit(lines, i + 1);
        if (startsWith(t, "journey"))
            return parseMermaidJourney(lines, i + 1);
        if (startsWith(t, "timeline"))
            return parseMermaidTimeline(lines, i + 1);
        if (startsWith(t, "quadrantchart"))
            return parseMermaidQuadrant(lines, i + 1);
        if (startsWith(t, "xychart-beta"))
            return parseMermaidXychart(lines, i + 1);
        if (startsWith(t, "block-beta"))
            return parseMermaidBlock(lines, i + 1);
        if (startsWith(t, "packet-beta"))
            return parseMermaidPacket(lines, i + 1);
        // 透传模式：不支持的 Mermaid 类型保存原始文本，不做深度解析
        std::string detected = mermaidTypeFromFirstLine(t);
        if (!detected.empty()) {
            Graph g;
            g.type       = detected;
            g.rawMermaid = text;
            return g;
        }
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
        if (c.tag == "diagram") {
            diagram = &c;
            break;
        }
    if (!diagram)
        throw ParseError("drawio: <mxfile> must contain a <diagram>");
    const detail::XmlNode* model = nullptr;
    for (auto& c : diagram->children)
        if (c.tag == "mxGraphModel") {
            model = &c;
            break;
        }
    if (!model)
        throw ParseError("drawio: <diagram> must contain an <mxGraphModel>");
    const detail::XmlNode* root = nullptr;
    for (auto& c : model->children)
        if (c.tag == "root") {
            root = &c;
            break;
        }
    if (!root)
        throw ParseError("drawio: <mxGraphModel> must contain a <root>");

    Graph g;
    if (diagram->attrs.count("name"))
        g.name = diagram->attrs.at("name");
    g.type = "flowchart";

    // 从 draw.io 样式字符串中提取指定 key 的值 (如 fillColor=#xxx)
    auto extractStyleVal = [](const std::string& style,
                              const std::string& key) -> std::string {
        size_t p = style.find(key + "=");
        if (p == std::string::npos)
            return "";
        p += key.size() + 1;
        size_t e = style.find(';', p);
        return style.substr(p,
                            e == std::string::npos ? std::string::npos : e - p);
    };

    struct Cell
    {
        std::string id, value, style, parent;
        std::string source, target;
        bool        vertex = false, edge = false;
        double      x = 0, y = 0, w = 140, h = 80;
        bool        hasSourcePoint = false;
        double      labelOffsetX = 0, labelOffsetY = 0;
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
                        if (it != pt.attrs.end() && it->second == "offset") {
                            auto ox = pt.attrs.find("x");
                            auto oy = pt.attrs.find("y");
                            if (ox != pt.attrs.end())
                                cell.labelOffsetX = std::stod(ox->second);
                            if (oy != pt.attrs.end())
                                cell.labelOffsetY = std::stod(oy->second);
                        }
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
        // group 的 fillColor=none 表示透明/默认，不写入字面量 "none"
        n.fillColor = isGroup ? "" : extractStyleVal(cell.style, "fillColor");
        if (n.fillColor == "none")
            n.fillColor.clear();
        n.strokeColor = extractStyleVal(cell.style, "strokeColor");
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

        std::string arrow    = "arrow";
        bool        noEnd    = styleHas(cell.style, "endArrow=none");
        bool        hasStart = styleHas(cell.style, "startArrow=classic") ||
                               styleHas(cell.style, "startArrow=block");
        if (noEnd && hasStart)
            arrow = "both";
        else if (noEnd)
            arrow = "none";
        else if (hasStart)
            arrow = "both";

        g.addEdge(cell.source, cell.target, label, edgeStyle, arrow);
        if (!g.edges.empty()) {
            gm::Edge& edge = g.edges.back();
            edge.strokeColor =
                extractStyleVal(cell.style, "strokeColor");
            // 还原边标签位置：draw.io offset 是相对边中心的偏移量
            if (!label.empty() &&
                (cell.labelOffsetX != 0 || cell.labelOffsetY != 0)) {
                const Node* src = g.findNode(cell.source);
                const Node* dst = g.findNode(cell.target);
                if (src && dst) {
                    double cx = (src->x + src->w / 2.0 + dst->x + dst->w / 2.0) / 2.0;
                    double cy = (src->y + src->h / 2.0 + dst->y + dst->h / 2.0) / 2.0;
                    edge.labelX = cx + cell.labelOffsetX;
                    edge.labelY = cy + cell.labelOffsetY;
                }
            }
        }
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
            // 结构化导入：读取填充/描边色（transparent/空视为默认）
            n.fillColor = el.str("backgroundColor");
            if (n.fillColor == "transparent" || n.fillColor == "none")
                n.fillColor.clear();
            n.strokeColor = el.str("strokeColor");
            if (n.strokeColor == "transparent" || n.strokeColor == "none")
                n.strokeColor.clear();
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
                std::string edgeStroke = el.str("strokeColor");
                if (!edgeStroke.empty() && edgeStroke != "transparent" &&
                    edgeStroke != "none")
                    g.edges.back().strokeColor = edgeStroke;
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
    // 先剥 BOM，再 trim，避免首字符识别被 EF BB BF 干扰
    std::string t = trim(stripUtf8Bom(text));
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
    auto formatLines  = splitLines(t);
    bool firstContent = true;
    for (size_t i = 0; i < formatLines.size(); i++) {
        std::string s = trim(formatLines[i]);
        if (s.empty() || startsWith(s, "%%"))
            continue;
        // 跳过 YAML 前置元数据（--- ... --- 或 --- ... ...）
        if (firstContent && s == "---") {
            for (i++; i < formatLines.size(); i++) {
                std::string inner = trim(formatLines[i]);
                if (inner == "---" || inner == "...")
                    break;
            }
            continue;
        }
        firstContent   = false;
        std::string sl = toLower(s);
        if (startsWith(sl, "graph ") || startsWith(sl, "flowchart") ||
            startsWith(sl, "mindmap") || startsWith(sl, "erdiagram") ||
            startsWith(sl, "sequencediagram") ||
            startsWith(sl, "classdiagram") || startsWith(sl, "statediagram") ||
            startsWith(sl, "gantt") || sl == "pie" || startsWith(sl, "pie ") ||
            startsWith(sl, "gitgraph") || startsWith(sl, "journey") ||
            startsWith(sl, "timeline") || startsWith(sl, "kanban") ||
            startsWith(sl, "quadrantchart") || startsWith(sl, "xychart-beta") ||
            startsWith(sl, "architecture-beta") ||
            startsWith(sl, "packet-beta") || startsWith(sl, "block-beta") ||
            startsWith(sl, "sankey-beta") ||
            startsWith(sl, "requirementdiagram"))
            return "mermaid";
        break;
    }
    if (t[0] == '#' || t[0] == '-' || t[0] == '*')
        return "markdown";
    // CSV 启发式判断：首行包含逗号且出现已知表头
    auto lines = splitLines(t);
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
