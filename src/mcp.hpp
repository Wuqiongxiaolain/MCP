// mcp.hpp - Model Context Protocol 服务端（基于 stdio 的 JSON-RPC 2.0，
// 按行分隔消息）。对外提供图创建/转换/导出/打开/校验/布局/版本管理/
// Cursor 操作与通用表（CSV）协作等工具。
#pragma once
#include "cursor_types.hpp"
#include "exporters.hpp"
#include "mcp_table_tools.hpp"
#include "parsers.hpp"
#include "storage.hpp"
#include "version_manager.hpp"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace mcp {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

inline const char* SERVER_NAME      = "graphmcp";
inline const char* PROTOCOL_VERSION = "2024-11-05";

// serverVersion: 从仓库根目录 VERSION 读取应用版本，作为单一版本来源（SSOT）
inline const std::string& serverVersion()
{
    static const std::string cached = []() {
        std::string v = ge::readFile("VERSION");
        while (!v.empty() && std::isspace((unsigned char)v.back()))
            v.pop_back();
        size_t p = 0;
        while (p < v.size() && std::isspace((unsigned char)v[p]))
            p++;
        if (p > 0)
            v = v.substr(p);
        return v.empty() ? std::string("unknown") : v;
    }();
    return cached;
}

// ---- 工具 Schema 辅助 ----

// prop: 构造单个工具参数描述（type + description）
inline Json prop(const std::string& type, const std::string& desc)
{
    Json p = Json::obj();
    p.set("type", type);
    p.set("description", desc);
    return p;
}

// toolDef: 组装 MCP 工具定义
inline Json toolDef(const std::string& name,
                    const std::string& desc,
                    Json               props,
                    Json               required)
{
    Json t = Json::obj();
    t.set("name", name);
    t.set("description", desc);
    Json schema = Json::obj();
    schema.set("type", "object");
    schema.set("properties", props);
    schema.set("required", required);
    t.set("inputSchema", schema);
    return t;
}

// toolList: 返回服务暴露的全部工具清单（以本函数为 OpenAPI 唯一 schema 源）
inline Json toolList()
{
    Json tools = Json::arr();

    // ── 1. graph_create ──────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("content",
              prop("string", "diagram source text (Mermaid, Markdown outline, "
                             "CSV, XML or Excalidraw JSON)"));
        p.set(
            "format",
            prop("string",
                 "input format: mermaid|markdown|csv|xml|excalidraw|model|auto "
                 "(default auto)"));
        p.set("type",
              prop("string",
                   "diagram type override: "
                   "flowchart|architecture|er|orgchart|mindmap|whiteboard"));
        p.set("name", prop("string", "human readable graph name"));
        p.set("no_validate",
              prop("boolean", "skip structural validation (default false)"));
        p.set("no_layout",
              prop("boolean", "skip automatic layout (default false)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("content"));
        tools.push(toolDef(
            "graph_create",
            "Parse structured diagram content into the unified graph model, "
            "optionally validate and auto-layout, and save to the versioned "
            "store. "
            "Returns the graph id.",
            p, req));
    }

    // ── 2. graph_convert ─────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("content", prop("string", "diagram source text to convert"));
        p.set("format", prop("string", "input format (default auto)"));
        p.set("to", prop("string",
                         "target format: "
                         "drawio|mermaid|excalidraw|svg|png|pdf|model|url"));
        Json req = Json::arr();
        req.push(Json("content"));
        req.push(Json("to"));
        tools.push(toolDef("graph_convert",
                           "One-shot conversion between diagram formats "
                           "without saving to the store.",
                           p, req));
    }

    // ── 3. graph_export ──────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id returned by graph_create"));
        p.set("to", prop("string",
                         "target format: "
                         "drawio|mermaid|excalidraw|svg|png|pdf|url|model"));
        p.set("path", prop("string", "output file path (optional; content "
                                     "returned inline when omitted)"));
        p.set("version",
              prop("number", "specific version to export (default: latest)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("to"));
        tools.push(toolDef("graph_export",
                           "Export a stored graph to a file or inline content. "
                           "png/pdf use an external converter when available, "
                           "otherwise an SVG fallback is written.",
                           p, req));
    }

    // ── 4. graph_open ────────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id to open"));
        p.set("editor", prop("string", "target editor: browser | drawio | "
                                       "excalidraw | svg (default browser)"));
        p.set("editorPath",
              prop("string", "explicit editor executable path (overrides "
                             "auto-detection and GRAPHMCP_EDITOR env var)"));
        p.set("version", prop("number", "version to open (default: latest)"));
        p.set("launch",
              prop("boolean",
                   "actually launch the OS handler (default true; set false "
                   "to only generate URL/file; also honored via "
                   "GRAPHMCP_NO_LAUNCH=1)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_open",
                           "Open a stored graph in an external editor: "
                           "generates a mermaid.live browser URL "
                           "or writes a .drawio/.excalidraw/.svg file and "
                           "launches the OS default handler.",
                           p, req));
    }

    // ── 4b. graph_import ──────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id",
              prop("string", "graph id to re-import after external edit"));
        p.set("content", prop("string", "edited diagram source (optional; "
                                        "auto-detects open.<ext> from store)"));
        p.set(
            "format",
            prop(
                "string",
                "input format: drawio|excalidraw|mermaid|markdown|csv|xml|auto "
                "(default auto)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "graph_import",
            "Re-import an externally edited diagram: reads the open.<ext> file "
            "(or provided content), parses it, validates, and saves as a new "
            "version.",
            p, req));
    }

    // ── 5. graph_validate ────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("content",
              prop("string", "diagram source to validate (alternative to id)"));
        p.set("format", prop("string", "input format (default auto)"));
        p.set("id",
              prop("string",
                   "stored graph id to validate (alternative to content)"));
        p.set("strict", prop("boolean", "treat warnings as errors"));
        tools.push(toolDef("graph_validate",
                           "Validate graph structure: duplicate ids, dangling "
                           "edges, hierarchy cycles, "
                           "isolated nodes. Returns {valid, issues[]}.",
                           p, Json::arr()));
    }

    // ── 6. graph_list ────────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("type", prop("string", "filter by diagram type"));
        p.set("format",
              prop("string", "output format: table|json (default json)"));
        tools.push(toolDef("graph_list",
                           "List all graphs in the store with type, version "
                           "count and timestamps.",
                           p, Json::arr()));
    }

    // ── 7. graph_history ─────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("limit", prop("number", "max entries to return (default: all)"));
        p.set("format",
              prop("string", "output format: full|oneline (default full)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_history",
                           "Show the saved version history of a graph with "
                           "optional limit and format.",
                           p, req));
    }

    // ── 8. graph_rollback ────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("version", prop("number", "version number to roll back to"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("version"));
        tools.push(toolDef("graph_rollback",
                           "Restore an old version as the new latest version "
                           "(non-destructive: "
                           "old snapshot saved as new version).",
                           p, req));
    }

    // ── 9. graph_layout 🆕 ───────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("strategy",
              prop("string", "layout strategy: auto|layered|tree-h|tree-v|grid "
                             "(default auto)"));
        p.set("save", prop("boolean",
                           "save layout result back to store (default false)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "graph_layout",
            "Apply automatic layout to a stored graph. Supports layered "
            "(flowchart), "
            "tree-h (mindmap), tree-v (orgchart), grid, and auto strategies. "
            "Optionally saves the result as a new version.",
            p, req));
    }

    // ── 10. graph_delete 🆕 ──────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id to delete"));
        p.set("force", prop("boolean", "skip confirmation (required)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("force"));
        tools.push(toolDef(
            "graph_delete",
            "Delete a graph and all its version history from the store. "
            "This operation is irreversible.",
            p, req));
    }

    // ── 11. graph_show 🆕 ────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("node", prop("string", "show details for a specific node id"));
        p.set("edge", prop("string", "show details for a specific edge id"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_show",
                           "Show graph summary, node details, or edge details. "
                           "Without node/edge params, returns the full "
                           "structure (nodes + edges list).",
                           p, req));
    }

    // ── 12. graph_update 🆕 ──────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("node", prop("string", "target node id"));
        p.set("edge", prop("string", "target edge id"));
        p.set(
            "selector",
            prop("string", "batch selector: shape=rect|label~=Step|parent=g1"));
        p.set("set",
              prop("string",
                   "field=value pairs (can be specified multiple times)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("set"));
        tools.push(toolDef(
            "graph_update",
            "Update node/edge attributes via cursor operation. Changes go to "
            "the draft. "
            "Use --node/--edge for single element or --selector for batch. "
            "Multiple --set flags allowed.",
            p, req));
    }

    // ── 13. graph_insert 🆕 ──────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("element", prop("string", "element type to insert: node|edge"));
        p.set(
            "type",
            prop("string",
                 "node shape: rect|round|diamond|ellipse|circle|stadium|group "
                 "(default rect)"));
        p.set("label", prop("string", "element label"));
        p.set("position", prop("string", "position as 'x y' (e.g. '400 200')"));
        p.set("size", prop("string", "size as 'w h' (e.g. '120 50')"));
        p.set("parent", prop("string", "parent node id"));
        p.set("from", prop("string", "edge source node id"));
        p.set("to", prop("string", "edge target node id"));
        p.set("style",
              prop("string", "edge style: solid|dashed|thick (default solid)"));
        p.set("arrow",
              prop("string", "edge arrow: arrow|none|both (default arrow)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("element"));
        tools.push(toolDef(
            "graph_insert",
            "Insert a node or edge into the graph. Changes go to the draft. "
            "For nodes: specify type, label, position, size, parent. "
            "For edges: specify from, to, label, style, arrow.",
            p, req));
    }

    // ── 14. graph_delete_element 🆕 ──────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("node", prop("string",
                           "node id to delete (cascades to connected edges)"));
        p.set("edge", prop("string", "edge id to delete"));
        p.set("selector", prop("string", "batch selector for mass deletion"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_delete_element",
                           "Delete a node or edge from the graph via cursor. "
                           "Changes go to the draft. "
                           "Deleting a node cascades to its connected edges. "
                           "Use --selector for batch deletion.",
                           p, req));
    }

    // ── 15. graph_status 🆕 ──────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "graph_status",
            "Show working tree status: HEAD version, draft operation count, "
            "staged operation count, and whether the working tree is clean or "
            "dirty.",
            p, req));
    }

    // ── 16. graph_draft 🆕 ───────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("action",
              prop("string", "action: show|reset|status (default show)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "graph_draft",
            "Manage the working draft. 'show' lists pending operations, "
            "'status' compares draft vs latest (added/removed/modified "
            "counts), "
            "'reset' discards all uncommitted changes (irreversible).",
            p, req));
    }

    // ── 17. graph_stage 🆕 ───────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("action", prop("string", "action: add|show|clear (default add)"));
        p.set(
            "select",
            prop("string",
                 "comma-separated operation indices to stage (default: all)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_stage",
                           "Manage the staging area. 'add' stages draft "
                           "operations for commit. "
                           "'show' displays staged operations. 'clear' empties "
                           "the staging area.",
                           p, req));
    }

    // ── 18. graph_commit 🆕 ──────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("message", prop("string", "commit message (required)"));
        p.set("all",
              prop("boolean",
                   "skip staging and commit all draft changes directly"));
        p.set("author", prop("string", "author identifier (default 'mcp')"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("message"));
        tools.push(toolDef(
            "graph_commit",
            "Commit staged (or all draft) changes as a new immutable version. "
            "Clears the staging area and submitted draft operations on "
            "success.",
            p, req));
    }

    // ── 19. graph_diff 🆕 ────────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("v1", prop("number", "old version number"));
        p.set("v2", prop("number", "new version number"));
        p.set("format",
              prop("string", "output format: unified|json (default unified)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("v1"));
        req.push(Json("v2"));
        tools.push(toolDef("graph_diff",
                           "Compare two versions and return the difference as "
                           "a list of operations "
                           "(node/edge insertions, updates, and deletions).",
                           p, req));
    }

    // ── 20. graph_checkout 🆕 ────────────────────────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("version", prop("number", "target version number"));
        p.set("force",
              prop("boolean",
                   "force checkout even with dirty draft (discards draft)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("version"));
        tools.push(toolDef(
            "graph_checkout",
            "Move HEAD to a specified version without creating a new commit. "
            "Requires a clean working tree (no draft) unless --force is used.",
            p, req));
    }

    // ── 21-24. graph_cursor_* 🆕 游标磁盘持久化工具 ────────────
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("target",
              prop("string",
                   "collection to traverse: nodes|edges (default nodes)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "graph_cursor_open",
            "Open a persistent cursor over nodes or edges. Creates a draft "
            "if none exists. Returns a cursor id for subsequent operations.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("cursor", prop("string", "cursor id from graph_cursor_open"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("cursor"));
        tools.push(toolDef("graph_cursor_get",
                           "Read the current item at the cursor position.", p,
                           req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("cursor", prop("string", "cursor id"));
        p.set("delta", prop("number", "+1 for next, -1 for prev (default 1)"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("cursor"));
        tools.push(
            toolDef("graph_cursor_move",
                    "Move the cursor forward (delta=1) or backward (delta=-1).",
                    p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("cursor", prop("string", "cursor id"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("cursor"));
        tools.push(toolDef("graph_cursor_close",
                           "Close the cursor and remove its state file.", p,
                           req));
    }

    // ── table_* 通用表工具（CSV / 表 XML / model）────────────────
    {
        Json p = Json::obj();
        p.set("content",
              prop("string",
                   "table source: CSV, table XML (<table>), or model JSON"));
        p.set("format",
              prop("string", "csv|xml|model (default csv; not auto)"));
        p.set("name", prop("string", "table display name"));
        p.set("id", prop("string", "optional table id"));
        p.set(
            "force",
            prop("boolean",
                 "allow create to overwrite existing table id (default false; "
                 "GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT=1|true restores upsert "
                 "without force)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("content"));
        tools.push(toolDef(
            "table_create",
            "Create a first-class data table from CSV / table XML / model JSON "
            "(separate from graph edge-list CSV or graph XML import).",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id",
              prop("string", "existing table id to overwrite (optional)"));
        p.set("content",
              prop("string",
                   "table source: CSV, table XML (<table>), or model JSON"));
        p.set("format", prop("string", "csv|xml|model (default csv)"));
        p.set("name", prop("string", "table name"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("content"));
        tools.push(toolDef(
            "table_import",
            "Import CSV / table XML / model JSON into a new or existing table "
            "id.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("to", prop("string", "csv|model|xml (default csv)"));
        p.set("path", prop("string", "optional output file path"));
        p.set("version", prop("number", "version to export (default latest)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("table_export",
                           "Export a stored table to CSV, JSON model, or table "
                           "XML.",
                           p, req));
    }
    {
        Json p = Json::obj();
        p.set("format", prop("string", "json|table (default json)"));
        tools.push(
            toolDef("table_list", "List stored tables.", p, Json::arr()));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("limit",
              prop("number", "max data rows to include (default all)"));
        p.set("version", prop("number", "version (default latest)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("table_show",
                           "Show table metadata and rows (optionally limited).",
                           p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("force", prop("boolean", "must be true to delete"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("force"));
        tools.push(toolDef("table_delete", "Delete a table and its versions.",
                           p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("table_history", "List table versions.", p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("version", prop("number", "version to restore"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("version"));
        tools.push(toolDef(
            "table_rollback",
            "Rollback table by re-saving an old snapshot as a new version.", p,
            req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("note", prop("string", "version note"));
        p.set("set_cells",
              prop("string",
                   "JSON array of {row,column|col_index,value} (row 0-based); "
                   "deprecated alias col still accepted"));
        p.set("add_rows", prop("string", "JSON array of row arrays"));
        p.set("delete_rows", prop("string", "JSON array of row indices"));
        p.set("add_columns",
              prop("string", "JSON array of {name,default?} objects"));
        p.set("set_column_values",
              prop("string", "JSON {column, values:[...]} fill column"));
        p.set("dry_run",
              prop("boolean",
                   "if true, compute changes without saving (default false)"));
        p.set("detail",
              prop("boolean",
                   "if true, include per-cell before/after in details "
                   "(default false)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "table_update",
            "Apply batch patches to a table and save a new version; returns "
            "an audit summary of changes. Use dry_run to preview; detail for "
            "per-cell before/after.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("v1", prop("number", "first version (0=latest)"));
        p.set("v2", prop("number", "second version (0=latest)"));
        p.set("content",
              prop("string",
                   "optional CSV / table XML / model JSON to diff against "
                   "latest"));
        p.set("format",
              prop("string", "csv|xml|model for content (default csv)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "table_diff",
            "Diff two table versions, or latest vs provided table content.", p,
            req));
    }
    {
        Json p = Json::obj();
        p.set("graph_id", prop("string", "source graph id"));
        p.set("mode", prop("string",
                           "skeleton|edgelist|hierarchylist|nodelist (default "
                           "skeleton)"));
        p.set("with_hint_row",
              prop("boolean", "skeleton: add enum/hint second row"));
        p.set("name", prop("string", "output table name"));
        p.set("save", prop("boolean", "save table to store (default true)"));
        p.set("preview_rows",
              prop("number",
                   "max rows in csv_preview (default 20; truncated responses "
                   "include hint)"));
        Json req = Json::arr();
        req.push(Json("graph_id"));
        tools.push(
            toolDef("table_from_graph",
                    "Lossy projection from graph to table (skeleton/edgelist/"
                    "hierarchylist/nodelist).",
                    p, req));
    }
    {
        Json p = Json::obj();
        p.set("table_id", prop("string", "source table id"));
        p.set(
            "content",
            prop("string",
                 "optional CSV / table XML / model JSON instead of table_id"));
        p.set("format",
              prop("string", "csv|xml|model for content (default csv)"));
        p.set("from_col", prop("string", "edge list from column"));
        p.set("to_col", prop("string", "edge list to column"));
        p.set("label_col", prop("string", "optional label column"));
        p.set("id_col", prop("string", "hierarchy id column"));
        p.set("parent_col", prop("string", "hierarchy parent column"));
        p.set("name", prop("string", "graph name"));
        p.set("save", prop("boolean", "save graph (default true)"));
        tools.push(toolDef(
            "graph_from_table",
            "Build a graph from edge/hierarchy table columns (lossy; other "
            "columns ignored). content may be CSV / table XML / model.",
            p, Json::arr()));
    }
    {
        Json p = Json::obj();
        p.set("primary_id", prop("string", "primary table id"));
        p.set("target_id", prop("string", "target table to pad"));
        p.set("primary_key", prop("string", "key column on primary"));
        p.set("target_key", prop("string", "key column on target"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("primary_id"));
        req.push(Json("target_id"));
        req.push(Json("primary_key"));
        req.push(Json("target_key"));
        tools.push(toolDef(
            "table_align",
            "Pad target table with missing primary-key rows (copy same-named "
            "columns when present).",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table to check"));
        p.set("allowed",
              prop("string",
                   "JSON object {column:[values]|\"a|b\"} of allowed enums"));
        p.set("rules_id", prop("string", "optional rules table id "
                                         "(columns: column/field, allowed)"));
        p.set("save", prop("boolean", "save report as a new table"));
        p.set(
            "ignore_hint_row",
            prop("boolean",
                 "skip first row when target.hasHintRow (default true if "
                 "target has hint row; GRAPHMCP_TABLE_CHECK_LEGACY_HINT=1|true "
                 "forces default false)"));
        p.set("name", prop("string", "report table name"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "table_check",
            "Validate table cells against allowed enums; returns a violation "
            "report table (row,field,actual,expected,suggestion).",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("graph_id", prop("string", "source mindmap/graph id"));
        p.set("name", prop("string", "rules table name"));
        p.set("id", prop("string", "optional rules table id"));
        p.set("save", prop("boolean", "save rules table (default true)"));
        Json req = Json::arr();
        req.push(Json("graph_id"));
        tools.push(toolDef(
            "table_rules_from_graph",
            "Extract enum rules table (column,allowed,hint) from mindmap "
            "skeleton heuristics (leaves=columns, children=allowed).",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "target table id"));
        p.set("allowed",
              prop("string",
                   "JSON object {column:[values]|\"a|b\"} of allowed enums"));
        p.set("rules_id", prop("string", "optional rules table id"));
        p.set("ignore_hint_row",
              prop("boolean", "skip first row when target.hasHintRow"));
        p.set("save", prop("boolean", "save fixed table (default true)"));
        p.set("save_skipped",
              prop("boolean", "save skipped violations report (default true)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "table_fix_enums",
            "Auto-fix enum violations using table_check suggestions; empty "
            "suggestion rows go to skipped report with reason.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("source_id", prop("string", "source table id"));
        p.set("mode",
              prop("string", "derive mode (only animation_checklist in v1)"));
        p.set("name", prop("string", "output table name"));
        p.set("id", prop("string", "optional output table id"));
        p.set("save", prop("boolean", "save derived table (default true)"));
        Json req = Json::arr();
        req.push(Json("source_id"));
        tools.push(toolDef(
            "table_derive",
            "Derive a new table from source. animation_checklist: columns "
            "containing 动画 with cell value √ become checklist rows.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("source_column", prop("string", "source column name"));
        p.set("target_column",
              prop("string", "target column (created if missing)"));
        p.set("transform", prop("string", "transform type (only slug in v1)"));
        p.set("save", prop("boolean", "save table (default true)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("source_column"));
        req.push(Json("target_column"));
        tools.push(toolDef(
            "table_transform_column",
            "Transform a column into another (slug: deterministic ASCII key).",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("count", prop("number", "number of sample rows (default 1)"));
        p.set("rules_id", prop("string", "optional rules table for enums"));
        p.set("save", prop("boolean", "save table (default true)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef(
            "table_sample_rows",
            "Append conservative placeholder rows (enum first allowed / 动画 "
            "default x / other TODO). Response sets placeholder=true.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "table id"));
        p.set("rows",
              prop("string", "JSON array of objects keyed by column name"));
        p.set("rules_id",
              prop("string",
                   "optional rules: non-empty cells validated; batch rejected "
                   "on illegal enum"));
        p.set("save", prop("boolean", "save table (default true)"));
        p.set("note", prop("string", "version note"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("rows"));
        tools.push(toolDef(
            "table_propose_rows",
            "Append structured object rows aligned to columns; optional enum "
            "validation via rules_id (no NL parsing).",
            p, req));
    }

    return tools;
}

// ---- OpenAPI 导出（代码即文档：以 toolList 为唯一 schema 源）----

// yamlNeedsQuotes: 判断标量是否需加双引号，避免 YAML 解析歧义
inline bool yamlNeedsQuotes(const std::string& s)
{
    if (s.empty())
        return true;
    if (s == "true" || s == "false" || s == "null" || s == "True" ||
        s == "False" || s == "Null" || s == "YES" || s == "NO")
        return true;
    if (s.front() == ' ' || s.back() == ' ')
        return true;
    if (s.front() == '-' || s.front() == '?' || s.front() == ':' ||
        s.front() == '&' || s.front() == '*' || s.front() == '!' ||
        s.front() == '|' || s.front() == '>' || s.front() == '%' ||
        s.front() == '@' || s.front() == '`')
        return true;
    unsigned char first = (unsigned char)s[0];
    if (std::isdigit(first) || first == '+' || first == '.')
        return true;
    for (unsigned char c : s) {
        if (c == ':' || c == '#' || c == '"' || c == '\'' || c == '\\' ||
            c == '\n' || c == '\r' || c == '\t' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ',' || c == '&' || c == '*')
            return true;
    }
    return false;
}

// yamlQuote: 将字符串序列化为 YAML 双引号标量
inline std::string yamlQuote(const std::string& s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                    out += buf;
                }
                else {
                    out += (char)c;
                }
        }
    }
    out += "\"";
    return out;
}

// yamlScalar: 按需引号输出 YAML 标量
inline std::string yamlScalar(const std::string& s)
{ return yamlNeedsQuotes(s) ? yamlQuote(s) : s; }

// indentSpaces: 生成 YAML 缩进空白
inline std::string indentSpaces(int n)
{ return std::string((size_t)n, ' '); }

// jsonToYaml: 将 Json 树序列化为 YAML 文本（保持键序）
// 参数 j: 待序列化值；out: 输出流；indent: 当前缩进空格数；inline_hint:
// 是否紧跟在 key: 后
inline void
jsonToYaml(const Json& j, std::ostringstream& out, int indent, bool after_key)
{
    if (j.isNull()) {
        if (!after_key)
            out << indentSpaces(indent);
        out << "null";
        return;
    }
    if (j.isBool()) {
        if (!after_key)
            out << indentSpaces(indent);
        out << (j.b ? "true" : "false");
        return;
    }
    if (j.isNum()) {
        if (!after_key)
            out << indentSpaces(indent);
        double n = j.n;
        if (n == (double)(long long)n)
            out << (long long)n;
        else
            out << n;
        return;
    }
    if (j.isStr()) {
        if (!after_key)
            out << indentSpaces(indent);
        out << yamlScalar(j.s);
        return;
    }
    if (j.isArr()) {
        if (!j.a || j.a->empty()) {
            if (!after_key)
                out << indentSpaces(indent);
            out << "[]";
            return;
        }
        if (after_key)
            out << "\n";
        for (size_t i = 0; i < j.a->size(); ++i) {
            out << indentSpaces(indent) << "-";
            const Json& item = (*j.a)[i];
            if (item.isObj() && item.o && !item.o->empty()) {
                // 对象数组：首字段与 "- " 同行，其余字段缩进对齐
                const auto& first = (*item.o)[0];
                out << " " << yamlScalar(first.first) << ":";
                if (first.second.isObj() || first.second.isArr()) {
                    if ((first.second.isObj() &&
                         (!first.second.o || first.second.o->empty())) ||
                        (first.second.isArr() &&
                         (!first.second.a || first.second.a->empty()))) {
                        out << " ";
                        jsonToYaml(first.second, out, 0, true);
                    }
                    else {
                        jsonToYaml(first.second, out, indent + 2, true);
                    }
                }
                else {
                    out << " ";
                    jsonToYaml(first.second, out, 0, true);
                }
                for (size_t k = 1; k < item.o->size(); ++k) {
                    out << "\n";
                    const auto& kv = (*item.o)[k];
                    out << indentSpaces(indent + 2) << yamlScalar(kv.first)
                        << ":";
                    if (kv.second.isObj() || kv.second.isArr()) {
                        if ((kv.second.isObj() &&
                             (!kv.second.o || kv.second.o->empty())) ||
                            (kv.second.isArr() &&
                             (!kv.second.a || kv.second.a->empty()))) {
                            out << " ";
                            jsonToYaml(kv.second, out, 0, true);
                        }
                        else {
                            jsonToYaml(kv.second, out, indent + 4, true);
                        }
                    }
                    else {
                        out << " ";
                        jsonToYaml(kv.second, out, 0, true);
                    }
                }
            }
            else if (item.isObj() || item.isArr()) {
                if ((item.isObj() && item.o && !item.o->empty()) ||
                    (item.isArr() && item.a && !item.a->empty())) {
                    out << "\n";
                    jsonToYaml(item, out, indent + 2, false);
                }
                else {
                    out << " ";
                    jsonToYaml(item, out, indent + 2, true);
                }
            }
            else {
                out << " ";
                jsonToYaml(item, out, 0, true);
            }
            if (i + 1 < j.a->size())
                out << "\n";
        }
        return;
    }
    if (j.isObj()) {
        if (!j.o || j.o->empty()) {
            if (!after_key)
                out << indentSpaces(indent);
            out << "{}";
            return;
        }
        if (after_key)
            out << "\n";
        for (size_t i = 0; i < j.o->size(); ++i) {
            const auto& kv = (*j.o)[i];
            out << indentSpaces(indent) << yamlScalar(kv.first) << ":";
            const Json& v = kv.second;
            if (v.isObj() || v.isArr()) {
                if ((v.isObj() && (!v.o || v.o->empty())) ||
                    (v.isArr() && (!v.a || v.a->empty()))) {
                    out << " ";
                    jsonToYaml(v, out, 0, true);
                }
                else {
                    jsonToYaml(v, out, indent + 2, true);
                }
            }
            else {
                out << " ";
                jsonToYaml(v, out, 0, true);
            }
            if (i + 1 < j.o->size())
                out << "\n";
        }
        return;
    }
}

// toolsToOpenApi: 将 toolList() 映射为 OpenAPI 3.0 文档对象
// 约定：每个 MCP 工具 → POST /{name}，requestBody = inputSchema
inline Json toolsToOpenApi()
{
    Json root = Json::obj();
    root.set("openapi", "3.0.3");

    Json info = Json::obj();
    info.set("title", "graphmcp MCP API");
    info.set("version", serverVersion());
    info.set("description",
             "graphmcp MCP tools/call 契约（非 HTTP REST）。"
             "实际传输：JSON-RPC 2.0 over stdio（graphmcp serve）。"
             "本文档由 graphmcp dump-tools 从 toolList() 自动生成。");
    root.set("info", info);

    Json servers = Json::arr();
    Json server  = Json::obj();
    server.set("url", "mcp://graphmcp");
    server.set("description", "本地 MCP 进程（示意，非真实 HTTP）");
    servers.push(server);
    root.set("servers", servers);

    Json paths = Json::obj();
    Json tools = toolList();
    for (size_t i = 0; i < tools.size(); ++i) {
        const Json& tool = tools[i];
        std::string name = tool.str("name");
        if (name.empty())
            continue;
        std::string desc = tool.str("description");

        Json resp200 = Json::obj();
        resp200.set("description",
                    "MCP tools/call 成功返回（content 文本或结构化结果）");

        Json responses = Json::obj();
        responses.set("200", resp200);

        Json        media  = Json::obj();
        const Json* schema = tool.find("inputSchema");
        if (schema)
            media.set("schema", *schema);
        else {
            Json empty = Json::obj();
            empty.set("type", "object");
            media.set("schema", empty);
        }

        Json content = Json::obj();
        content.set("application/json", media);

        Json body = Json::obj();
        body.set("required", true);
        body.set("content", content);

        Json post = Json::obj();
        post.set("operationId", name);
        post.set("summary", name);
        post.set("description", desc);
        post.set("requestBody", body);
        post.set("responses", responses);

        Json path_item = Json::obj();
        path_item.set("post", post);
        paths.set("/" + name, path_item);
    }
    root.set("paths", paths);
    return root;
}

// dumpOpenApiYaml: 将 OpenAPI 文档序列化为 YAML 文本
inline std::string dumpOpenApiYaml()
{
    std::ostringstream out;
    jsonToYaml(toolsToOpenApi(), out, 0, false);
    out << "\n";
    return out.str();
}

// ---- 工具执行辅助 ----

// textContent: MCP tools/call 的标准文本返回封装
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

// logLevel: 读取 GRAPHMCP_LOG 日志级别
inline const char* logLevel()
{
    const char* lvl = std::getenv("GRAPHMCP_LOG");
    return (lvl && *lvl) ? lvl : "off";
}

// logEnabled: 是否启用 MCP 运行时日志
inline bool logEnabled()
{ return std::string(logLevel()) != "off"; }

// logDebugEnabled: 是否启用 debug 级日志
inline bool logDebugEnabled()
{ return std::string(logLevel()) == "debug"; }

// logEvent: 仅向 stderr 输出结构化日志，避免污染 JSON-RPC stdout
inline void logEvent(const std::string& level,
                     const std::string& method,
                     const std::string& tool,
                     long long          durationMs,
                     const std::string& status,
                     const std::string& note = "")
{
    if (!logEnabled())
        return;
    if (level == "debug" && !logDebugEnabled())
        return;
    std::cerr << "[graphmcp] level=" << level << " method=" << method;
    if (!tool.empty())
        std::cerr << " tool=" << tool;
    std::cerr << " duration_ms=" << durationMs << " status=" << status;
    if (!note.empty())
        std::cerr << " note=\"" << note << "\"";
    std::cerr << "\n";
}

// issuesToJson: 将校验问题列表转换为 JSON 数组
inline Json issuesToJson(const std::vector<gl::Issue>& issues)
{
    Json arr = Json::arr();
    for (auto& i : issues) {
        Json j = Json::obj();
        j.set("severity", i.severity);
        j.set("message", i.message);
        arr.push(j);
    }
    return arr;
}

// opsToJson: 将 Operation 列表转为 JSON 数组
inline Json opsToJson(const std::vector<gv::Operation>& ops)
{
    Json arr = Json::arr();
    for (auto& op : ops)
        arr.push(op.toJson());
    return arr;
}

// parseSetPairs: 解析 "key=value" 字符串对列表
inline std::vector<std::pair<std::string, std::string>>
parseSetPairs(const Json& args)
{
    std::vector<std::pair<std::string, std::string>> result;
    // 支持 set 为字符串数组（多次指定）或单字符串
    auto extract = [&](const std::string& raw) {
        size_t eq = raw.find('=');
        if (eq != std::string::npos) {
            result.push_back({raw.substr(0, eq), raw.substr(eq + 1)});
        }
    };
    if (const Json* s = args.find("set")) {
        if (s->isArr()) {
            for (auto& v : *s->a)
                if (v.isStr())
                    extract(v.s);
        }
        else if (s->isStr()) {
            extract(s->s);
        }
    }
    return result;
}

// ToolRunner: MCP 工具执行器（工具名 -> 具体业务逻辑）
class ToolRunner {
  public:
    explicit ToolRunner(gs::Store& store)
        : store_(store), tables_(store.root()), vm_(store.root())
    {
    }

    // call: 工具分发总入口，统一捕获异常并返回 isError 文本
    Json call(const std::string& name, const Json& args)
    {
        try {
            // ── 原有工具（部分增强）──
            if (name == "graph_create")
                return create(args);
            if (name == "graph_convert")
                return convert(args);
            if (name == "graph_export")
                return exportTool(args);
            if (name == "graph_open")
                return open(args);
            if (name == "graph_import")
                return importTool(args);
            if (name == "graph_validate")
                return validateTool(args);
            if (name == "graph_list")
                return list(args);
            if (name == "graph_history")
                return history(args);
            if (name == "graph_rollback")
                return rollback(args);
            // ── 新增工具 ──
            if (name == "graph_layout")
                return layout(args);
            if (name == "graph_delete")
                return deleteGraph(args);
            if (name == "graph_show")
                return show(args);
            if (name == "graph_update")
                return update(args);
            if (name == "graph_insert")
                return insert(args);
            if (name == "graph_delete_element")
                return deleteElement(args);
            if (name == "graph_status")
                return status(args);
            if (name == "graph_draft")
                return draft(args);
            if (name == "graph_stage")
                return stage(args);
            if (name == "graph_commit")
                return commit(args);
            if (name == "graph_diff")
                return diff(args);
            if (name == "graph_checkout")
                return checkout(args);
            // ── 游标持久化工具 ──
            if (name == "graph_cursor_open")
                return cursorOpen(args);
            if (name == "graph_cursor_get")
                return cursorGet(args);
            if (name == "graph_cursor_move")
                return cursorMove(args);
            if (name == "graph_cursor_close")
                return cursorClose(args);
            // ── 通用表工具 ──
            if (name == "table_create")
                return tableCreate(args);
            if (name == "table_import")
                return tableImport(args);
            if (name == "table_export")
                return tableExport(args);
            if (name == "table_list")
                return tableList(args);
            if (name == "table_show")
                return tableShow(args);
            if (name == "table_delete")
                return tableDelete(args);
            if (name == "table_history")
                return tableHistory(args);
            if (name == "table_rollback")
                return tableRollback(args);
            if (name == "table_update")
                return tableUpdate(args);
            if (name == "table_diff")
                return tableDiffTool(args);
            if (name == "table_from_graph")
                return tableFromGraphTool(args);
            if (name == "graph_from_table")
                return graphFromTableTool(args);
            if (name == "table_align")
                return tableAlignTool(args);
            if (name == "table_check")
                return tableCheckTool(args);
            if (name == "table_rules_from_graph")
                return tableRulesFromGraphTool(args);
            if (name == "table_fix_enums")
                return tableFixEnumsTool(args);
            if (name == "table_derive")
                return tableDeriveTool(args);
            if (name == "table_transform_column")
                return tableTransformColumnTool(args);
            if (name == "table_sample_rows")
                return tableSampleRowsTool(args);
            if (name == "table_propose_rows")
                return tableProposeRowsTool(args);
            return textContent("unknown tool: " + name, true);
        }
        catch (const std::exception& e) {
            return textContent(std::string("error: ") + e.what(), true);
        }
    }

  private:
    gs::Store&              store_;
    gts::TableStore         tables_;
    gv::GraphVersionManager vm_;

    // ==========================================================
    // 原有工具实现（部分增强）
    // ==========================================================

    // create: 解析输入并保存图（增强：支持 no_validate / no_layout）
    Json create(const Json& a)
    {
        Graph g = gp::parseAny(a.str("content"), a.str("format", "auto"),
                               a.str("type"));
        if (!a.str("name").empty())
            g.name = a.str("name");

        bool doValidate = !a.boolean("no_validate", false);
        bool doLayout   = !a.boolean("no_layout", false);

        std::vector<gl::Issue> issues;
        if (doValidate) {
            issues = gl::validate(g);
            if (gl::hasErrors(issues)) {
                Json out = Json::obj();
                out.set("status", "rejected");
                out.set("issues", issuesToJson(issues));
                return textContent(out.dump(2), true);
            }
        }
        if (doLayout)
            gl::layout(g);

        std::string note = a.str("note", "created via MCP");
        int         v    = store_.save(g, note);

        Json out = Json::obj();
        out.set("status", "created");
        out.set("id", g.id);
        out.set("name", g.name);
        out.set("type", g.type);
        out.set("version", (double)v);
        out.set("nodes", (double)g.nodes.size());
        out.set("edges", (double)g.edges.size());
        if (!issues.empty())
            out.set("warnings", issuesToJson(issues));
        return textContent(out.dump(2));
    }

    // convert: 一次性格式转换
    Json convert(const Json& a)
    {
        Graph g = gp::parseAny(a.str("content"), a.str("format", "auto"));
        ge::ExportResult r = ge::exportGraph(g, a.str("to"));
        if (!r.ok)
            return textContent(r.message, true);
        return textContent(r.content.empty() ? r.message : r.content);
    }

    // exportTool: 从存储导出
    Json exportTool(const Json& a)
    {
        Graph       g;
        std::string err;
        if (!store_.load(a.str("id"), g, (int)a.num("version", 0), &err))
            return textContent(err, true);
        ge::ExportResult r = ge::exportGraph(g, a.str("to"), a.str("path"));
        if (!r.ok)
            return textContent(r.message, true);
        std::string text = r.content.empty() ?
                               r.message :
                               (r.path.empty() ? r.content : r.message);
        return textContent(text);
    }

    // open: 生成文件/URL 并调起外部编辑器（增强：支持 version 参数）
    Json open(const Json& a)
    {
        Graph       g;
        std::string err;
        int         ver = (int)a.num("version", 0);
        if (!store_.load(a.str("id"), g, ver, &err))
            return textContent(err, true);
        std::string editor = a.str("editor", "browser");
        std::string target;
        if (editor == "browser" || editor == "mermaid") {
            target = ge::toMermaidLiveUrl(g);
        }
        else {
            std::string ext = editor == "drawio"     ? ".drawio" :
                              editor == "excalidraw" ? ".excalidraw" :
                                                       ".svg";
            target          = store_.root() + "/" + g.id + "/open" + ext;
            ge::ExportResult r =
                ge::exportGraph(g,
                                editor == "drawio"     ? "drawio" :
                                editor == "excalidraw" ? "excalidraw" :
                                                         "svg",
                                target);
            if (!r.ok)
                return textContent(r.message, true);
        }
        std::string editorPath = ge::resolveEditor(editor, a.str("editorPath"));
        // launch=false 或 GRAPHMCP_NO_LAUNCH 时只生成目标，不拉起外部程序
        bool do_launch = a.boolean("launch", true);
        bool launched  = false;
        if (do_launch)
            launched = ge::openExternal(target, editorPath);
        Json out = Json::obj();
        out.set("target", target);
        out.set("launched", launched);
        out.set("editor", editor);
        if (!editorPath.empty())
            out.set("editorPath", editorPath);
        // 列出可用编辑器
        std::string available;
        if (!ge::findDrawioDesktop().empty())
            available += "drawio ";
        if (!ge::findVSCode().empty())
            available += "vscode";
        if (!available.empty())
            out.set("availableEditors", available);
        else
            out.set("availableEditors", "");
        out.set("hint",
                !do_launch ?
                    "launch skipped; open the target manually" :
                launched ?
                    (editorPath.empty() ? "opened with system default handler" :
                                          "opened with editor: " + editorPath) :
                    "could not launch automatically; open the target manually");
        return textContent(out.dump(2));
    }

    // importTool: 重新导入外部编辑后的图文件
    Json importTool(const Json& a)
    {
        Graph       g;
        std::string err;
        if (!store_.load(a.str("id"), g, 0, &err))
            return textContent(err, true);
        std::string content = a.str("content");
        std::string fmt     = a.str("format", "auto");
        if (content.empty())
            content = ge::readOpenFile(store_.root(), g.id, fmt);
        if (content.empty())
            return textContent("no edited content found: provide 'content' or "
                               "run 'graph_open' "
                               "first to generate an editable file",
                               true);
        Graph imported = gp::parseAny(content, fmt, g.type);
        imported.id    = g.id;
        imported.name  = g.name;
        auto issues    = gl::validate(imported);
        if (gl::hasErrors(issues)) {
            Json out = Json::obj();
            out.set("status", "rejected");
            out.set("reason", "imported graph has structural errors");
            Json is = Json::arr();
            for (auto& iss : issues) {
                Json ji = Json::obj();
                ji.set("severity", iss.severity);
                ji.set("message", iss.message);
                is.push(ji);
            }
            out.set("issues", is);
            return textContent(out.dump(2), true);
        }
        gl::layout(imported);
        int  v   = store_.save(imported, "re-imported after external edit");
        Json out = Json::obj();
        out.set("status", "imported");
        out.set("id", imported.id);
        out.set("name", imported.name);
        out.set("type", imported.type);
        out.set("version", v);
        out.set("nodes", (double)imported.nodes.size());
        out.set("edges", (double)imported.edges.size());
        if (!issues.empty()) {
            Json ws = Json::arr();
            for (auto& iss : issues) {
                Json ji = Json::obj();
                ji.set("severity", iss.severity);
                ji.set("message", iss.message);
                ws.push(ji);
            }
            out.set("warnings", ws);
        }
        out.set("hint", "use graph_open to edit again or graph_export to "
                        "save the updated diagram");
        return textContent(out.dump(2));
    }

    // validateTool: 校验（增强：支持 strict 模式）
    Json validateTool(const Json& a)
    {
        Graph g;
        if (!a.str("id").empty()) {
            std::string err;
            if (!store_.load(a.str("id"), g, 0, &err))
                return textContent(err, true);
        }
        else if (!a.str("content").empty()) {
            g = gp::parseAny(a.str("content"), a.str("format", "auto"));
        }
        else {
            return textContent("provide either 'id' or 'content'", true);
        }
        auto issues = gl::validate(g);
        bool strict = a.boolean("strict", false);
        bool valid  = strict ? issues.empty() : !gl::hasErrors(issues);

        Json out = Json::obj();
        out.set("valid", valid);
        out.set("issues", issuesToJson(issues));
        if (strict && !valid) {
            out.set("strictFailure", true);
            out.set("warningCount", (double)(issues.size()));
        }
        return textContent(out.dump(2));
    }

    // list: 列出存储（增强：支持 type/filter + format）
    Json list(const Json& a)
    {
        Json        idx        = store_.loadIndex();
        std::string filterType = a.str("type");
        std::string format     = a.str("format", "json");

        if (format == "table") {
            std::ostringstream os;
            int                count = 0;
            for (auto& e : *idx["graphs"].a) {
                if (!filterType.empty() && e.str("type") != filterType)
                    continue;
                os << e.str("id") << "  " << e.str("type") << "  v"
                   << (int)e.num("versions") << "  " << e.str("updatedAt")
                   << "  " << e.str("name") << "\n";
                count++;
            }
            if (count == 0)
                os << "(store is empty)\n";
            else
                os << count << " graph(s)\n";
            return textContent(os.str());
        }

        // JSON format
        if (!filterType.empty()) {
            Json filtered = Json::obj();
            Json graphs   = Json::arr();
            for (auto& e : *idx["graphs"].a)
                if (e.str("type") == filterType)
                    graphs.push(e);
            filtered.set("graphs", graphs);
            return textContent(filtered.dump(2));
        }
        return textContent(idx.dump(2));
    }

    // history: 版本历史（增强：limit + format）
    Json history(const Json& a)
    {
        std::string id   = a.str("id");
        auto        hist = vm_.history(id);
        if (hist.empty())
            return textContent("no history for graph: " + id, true);

        int         limit  = (int)a.num("limit", 0);
        std::string format = a.str("format", "full");

        if (format == "oneline") {
            std::ostringstream os;
            int                shown = 0;
            for (auto& m : hist) {
                if (limit > 0 && shown >= limit)
                    break;
                os << "v" << m.version << " " << m.message << "\n";
                shown++;
            }
            return textContent(os.str());
        }

        Json arr   = Json::arr();
        int  shown = 0;
        for (auto& m : hist) {
            if (limit > 0 && shown >= limit)
                break;
            Json e = Json::obj();
            e.set("version", (double)m.version);
            e.set("message", m.message);
            e.set("timestamp", m.timestamp);
            e.set("nodes", (double)m.nodeCount);
            e.set("edges", (double)m.edgeCount);
            arr.push(e);
            shown++;
        }
        return textContent(arr.dump(2));
    }

    // rollback: 回滚（保持向后兼容）
    Json rollback(const Json& a)
    {
        int         nv = 0;
        std::string err;
        if (!store_.rollback(a.str("id"), (int)a.num("version"), &nv, &err))
            return textContent(err, true);
        Json out = Json::obj();
        out.set("status", "rolled back");
        out.set("restoredFrom", a.num("version"));
        out.set("newVersion", (double)nv);
        return textContent(out.dump(2));
    }

    // ==========================================================
    // 新增工具实现
    // ==========================================================

    // layout: 独立布局计算
    Json layout(const Json& a)
    {
        std::string id = a.str("id");
        Graph       g;
        std::string err;
        if (!store_.load(id, g, 0, &err))
            return textContent(err, true);

        std::string strategy = a.str("strategy", "auto");
        if (strategy == "auto" || strategy == "layered" ||
            strategy == "tree-h" || strategy == "tree-v" ||
            strategy == "grid") {
            gl::layout(g, true, strategy);
        }
        else {
            return textContent("unknown layout strategy: " + strategy, true);
        }

        bool shouldSave = a.boolean("save", false);
        if (shouldSave) {
            int  v   = store_.save(g, "layout via MCP (" + strategy + ")");
            Json out = Json::obj();
            out.set("status", "layout applied and saved");
            out.set("version", (double)v);
            out.set("nodes", (double)g.nodes.size());
            return textContent(out.dump(2));
        }

        Json out = Json::obj();
        out.set("status", "layout applied (not saved)");
        out.set("nodes", (double)g.nodes.size());
        return textContent(out.dump(2));
    }

    // deleteGraph: 删除图
    Json deleteGraph(const Json& a)
    {
        std::string id = a.str("id");
        if (!a.boolean("force", false))
            return textContent(
                "set force=true to confirm deletion of graph: " + id, true);

        // Remove graph directory
        std::string dir = store_.root() + "/" + id;
        ge::removeDirectory(dir);

        // Update index
        Json idx       = store_.loadIndex();
        Json newGraphs = Json::arr();
        for (auto& e : *idx["graphs"].a)
            if (e.str("id") != id)
                newGraphs.push(e);
        idx.set("graphs", newGraphs);
        ge::writeFile(store_.root() + "/index.json", idx.dump(2));

        Json out = Json::obj();
        out.set("status", "deleted");
        out.set("id", id);
        return textContent(out.dump(2));
    }

    // show: 查看图/节点/边详情
    Json show(const Json& a)
    {
        std::string id = a.str("id");
        Graph       g  = vm_.materializeDraft(id);
        if (g.id.empty()) {
            std::string err;
            if (!store_.load(id, g, 0, &err))
                return textContent(err, true);
        }

        if (!a.str("node").empty()) {
            const Node* n = g.findNode(a.str("node"));
            if (!n)
                return textContent("node not found: " + a.str("node"), true);
            Json out = Json::obj();
            out.set("id", n->id);
            out.set("label", n->label);
            out.set("shape", n->shape);
            out.set("parent", n->parent);
            out.set("style", n->style);
            out.set("x", n->x);
            out.set("y", n->y);
            out.set("w", n->w);
            out.set("h", n->h);
            if (!n->attrs.empty()) {
                Json attrs = Json::arr();
                for (auto& at : n->attrs)
                    attrs.push(Json(at));
                out.set("attrs", attrs);
            }
            // Incoming/outgoing edges
            Json inEdges = Json::arr(), outEdges = Json::arr();
            for (auto& e : g.edges) {
                if (e.to == n->id)
                    inEdges.push(Json(e.id));
                if (e.from == n->id)
                    outEdges.push(Json(e.id));
            }
            out.set("incomingEdges", inEdges);
            out.set("outgoingEdges", outEdges);
            return textContent(out.dump(2));
        }

        if (!a.str("edge").empty()) {
            const Edge* e = nullptr;
            for (auto& ed : g.edges)
                if (ed.id == a.str("edge")) {
                    e = &ed;
                    break;
                }
            if (!e)
                return textContent("edge not found: " + a.str("edge"), true);
            Json out = Json::obj();
            out.set("id", e->id);
            out.set("from", e->from);
            out.set("to", e->to);
            out.set("label", e->label);
            out.set("style", e->style);
            out.set("arrow", e->arrow);
            return textContent(out.dump(2));
        }

        // 全图摘要
        auto st  = vm_.status(id);
        Json out = Json::obj();
        out.set("id", g.id);
        out.set("name", g.name);
        out.set("type", g.type);
        out.set("headVersion", (double)st.headVersion);
        out.set("nodes", (double)g.nodes.size());
        out.set("edges", (double)g.edges.size());
        out.set("dirty", st.dirty);
        Json nodesArr = Json::arr();
        for (auto& n : g.nodes) {
            Json nj = Json::obj();
            nj.set("id", n.id);
            nj.set("label", n.label);
            nj.set("shape", n.shape);
            if (!n.parent.empty())
                nj.set("parent", n.parent);
            nodesArr.push(nj);
        }
        out.set("nodeList", nodesArr);
        Json edgesArr = Json::arr();
        for (auto& ed : g.edges) {
            Json ej = Json::obj();
            ej.set("id", ed.id);
            ej.set("from", ed.from);
            ej.set("to", ed.to);
            if (!ed.label.empty())
                ej.set("label", ed.label);
            ej.set("style", ed.style);
            edgesArr.push(ej);
        }
        out.set("edgeList", edgesArr);
        return textContent(out.dump(2));
    }

    // update: Cursor 更新操作
    Json update(const Json& a)
    {
        std::string id = a.str("id");
        Graph       g  = vm_.materializeDraft(id);
        if (g.id.empty()) {
            std::string err;
            if (!store_.load(id, g, 0, &err))
                return textContent(err, true);
        }

        gv::Draft draft = vm_.loadDraft(id);
        auto      pairs = parseSetPairs(a);
        if (pairs.empty())
            return textContent("no set pairs provided", true);

        int updated = 0;
        if (!a.str("node").empty()) {
            gv::NodeCursor nc(g, &draft, a.str("node"));
            if (!nc.valid())
                return textContent("node not found: " + a.str("node"), true);
            for (auto& p : pairs)
                nc.set(p.first, p.second);
            updated = 1;
        }
        else if (!a.str("edge").empty()) {
            gv::EdgeCursor ec(g, &draft, a.str("edge"));
            if (!ec.valid())
                return textContent("edge not found: " + a.str("edge"), true);
            for (auto& p : pairs)
                ec.set(p.first, p.second);
            updated = 1;
        }
        else if (!a.str("selector").empty()) {
            gv::Selector        sel = gv::Selector::parse(a.str("selector"));
            gv::SelectionCursor sc(g, &draft, sel);
            updated = sc.count();
            if (updated == 0)
                return textContent(
                    "no elements matched selector: " + a.str("selector"), true);
            for (auto& p : pairs)
                sc.setAll(p.first, p.second);
        }
        else {
            return textContent("provide 'node', 'edge', or 'selector'", true);
        }

        vm_.saveDraft(id, draft);
        Json out = Json::obj();
        out.set("status", "updated");
        out.set("elementsAffected", (double)updated);
        out.set("draftOperations", (double)draft.operationCount());
        return textContent(out.dump(2));
    }

    // insert: Cursor 插入操作
    Json insert(const Json& a)
    {
        std::string id = a.str("id");
        Graph       g  = vm_.materializeDraft(id);
        if (g.id.empty()) {
            std::string err;
            if (!store_.load(id, g, 0, &err))
                return textContent(err, true);
        }

        gv::Draft   draft   = vm_.loadDraft(id);
        std::string element = a.str("element", "node");

        Json out = Json::obj();
        if (element == "node") {
            std::string shape = a.str("type", "rect");
            std::string label = a.str("label");
            double      x = 0, y = 0, w = 0, h = 0;
            std::string posStr = a.str("position");
            if (!posStr.empty()) {
                size_t sp = posStr.find(' ');
                if (sp != std::string::npos) {
                    x = std::atof(posStr.substr(0, sp).c_str());
                    y = std::atof(posStr.substr(sp + 1).c_str());
                }
            }
            std::string sizeStr = a.str("size");
            if (!sizeStr.empty()) {
                size_t sp = sizeStr.find(' ');
                if (sp != std::string::npos) {
                    w = std::atof(sizeStr.substr(0, sp).c_str());
                    h = std::atof(sizeStr.substr(sp + 1).c_str());
                }
            }
            std::string parent = a.str("parent");
            std::string nid =
                gv::insertNode(g, &draft, shape, label, x, y, w, h, parent);
            out.set("status", "inserted");
            out.set("elementType", "node");
            out.set("elementId", nid);
        }
        else if (element == "edge") {
            std::string from = a.str("from");
            std::string to   = a.str("to");
            if (from.empty() || to.empty())
                return textContent("edge insert requires 'from' and 'to'",
                                   true);
            std::string label = a.str("label");
            std::string style = a.str("style", "solid");
            std::string arrow = a.str("arrow", "arrow");
            std::string eid =
                gv::insertEdge(g, &draft, from, to, label, style, arrow);
            out.set("status", "inserted");
            out.set("elementType", "edge");
            out.set("elementId", eid);
        }
        else {
            return textContent("element must be 'node' or 'edge'", true);
        }

        vm_.saveDraft(id, draft);
        out.set("draftOperations", (double)draft.operationCount());
        return textContent(out.dump(2));
    }

    // deleteElement: Cursor 删除操作
    Json deleteElement(const Json& a)
    {
        std::string id = a.str("id");
        Graph       g  = vm_.materializeDraft(id);
        if (g.id.empty()) {
            std::string err;
            if (!store_.load(id, g, 0, &err))
                return textContent(err, true);
        }

        gv::Draft draft = vm_.loadDraft(id);
        Json      out   = Json::obj();

        if (!a.str("node").empty()) {
            if (!gv::deleteNode(g, &draft, a.str("node")))
                return textContent("node not found: " + a.str("node"), true);
            out.set("status", "deleted");
            out.set("elementType", "node");
            out.set("elementId", a.str("node"));
        }
        else if (!a.str("edge").empty()) {
            if (!gv::deleteEdge(g, &draft, a.str("edge")))
                return textContent("edge not found: " + a.str("edge"), true);
            out.set("status", "deleted");
            out.set("elementType", "edge");
            out.set("elementId", a.str("edge"));
        }
        else if (!a.str("selector").empty()) {
            gv::Selector        sel = gv::Selector::parse(a.str("selector"));
            gv::SelectionCursor sc(g, &draft, sel);
            if (sc.count() == 0)
                return textContent(
                    "no elements matched selector: " + a.str("selector"), true);
            int n = sc.count();
            sc.deleteAll();
            out.set("status", "deleted");
            out.set("elementsAffected", (double)n);
        }
        else {
            return textContent("provide 'node', 'edge', or 'selector'", true);
        }

        vm_.saveDraft(id, draft);
        out.set("draftOperations", (double)draft.operationCount());
        return textContent(out.dump(2));
    }

    // status: 版本状态
    Json status(const Json& a)
    {
        std::string id = a.str("id");
        auto        st = vm_.status(id);
        if (st.headVersion == 0)
            return textContent("graph not found: " + id, true);

        Json out = Json::obj();
        out.set("graphId", id);
        out.set("graphName", st.graphName);
        out.set("graphType", st.graphType);
        out.set("headVersion", (double)st.headVersion);
        out.set("draftOpCount", (double)st.draftOpCount);
        out.set("stagedOpCount", (double)st.stagedOpCount);
        out.set("dirty", st.dirty);
        out.set("status",
                st.dirty ? "uncommitted changes" : "working tree clean");
        return textContent(out.dump(2));
    }

    // draft: Draft 查看/重置/状态对比
    Json draft(const Json& a)
    {
        std::string id     = a.str("id");
        std::string action = a.str("action", "show");

        if (action == "status") {
            return textContent(vm_.draftStatus(id).dump(2));
        }
        if (action == "reset") {
            vm_.resetDraft(id);
            Json out = Json::obj();
            out.set("status", "draft discarded");
            return textContent(out.dump(2));
        }

        // show
        auto d   = vm_.loadDraft(id);
        Json out = Json::obj();
        out.set("graphId", id);
        out.set("baseVersion", (double)d.baseVersion);
        out.set("operationCount", (double)d.operationCount());
        out.set("isEmpty", d.isEmpty());
        out.set("updatedAt", d.updatedAt);
        Json ops = Json::arr();
        for (int i = 0; i < d.operationCount(); i++) {
            Json op = d.operations[i].toJson();
            op.set("index", (double)i);
            ops.push(op);
        }
        out.set("operations", ops);
        return textContent(out.dump(2));
    }

    // stage: Stage 管理
    Json stage(const Json& a)
    {
        std::string id     = a.str("id");
        std::string action = a.str("action", "add");

        if (action == "clear") {
            vm_.clearStage(id);
            Json out = Json::obj();
            out.set("status", "stage cleared");
            return textContent(out.dump(2));
        }

        if (action == "show") {
            auto s   = vm_.loadStage(id);
            Json out = Json::obj();
            out.set("graphId", id);
            out.set("isEmpty", s.isEmpty());
            out.set("message", s.message);
            out.set("stagedAt", s.stagedAt);
            Json indices = Json::arr();
            for (int idx : s.stagedOpIndices)
                indices.push(Json((double)idx));
            out.set("stagedOpIndices", indices);
            out.set("count", (double)s.stagedOpIndices.size());
            return textContent(out.dump(2));
        }

        // add
        std::string selStr = a.str("select");
        if (!selStr.empty()) {
            std::vector<int> indices;
            std::string      cur;
            for (char c : selStr) {
                if (c == ',') {
                    if (!cur.empty()) {
                        indices.push_back(atoi(cur.c_str()));
                        cur.clear();
                    }
                }
                else
                    cur += c;
            }
            if (!cur.empty())
                indices.push_back(atoi(cur.c_str()));
            vm_.stageSelected(id, indices);
        }
        else {
            vm_.stageAll(id);
        }

        auto s   = vm_.loadStage(id);
        Json out = Json::obj();
        out.set("status", "staged");
        out.set("count", (double)s.stagedOpIndices.size());
        return textContent(out.dump(2));
    }

    // commit: 提交
    Json commit(const Json& a)
    {
        std::string id  = a.str("id");
        std::string msg = a.str("message");
        if (msg.empty())
            return textContent("commit requires 'message'", true);

        int nv;
        if (a.boolean("all", false)) {
            nv = vm_.commitAll(id, msg, a.str("author", "mcp"));
        }
        else {
            nv = vm_.commit(id, msg, a.str("author", "mcp"));
        }

        if (nv < 0)
            return textContent("nothing to commit (stage is empty)", true);

        Json out = Json::obj();
        out.set("status", "committed");
        out.set("version", (double)nv);
        out.set("message", msg);
        return textContent(out.dump(2));
    }

    // diff: 版本对比
    Json diff(const Json& a)
    {
        std::string id     = a.str("id");
        int         v1     = (int)a.num("v1");
        int         v2     = (int)a.num("v2");
        std::string format = a.str("format", "unified");

        auto ops = vm_.diff(id, v1, v2);

        if (format == "json") {
            Json out = Json::obj();
            out.set("graphId", id);
            out.set("v1", (double)v1);
            out.set("v2", (double)v2);
            out.set("operations", opsToJson(ops));
            int nAdd = 0, nDel = 0, nMod = 0;
            for (auto& op : ops) {
                if (op.type == gv::OpType::NODE_INSERT ||
                    op.type == gv::OpType::EDGE_INSERT)
                    nAdd++;
                else if (op.type == gv::OpType::NODE_DELETE ||
                         op.type == gv::OpType::EDGE_DELETE)
                    nDel++;
                else
                    nMod++;
            }
            out.set("added", (double)nAdd);
            out.set("modified", (double)nMod);
            out.set("deleted", (double)nDel);
            return textContent(out.dump(2));
        }

        // unified format
        std::ostringstream os;
        os << "diff " << id << " v" << v1 << " -> v" << v2 << "\n";
        for (auto& op : ops) {
            std::string prefix;
            if (op.type == gv::OpType::NODE_INSERT ||
                op.type == gv::OpType::EDGE_INSERT)
                prefix = "+";
            else if (op.type == gv::OpType::NODE_DELETE ||
                     op.type == gv::OpType::EDGE_DELETE)
                prefix = "-";
            else
                prefix = "~";
            os << "  " << prefix << " " << op.summary() << "\n";
            for (auto& ch : op.changes) {
                os << "       " << ch.field << ": \"" << ch.oldValue
                   << "\" -> \"" << ch.newValue << "\"\n";
            }
        }
        return textContent(os.str());
    }

    // checkout: 切换版本
    Json checkout(const Json& a)
    {
        std::string id    = a.str("id");
        int         ver   = (int)a.num("version");
        bool        force = a.boolean("force", false);

        if (!vm_.checkout(id, ver, force))
            return textContent(
                "cannot checkout: draft has uncommitted changes. "
                "commit or reset draft first, or use force=true to discard.",
                true);

        Json out = Json::obj();
        out.set("status", "checkout complete");
        out.set("headVersion", (double)ver);
        return textContent(out.dump(2));
    }

    // ── 游标磁盘持久化工具（基于 gv::openCursor 等函数）─────
    Json cursorOpen(const Json& a)
    {
        std::string id = a.str("id");
        if (id.empty())
            return textContent("cursor: 'id' is required", true);
        Json        r   = gv::openCursor(store_, id, a.str("target", "nodes"));
        const Json* err = r.find("error");
        return textContent(err ? err->s : r.dump(2), err != nullptr);
    }

    Json cursorGet(const Json& a)
    {
        std::string id = a.str("id"), cid = a.str("cursor");
        if (id.empty() || cid.empty())
            return textContent("cursor: 'id' and 'cursor' are required", true);
        Json        r   = gv::getCursor(store_, id, cid);
        const Json* err = r.find("error");
        return textContent(err ? err->s : r.dump(2), err != nullptr);
    }

    Json cursorMove(const Json& a)
    {
        std::string id = a.str("id"), cid = a.str("cursor");
        int         delta = (int)a.num("delta", 1);
        if (id.empty() || cid.empty())
            return textContent("cursor: 'id' and 'cursor' are required", true);
        Json        r   = gv::moveCursor(store_, id, cid, delta);
        const Json* err = r.find("error");
        return textContent(err ? err->s : r.dump(2), err != nullptr);
    }

    Json cursorClose(const Json& a)
    {
        std::string id = a.str("id"), cid = a.str("cursor");
        if (id.empty() || cid.empty())
            return textContent("cursor: 'id' and 'cursor' are required", true);
        Json        r   = gv::closeCursor(store_, id, cid);
        const Json* err = r.find("error");
        return textContent(err ? err->s : r.dump(2), err != nullptr);
    }

    // ==========================================================
    // 通用表工具
    // ==========================================================

    Json tableCreate(const Json& a)
    { return tabletools::tableCreate(tables_, a); }

    Json tableImport(const Json& a)
    { return tabletools::tableImport(tables_, a); }

    Json tableExport(const Json& a)
    { return tabletools::tableExport(tables_, a); }

    Json tableList(const Json& a)
    { return tabletools::tableList(tables_, a); }

    Json tableShow(const Json& a)
    { return tabletools::tableShow(tables_, a); }

    Json tableDelete(const Json& a)
    { return tabletools::tableDelete(tables_, a); }

    Json tableHistory(const Json& a)
    { return tabletools::tableHistory(tables_, a); }

    Json tableRollback(const Json& a)
    { return tabletools::tableRollback(tables_, a); }

    Json tableUpdate(const Json& a)
    { return tabletools::tableUpdate(tables_, a); }

    Json tableDiffTool(const Json& a)
    { return tabletools::tableDiffTool(tables_, a); }

    Json tableFromGraphTool(const Json& a)
    { return tabletools::tableFromGraphTool(store_, tables_, a); }

    Json graphFromTableTool(const Json& a)
    { return tabletools::graphFromTableTool(store_, tables_, a); }

    Json tableAlignTool(const Json& a)
    { return tabletools::tableAlignTool(tables_, a); }

    Json tableCheckTool(const Json& a)
    { return tabletools::tableCheckTool(tables_, a); }

    Json tableRulesFromGraphTool(const Json& a)
    { return tabletools::tableRulesFromGraphTool(store_, tables_, a); }

    Json tableFixEnumsTool(const Json& a)
    { return tabletools::tableFixEnumsTool(tables_, a); }

    Json tableDeriveTool(const Json& a)
    { return tabletools::tableDeriveTool(tables_, a); }

    Json tableTransformColumnTool(const Json& a)
    { return tabletools::tableTransformColumnTool(tables_, a); }

    Json tableSampleRowsTool(const Json& a)
    { return tabletools::tableSampleRowsTool(tables_, a); }

    Json tableProposeRowsTool(const Json& a)
    { return tabletools::tableProposeRowsTool(tables_, a); }
};

// ---- JSON-RPC 通信处理 ----

// rpcResult/rpcError: JSON-RPC 响应构造工具
inline Json rpcResult(const Json& id, Json result)
{
    Json r = Json::obj();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    r.set("result", std::move(result));
    return r;
}

inline Json rpcError(const Json& id, int code, const std::string& msg)
{
    Json r = Json::obj();
    r.set("jsonrpc", "2.0");
    r.set("id", id);
    Json e = Json::obj();
    e.set("code", code);
    e.set("message", msg);
    r.set("error", e);
    return r;
}

// 处理一条请求；若是通知消息且无需响应则返回 false
// handleMessage: 处理单条 JSON-RPC 请求
inline bool handleMessage(const Json& msg, gs::Store& store, Json& response)
{
    auto        started        = std::chrono::steady_clock::now();
    std::string method         = msg.str("method");
    const Json* idPtr          = msg.find("id");
    bool        isNotification = (idPtr == nullptr);
    Json        id             = idPtr ? *idPtr : Json();

    if (method == "initialize") {
        Json result = Json::obj();
        result.set("protocolVersion", PROTOCOL_VERSION);
        Json caps = Json::obj();
        caps.set("tools", Json::obj());
        result.set("capabilities", caps);
        Json info = Json::obj();
        info.set("name", SERVER_NAME);
        info.set("version", serverVersion());
        result.set("serverInfo", info);
        response   = rpcResult(id, result);
        auto ended = std::chrono::steady_clock::now();
        logEvent("info", method, "",
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 "ok");
        return !isNotification;
    }
    if (method == "notifications/initialized" || method == "initialized") {
        auto ended = std::chrono::steady_clock::now();
        logEvent("debug", method, "",
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 "notification");
        return false;
    }
    if (method == "ping") {
        response   = rpcResult(id, Json::obj());
        auto ended = std::chrono::steady_clock::now();
        logEvent("debug", method, "",
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 "ok");
        return !isNotification;
    }
    if (method == "tools/list") {
        Json result = Json::obj();
        result.set("tools", toolList());
        response   = rpcResult(id, result);
        auto ended = std::chrono::steady_clock::now();
        logEvent("info", method, "",
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 "ok");
        return !isNotification;
    }
    if (method == "tools/call") {
        const Json* params = msg.find("params");
        if (!params) {
            response   = rpcError(id, -32602, "missing params");
            auto ended = std::chrono::steady_clock::now();
            logEvent("error", method, "",
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         ended - started)
                         .count(),
                     "error", "missing params");
            return !isNotification;
        }
        std::string name = params->str("name");
        const Json* args = params->find("arguments");
        ToolRunner  runner(store);
        Json        toolResp = runner.call(name, args ? *args : Json::obj());
        bool        isError  = toolResp.boolean("isError", false);
        response             = rpcResult(id, toolResp);
        auto ended           = std::chrono::steady_clock::now();
        logEvent(isError ? "error" : "info", method, name,
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 isError ? "error" : "ok");
        return !isNotification;
    }
    if (isNotification) {
        auto ended = std::chrono::steady_clock::now();
        logEvent("debug", method, "",
                 std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                       started)
                     .count(),
                 "ignored");
        return false;
    }
    response   = rpcError(id, -32601, "method not found: " + method);
    auto ended = std::chrono::steady_clock::now();
    logEvent(
        "error", method, "",
        std::chrono::duration_cast<std::chrono::milliseconds>(ended - started)
            .count(),
        "error", "method not found");
    return true;
}

// 阻塞式 stdio 服务循环（按行读取 JSON-RPC）
// serve: MCP stdio 主循环（逐行读取 JSON-RPC）
inline int serve(gs::Store& store)
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (gm::trim(line).empty())
            continue;
        std::string err;
        Json        msg = Json::parse(line, &err);
        if (!err.empty()) {
            std::cout << rpcError(Json(), -32700, "parse error: " + err).dump()
                      << "\n"
                      << std::flush;
            logEvent("error", "parse", "", 0, "error", err);
            continue;
        }
        Json response;
        if (handleMessage(msg, store, response))
            std::cout << response.dump() << "\n" << std::flush;
    }
    return 0;
}

}  // namespace mcp
