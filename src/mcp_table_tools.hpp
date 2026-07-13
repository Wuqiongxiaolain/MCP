// mcp_table_tools.hpp - MCP table/tool helper implementations
// 可被 mcp.hpp 纳入 mcp 命名空间，也可被 IDE 单独解析；故自带 textContent。
#pragma once
#include "exporters.hpp"
#include "storage.hpp"
#include "table_bridge.hpp"
#include "table_storage.hpp"
#include <algorithm>
#include <functional>
#include <sstream>

namespace tabletools {

using gj::Json;
using gm::Graph;

// textContent: 与 mcp::textContent 同形；本头独立解析时不依赖 mcp.hpp
inline Json textContent(const std::string& text, bool isError = false)
{
    Json result  = Json::obj();
    Json content = Json::arr();
    Json item    = Json::obj();
    item.set("type", "text");
    item.set("text", text);
    content.push(item);
    result.set("content", content);
    if (isError)
        result.set("isError", true);
    return result;
}

inline Json parseJsonField(const Json& args, const std::string& key)
{
    std::string raw = args.str(key);
    if (raw.empty()) {
        if (const Json* j = args.find(key))
            return *j;
        return Json();
    }
    std::string err;
    Json        j = Json::parse(raw, &err);
    if (!err.empty())
        throw gt::TableError("invalid JSON in " + key + ": " + err);
    return j;
}

// appendCompatWarning: 向响应追加 compat_warnings（同文案去重）
inline void appendCompatWarning(Json& out, const std::string& msg)
{
    if (const Json* existing = out.find("compat_warnings")) {
        if (existing->isArr()) {
            for (auto& w : *existing->a)
                if (w.isStr() && w.s == msg)
                    return;
            Json warnings = *existing;
            warnings.push(Json(msg));
            out.set("compat_warnings", warnings);
            return;
        }
    }
    Json warnings = Json::arr();
    warnings.push(Json(msg));
    out.set("compat_warnings", warnings);
}

inline Json tableCreate(gts::TableStore& tables, const Json& a)
{
    gt::Table t = gt::Table::fromCsv(a.str("content"));
    if (!a.str("id").empty())
        t.id = a.str("id");
    if (!a.str("name").empty())
        t.name = a.str("name");

    bool force = a.boolean("force", false);
    // 向后兼容接口，等待后续处理或删除
    bool legacy_upsert =
        ge::envFlagEnabled("GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT");
    bool used_legacy_upsert = false;
    if (!t.id.empty() && tables.exists(t.id) && !force) {
        if (legacy_upsert)
            used_legacy_upsert = true;
        else
            return textContent(
                "table already exists: " + t.id +
                    " (use force=true or table_import for upsert)",
                true);
    }

    std::string err;
    int         v = tables.save(t, a.str("note", "created via MCP"), &err);
    if (v < 0)
        return textContent(err.empty() ? "failed to save table" : err, true);
    Json out = Json::obj();
    out.set("status", "created");
    out.set("id", t.id);
    out.set("name", t.name);
    out.set("version", (double)v);
    out.set("columns", (double)t.columns.size());
    out.set("rows", (double)t.rows.size());
    // 向后兼容接口，等待后续处理或删除
    if (used_legacy_upsert)
        appendCompatWarning(
            out,
            "GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT enabled; prefer force=true "
            "or table_import");
    return textContent(out.dump(2));
}

inline Json tableImport(gts::TableStore& tables, const Json& a)
{
    gt::Table t = gt::Table::fromCsv(a.str("content"));
    if (!a.str("id").empty())
        t.id = a.str("id");
    if (!a.str("name").empty())
        t.name = a.str("name");
    std::string err;
    int         v = tables.save(t, a.str("note", "imported via MCP"), &err);
    if (v < 0)
        return textContent(err.empty() ? "failed to save table" : err, true);
    Json out = Json::obj();
    out.set("status", "imported");
    out.set("id", t.id);
    out.set("version", (double)v);
    out.set("columns", (double)t.columns.size());
    out.set("rows", (double)t.rows.size());
    return textContent(out.dump(2));
}

inline Json tableExport(gts::TableStore& tables, const Json& a)
{
    gt::Table   t;
    std::string err;
    if (!tables.load(a.str("id"), t, (int)a.num("version", 0), &err))
        return textContent(err, true);
    std::string to   = a.str("to", "csv");
    std::string text = (to == "model") ? t.toJson().dump(2) : t.toCsv();
    std::string path = a.str("path");
    if (!path.empty()) {
        if (!ge::writeFile(path, text))
            return textContent("failed to write " + path, true);
        return textContent("wrote " + path);
    }
    return textContent(text);
}

inline Json tableList(gts::TableStore& tables, const Json& a)
{
    Json idx = tables.loadIndex();
    if (a.str("format", "json") == "table") {
        std::ostringstream oss;
        oss << "id\tname\tcolumns\trows\tversions\n";
        for (auto& e : *idx["tables"].a)
            oss << e.str("id") << '\t' << e.str("name") << '\t'
                << (int)e.num("columns") << '\t' << (int)e.num("rows") << '\t'
                << (int)e.num("versions") << '\n';
        return textContent(oss.str());
    }
    return textContent(idx.dump(2));
}

inline Json tableShow(gts::TableStore& tables, const Json& a)
{
    gt::Table   t;
    std::string err;
    if (!tables.load(a.str("id"), t, (int)a.num("version", 0), &err))
        return textContent(err, true);
    Json out = t.toJson();
    int  lim = (int)a.num("limit", 0);
    if (lim > 0 && (int)t.rows.size() > lim) {
        Json rs = Json::arr();
        for (int i = 0; i < lim; i++) {
            Json jr = Json::arr();
            for (size_t c = 0; c < t.columns.size(); c++)
                jr.push(Json(t.cell((size_t)i, c)));
            rs.push(jr);
        }
        out.set("rows", rs);
        out.set("truncated", true);
        out.set("total_rows", (double)t.rows.size());
    }
    return textContent(out.dump(2));
}

inline Json tableDelete(gts::TableStore& tables, const Json& a)
{
    if (!a.boolean("force", false))
        return textContent("pass force=true to delete table", true);
    std::string err;
    if (!tables.remove(a.str("id"), &err))
        return textContent(err, true);
    Json out = Json::obj();
    out.set("status", "deleted");
    out.set("id", a.str("id"));
    return textContent(out.dump(2));
}

inline Json tableHistory(gts::TableStore& tables, const Json& a)
{
    return textContent(tables.history(a.str("id")).dump(2));
}

inline Json tableRollback(gts::TableStore& tables, const Json& a)
{
    int         nv = 0;
    std::string err;
    if (!tables.rollback(a.str("id"), (int)a.num("version"), &nv, &err))
        return textContent(err, true);
    Json out = Json::obj();
    out.set("status", "ok");
    out.set("new_version", (double)nv);
    return textContent(out.dump(2));
}

inline void applyTablePatches(gt::Table& t, const Json& a, Json& summary,
                              Json& compat_out)
{
    int cells = 0, addedRows = 0, deletedRows = 0, addedCols = 0;

    Json setCells = parseJsonField(a, "set_cells");
    if (setCells.isArr()) {
        for (auto& item : *setCells.a) {
            int row = (int)item.num("row");
            if (row < 0)
                throw gt::TableError("set_cells.row must be >= 0");

            std::string colName = item.str("column");
            int         col     = -1;
            if (!colName.empty()) {
                col = t.colIndex(colName);
                if (col < 0)
                    throw gt::TableError("unknown column: " + colName);
            }
            else if (item.find("col_index")) {
                col = (int)item.num("col_index", -1);
            }
            // 向后兼容接口，等待后续处理或删除
            else if (const Json* legacy_col = item.find("col")) {
                if (legacy_col->isStr()) {
                    col = t.colIndex(legacy_col->s);
                    if (col < 0)
                        throw gt::TableError("unknown column: " +
                                             legacy_col->s);
                }
                else if (legacy_col->isNum()) {
                    col = (int)legacy_col->as_num();
                }
                else {
                    throw gt::TableError(
                        "set_cells.col must be string or number");
                }
                appendCompatWarning(
                    compat_out,
                    "set_cells.col is deprecated; use column or col_index");
            }
            else {
                throw gt::TableError(
                    "set_cells requires column or col_index");
            }
            if (col < 0 || (size_t)col >= t.columns.size())
                throw gt::TableError("col_index out of range");

            t.setCell((size_t)row, (size_t)col, item.str("value"));
            cells++;
        }
    }

    Json addRows = parseJsonField(a, "add_rows");
    if (addRows.isArr()) {
        for (auto& rowJ : *addRows.a) {
            std::vector<std::string> row;
            if (rowJ.isArr()) {
                for (auto& c : *rowJ.a)
                    row.push_back(c.isStr() ? c.s : c.dump());
            }
            else if (rowJ.isObj() && rowJ.o) {
                row.assign(t.columns.size(), "");
                for (auto& kv : *rowJ.o) {
                    int ci = t.colIndex(kv.first);
                    if (ci >= 0)
                        row[(size_t)ci] =
                            kv.second.isStr() ? kv.second.s : kv.second.dump();
                }
            }
            t.appendRow(row);
            addedRows++;
        }
    }

    Json delRows = parseJsonField(a, "delete_rows");
    if (delRows.isArr()) {
        std::vector<int> idxs;
        for (auto& v : *delRows.a)
            idxs.push_back((int)v.as_num());
        std::sort(idxs.begin(), idxs.end(), std::greater<int>());
        for (int idx : idxs) {
            if (idx >= 0 && (size_t)idx < t.rows.size()) {
                t.deleteRow((size_t)idx);
                deletedRows++;
            }
        }
    }

    Json addCols = parseJsonField(a, "add_columns");
    if (addCols.isArr()) {
        for (auto& c : *addCols.a) {
            std::string name = c.isStr() ? c.s : c.str("name");
            std::string def  = c.isObj() ? c.str("default") : "";
            if (name.empty())
                continue;
            t.addColumn(name, def);
            addedCols++;
        }
    }

    Json setCol = parseJsonField(a, "set_column_values");
    if (setCol.isObj()) {
        std::string colName = setCol.str("column");
        int         ci      = t.colIndex(colName);
        if (ci < 0)
            throw gt::TableError("unknown column: " + colName);
        const Json* vals = setCol.find("values");
        if (vals && vals->isArr()) {
            for (size_t i = 0; i < vals->a->size(); i++) {
                const Json& v = (*vals->a)[i];
                t.setCell(i, (size_t)ci, v.isStr() ? v.s : v.dump());
                cells++;
            }
        }
    }

    summary.set("set_cells", (double)cells);
    summary.set("added_rows", (double)addedRows);
    summary.set("deleted_rows", (double)deletedRows);
    summary.set("added_columns", (double)addedCols);
}

inline Json tableUpdate(gts::TableStore& tables, const Json& a)
{
    gt::Table   t;
    std::string err;
    if (!tables.load(a.str("id"), t, 0, &err))
        return textContent(err, true);
    Json summary = Json::obj();
    Json out     = Json::obj();
    applyTablePatches(t, a, summary, out);
    int v = tables.save(t, a.str("note", "updated via MCP"), &err);
    if (v < 0)
        return textContent(err.empty() ? "failed to save table" : err, true);
    out.set("status", "updated");
    out.set("id", t.id);
    out.set("version", (double)v);
    out.set("columns", (double)t.columns.size());
    out.set("rows", (double)t.rows.size());
    out.set("changes", summary);
    return textContent(out.dump(2));
}

inline Json tableDiffTool(gts::TableStore& tables, const Json& a)
{
    gt::Table   aT, bT;
    std::string err;
    std::string content = a.str("content");
    if (!content.empty()) {
        if (!tables.load(a.str("id"), aT, 0, &err))
            return textContent(err, true);
        bT = gt::Table::fromCsv(content);
    }
    else {
        int v1 = (int)a.num("v1", 0);
        int v2 = (int)a.num("v2", 0);
        if (!tables.load(a.str("id"), aT, v1, &err))
            return textContent(err, true);
        if (!tables.load(a.str("id"), bT, v2, &err))
            return textContent(err, true);
    }
    return textContent(gt::tableDiff(aT, bT).dump(2));
}

inline Json tableFromGraphTool(gs::Store& store, gts::TableStore& tables,
                               const Json& a)
{
    Graph       g;
    std::string err;
    if (!store.load(a.str("graph_id"), g, 0, &err))
        return textContent(err, true);
    gt::Table t = gtb::tableFromGraph(g, a.str("mode", "skeleton"),
                                      a.boolean("with_hint_row", false));
    if (!a.str("name").empty())
        t.name = a.str("name");
    Json out = Json::obj();
    out.set("status", "ok");
    out.set("mode", a.str("mode", "skeleton"));
    out.set("has_hint_row", t.hasHintRow);
    if (a.boolean("save", true)) {
        int v = tables.save(t, "from graph " + g.id, &err);
        if (v < 0)
            return textContent(err.empty() ? "failed to save table" : err, true);
        out.set("id", t.id);
        out.set("version", (double)v);
    }
    out.set("columns", (double)t.columns.size());
    out.set("rows", (double)t.rows.size());

    int previewRows = (int)a.num("preview_rows", 20);
    if (previewRows <= 0)
        previewRows = 20;
    gt::Table preview = t;
    if ((int)preview.rows.size() > previewRows) {
        preview.rows.resize((size_t)previewRows);
        out.set("truncated", true);
        out.set("total_rows", (double)t.rows.size());
        out.set("hint",
                "csv_preview truncated; use table_export for full content");
    }
    out.set("csv_preview", preview.toCsv());
    return textContent(out.dump(2));
}

inline Json graphFromTableTool(gs::Store& store, gts::TableStore& tables,
                               const Json& a)
{
    gt::Table t;
    if (!a.str("content").empty()) {
        t = gt::Table::fromCsv(a.str("content"));
    }
    else {
        std::string err;
        if (!tables.load(a.str("table_id"), t, 0, &err))
            return textContent(err.empty() ? "table_id or content required" : err,
                               true);
    }
    Graph g = gtb::graphFromTable(t, a.str("from_col"), a.str("to_col"),
                                  a.str("label_col"), a.str("id_col"),
                                  a.str("parent_col"));
    if (!a.str("name").empty())
        g.name = a.str("name");
    Json out = Json::obj();
    out.set("status", "ok");
    if (a.boolean("save", true)) {
        int v = store.save(g, "from table " + t.id);
        if (v < 0)
            return textContent("failed to save graph", true);
        out.set("id", g.id);
        out.set("version", (double)v);
    }
    out.set("type", g.type);
    out.set("nodes", (double)g.nodes.size());
    out.set("edges", (double)g.edges.size());
    return textContent(out.dump(2));
}

inline Json tableAlignTool(gts::TableStore& tables, const Json& a)
{
    gt::Table   primary, target;
    std::string err;
    if (!tables.load(a.str("primary_id"), primary, 0, &err))
        return textContent(err, true);
    if (!tables.load(a.str("target_id"), target, 0, &err))
        return textContent(err, true);
    Json align =
        gtb::tableAlign(primary, target, a.str("primary_key"), a.str("target_key"));
    int v = tables.save(target, a.str("note", "aligned via MCP"), &err);
    if (v < 0)
        return textContent(err.empty() ? "failed to save target table" : err, true);
    Json out = Json::obj();
    out.set("status", "ok");
    out.set("target_id", target.id);
    out.set("version", (double)v);
    out.set("align", align);
    return textContent(out.dump(2));
}

inline Json tableCheckTool(gts::TableStore& tables, const Json& a)
{
    gt::Table   target;
    std::string err;
    if (!tables.load(a.str("id"), target, 0, &err))
        return textContent(err, true);

    Json        allowed;
    std::string allowedRaw = a.str("allowed");
    if (!allowedRaw.empty()) {
        std::string perr;
        allowed = Json::parse(allowedRaw, &perr);
        if (!perr.empty())
            return textContent("invalid allowed JSON: " + perr, true);
    }
    else if (const Json* j = a.find("allowed")) {
        allowed = *j;
    }

    gt::Table       rules;
    const gt::Table* rulesPtr = nullptr;
    if (!a.str("rules_id").empty()) {
        if (!tables.load(a.str("rules_id"), rules, 0, &err))
            return textContent(err, true);
        rulesPtr = &rules;
    }

    bool ignoreHint = false;
    bool used_legacy_hint_default = false;
    if (a.find("ignore_hint_row")) {
        ignoreHint = a.boolean("ignore_hint_row", false);
    }
    else {
        // 向后兼容接口，等待后续处理或删除
        if (ge::envFlagEnabled("GRAPHMCP_TABLE_CHECK_LEGACY_HINT")) {
            ignoreHint               = false;
            used_legacy_hint_default = true;
        }
        else {
            ignoreHint = target.hasHintRow;
        }
    }
    gt::Table report = gtb::tableCheck(target, allowed, rulesPtr, ignoreHint);
    if (!a.str("name").empty())
        report.name = a.str("name");
    Json out = Json::obj();
    out.set("status", "ok");
    out.set("violations", (double)report.rows.size());
    out.set("ignore_hint_row", ignoreHint);
    // 向后兼容接口，等待后续处理或删除
    if (used_legacy_hint_default)
        appendCompatWarning(
            out,
            "GRAPHMCP_TABLE_CHECK_LEGACY_HINT enabled; default ignore_hint_row "
            "forced to false");
    out.set("report_csv", report.toCsv());
    if (a.boolean("save", false)) {
        int v = tables.save(report, "check report for " + target.id, &err);
        if (v < 0)
            return textContent(err.empty() ? "failed to save report" : err, true);
        out.set("report_id", report.id);
        out.set("version", (double)v);
    }
    return textContent(out.dump(2));
}

}  // namespace tabletools
