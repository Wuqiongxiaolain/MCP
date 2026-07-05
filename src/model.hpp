// model.hpp - unified graph model for graphmcp
// One model to represent flowcharts, architecture diagrams, ER diagrams,
// org charts, mind maps and freehand whiteboard scenes.
#pragma once
#include "json.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <ctime>
#include <cstdlib>

namespace gm {

using gj::Json;

// diagram kinds understood by parsers / exporters / layout
// "flowchart" | "architecture" | "er" | "orgchart" | "mindmap" | "whiteboard"

struct Node {
    std::string id;
    std::string label;
    std::string shape;   // rect | round | diamond | ellipse | circle | stadium | group
    std::string parent;  // hierarchy: parent node id ("" = root level)
    std::string style;   // free-form style hint (color etc.)
    std::vector<std::string> attrs; // ER attributes: "type name" entries
    double x = 0, y = 0, w = 0, h = 0;
};

struct Edge {
    std::string id;
    std::string from, to;
    std::string label;
    std::string style;   // solid | dashed | thick
    std::string arrow;   // arrow | none | both
};

struct Graph {
    std::string id;
    std::string name;
    std::string type = "flowchart";
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Json> elements; // raw whiteboard (excalidraw-like) elements
    bool laidOut = false;

    Node* findNode(const std::string& nid) {
        for (auto& n : nodes) if (n.id == nid) return &n;
        return nullptr;
    }
    const Node* findNode(const std::string& nid) const {
        for (auto& n : nodes) if (n.id == nid) return &n;
        return nullptr;
    }
    Node& ensureNode(const std::string& nid, const std::string& label = "") {
        for (auto& n : nodes) {
            if (n.id == nid) {
                if (!label.empty() && (n.label.empty() || n.label == n.id))
                    n.label = label;
                return n;
            }
        }
        Node n;
        n.id = nid;
        n.label = label.empty() ? nid : label;
        n.shape = "rect";
        nodes.push_back(n);
        return nodes.back();
    }
    void addEdge(const std::string& from, const std::string& to,
                 const std::string& label = "", const std::string& style = "solid",
                 const std::string& arrow = "arrow") {
        Edge e;
        e.id = "e" + std::to_string(edges.size() + 1);
        e.from = from; e.to = to; e.label = label; e.style = style; e.arrow = arrow;
        edges.push_back(e);
    }

    // ---- JSON (de)serialization of the unified model ----
    Json toJson() const {
        Json j = Json::obj();
        j.set("id", id);
        j.set("name", name);
        j.set("type", type);
        j.set("laidOut", laidOut);
        Json ns = Json::arr();
        for (auto& n : nodes) {
            Json jn = Json::obj();
            jn.set("id", n.id);
            jn.set("label", n.label);
            jn.set("shape", n.shape);
            if (!n.parent.empty()) jn.set("parent", n.parent);
            if (!n.style.empty())  jn.set("style", n.style);
            if (!n.attrs.empty()) {
                Json ja = Json::arr();
                for (auto& a : n.attrs) ja.push(Json(a));
                jn.set("attrs", ja);
            }
            jn.set("x", n.x); jn.set("y", n.y);
            jn.set("w", n.w); jn.set("h", n.h);
            ns.push(jn);
        }
        j.set("nodes", ns);
        Json es = Json::arr();
        for (auto& e : edges) {
            Json je = Json::obj();
            je.set("id", e.id);
            je.set("from", e.from);
            je.set("to", e.to);
            if (!e.label.empty()) je.set("label", e.label);
            je.set("style", e.style);
            je.set("arrow", e.arrow);
            es.push(je);
        }
        j.set("edges", es);
        if (!elements.empty()) {
            Json els = Json::arr();
            for (auto& el : elements) els.push(el);
            j.set("elements", els);
        }
        return j;
    }

    static Graph fromJson(const Json& j) {
        Graph g;
        g.id = j.str("id");
        g.name = j.str("name");
        g.type = j.str("type", "flowchart");
        g.laidOut = j.boolean("laidOut", false);
        if (const Json* ns = j.find("nodes")) {
            if (ns->isArr()) for (auto& jn : *ns->a) {
                Node n;
                n.id = jn.str("id");
                n.label = jn.str("label");
                n.shape = jn.str("shape", "rect");
                n.parent = jn.str("parent");
                n.style = jn.str("style");
                if (const Json* ja = jn.find("attrs")) {
                    if (ja->isArr()) for (auto& a : *ja->a)
                        if (a.isStr()) n.attrs.push_back(a.s);
                }
                n.x = jn.num("x"); n.y = jn.num("y");
                n.w = jn.num("w"); n.h = jn.num("h");
                g.nodes.push_back(n);
            }
        }
        if (const Json* es = j.find("edges")) {
            if (es->isArr()) for (auto& je : *es->a) {
                Edge e;
                e.id = je.str("id");
                e.from = je.str("from");
                e.to = je.str("to");
                e.label = je.str("label");
                e.style = je.str("style", "solid");
                e.arrow = je.str("arrow", "arrow");
                g.edges.push_back(e);
            }
        }
        if (const Json* els = j.find("elements")) {
            if (els->isArr()) for (auto& el : *els->a) g.elements.push_back(el);
        }
        return g;
    }
};

// ---- small shared utilities ----

inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)tolower(c); });
    return s;
}

inline bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// generate a short unique-ish id: g<epoch36><rand>
inline std::string genId(const std::string& prefix = "g") {
    static const char* al = "0123456789abcdefghijklmnopqrstuvwxyz";
    unsigned long long v = (unsigned long long)time(nullptr);
    std::string s;
    while (v) { s += al[v % 36]; v /= 36; }
    std::reverse(s.begin(), s.end());
    s += al[rand() % 36];
    s += al[rand() % 36];
    return prefix + s;
}

// default node size derived from label length (UTF-8 aware-ish: count code points)
inline void defaultSize(Node& n) {
    size_t cps = 0;
    for (unsigned char c : n.label) if ((c & 0xC0) != 0x80) cps++;
    double wide = 0;
    for (unsigned char c : n.label) if (c >= 0x80) { wide = 1; break; }
    double perChar = wide ? 14.0 : 8.5;
    n.w = std::max(100.0, cps * perChar + 32.0);
    n.h = (n.shape == "diamond") ? 60.0 : 44.0;
    if (n.shape == "circle") { n.w = std::max(n.w, n.h); n.h = n.w; }
    if (!n.attrs.empty()) // ER entity: room for attribute rows
        n.h = 30.0 + 22.0 * (double)n.attrs.size();
}

} // namespace gm
