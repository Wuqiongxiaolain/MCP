// table_bridge.hpp - 图↔表有损投影与表间对齐/校验
#pragma once
#include "model.hpp"
#include "parsers.hpp"
#include "table_model.hpp"
#include <map>
#include <set>

namespace gtb {

using gj::Json;
using gm::Graph;
using gm::Node;
using gm::toLower;
using gm::trim;
using gt::Table;
using gt::TableError;

// ---- table_from_graph ----

// skeleton: 用叶子节点 label 作表头；可选第二行写子节点文案作说明
inline Table tableFromGraphSkeleton(const Graph& g, bool with_hint_row)
{
    Table t;
    t.name = g.name.empty() ? g.id : g.name;
    std::set<std::string> parents;
    for (auto& n : g.nodes)
        if (!n.parent.empty())
            parents.insert(n.parent);
    std::vector<const Node*> leaves;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        if (!parents.count(n.id))
            leaves.push_back(&n);
    }
    if (leaves.empty()) {
        for (auto& n : g.nodes)
            if (n.shape != "group")
                leaves.push_back(&n);
    }
    std::set<std::string>    seen;
    std::vector<std::string> hints;
    for (auto* n : leaves) {
        std::string col = n->label.empty() ? n->id : n->label;
        std::string key = toLower(col);
        if (seen.count(key))
            continue;
        seen.insert(key);
        t.columns.push_back(col);
        std::string hint;
        for (auto& c : g.nodes) {
            if (c.parent != n->id)
                continue;
            if (!hint.empty())
                hint += "|";
            hint += c.label.empty() ? c.id : c.label;
        }
        hints.push_back(hint);
    }
    if (t.columns.empty())
        throw TableError("skeleton: no usable node labels");
    if (with_hint_row) {
        t.appendRow(hints);
        t.hasHintRow = true;
    }
    return t;
}

inline Table tableFromGraphEdgelist(const Graph& g)
{
    Table t;
    t.name = g.name.empty() ? g.id : g.name;
    t.columns = {"from", "to", "label"};
    for (auto& e : g.edges) {
        t.appendRow({e.from, e.to, e.label});
    }
    return t;
}

inline Table tableFromGraphHierarchy(const Graph& g)
{
    Table t;
    t.name = g.name.empty() ? g.id : g.name;
    t.columns = {"id", "label", "parent"};
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        t.appendRow({n.id, n.label, n.parent});
    }
    return t;
}

inline Table tableFromGraphNodelist(const Graph& g)
{
    Table t;
    t.name = g.name.empty() ? g.id : g.name;
    t.columns = {"id", "label", "shape", "parent", "style"};
    for (auto& n : g.nodes) {
        t.appendRow({n.id, n.label, n.shape, n.parent, n.style});
    }
    return t;
}

// tableFromGraph: 按 mode 投影；lossy
inline Table tableFromGraph(const Graph&       g,
                            const std::string& mode,
                            bool               with_hint_row = false)
{
    std::string m = toLower(mode);
    if (m == "skeleton")
        return tableFromGraphSkeleton(g, with_hint_row);
    if (m == "edgelist")
        return tableFromGraphEdgelist(g);
    if (m == "hierarchylist" || m == "hierarchy")
        return tableFromGraphHierarchy(g);
    if (m == "nodelist")
        return tableFromGraphNodelist(g);
    throw TableError(
        "unknown table_from_graph mode (use skeleton|edgelist|hierarchylist|nodelist)");
}

// ---- graph_from_table ----

// 用指定列映射构建边表 CSV 文本后走 parseCSV，或直接建图
inline Graph graphFromTableMapped(const Table&       t,
                                  const std::string& from_col,
                                  const std::string& to_col,
                                  const std::string& label_col)
{
    int cFrom = t.colIndex(from_col);
    int cTo   = t.colIndex(to_col);
    int cLbl  = label_col.empty() ? -1 : t.colIndex(label_col);
    if (cFrom < 0 || cTo < 0)
        throw TableError("from_col/to_col not found in table");
    Graph g;
    g.type = "flowchart";
    g.name = t.name.empty() ? t.id : t.name;
    for (size_t r = 0; r < t.rows.size(); r++) {
        std::string from = t.cell(r, (size_t)cFrom);
        std::string to   = t.cell(r, (size_t)cTo);
        if (from.empty() || to.empty())
            continue;
        g.ensureNode(from);
        g.ensureNode(to);
        std::string lbl = cLbl >= 0 ? t.cell(r, (size_t)cLbl) : "";
        g.addEdge(from, to, lbl);
    }
    return g;
}

inline Graph graphFromTableHierarchyMapped(const Table&       t,
                                           const std::string& id_col,
                                           const std::string& label_col,
                                           const std::string& parent_col)
{
    int cId  = t.colIndex(id_col);
    int cLbl = label_col.empty() ? -1 : t.colIndex(label_col);
    int cPar = parent_col.empty() ? -1 : t.colIndex(parent_col);
    if (cId < 0)
        throw TableError("id_col not found in table");
    Graph g;
    g.type = "orgchart";
    g.name = t.name.empty() ? t.id : t.name;
    for (size_t r = 0; r < t.rows.size(); r++) {
        std::string id = t.cell(r, (size_t)cId);
        if (id.empty())
            continue;
        std::string label = cLbl >= 0 ? t.cell(r, (size_t)cLbl) : "";
        Node&       n     = g.ensureNode(id, label);
        std::string parent = cPar >= 0 ? t.cell(r, (size_t)cPar) : "";
        if (!parent.empty()) {
            n.parent = parent;
            g.ensureNode(parent);
            g.addEdge(parent, id, "", "solid", "none");
        }
    }
    return g;
}

// graphFromTable: 自动识别边表/层级表，或使用显式映射
inline Graph graphFromTable(const Table&       t,
                            const std::string& from_col   = "",
                            const std::string& to_col     = "",
                            const std::string& label_col  = "",
                            const std::string& id_col     = "",
                            const std::string& parent_col = "")
{
    if (!from_col.empty() && !to_col.empty())
        return graphFromTableMapped(t, from_col, to_col, label_col);
    if (!id_col.empty())
        return graphFromTableHierarchyMapped(
            t, id_col, label_col.empty() ? "label" : label_col, parent_col);

    int cFrom =
        t.colIndex("from") >= 0 ? t.colIndex("from") : t.colIndex("source");
    int cTo = t.colIndex("to") >= 0 ? t.colIndex("to") : t.colIndex("target");
    if (cFrom >= 0 && cTo >= 0) {
        std::string fromName =
            t.colIndex("from") >= 0 ? "from" : "source";
        std::string toName = t.colIndex("to") >= 0 ? "to" : "target";
        std::string lblName = t.colIndex("label") >= 0 ? "label" : "";
        return graphFromTableMapped(t, fromName, toName, lblName);
    }
    if (t.colIndex("id") >= 0) {
        std::string lbl = t.colIndex("label") >= 0 ? "label" : "";
        std::string par = t.colIndex("parent") >= 0 ? "parent" : "";
        return graphFromTableHierarchyMapped(t, "id", lbl, par);
    }
    throw TableError("graph_from_table: table must contain edge columns "
                     "(from/to or source/target) or hierarchy columns "
                     "(id,label,parent); or provide explicit mapping");
}

// ---- table_align ----

// 以主表主键列为准，向目标表补缺失行（主键拷贝，其余空）
inline Json tableAlign(const Table&       primary,
                       Table&             target,
                       const std::string& primary_key,
                       const std::string& target_key)
{
    int pk = primary.colIndex(primary_key);
    int tk = target.colIndex(target_key);
    if (pk < 0)
        throw TableError("primary key column not found: " + primary_key);
    if (tk < 0) {
        // 目标没有该列则添加
        target.addColumn(target_key);
        tk = target.colIndex(target_key);
    }
    std::set<std::string> existing;
    for (size_t r = 0; r < target.rows.size(); r++) {
        std::string v = target.cell(r, (size_t)tk);
        if (!v.empty())
            existing.insert(v);
    }
    int added = 0;
    for (size_t r = 0; r < primary.rows.size(); r++) {
        std::string key = primary.cell(r, (size_t)pk);
        if (key.empty() || existing.count(key))
            continue;
        std::vector<std::string> row(target.columns.size(), "");
        row[(size_t)tk] = key;
        // 若目标也有同名列，从主表拷贝
        for (size_t c = 0; c < target.columns.size(); c++) {
            if ((int)c == tk)
                continue;
            int pc = primary.colIndex(target.columns[c]);
            if (pc >= 0)
                row[c] = primary.cell(r, (size_t)pc);
        }
        target.appendRow(row);
        existing.insert(key);
        added++;
    }
    Json out = Json::obj();
    out.set("added_rows", (double)added);
    out.set("target_rows", (double)target.rows.size());
    return out;
}

// ---- table_check ----

// allowed: { "列名": ["v1","v2",...] } 或规则表（列 column / allowed，allowed 用 | 分隔）
inline Table tableCheck(const Table&       target,
                        const Json&        allowedObj,
                        const Table*       rulesTable = nullptr,
                        bool               ignore_hint_row = false)
{
    std::map<std::string, std::set<std::string>> allowed;
    if (allowedObj.isObj() && allowedObj.o) {
        for (auto& kv : *allowedObj.o) {
            std::set<std::string> vals;
            if (kv.second.isArr()) {
                for (auto& v : *kv.second.a)
                    if (v.isStr())
                        vals.insert(v.s);
            }
            else if (kv.second.isStr()) {
                // 支持 "a|b|c"
                std::string s = kv.second.s;
                size_t      start = 0;
                while (start <= s.size()) {
                    size_t p = s.find('|', start);
                    if (p == std::string::npos) {
                        std::string part = trim(s.substr(start));
                        if (!part.empty())
                            vals.insert(part);
                        break;
                    }
                    std::string part = trim(s.substr(start, p - start));
                    if (!part.empty())
                        vals.insert(part);
                    start = p + 1;
                }
            }
            allowed[toLower(kv.first)] = std::move(vals);
        }
    }
    if (rulesTable) {
        int cCol = rulesTable->colIndex("column");
        int cAll = rulesTable->colIndex("allowed");
        if (cCol < 0)
            cCol = rulesTable->colIndex("field");
        if (cCol >= 0 && cAll >= 0) {
            for (size_t r = 0; r < rulesTable->rows.size(); r++) {
                std::string col = rulesTable->cell(r, (size_t)cCol);
                std::string all = rulesTable->cell(r, (size_t)cAll);
                std::set<std::string> vals;
                size_t start = 0;
                while (start <= all.size()) {
                    size_t p = all.find('|', start);
                    if (p == std::string::npos) {
                        std::string part = trim(all.substr(start));
                        if (!part.empty())
                            vals.insert(part);
                        break;
                    }
                    std::string part = trim(all.substr(start, p - start));
                    if (!part.empty())
                        vals.insert(part);
                    start = p + 1;
                }
                allowed[toLower(col)] = std::move(vals);
            }
        }
    }

    Table report;
    report.name = "check_report";
    report.columns = {"row", "field", "actual", "expected", "suggestion"};
    size_t start_row = (ignore_hint_row && target.hasHintRow && !target.rows.empty())
                           ? 1
                           : 0;
    for (size_t r = start_row; r < target.rows.size(); r++) {
        for (size_t c = 0; c < target.columns.size(); c++) {
            auto it = allowed.find(toLower(target.columns[c]));
            if (it == allowed.end() || it->second.empty())
                continue;
            std::string actual = target.cell(r, c);
            if (actual.empty())
                continue;
            if (it->second.count(actual))
                continue;
            std::string expected;
            for (auto& v : it->second) {
                if (!expected.empty())
                    expected += "|";
                expected += v;
            }
            std::string suggestion =
                it->second.empty() ? "" : *it->second.begin();
            report.appendRow({std::to_string((int)r + 1), target.columns[c],
                              actual, expected, suggestion});
        }
    }
    return report;
}

}  // namespace gtb
