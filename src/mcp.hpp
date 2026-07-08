// mcp.hpp - Model Context Protocol 服务端（基于 stdio 的 JSON-RPC 2.0，
// 按行分隔消息）。对外提供图创建/转换/导出/打开/校验/历史等工具。
#pragma once
#include "exporters.hpp"
#include "parsers.hpp"
#include "storage.hpp"
#include <iostream>

namespace mcp {

using gj::Json;
using gm::Graph;

inline const char* SERVER_NAME      = "graphmcp";
inline const char* SERVER_VERSION   = "1.0.0";
inline const char* PROTOCOL_VERSION = "2024-11-05";

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
// 参数命名：name=工具名，props=参数集合，required=必填参数列表
inline Json toolDef(const std::string& name,
                    const std::string& desc,
                    const Json&        props,
                    const Json&        required)
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

// toolList: 返回服务暴露的全部工具清单
inline Json toolList()
{
    Json tools = Json::arr();
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
        p.set("id",
              prop("string",
                   "existing graph id to update (optional; creates new graph "
                   "if omitted)"));
        Json req = Json::arr();
        req.push(Json("content"));
        tools.push(toolDef("graph_create",
                           "Parse structured diagram content into the unified "
                           "graph model, validate it, "
                           "apply automatic layout and save it to the "
                           "versioned store. Returns the graph id. "
                           "When an existing graph id is provided, the graph "
                           "is updated and the version number is incremented.",
                           p, req));
    }
    {
        Json p = Json::obj();
        p.set("content", prop("string", "diagram source text to convert"));
        p.set("format", prop("string", "input format (default auto)"));
        p.set("to",
              prop("string",
                   "target format: drawio|mermaid|excalidraw|svg|model|url"));
        Json req = Json::arr();
        req.push(Json("content"));
        req.push(Json("to"));
        tools.push(toolDef("graph_convert",
                           "One-shot conversion between diagram formats "
                           "without saving to the store.",
                           p, req));
    }
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
        tools.push(toolDef(
            "graph_export",
            "Export a stored graph to a file or inline content. png/pdf use an "
            "external "
            "converter when available, otherwise an SVG fallback is written.",
            p, req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id to open"));
        p.set("editor",
              prop("string", "target editor: browser (mermaid.live URL) | "
                             "drawio | excalidraw | svg (default browser)"));
        p.set("editorPath",
              prop("string",
                   "explicit editor executable path (overrides OS default and "
                   "GRAPHMCP_EDITOR env var)"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_open",
                           "Open a stored graph in an external editor: "
                           "generates a mermaid.live browser URL "
                           "or writes a .drawio/.excalidraw/.svg file and "
                           "launches the OS default handler.",
                           p, req));
    }
    {
        Json p = Json::obj();
        p.set("content",
              prop("string", "diagram source to validate (alternative to id)"));
        p.set("format", prop("string", "input format (default auto)"));
        p.set("id",
              prop("string",
                   "stored graph id to validate (alternative to content)"));
        tools.push(
            toolDef("graph_validate",
                    "Validate graph structure: duplicate ids, dangling edges, "
                    "hierarchy cycles, "
                    "isolated nodes. Returns a list of errors and warnings.",
                    p, Json::arr()));
    }
    tools.push(toolDef(
        "graph_list",
        "List all graphs in the store with type, version count and timestamps.",
        Json::obj(), Json::arr()));
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        Json req = Json::arr();
        req.push(Json("id"));
        tools.push(toolDef("graph_history",
                           "Show the saved version history of a graph.", p,
                           req));
    }
    {
        Json p = Json::obj();
        p.set("id", prop("string", "graph id"));
        p.set("version", prop("number", "version number to roll back to"));
        Json req = Json::arr();
        req.push(Json("id"));
        req.push(Json("version"));
        tools.push(toolDef("graph_rollback",
                           "Restore an old version as the new latest version "
                           "(non-destructive).",
                           p, req));
    }
    return tools;
}

// ---- 工具执行 ----

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

// ToolRunner: MCP 工具执行器（工具名 -> 具体业务逻辑）
class ToolRunner {
  public:
    explicit ToolRunner(gs::Store& store) : store_(store) {}

    // call: 工具分发总入口，统一捕获异常并返回 isError 文本
    Json call(const std::string& name, const Json& args)
    {
        try {
            if (name == "graph_create")
                return create(args);
            if (name == "graph_convert")
                return convert(args);
            if (name == "graph_export")
                return exportTool(args);
            if (name == "graph_open")
                return open(args);
            if (name == "graph_validate")
                return validateTool(args);
            if (name == "graph_list")
                return list();
            if (name == "graph_history")
                return history(args);
            if (name == "graph_rollback")
                return rollback(args);
            return textContent("unknown tool: " + name, true);
        }
        catch (const std::exception& e) {
            return textContent(std::string("error: ") + e.what(), true);
        }
    }

  private:
    gs::Store& store_;

    // create: 解析输入并保存图；有结构性错误时直接拒绝创建
    // 若提供 id 且对应图已存在，则更新并升版本
    Json create(const Json& a)
    {
        Graph g = gp::parseAny(a.str("content"), a.str("format", "auto"),
                               a.str("type"));
        if (!a.str("id").empty())
            g.id = a.str("id");
        if (!a.str("name").empty())
            g.name = a.str("name");
        auto issues = gl::validate(g);
        if (gl::hasErrors(issues)) {
            Json out = Json::obj();
            out.set("status", "rejected");
            out.set("issues", issuesToJson(issues));
            return textContent(out.dump(2), true);
        }
        gl::layout(g);
        std::string note = a.str("id").empty() ? "created via MCP" :
                                                 "updated via MCP";
        int         v    = store_.save(g, note);
        Json out = Json::obj();
        out.set("status", v > 1 ? "updated" : "created");
        out.set("id", g.id);
        out.set("name", g.name);
        out.set("type", g.type);
        out.set("version", v);
        out.set("nodes", (double)g.nodes.size());
        out.set("edges", (double)g.edges.size());
        if (!issues.empty())
            out.set("warnings", issuesToJson(issues));
        return textContent(out.dump(2));
    }

    // convert: 一次性格式转换，不进入存储层
    static Json convert(const Json& a)
    {
        Graph g = gp::parseAny(a.str("content"), a.str("format", "auto"));
        ge::ExportResult r = ge::exportGraph(g, a.str("to"));
        if (!r.ok)
            return textContent(r.message, true);
        return textContent(r.content.empty() ? r.message : r.content);
    }

    // exportTool: 读取存储图并导出到目标格式
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

    // open: 生成目标文件或 URL，并尝试调用系统默认程序打开
    Json open(const Json& a)
    {
        Graph       g;
        std::string err;
        if (!store_.load(a.str("id"), g, 0, &err))
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
        std::string editorPath = a.str("editorPath");
        if (editorPath.empty())
            editorPath = ge::editorFromEnv();
        bool launched = ge::openExternal(target, editorPath);
        Json out      = Json::obj();
        out.set("target", target);
        out.set("launched", launched);
        std::string hint =
            launched ?
                (editorPath.empty() ?
                     "opened with system default handler" :
                     "opened with editor: " + editorPath) :
                "could not launch automatically; open the target manually";
        if (!ge::findDrawioDesktop().empty())
            hint += " (draw.io Desktop detected)";
        if (!ge::findVSCode().empty())
            hint += " (VS Code detected)";
        out.set("hint", hint);
        return textContent(out.dump(2));
    }

    // validateTool: 校验内容或已存图，返回 valid + issues
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
        Json out    = Json::obj();
        out.set("valid", !gl::hasErrors(issues));
        out.set("issues", issuesToJson(issues));
        return textContent(out.dump(2));
    }

    // list: 返回存储索引 JSON
    Json list()
    {
        Json idx = store_.loadIndex();
        return textContent(idx.dump(2));
    }

    // history: 返回指定图的历史版本信息
    Json history(const Json& a)
    {
        Json h = store_.history(a.str("id"));
        if (h.size() == 0)
            return textContent("no history for graph: " + a.str("id"), true);
        return textContent(h.dump(2));
    }

    // rollback: 回滚到指定版本并生成一个新的最新版本
    Json rollback(const Json& a)
    {
        int         nv = 0;
        std::string err;
        if (!store_.rollback(a.str("id"), (int)a.num("version"), &nv, &err))
            return textContent(err, true);
        Json out = Json::obj();
        out.set("status", "rolled back");
        out.set("restoredFrom", a.num("version"));
        out.set("newVersion", nv);
        return textContent(out.dump(2));
    }
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
// 关键步骤：识别方法 -> 分发工具或协议方法 -> 返回 result/error
inline bool handleMessage(const Json& msg, gs::Store& store, Json& response)
{
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
        info.set("version", SERVER_VERSION);
        result.set("serverInfo", info);
        response = rpcResult(id, result);
        return !isNotification;
    }
    if (method == "notifications/initialized" || method == "initialized")
        return false;
    if (method == "ping") {
        response = rpcResult(id, Json::obj());
        return !isNotification;
    }
    if (method == "tools/list") {
        Json result = Json::obj();
        result.set("tools", toolList());
        response = rpcResult(id, result);
        return !isNotification;
    }
    if (method == "tools/call") {
        const Json* params = msg.find("params");
        if (!params) {
            response = rpcError(id, -32602, "missing params");
            return !isNotification;
        }
        std::string name = params->str("name");
        const Json* args = params->find("arguments");
        ToolRunner  runner(store);
        response = rpcResult(id, runner.call(name, args ? *args : Json::obj()));
        return !isNotification;
    }
    if (isNotification)
        return false;
    response = rpcError(id, -32601, "method not found: " + method);
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
            continue;
        }
        Json response;
        if (handleMessage(msg, store, response))
            std::cout << response.dump() << "\n" << std::flush;
    }
    return 0;
}

}  // namespace mcp
