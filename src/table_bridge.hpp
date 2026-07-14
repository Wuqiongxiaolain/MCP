// table_bridge.hpp - 图↔表有损投影与表间对齐/校验
#pragma once
#include "model.hpp"
#include "parsers.hpp"
#include "table_model.hpp"
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace gtb {

using gj::Json;
using gm::Graph;
using gm::Node;
using gm::toLower;
using gm::trim;
using gt::Table;
using gt::TableError;

// ---- table_from_graph ----

// skeleton: 优先用「子节点全为叶子」的父节点作列，子节点文案作枚举说明；
// 若无此类父节点则回退为叶子作列（无枚举）
inline Table tableFromGraphSkeleton(const Graph& g, bool with_hint_row)
{
    Table t;
    t.name = g.name.empty() ? g.id : g.name;

    std::map<std::string, std::vector<const Node*>> kids;
    std::set<std::string>                          hasKids;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        if (n.parent.empty())
            continue;
        kids[n.parent].push_back(&n);
        hasKids.insert(n.parent);
    }

    // 列候选：至少有一个子节点，且所有子节点自身不再有子节点
    std::vector<const Node*> columns;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        auto it = kids.find(n.id);
        if (it == kids.end() || it->second.empty())
            continue;
        bool all_leaves = true;
        for (auto* c : it->second) {
            if (hasKids.count(c->id)) {
                all_leaves = false;
                break;
            }
        }
        if (all_leaves)
            columns.push_back(&n);
    }

    // 回退：无枚举父节点时，用叶子 label 作列
    if (columns.empty()) {
        for (auto& n : g.nodes) {
            if (n.shape == "group")
                continue;
            if (!hasKids.count(n.id))
                columns.push_back(&n);
        }
        if (columns.empty()) {
            for (auto& n : g.nodes)
                if (n.shape != "group")
                    columns.push_back(&n);
        }
    }

    std::set<std::string>    seen;
    std::vector<std::string> hints;
    for (auto* n : columns) {
        std::string col = n->label.empty() ? n->id : n->label;
        std::string key = toLower(col);
        if (seen.count(key))
            continue;
        seen.insert(key);
        t.columns.push_back(col);
        std::string hint;
        auto        it = kids.find(n->id);
        if (it != kids.end()) {
            for (auto* c : it->second) {
                if (!hint.empty())
                    hint += "|";
                hint += c->label.empty() ? c->id : c->label;
            }
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

// ---- table_rules_from_graph ----

// tableRulesFromGraph: 叶子节点→column，子节点文案→allowed|hint（与 skeleton 启发式一致）
inline Table tableRulesFromGraph(const Graph& g)
{
    Table skel = tableFromGraphSkeleton(g, true);
    Table rules;
    rules.name = (g.name.empty() ? g.id : g.name) + "-rules";
    rules.columns = {"column", "allowed", "hint"};
    if (skel.hasHintRow && !skel.rows.empty()) {
        for (size_t c = 0; c < skel.columns.size(); c++) {
            std::string allowed = skel.cell(0, c);
            rules.appendRow({skel.columns[c], allowed, allowed});
        }
    }
    else {
        for (auto& col : skel.columns)
            rules.appendRow({col, "", ""});
    }
    if (rules.rows.empty())
        throw TableError("table_rules_from_graph: no columns produced");
    return rules;
}

// ---- table_fix_enums ----

// FixEnumsResult: 自动修复结果（跳过清单 + 计数）
struct FixEnumsResult
{
    Table skipped;
    int   fixed_count   = 0;
    int   skipped_count = 0;
};

// tableFixEnums: 按 tableCheck 报告写回 suggestion；空 suggestion 记入 skipped
inline FixEnumsResult tableFixEnums(Table&       target,
                                    const Json&  allowedObj,
                                    const Table* rulesTable,
                                    bool         ignore_hint_row)
{
    Table report =
        tableCheck(target, allowedObj, rulesTable, ignore_hint_row);
    FixEnumsResult out;
    out.skipped.name = "fix_enums_skipped";
    out.skipped.columns = {"row",    "field",      "actual",
                           "expected", "suggestion", "reason"};
    for (size_t i = 0; i < report.rows.size(); i++) {
        std::string rowStr = report.cell(i, 0);
        std::string field  = report.cell(i, 1);
        std::string actual = report.cell(i, 2);
        std::string expect = report.cell(i, 3);
        std::string sugg   = report.cell(i, 4);
        if (sugg.empty()) {
            out.skipped.appendRow(
                {rowStr, field, actual, expect, sugg, "empty_suggestion"});
            out.skipped_count++;
            continue;
        }
        int row = 0;
        try {
            row = std::stoi(rowStr) - 1;
        }
        catch (...) {
            out.skipped.appendRow(
                {rowStr, field, actual, expect, sugg, "bad_row_index"});
            out.skipped_count++;
            continue;
        }
        int col = target.colIndex(field);
        if (row < 0 || col < 0) {
            out.skipped.appendRow(
                {rowStr, field, actual, expect, sugg, "row_or_field_missing"});
            out.skipped_count++;
            continue;
        }
        target.setCell((size_t)row, (size_t)col, sugg);
        out.fixed_count++;
    }
    return out;
}

// ---- table_derive ----

// tableDeriveAnimationChecklist: 列名含「动画」且值为 √ → 清单行
inline Table tableDeriveAnimationChecklist(const Table& src)
{
    Table out;
    out.name = (src.name.empty() ? src.id : src.name) + "-anim-checklist";
    out.columns = {"编号", "名称", "动画字段", "需求"};
    int idCol   = src.colIndex("编号");
    int nameCol = src.colIndex("名称");
    size_t start =
        (src.hasHintRow && !src.rows.empty()) ? (size_t)1 : (size_t)0;
    for (size_t r = start; r < src.rows.size(); r++) {
        std::string id   = idCol >= 0 ? src.cell(r, (size_t)idCol) : "";
        std::string name = nameCol >= 0 ? src.cell(r, (size_t)nameCol) : "";
        for (size_t c = 0; c < src.columns.size(); c++) {
            const std::string& col = src.columns[c];
            if (col.find("动画") == std::string::npos)
                continue;
            std::string val = trim(src.cell(r, c));
            if (val != "√")
                continue;
            out.appendRow({id, name, col, val});
        }
    }
    return out;
}

// tableDerive: 按 mode 派生新表
inline Table tableDerive(const Table& src, const std::string& mode)
{
    std::string m = toLower(mode);
    if (m == "animation_checklist")
        return tableDeriveAnimationChecklist(src);
    throw TableError(
        "unknown table_derive mode (supported: animation_checklist)");
}

// ---- table_transform_column ----

// slugify: 确定性稳定键（ASCII）；空则 col_<index>
inline std::string slugify(const std::string& raw, size_t fallback_index)
{
    std::string s = trim(raw);
    std::string out;
    out.reserve(s.size());
    bool prevUnderscore = false;
    for (unsigned char ch : s) {
        if (std::isspace(ch)) {
            if (!prevUnderscore && !out.empty()) {
                out.push_back('_');
                prevUnderscore = true;
            }
            continue;
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_') {
            out.push_back((char)ch);
            prevUnderscore = (ch == '_');
            continue;
        }
        // 非 ASCII / 其它符号跳过
    }
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    if (out.empty())
        return "col_" + std::to_string(fallback_index);
    return out;
}

// tableTransformColumnSlug: source→target；target 不存在则添加
inline void tableTransformColumnSlug(Table&             t,
                                     const std::string& source_column,
                                     const std::string& target_column)
{
    int si = t.colIndex(source_column);
    if (si < 0)
        throw TableError("source column not found: " + source_column);
    if (t.colIndex(target_column) < 0)
        t.addColumn(target_column);
    int ti = t.colIndex(target_column);
    for (size_t r = 0; r < t.rows.size(); r++)
        t.setCell(r, (size_t)ti, slugify(t.cell(r, (size_t)si), r));
}

// ---- table_sample_rows / propose helpers ----

// firstAllowedFromPipe: "a|b|c" → "a"
inline std::string firstAllowedFromPipe(const std::string& all)
{
    size_t p = all.find('|');
    if (p == std::string::npos)
        return trim(all);
    return trim(all.substr(0, p));
}

// loadAllowedMapFromRules: 规则表 → 列名小写 → allowed 原文（| 分隔）
inline std::map<std::string, std::string> loadAllowedMapFromRules(
    const Table& rules)
{
    std::map<std::string, std::string> m;
    int cCol = rules.colIndex("column");
    if (cCol < 0)
        cCol = rules.colIndex("field");
    int cAll = rules.colIndex("allowed");
    if (cCol < 0 || cAll < 0)
        return m;
    for (size_t r = 0; r < rules.rows.size(); r++) {
        std::string col = rules.cell(r, (size_t)cCol);
        std::string all = rules.cell(r, (size_t)cAll);
        if (!col.empty())
            m[toLower(col)] = all;
    }
    return m;
}

// tableSampleRows: 追加 count 行占位样例（枚举首值 / 动画默认 x / 文本 TODO）
inline void tableSampleRows(Table&             t,
                            int                count,
                            const Table*       rulesTable)
{
    if (count <= 0)
        throw TableError("table_sample_rows: count must be > 0");
    std::map<std::string, std::string> allowed;
    if (rulesTable)
        allowed = loadAllowedMapFromRules(*rulesTable);
    else if (t.hasHintRow && !t.rows.empty()) {
        for (size_t c = 0; c < t.columns.size(); c++)
            allowed[toLower(t.columns[c])] = t.cell(0, c);
    }
    for (int n = 0; n < count; n++) {
        std::vector<std::string> row(t.columns.size(), "");
        for (size_t c = 0; c < t.columns.size(); c++) {
            auto it = allowed.find(toLower(t.columns[c]));
            if (it != allowed.end() && !it->second.empty()) {
                row[c] = firstAllowedFromPipe(it->second);
                continue;
            }
            if (t.columns[c].find("动画") != std::string::npos) {
                row[c] = "x";
                continue;
            }
            row[c] = "TODO";
        }
        t.appendRow(row);
    }
}

// tableProposeRows: 对象行写入；可选 rules 枚举校验（非法整批拒绝）
inline void tableProposeRows(Table& t, const Json& rowsArr, const Table* rulesTable)
{
    if (!rowsArr.isArr())
        throw TableError("table_propose_rows: rows must be a JSON array");
    std::map<std::string, std::set<std::string>> allowed;
    if (rulesTable) {
        auto raw = loadAllowedMapFromRules(*rulesTable);
        for (auto& kv : raw) {
            std::set<std::string> vals;
            std::string           s     = kv.second;
            size_t                start = 0;
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
            allowed[kv.first] = std::move(vals);
        }
    }
    std::vector<std::vector<std::string>> pending;
    for (auto& rowJ : *rowsArr.a) {
        if (!rowJ.isObj() || !rowJ.o)
            throw TableError("table_propose_rows: each row must be an object");
        std::vector<std::string> row(t.columns.size(), "");
        for (auto& kv : *rowJ.o) {
            int ci = t.colIndex(kv.first);
            if (ci < 0)
                continue;
            std::string val =
                kv.second.isStr() ? kv.second.s : kv.second.dump();
            if (!val.empty() && !allowed.empty()) {
                auto it = allowed.find(toLower(t.columns[(size_t)ci]));
                if (it != allowed.end() && !it->second.empty() &&
                    !it->second.count(val))
                    throw TableError("table_propose_rows: illegal value for " +
                                     t.columns[(size_t)ci] + ": " + val);
            }
            row[(size_t)ci] = val;
        }
        pending.push_back(std::move(row));
    }
    for (auto& row : pending)
        t.appendRow(row);
}

}  // namespace gtb
