// main.cpp - graphmcp 命令行入口
// 这是一个图设计与绘制 MCP 工具：将结构化图描述（Mermaid / Markdown 大纲 /
// CSV / XML / Excalidraw JSON）解析为统一图模型，再执行校验、自动布局、
// 带版本历史的存储、导出（drawio / Mermaid / Excalidraw / SVG / PNG / PDF /
// 浏览器 URL）、外部编辑器打开、版本管理（Draft/Stage/Commit）、
// Cursor 元素操作，以及通过 MCP 协议提供服务。
#include "cursor_types.hpp"
#include "mcp.hpp"
#include "model.hpp"
#include "parsers.hpp"
#include "table_bridge.hpp"
#include "table_storage.hpp"
#include "version_manager.hpp"

#include <iostream>
#ifdef _WIN32
#    include <shellapi.h>
#    include <windows.h>
#endif

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;
namespace {

// ===================================================================
// Args: 命令行参数解析结果
// family    = 命令族
// (create/convert/export/edit/layout/validate/store/version/graph/serve/dump-tools)
// subcommand = 子命令 (from-mermaid/draft/commit/show/update/insert/delete...)
// positionals = 位置参数 (图 ID、版本号等)
// opts       = --key value 选项（多值）
// ===================================================================
struct Args
{
    std::string                                     family;
    std::string                                     subcommand;
    std::vector<std::string>                        positionals;
    std::map<std::string, std::vector<std::string>> opts;

    bool has(const std::string& k) const
    { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const
    {
        auto it = opts.find(k);
        return (it == opts.end() || it->second.empty()) ? def : it->second[0];
    }
    std::vector<std::string> getAll(const std::string& k) const
    {
        auto it = opts.find(k);
        return it == opts.end() ? std::vector<std::string>{} : it->second;
    }
};

// parseArgs: 解析命令行参数（支持嵌套子命令 + 多值选项）
Args parseArgs(int argc, char** argv)
{
    Args a;
    if (argc >= 2)
        a.family = argv[1];
    if (argc >= 3 && argv[2][0] != '-')
        a.subcommand = argv[2];

    int pos = a.subcommand.empty() ? 2 : 3;
    for (int i = pos; i < argc; i++) {
        std::string s = argv[i];
        if (s.size() > 2 && s.compare(0, 2, "--") == 0) {
            std::string key = s.substr(2);
            std::string val = "true";
            size_t      eq  = key.find('=');
            if (eq != std::string::npos) {
                val = key.substr(eq + 1);
                key.resize(eq);
            }
            else if (i + 1 < argc && argv[i + 1][0] != '-') {
                val = argv[++i];
            }
            // 去除 val 两端引号
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            a.opts[key].push_back(val);
        }
        else if (s == "-o" && i + 1 < argc) {
            a.opts["output"].push_back(argv[++i]);
        }
        else if (s == "-m" && i + 1 < argc) {
            a.opts["message"].push_back(argv[++i]);
        }
        else if (s[0] != '-') {
            a.positionals.push_back(s);
        }
    }
    return a;
}

// readInput: 输入源统一入口（--file > --content > stdin）
std::string readInput(const Args& a)
{
    if (a.has("input") || a.has("file")) {
        std::string path = a.has("file") ? a.get("file") : a.get("input");
        std::string txt  = ge::readFile(path);
        if (txt.empty()) {
            std::cerr << "error: cannot read file: " << path << "\n";
            exit(2);
        }
        return txt;
    }
    if (a.has("content"))
        return a.get("content");
    std::ostringstream os;
    os << std::cin.rdbuf();
    return os.str();
}

// getStoreDir: 取存储目录
std::string getStoreDir(const Args& a)
{
    if (a.has("store"))
        return a.get("store");
    const char* env = getenv("GRAPHMCP_STORE");
    return env ? env : "graph-store";
}

// getGraphId: 从参数中取图 ID（positionals[0] 或 --id）
std::string getGraphId(const Args& a)
{
    if (a.has("id"))
        return a.get("id");
    if (!a.positionals.empty())
        return a.positionals[0];
    return "";
}

// resolveVersionNested: 解析 version draft/stage 的 action 与 graph id
// 支持 version draft show my-graph（positionals=[show,my-graph]）
static bool resolveVersionNested(const Args&        a,
                                 const std::string& defaultAction,
                                 std::string&       outId,
                                 std::string&       outAction)
{
    auto isKnownAction = [](const std::string& s) {
        return s == "show" || s == "reset" || s == "add" || s == "clear";
    };
    outId     = a.has("id") ? a.get("id") : "";
    outAction = defaultAction;
    if (!a.positionals.empty()) {
        const std::string& p0 = a.positionals[0];
        if (isKnownAction(p0)) {
            outAction = p0;
            if (outId.empty() && a.positionals.size() > 1)
                outId = a.positionals[1];
        }
        else if (outId.empty()) {
            outId = p0;
        }
    }
    return !outId.empty();
}

// printIssues: 打印校验问题列表
void printIssues(const std::vector<gl::Issue>& issues)
{
    for (auto& i : issues)
        std::cout << "  [" << i.severity << "] " << i.message << "\n";
}

// ===================================================================
// 帮助信息
// ===================================================================
int usage(const std::string& hint = "", int exitCode = 1)
{
    if (!hint.empty())
        std::cerr << hint << "\n\n";

    std::cout
        << "graphmcp " << mcp::serverVersion()
        << " - graph design & drawing MCP tool\n"
           "\n"
           "usage: graphmcp <family> <subcommand> [positionals] [options]\n"
           "\n"
           "command families:\n"
           "  create    "
           "from-{mermaid|markdown|csv|xml|excalidraw|model|input}\n"
           "  convert   to-{drawio|mermaid|excalidraw|svg|png|pdf|url|model}\n"
           "  export    to-{drawio|mermaid|excalidraw|svg|png|pdf|url|model}\n"
           "  edit      with-{browser|drawio|excalidraw|svg}\n"
           "  layout    {auto|layered|tree-h|tree-v|grid}\n"
           "  validate  {graph|input}\n"
           "  store     {list|show|load|delete}\n"
           "  table     {create|import|export|list|show|update|delete|history|"
           "rollback|from-graph|from-table|align|check}\n"
           "  version   {status|draft|stage|commit|log|show|diff|checkout}\n"
           "  graph     {show|update|insert|delete}\n"
           "  cursor    {open|get|next|prev|close}\n"
           "  draft     {status|discard}\n"
           "  serve     (no subcommand)\n"
           "  dump-tools  export MCP tool schemas (openapi|json)\n"
           "\n"
           "common options:\n"
           "  --file <path>       input file\n"
           "  --content <text>    inline input text\n"
           "  --output <path>     output file path (-o shorthand)\n"
           "  --stdout            force output to stdout\n"
           "  --id <graph-id>     graph identifier\n"
           "  --store <dir>       store directory (env GRAPHMCP_STORE)\n"
           "\n"
           "formats:\n"
           "  input : mermaid | markdown | csv | xml | excalidraw | model | "
           "auto\n"
           "  output: drawio | mermaid | excalidraw | svg | png | pdf | url | "
           "model\n"
           "\n"
           "examples:\n"
           "  graphmcp create from-mermaid --file flow.mmd --name \"login\"\n"
           "  graphmcp convert to-svg --content \"A-->B\" --input-format "
           "mermaid --stdout\n"
           "  graphmcp store list\n"
           "  graphmcp version commit g7abc -m \"add error handling\"\n"
           "  graphmcp graph update --node A --set label=\"Start\"\n"
           "  graphmcp graph insert --node --type rect --label \"Step\" "
           "--position 400 200\n"
           "  graphmcp serve\n"
           "\n"
           "run 'graphmcp <family> --help' for family-specific options.\n";
    return exitCode;
}

// ===================================================================
// OLD COMMAND HANDLERS (backward compatible)
// ===================================================================

// 旧版指令在此函数中处理，返回 true 表示已处理，false 表示不是旧版指令
bool handleLegacyCommand(const std::string& command, Args& a, gs::Store& store)
{
    // create (旧版)
    if (command == "create" && a.subcommand.empty()) {
        Graph g =
            gp::parseAny(readInput(a), a.get("format", "auto"), a.get("type"));
        if (a.has("name"))
            g.name = a.get("name");
        if (a.has("id"))
            g.id = a.get("id");
        auto issues = gl::validate(g);
        if (!issues.empty())
            printIssues(issues);
        if (gl::hasErrors(issues)) {
            std::cerr << "rejected: graph has structural errors\n";
            exit(3);
        }
        gl::layout(g);
        int v = store.save(g, a.get("note", "created via CLI"));
        std::cout << "created graph '" << g.name << "' id=" << g.id << " v" << v
                  << " (" << g.nodes.size() << " nodes, " << g.edges.size()
                  << " edges, type=" << g.type << ")\n";
        return true;
    }

    // convert (旧版)
    if (command == "convert" && a.subcommand.empty()) {
        if (!a.has("to")) {
            usage("missing --to");
            exit(1);
        }
        Graph g =
            gp::parseAny(readInput(a), a.get("format", "auto"), a.get("type"));
        ge::ExportResult r = ge::exportGraph(g, a.get("to"), a.get("output"));
        if (!r.ok) {
            std::cerr << "error: " << r.message << "\n";
            exit(4);
        }
        if (!r.content.empty())
            std::cout << r.content << "\n";
        else
            std::cout << r.message << "\n";
        return true;
    }

    // export (旧版)
    if (command == "export" && a.subcommand.empty()) {
        if (!a.has("id") || !a.has("to")) {
            usage("missing --id or --to");
            exit(1);
        }
        Graph       g;
        std::string err;
        if (!store.load(a.get("id"), g, atoi(a.get("version", "0").c_str()),
                        &err)) {
            std::cerr << "error: " << err << "\n";
            exit(5);
        }
        ge::ExportResult r = ge::exportGraph(g, a.get("to"), a.get("output"));
        if (!r.ok) {
            std::cerr << "error: " << r.message << "\n";
            exit(4);
        }
        if (!r.content.empty())
            std::cout << r.content << "\n";
        else
            std::cout << r.message << "\n";
        return true;
    }

    // open (旧版)
    if (command == "open" && a.subcommand.empty()) {
        if (!a.has("id")) {
            usage("missing --id");
            exit(1);
        }
        Graph       g;
        std::string err;
        if (!store.load(a.get("id"), g, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            exit(5);
        }
        std::string editor = a.get("editor", "browser");
        std::string target;
        if (editor == "browser" || editor == "mermaid") {
            target = ge::toMermaidLiveUrl(g);
        }
        else {
            std::string fmt    = editor == "drawio"     ? "drawio" :
                                 editor == "excalidraw" ? "excalidraw" :
                                                          "svg";
            std::string ext    = editor == "drawio"     ? ".drawio" :
                                 editor == "excalidraw" ? ".excalidraw" :
                                                          ".svg";
            target             = store.root() + "/" + g.id + "/open" + ext;
            ge::ExportResult r = ge::exportGraph(g, fmt, target);
            if (!r.ok) {
                std::cerr << "error: " << r.message << "\n";
                exit(4);
            }
        }
        std::cout << "opening: " << target << "\n";
        if (!ge::openExternal(target))
            std::cerr << "warning: could not launch handler; open manually\n";
        return true;
    }

    // validate (旧版)
    if (command == "validate" && a.subcommand.empty()) {
        Graph g;
        if (a.has("id")) {
            std::string err;
            if (!store.load(a.get("id"), g, 0, &err)) {
                std::cerr << "error: " << err << "\n";
                exit(5);
            }
        }
        else {
            g = gp::parseAny(readInput(a), a.get("format", "auto"));
        }
        auto issues = gl::validate(g);
        if (issues.empty()) {
            std::cout << "valid: no issues found\n";
            return true;
        }
        printIssues(issues);
        exit(gl::hasErrors(issues) ? 3 : 0);
    }

    // list (旧版)
    if (command == "list" && a.subcommand.empty()) {
        Json idx = store.loadIndex();
        for (auto& e : *idx["graphs"].a) {
            std::cout << e.str("id") << "  " << e.str("type") << "  v"
                      << (int)e.num("versions") << "  " << e.str("updatedAt")
                      << "  " << e.str("name") << "\n";
        }
        if (idx["graphs"].size() == 0)
            std::cout << "(store is empty)\n";
        return true;
    }

    // history (旧版)
    if (command == "history" && a.subcommand.empty()) {
        if (!a.has("id")) {
            usage("missing --id");
            exit(1);
        }
        Json h = store.history(a.get("id"));
        if (h.size() == 0) {
            std::cerr << "no history for graph: " << a.get("id") << "\n";
            exit(5);
        }
        for (auto& e : *h.a) {
            std::cout << "v" << (int)e.num("version") << "  "
                      << e.str("savedAt") << "  nodes=" << (int)e.num("nodes")
                      << " edges=" << (int)e.num("edges");
            if (!e.str("note").empty())
                std::cout << "  (" << e.str("note") << ")";
            std::cout << "\n";
        }
        return true;
    }

    // rollback (旧版)
    if (command == "rollback" && a.subcommand.empty()) {
        if (!a.has("id") || !a.has("version")) {
            usage("missing --id or --version");
            exit(1);
        }
        int         nv = 0;
        std::string err;
        if (!store.rollback(a.get("id"), atoi(a.get("version").c_str()), &nv,
                            &err)) {
            std::cerr << "error: " << err << "\n";
            exit(5);
        }
        std::cout << "rolled back " << a.get("id") << " to v"
                  << a.get("version") << " (saved as v" << nv << ")\n";
        return true;
    }

    return false;  // 不是旧版指令
}

// ===================================================================
// NEW COMMAND FAMILY HANDLERS
// ===================================================================

// ─── create 命令族 ─────────────────────────────────────────────
int cmdCreate(Args& a, gs::Store& store)
{
    std::string fmt = a.subcommand;
    if (fmt.empty()) {
        usage("create requires subcommand");
        return 1;
    }

    // from-mermaid / from-markdown / from-csv / from-xml / from-excalidraw /
    // from-model / from-input
    std::string inputFormat;
    if (fmt == "from-mermaid")
        inputFormat = "mermaid";
    else if (fmt == "from-markdown")
        inputFormat = "markdown";
    else if (fmt == "from-csv")
        inputFormat = "csv";
    else if (fmt == "from-xml")
        inputFormat = "xml";
    else if (fmt == "from-excalidraw")
        inputFormat = "excalidraw";
    else if (fmt == "from-model")
        inputFormat = "model";
    else if (fmt == "from-input")
        inputFormat = "auto";
    else {
        usage("unknown create subcommand: " + fmt);
        return 1;
    }

    Graph g = gp::parseAny(readInput(a), a.get("input-format", inputFormat),
                           a.get("type"));
    if (a.has("name"))
        g.name = a.get("name");
    if (a.has("id"))
        g.id = a.get("id");

    bool doValidate = !a.has("no-validate");
    bool doLayout   = !a.has("no-layout");

    if (doValidate) {
        auto issues = gl::validate(g);
        if (!issues.empty())
            printIssues(issues);
        if (gl::hasErrors(issues)) {
            std::cerr << "rejected: graph has structural errors (use "
                         "--no-validate to force)\n";
            return 3;
        }
    }
    if (doLayout)
        gl::layout(g);

    int v = store.save(g, a.get("note", "created via CLI"));
    std::cout << "created graph '" << g.name << "' id=" << g.id << " v" << v
              << " (" << g.nodes.size() << " nodes, " << g.edges.size()
              << " edges, type=" << g.type << ")\n";
    return 0;
}

// ─── convert 命令族 ────────────────────────────────────────────
int cmdConvert(Args& a)
{
    std::string targetFmt;
    if (a.subcommand.size() > 3 && a.subcommand.compare(0, 3, "to-") == 0)
        targetFmt = a.subcommand.substr(3);
    if (targetFmt.empty()) {
        usage("convert requires to-<format> subcommand");
        return 1;
    }

    Graph g = gp::parseAny(readInput(a), a.get("input-format", "auto"));
    if (!a.has("no-layout") && targetFmt != "url")
        gl::layout(g);

    std::string outPath = a.get("output");
    if (a.has("stdout"))
        outPath = "";
    ge::ExportResult r = ge::exportGraph(g, targetFmt, outPath);
    if (!r.ok) {
        std::cerr << "error: " << r.message << "\n";
        return 4;
    }
    if (!r.content.empty())
        std::cout << r.content << "\n";
    else if (!r.message.empty())
        std::cout << r.message << "\n";
    return 0;
}

// ─── export 命令族 ─────────────────────────────────────────────
int cmdExport(Args& a, gs::Store& store)
{
    std::string targetFmt;
    if (a.subcommand.size() > 3 && a.subcommand.compare(0, 3, "to-") == 0)
        targetFmt = a.subcommand.substr(3);
    if (targetFmt.empty()) {
        usage("export requires to-<format> subcommand");
        return 1;
    }

    std::string id = getGraphId(a);
    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    Graph       g;
    std::string err;
    if (!store.load(id, g, atoi(a.get("version", "0").c_str()), &err)) {
        std::cerr << "error: " << err << "\n";
        return 5;
    }
    if (!a.has("no-layout") && targetFmt != "url" && targetFmt != "model")
        gl::layout(g);

    std::string outPath = a.get("output");
    if (a.has("stdout"))
        outPath = "";
    ge::ExportResult r = ge::exportGraph(g, targetFmt, outPath);
    if (!r.ok) {
        std::cerr << "error: " << r.message << "\n";
        return 4;
    }
    if (!r.content.empty())
        std::cout << r.content << "\n";
    else if (!r.message.empty())
        std::cout << r.message << "\n";
    return 0;
}

// ─── edit 命令族 ───────────────────────────────────────────────
int cmdEdit(Args& a, gs::Store& store)
{
    std::string editor;
    if (a.subcommand.size() > 5 && a.subcommand.compare(0, 5, "with-") == 0)
        editor = a.subcommand.substr(5);
    if (editor.empty()) {
        usage("edit requires with-<editor> subcommand");
        return 1;
    }

    std::string id = getGraphId(a);
    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    Graph       g;
    std::string err;
    int         ver = atoi(a.get("version", "0").c_str());
    if (!store.load(id, g, ver, &err)) {
        std::cerr << "error: " << err << "\n";
        return 5;
    }
    int currentVer = ver > 0 ? ver : (int)store.history(id).size();
    std::cout << "editing graph '" << g.name << "' v"
              << currentVer << " (" << g.nodes.size() << " nodes, "
              << g.edges.size() << " edges)\n";

    std::string target;
    if (editor == "browser") {
        target = ge::toMermaidLiveUrl(g);
    }
    else {
        std::string fmt = editor == "drawio"     ? "drawio" :
                          editor == "excalidraw" ? "excalidraw" :
                                                   "svg";
        std::string ext = editor == "drawio"     ? ".drawio" :
                          editor == "excalidraw" ? ".excalidraw" :
                                                   ".svg";
        target = a.get("output", store.root() + "/" + g.id + "/open" + ext);
        // 检查是否已有编辑中的文件，提醒用户先 import 保存
        std::string existing = ge::readFile(target);
        if (!existing.empty())
            std::cout << "note: " << target
                      << " already exists and will be overwritten.\n"
                      << "  run 'graphmcp import --id " << id
                      << "' first to save changes.\n";
        ge::ExportResult r = ge::exportGraph(g, fmt, target);
        if (!r.ok) {
            std::cerr << "error: " << r.message << "\n";
            return 4;
        }
    }
    std::string editorPath = ge::resolveEditor(editor,
                                                a.get("editor-path"));
    std::cout << "opening: " << target;
    if (!editorPath.empty())
        std::cout << " (editor: " << editorPath << ")";
    std::cout << "\n";
    if (!ge::openExternal(target, editorPath)) {
        std::cerr << "warning: could not launch editor; open manually: "
                  << target << "\n";
        std::string drawio = ge::findDrawioDesktop();
        std::string vscode = ge::findVSCode();
        if (!drawio.empty())
            std::cerr << "  hint: draw.io Desktop found at " << drawio << "\n";
        if (!vscode.empty())
            std::cerr << "  hint: VS Code found at " << vscode << "\n";
    }
    return 0;
}

// ─── import 命令 ───────────────────────────────────────────────
int cmdImport(Args& a, gs::Store& store) {
    std::string id = getGraphId(a);
    if (id.empty()) { usage("missing graph id"); return 1; }

    Graph g; std::string err;
    if (!store.load(id, g, 0, &err)) {
        std::cerr << "error: " << err << "\n"; return 5;
    }

    std::string content = readInput(a);
    std::string fmt     = a.get("format", "auto");
    if (content.empty())
        content = ge::readOpenFile(store.root(), g.id, fmt);
    if (content.empty()) {
        std::cerr << "error: no input provided.\n"
                     "  graphmcp import --id <id>\n"
                     "  graphmcp import --id <id> --file <edited-file>\n"
                     "  graphmcp import --id <id> --content \"...\" --format "
                     "<fmt>\n";
        return 1;
    }

    Graph imported = gp::parseAny(content, fmt, g.type);
    imported.id    = g.id;
    imported.name  = g.name;
    auto issues    = gl::validate(imported);
    if (!issues.empty())
        printIssues(issues);
    if (gl::hasErrors(issues)) {
        std::cerr << "rejected: imported graph has structural errors\n";
        return 3;
    }
    gl::layout(imported);
    int v = store.save(imported, a.get("note", "re-imported after external edit"));
    std::cout << "imported '" << imported.name
              << "' id=" << imported.id << " v" << v << " ("
              << imported.nodes.size() << " nodes, "
              << imported.edges.size() << " edges)\n";
    return 0;
}

// ─── layout 命令族 ─────────────────────────────────────────────
int cmdLayout(Args& a, gs::Store& store)
{
    std::string id = getGraphId(a);
    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    Graph       g;
    std::string err;
    if (!store.load(id, g, 0, &err)) {
        std::cerr << "error: " << err << "\n";
        return 5;
    }

    std::string strat = a.subcommand;  // auto|layered|tree-h|tree-v|grid
    if (strat == "auto" || strat == "layered" || strat == "tree-h" ||
        strat == "tree-v" || strat == "grid") {
        gl::layout(g, true, strat);
    }
    else {
        usage("unknown layout subcommand: " + a.subcommand);
        return 1;
    }

    if (a.has("save")) {
        int v = store.save(g, "layout updated");
        std::cout << "layout applied and saved as v" << v << "\n";
    }
    else {
        std::cout << "layout applied (" << g.nodes.size()
                  << " nodes positioned)\n";
    }
    return 0;
}

// ─── validate 命令族 ──────────────────────────────────────────
int cmdValidate(Args& a, gs::Store& store)
{
    Graph g;
    if (a.subcommand == "graph") {
        std::string id = getGraphId(a);
        if (id.empty()) {
            usage("missing graph id");
            return 1;
        }
        std::string err;
        if (!store.load(id, g, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
    }
    else if (a.subcommand == "input") {
        g = gp::parseAny(readInput(a), a.get("input-format", "auto"));
    }
    else {
        usage("validate requires 'graph' or 'input' subcommand");
        return 1;
    }

    auto issues = gl::validate(g);
    if (issues.empty()) {
        std::cout << "valid: no issues found\n";
        return 0;
    }

    bool quiet  = a.has("quiet");
    bool strict = a.has("strict");
    for (auto& i : issues) {
        if (quiet && i.severity == "warning")
            continue;
        std::cout << "  [" << i.severity << "] " << i.message << "\n";
    }
    if (strict)
        return gl::hasErrors(issues) ? 3 : 1;
    return gl::hasErrors(issues) ? 3 : 0;
}

// ─── store 命令族 ─────────────────────────────────────────────
int cmdStore(Args& a, gs::Store& store)
{
    if (a.subcommand == "list") {
        Json        idx        = store.loadIndex();
        std::string filterType = a.get("type");
        std::string format     = a.get("format", "table");
        int         count      = 0;

        if (format == "json") {
            std::cout << idx.dump(2) << "\n";
            return 0;
        }

        for (auto& e : *idx["graphs"].a) {
            if (!filterType.empty() && e.str("type") != filterType)
                continue;
            std::cout << e.str("id") << "  " << e.str("type") << "  v"
                      << (int)e.num("versions") << "  " << e.str("updatedAt")
                      << "  " << e.str("name") << "\n";
            count++;
        }
        if (count == 0)
            std::cout << "(store is empty)\n";
        else
            std::cout << count << " graph(s)\n";
        return 0;
    }

    std::string id = getGraphId(a);
    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    if (a.subcommand == "show") {
        Graph       g;
        std::string err;
        if (!store.load(id, g, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        std::cout << "Graph: " << g.name << " (" << g.id << ")\n";
        std::cout << "  type: " << g.type << "\n";
        std::cout << "  nodes: " << g.nodes.size() << "\n";
        std::cout << "  edges: " << g.edges.size() << "\n";
        std::cout << "  laidOut: " << (g.laidOut ? "yes" : "no") << "\n";
        Json idx = store.loadIndex();
        for (auto& e : *idx["graphs"].a) {
            if (e.str("id") == id) {
                std::cout << "  created: " << e.str("createdAt") << "\n";
                std::cout << "  updated: " << e.str("updatedAt") << "\n";
                std::cout << "  versions: " << (int)e.num("versions") << "\n";
                break;
            }
        }
        return 0;
    }

    if (a.subcommand == "load") {
        int         ver = atoi(a.get("version", "0").c_str());
        Graph       g;
        std::string err;
        if (!store.load(id, g, ver, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        std::cout << g.toJson().dump(2) << "\n";
        return 0;
    }

    if (a.subcommand == "delete") {
        if (!a.has("force")) {
            std::cerr << "warning: this will delete graph '" << id
                      << "' and all its versions.\n"
                      << "use --force to confirm deletion.\n";
            return 1;
        }
        // 删除存储目录
        std::string dir = store.root() + "/" + id;
        ge::removeDirectory(dir);
        // 更新 index
        Json idx       = store.loadIndex();
        Json newGraphs = Json::arr();
        for (auto& e : *idx["graphs"].a) {
            if (e.str("id") != id)
                newGraphs.push(e);
        }
        idx.set("graphs", newGraphs);
        ge::writeFile(store.root() + "/index.json", idx.dump(2));
        std::cout << "deleted graph: " << id << "\n";
        return 0;
    }

    usage("unknown store subcommand: " + a.subcommand);
    return 1;
}

// ─── table 命令族（通用 CSV 表）────────────────────────────────
int cmdTable(Args& a, gs::Store& store)
{
    gts::TableStore tables(store.root());

    if (a.subcommand == "list") {
        Json idx = tables.loadIndex();
        for (auto& e : *idx["tables"].a) {
            std::cout << e.str("id") << "\t" << e.str("name") << "\tcols="
                      << (int)e.num("columns") << "\trows=" << (int)e.num("rows")
                      << "\tv" << (int)e.num("versions") << "\n";
        }
        return 0;
    }

    if (a.subcommand == "create" || a.subcommand == "import") {
        std::string content = readInput(a);
        if (content.empty()) {
            usage("missing --file/--content");
            return 1;
        }
        gt::Table t = gt::Table::fromCsv(content);
        if (a.has("id"))
            t.id = a.get("id");
        if (a.has("name"))
            t.name = a.get("name");
        if (a.subcommand == "create" && !t.id.empty() && tables.exists(t.id) &&
            !a.has("force")) {
            // 向后兼容接口，等待后续处理或删除
            std::string legacy =
                ge::getEnvVar("GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT");
            if (legacy.empty() || legacy == "0") {
                std::cerr << "error: table already exists: " << t.id
                          << " (use --force or table import)\n";
                return 1;
            }
            std::cerr
                << "warning: GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT enabled; "
                   "overwriting existing table\n";
        }
        std::string err;
        int v = tables.save(t, a.get("note", a.subcommand + " via CLI"), &err);
        if (v < 0) {
            std::cerr << "error: " << (err.empty() ? "save failed" : err)
                      << "\n";
            return 5;
        }
        std::cout << "table " << t.id << " v" << v << " (" << t.columns.size()
                  << " cols, " << t.rows.size() << " rows)\n";
        return 0;
    }

    std::string id = getGraphId(a);

    if (a.subcommand == "show" || a.subcommand == "export") {
        if (id.empty()) {
            usage("missing table id");
            return 1;
        }
        gt::Table   t;
        std::string err;
        int         ver = atoi(a.get("version", "0").c_str());
        if (!tables.load(id, t, ver, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        std::string text =
            (a.get("to", "csv") == "model") ? t.toJson().dump(2) : t.toCsv();
        if (a.has("output")) {
            if (!ge::writeFile(a.get("output"), text)) {
                std::cerr << "error: failed to write " << a.get("output")
                          << "\n";
                return 5;
            }
            std::cout << "wrote " << a.get("output") << "\n";
        }
        else {
            std::cout << text;
        }
        return 0;
    }

    if (a.subcommand == "history") {
        if (id.empty()) {
            usage("missing table id");
            return 1;
        }
        std::cout << tables.history(id).dump(2) << "\n";
        return 0;
    }

    if (a.subcommand == "rollback") {
        if (id.empty() || a.positionals.size() < 2) {
            usage("table rollback <id> <version>");
            return 1;
        }
        int         ver = atoi(a.positionals[1].c_str());
        int         nv  = 0;
        std::string err;
        if (!tables.rollback(id, ver, &nv, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        std::cout << "rolled back to new v" << nv << "\n";
        return 0;
    }

    if (a.subcommand == "delete") {
        if (id.empty()) {
            usage("missing table id");
            return 1;
        }
        if (!a.has("force")) {
            std::cerr << "use --force to confirm deletion\n";
            return 1;
        }
        std::string err;
        if (!tables.remove(id, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        std::cout << "deleted table: " << id << "\n";
        return 0;
    }

    if (a.subcommand == "update") {
        if (id.empty()) {
            usage("missing table id");
            return 1;
        }
        // CLI 便捷：--add-row CSV 行；--set col=value 作用于最后一行或 --row
        gt::Table   t;
        std::string err;
        if (!tables.load(id, t, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        if (a.has("add-row")) {
            t.appendRow(gt::splitCsvLine(a.get("add-row")));
        }
        if (a.has("add-column")) {
            t.addColumn(a.get("add-column"), a.get("default", ""));
        }
        if (a.has("set")) {
            int row = atoi(a.get("row", "-1").c_str());
            if (row < 0)
                row = (int)t.rows.size() - 1;
            if (row < 0) {
                t.appendRow({});
                row = 0;
            }
            for (auto& pair : a.getAll("set")) {
                size_t eq = pair.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string col = pair.substr(0, eq);
                std::string val = pair.substr(eq + 1);
                int         ci  = t.colIndex(col);
                if (ci < 0) {
                    std::cerr << "unknown column: " << col << "\n";
                    return 1;
                }
                t.setCell((size_t)row, (size_t)ci, val);
            }
        }
        std::string err2;
        int v = tables.save(t, a.get("note", "updated via CLI"), &err2);
        if (v < 0) {
            std::cerr << "error: " << (err2.empty() ? "save failed" : err2)
                      << "\n";
            return 5;
        }
        std::cout << "table " << t.id << " v" << v << "\n";
        return 0;
    }

    if (a.subcommand == "from-graph") {
        std::string gid = a.get("graph-id");
        if (gid.empty())
            gid = a.has("graph") ? a.get("graph") : id;
        if (gid.empty()) {
            usage("table from-graph --graph-id <id> --mode skeleton");
            return 1;
        }
        Graph       g;
        std::string err;
        if (!store.load(gid, g, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        gt::Table t = gtb::tableFromGraph(g, a.get("mode", "skeleton"),
                                          a.has("with-hint-row"));
        if (a.has("name"))
            t.name = a.get("name");
        int v = 0;
        std::string saveErr;
        v = tables.save(t, "from graph " + gid, &saveErr);
        if (v < 0) {
            std::cerr << "error: " << (saveErr.empty() ? "save failed" : saveErr)
                      << "\n";
            return 5;
        }
        std::cout << "table " << t.id << " v" << v << "\n";
        std::cout << t.toCsv();
        return 0;
    }

    if (a.subcommand == "from-table" || a.subcommand == "to-graph") {
        // graph from table
        if (id.empty() && !a.has("file") && !a.has("content")) {
            usage("table from-table <table-id> or --file csv");
            return 1;
        }
        gt::Table t;
        if (a.has("file") || a.has("content")) {
            t = gt::Table::fromCsv(readInput(a));
        }
        else {
            std::string err;
            if (!tables.load(id, t, 0, &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
            }
        }
        Graph g = gtb::graphFromTable(t, a.get("from-col"), a.get("to-col"),
                                      a.get("label-col"), a.get("id-col"),
                                      a.get("parent-col"));
        if (a.has("name"))
            g.name = a.get("name");
        int v = store.save(g, "from table");
        std::cout << "graph " << g.id << " v" << v << " nodes=" << g.nodes.size()
                  << " edges=" << g.edges.size() << "\n";
        return 0;
    }

    if (a.subcommand == "align") {
        std::string pid = a.get("primary");
        std::string tid = a.get("target");
        if (pid.empty() || tid.empty()) {
            usage("table align --primary <id> --target <id> --primary-key "
                  "--target-key");
            return 1;
        }
        gt::Table   primary, target;
        std::string err;
        if (!tables.load(pid, primary, 0, &err) ||
            !tables.load(tid, target, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        Json align = gtb::tableAlign(primary, target, a.get("primary-key"),
                                     a.get("target-key"));
        std::string saveErr;
        int  v     = tables.save(target, "aligned via CLI", &saveErr);
        if (v < 0) {
            std::cerr << "error: " << (saveErr.empty() ? "save failed" : saveErr)
                      << "\n";
            return 5;
        }
        std::cout << "aligned target " << tid << " v" << v << " "
                  << align.dump() << "\n";
        return 0;
    }

    if (a.subcommand == "check") {
        if (id.empty()) {
            usage("table check <id> --allowed '{...}'");
            return 1;
        }
        gt::Table   target;
        std::string err;
        if (!tables.load(id, target, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
        Json allowed;
        if (a.has("allowed")) {
            std::string perr;
            allowed = Json::parse(a.get("allowed"), &perr);
            if (!perr.empty()) {
                std::cerr << "error: " << perr << "\n";
                return 1;
            }
        }
        gt::Table rules;
        const gt::Table* rp = nullptr;
        if (a.has("rules")) {
            if (!tables.load(a.get("rules"), rules, 0, &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
            }
            rp = &rules;
        }
        bool ignore_hint_row = false;
        if (a.has("ignore-hint-row")) {
            ignore_hint_row = true;
        }
        else {
            // 向后兼容接口，等待后续处理或删除
            std::string legacy =
                ge::getEnvVar("GRAPHMCP_TABLE_CHECK_LEGACY_HINT");
            if (!legacy.empty() && legacy != "0")
                ignore_hint_row = false;
            else
                ignore_hint_row = target.hasHintRow;
        }
        gt::Table report = gtb::tableCheck(target, allowed, rp, ignore_hint_row);
        std::cout << report.toCsv();
        if (a.has("save")) {
            std::string saveErr;
            int v = tables.save(report, "check report", &saveErr);
            if (v < 0) {
                std::cerr << "error: "
                          << (saveErr.empty() ? "save failed" : saveErr) << "\n";
                return 5;
            }
            std::cout << "# saved report " << report.id << " v" << v << "\n";
        }
        return 0;
    }

    usage("unknown table subcommand: " + a.subcommand);
    return 1;
}

// ─── version 命令族 ───────────────────────────────────────────
int cmdVersion(Args& a, gs::Store& store)
{
    std::string             id = getGraphId(a);
    gv::GraphVersionManager vm(store.root());

    if (a.subcommand == "status") {
        if (id.empty()) {
            usage("missing graph id");
            return 1;
        }
        auto s = vm.status(id);
        std::cout << "Graph: " << s.graphName << " (" << id << ")\n";
        std::cout << "  HEAD:     v" << s.headVersion << "\n";
        std::cout << "  Draft:    " << s.draftOpCount << " operation(s)"
                  << "\n";
        std::cout << "  Staged:   " << s.stagedOpCount << " operation(s)"
                  << "\n";
        std::cout << "  Status:   "
                  << (s.dirty ? "⚠ uncommitted changes" :
                                "✓ working tree clean")
                  << "\n";
        return 0;
    }

    if (a.subcommand == "draft") {
        std::string graphId, action;
        if (!resolveVersionNested(a, "show", graphId, action)) {
            usage("missing graph id");
            return 1;
        }
        if (action == "reset") {
            bool ok = vm.resetDraft(graphId);
            std::cout << (ok ? "draft discarded\n" : "no draft to discard\n");
            return 0;
        }
        // show
        auto draft = vm.loadDraft(graphId);
        if (draft.isEmpty()) {
            std::cout << "Draft is empty (based on v" << draft.baseVersion
                      << ")\n";
            return 0;
        }
        std::cout << "Draft (based on v" << draft.baseVersion << ") — "
                  << draft.operationCount() << " pending operations:\n\n";
        for (int i = 0; i < draft.operationCount(); i++) {
            auto& op = draft.operations[i];
            std::cout << " [" << i << "] " << op.summary() << "\n";
            for (auto& ch : op.changes) {
                std::cout << "       " << ch.field << ": \"" << ch.oldValue
                          << "\" → \"" << ch.newValue << "\"\n";
            }
        }
        return 0;
    }

    if (a.subcommand == "stage") {
        std::string graphId, action;
        if (!resolveVersionNested(a, "add", graphId, action)) {
            usage("missing graph id");
            return 1;
        }
        if (action == "show") {
            auto stage = vm.loadStage(graphId);
            if (stage.isEmpty()) {
                std::cout << "Stage is empty\n";
                return 0;
            }
            std::cout << "Staged " << stage.stagedOpIndices.size()
                      << " operations:\n";
            for (int idx : stage.stagedOpIndices)
                std::cout << "  [" << idx << "]\n";
            if (!stage.message.empty())
                std::cout << "message: " << stage.message << "\n";
            return 0;
        }
        if (action == "clear") {
            vm.clearStage(graphId);
            std::cout << "stage cleared\n";
            return 0;
        }
        // add
        if (a.has("select")) {
            std::string      sel = a.get("select");
            std::vector<int> indices;
            // 解析逗号分隔的索引
            std::string cur;
            for (char c : sel) {
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
            vm.stageSelected(graphId, indices);
            std::cout << "staged " << indices.size() << " operation(s) for "
                      << graphId << "\n";
        }
        else {
            vm.stageAll(graphId);
            auto draft = vm.loadDraft(graphId);
            std::cout << "staged " << draft.operationCount()
                      << " operation(s) for " << graphId << "\n";
        }
        return 0;
    }

    if (a.subcommand == "commit") {
        if (id.empty()) {
            usage("missing graph id");
            return 1;
        }
        std::string msg = a.get("message");
        if (msg.empty()) {
            usage("commit requires -m <message>");
            return 1;
        }
        int nv;
        if (a.has("all")) {
            nv = vm.commitAll(id, msg, a.get("author", "cli"));
        }
        else {
            nv = vm.commit(id, msg, a.get("author", "cli"));
        }
        if (nv < 0) {
            std::cerr << "nothing to commit (stage is empty)\n";
            return 1;
        }
        std::cout << "committed " << id << " v" << nv << ": " << msg << "\n";
        return 0;
    }

    if (a.subcommand == "log") {
        if (id.empty()) {
            usage("missing graph id");
            return 1;
        }
        auto hist = vm.history(id);
        if (hist.empty()) {
            std::cerr << "no history for graph: " << id << "\n";
            return 1;
        }

        std::string format = a.get("format", "table");
        int         limit  = atoi(a.get("limit", "0").c_str());
        int         shown  = 0;

        if (format == "json") {
            Json arr = Json::arr();
            for (auto& m : hist) {
                Json e = Json::obj();
                e.set("version", (double)m.version);
                e.set("message", m.message);
                e.set("timestamp", m.timestamp);
                e.set("nodes", (double)m.nodeCount);
                e.set("edges", (double)m.edgeCount);
                arr.push(e);
            }
            std::cout << arr.dump(2) << "\n";
            return 0;
        }

        for (auto& m : hist) {
            if (limit > 0 && shown >= limit)
                break;
            if (format == "oneline") {
                std::cout << "v" << m.version << " " << m.message << "\n";
            }
            else {
                std::cout << "v" << m.version << "  " << m.timestamp << "  "
                          << m.message << "  (nodes=" << m.nodeCount
                          << " edges=" << m.edgeCount << ")\n";
            }
            shown++;
        }
        return 0;
    }

    if (a.subcommand == "show") {
        if (id.empty()) {
            usage("missing graph id");
            return 1;
        }
        int  ver    = atoi(a.get("version", "0").c_str());
        auto commit = vm.show(id, ver > 0 ? ver : 0);
        if (commit.version == 0) {
            Graph g = vm.loadLatest(id);
            if (g.id.empty()) {
                std::cerr << "graph not found: " << id << "\n";
                return 5;
            }
            std::cout << "Graph: " << g.name << " (" << g.id << ")\n";
            std::cout << "  type: " << g.type << "\n";
            std::cout << "  nodes: " << g.nodes.size() << "\n";
            std::cout << "  edges: " << g.edges.size() << "\n";
            std::cout << g.toJson().dump(2) << "\n";
            return 0;
        }
        std::cout << "v" << commit.version << "  " << commit.timestamp << "  "
                  << commit.message << "\n";
        std::cout << commit.modelSnapshot.dump(2) << "\n";
        return 0;
    }

    if (a.subcommand == "diff") {
        if (a.positionals.size() < 3) {
            usage("version diff requires: <graph-id> <v1> <v2>");
            return 1;
        }
        id       = a.positionals[0];
        int  v1  = atoi(a.positionals[1].c_str());
        int  v2  = atoi(a.positionals[2].c_str());
        auto ops = vm.diff(id, v1, v2);

        std::string format = a.get("format", "unified");
        if (format == "json") {
            Json arr = Json::arr();
            for (auto& op : ops)
                arr.push(op.toJson());
            std::cout << arr.dump(2) << "\n";
            return 0;
        }

        std::cout << "diff " << id << " v" << v1 << " → v" << v2 << "\n";
        int nAdd = 0, nDel = 0, nMod = 0;
        for (auto& op : ops) {
            std::string prefix;
            if (op.type == gv::OpType::NODE_INSERT ||
                op.type == gv::OpType::EDGE_INSERT) {
                prefix = "  +";
                nAdd++;
            }
            else if (op.type == gv::OpType::NODE_DELETE ||
                     op.type == gv::OpType::EDGE_DELETE) {
                prefix = "  -";
                nDel++;
            }
            else {
                prefix = "  ~";
                nMod++;
            }
            std::cout << prefix << " " << op.summary() << "\n";
            for (auto& ch : op.changes) {
                std::cout << "       " << ch.field << ": \"" << ch.oldValue
                          << "\" → \"" << ch.newValue << "\"\n";
            }
        }
        std::cout << "\nSummary: +" << nAdd << " added, ~" << nMod
                  << " modified, -" << nDel << " deleted\n";
        return 0;
    }

    if (a.subcommand == "checkout") {
        if (a.positionals.size() < 2) {
            usage("version checkout requires: <graph-id> <version>");
            return 1;
        }
        id         = a.positionals[0];
        int  ver   = atoi(a.positionals[1].c_str());
        bool force = a.has("force");

        if (!vm.checkout(id, ver, force)) {
            std::cerr
                << "cannot checkout: draft has uncommitted changes.\n"
                << "commit or reset draft first, or use --force to discard.\n";
            return 1;
        }
        std::cout << "HEAD moved to v" << ver << "\n";
        return 0;
    }

    usage("unknown version subcommand: " + a.subcommand);
    return 1;
}

// ─── graph 命令族 (Cursor 操作) ───────────────────────────────
int cmdGraph(Args& a, gs::Store& store)
{
    gv::GraphVersionManager vm(store.root());
    std::string             id = getGraphId(a);
    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    // 加载工作区图（draft materialize）
    Graph g = vm.materializeDraft(id);
    if (g.id.empty() && a.subcommand != "show") {
        // 尝试从存储加载
        std::string err;
        if (!store.load(id, g, 0, &err)) {
            std::cerr << "error: " << err << "\n";
            return 5;
        }
    }

    // 加载或创建 Draft 用于记录操作
    gv::Draft  draft    = vm.loadDraft(id);
    gv::Draft* draftPtr = &draft;

    // ── show ──
    if (a.subcommand == "show") {
        if (a.has("node")) {
            auto        nc = gv::selectNode(g, nullptr, a.get("node"));
            const Node* n  = nc.get();
            if (!n) {
                std::cerr << "node not found: " << a.get("node") << "\n";
                return 5;
            }
            std::cout << "Node: " << n->id << "\n"
                      << "  label:  " << n->label << "\n"
                      << "  shape:  " << n->shape << "\n"
                      << "  parent: "
                      << (n->parent.empty() ? "(root)" : n->parent) << "\n"
                      << "  style:  "
                      << (n->style.empty() ? "(none)" : n->style) << "\n"
                      << "  pos:    (" << n->x << ", " << n->y << ")\n"
                      << "  size:   " << n->w << " × " << n->h << "\n";
            if (!n->attrs.empty()) {
                for (auto& attr : n->attrs)
                    std::cout << "  attr:   " << attr << "\n";
            }
        }
        else if (a.has("edge")) {
            auto        ec = gv::selectEdge(g, nullptr, a.get("edge"));
            const Edge* e  = ec.get();
            if (!e) {
                std::cerr << "edge not found: " << a.get("edge") << "\n";
                return 5;
            }
            std::cout << "Edge: " << e->id << "\n"
                      << "  from:  " << e->from << "\n"
                      << "  to:    " << e->to << "\n"
                      << "  label: " << (e->label.empty() ? "(none)" : e->label)
                      << "\n"
                      << "  style: " << e->style << "\n"
                      << "  arrow: " << e->arrow << "\n";
        }
        else {
            if (g.id.empty()) {
                std::string err;
                if (!store.load(id, g, 0, &err)) {
                    std::cerr << "error: " << err << "\n";
                    return 5;
                }
            }
            std::cout << "══ " << g.name << " ══\n"
                      << "id: " << g.id << "   type: " << g.type
                      << "   version: " << vm.status(id).headVersion << "\n"
                      << "nodes: " << g.nodes.size()
                      << "   edges: " << g.edges.size() << "\n\n";
            std::cout << "Nodes:\n";
            for (auto& n : g.nodes) {
                std::cout << "  " << n.id << "  " << n.shape << "  \""
                          << n.label << "\"";
                if (!n.parent.empty())
                    std::cout << "  parent=" << n.parent;
                std::cout << "\n";
            }
            std::cout << "\nEdges:\n";
            for (auto& e : g.edges) {
                std::cout << "  " << e.id << "  " << e.from << "→" << e.to
                          << "  " << e.style << "  " << e.arrow;
                if (!e.label.empty())
                    std::cout << "  \"" << e.label << "\"";
                std::cout << "\n";
            }
        }
        return 0;
    }

    // ── update ──
    if (a.subcommand == "update") {
        auto setVals = a.getAll("set");
        if (setVals.empty()) {
            usage("update requires --set key=value");
            return 1;
        }

        if (a.has("node")) {
            auto nc = gv::selectNode(g, draftPtr, a.get("node"));
            if (!nc.valid()) {
                std::cerr << "node not found: " << a.get("node") << "\n";
                return 5;
            }
            for (auto& sv : setVals) {
                size_t eq = sv.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = sv.substr(0, eq);
                std::string val = sv.substr(eq + 1);
                nc.set(key, val);
            }
            std::cout << "updated node " << a.get("node") << "\n";
        }
        else if (a.has("edge")) {
            auto ec = gv::selectEdge(g, draftPtr, a.get("edge"));
            if (!ec.valid()) {
                std::cerr << "edge not found: " << a.get("edge") << "\n";
                return 5;
            }
            for (auto& sv : setVals) {
                size_t eq = sv.find('=');
                if (eq == std::string::npos)
                    continue;
                ec.set(sv.substr(0, eq), sv.substr(eq + 1));
            }
            std::cout << "updated edge " << a.get("edge") << "\n";
        }
        else if (a.has("selector")) {
            auto sel = gv::Selector::parse(a.get("selector"));
            auto sc  = gv::select(g, draftPtr, sel);
            if (sc.count() == 0) {
                std::cerr << "no elements matched\n";
                return 1;
            }
            for (auto& sv : setVals) {
                size_t eq = sv.find('=');
                if (eq == std::string::npos)
                    continue;
                sc.setAll(sv.substr(0, eq), sv.substr(eq + 1));
            }
            std::cout << "updated " << sc.count()
                      << " element(s) matching: " << a.get("selector") << "\n";
        }
        else {
            usage("update requires --node, --edge, or --selector");
            return 1;
        }

        vm.saveDraft(id, draft);
        std::cout << "(draft saved: " << draft.operationCount()
                  << " total operation(s))\n";
        return 0;
    }

    // ── insert ──
    if (a.subcommand == "insert") {
        if (a.has("node")) {
            std::string shape  = a.get("type", "rect");
            std::string label  = a.get("label");
            std::string parent = a.get("parent");
            double      x = 0, y = 0, w = 0, h = 0;
            if (a.has("position")) {
                // --position "400 200"
                std::string pos = a.get("position");
                size_t      sp  = pos.find(' ');
                if (sp != std::string::npos) {
                    x = std::atof(pos.substr(0, sp).c_str());
                    y = std::atof(pos.substr(sp + 1).c_str());
                }
            }
            if (a.has("size")) {
                std::string sz = a.get("size");
                size_t      sp = sz.find(' ');
                if (sp != std::string::npos) {
                    w = std::atof(sz.substr(0, sp).c_str());
                    h = std::atof(sz.substr(sp + 1).c_str());
                }
            }
            std::string nid =
                gv::insertNode(g, draftPtr, shape, label, x, y, w, h, parent);
            vm.saveDraft(id, draft);
            std::cout << "inserted node " << nid << ": " << shape << " \""
                      << label << "\"\n";
        }
        else if (a.has("edge")) {
            std::string from  = a.get("from");
            std::string to    = a.get("to");
            std::string label = a.get("label");
            std::string style = a.get("style", "solid");
            std::string arrow = a.get("arrow", "arrow");
            if (from.empty() || to.empty()) {
                usage("insert edge requires --from and --to");
                return 1;
            }
            std::string eid =
                gv::insertEdge(g, draftPtr, from, to, label, style, arrow);
            vm.saveDraft(id, draft);
            std::cout << "inserted edge " << eid << ": " << from << " → " << to
                      << "\n";
        }
        else if (a.has("attr")) {
            std::string nid = a.get("node");
            std::string val = a.get("value");
            if (nid.empty() || val.empty()) {
                usage("insert attr requires --node and --value");
                return 1;
            }
            Node* n = g.findNode(nid);
            if (!n) {
                std::cerr << "node not found: " << nid << "\n";
                return 5;
            }
            n->attrs.push_back(val);
            // 记录为 NODE_UPDATE
            if (draftPtr) {
                gv::Operation op;
                op.type       = gv::OpType::NODE_UPDATE;
                op.targetId   = nid;
                op.targetType = "node";
                op.changes.push_back({"attrs", "", val});
                op.timestamp = gv::nowIso();
                draftPtr->operations.push_back(op);
                draftPtr->updatedAt = gv::nowIso();
                vm.saveDraft(id, draft);
            }
            std::cout << "inserted attr to node " << nid << ": " << val << "\n";
        }
        else {
            usage("insert requires --node, --edge, or --attr");
            return 1;
        }
        return 0;
    }

    // ── delete ──
    if (a.subcommand == "delete") {
        if (a.has("node")) {
            if (!gv::deleteNode(g, draftPtr, a.get("node"))) {
                std::cerr << "node not found: " << a.get("node") << "\n";
                return 5;
            }
            vm.saveDraft(id, draft);
            std::cout << "deleted node " << a.get("node") << "\n";
        }
        else if (a.has("edge")) {
            if (!gv::deleteEdge(g, draftPtr, a.get("edge"))) {
                std::cerr << "edge not found: " << a.get("edge") << "\n";
                return 5;
            }
            vm.saveDraft(id, draft);
            std::cout << "deleted edge " << a.get("edge") << "\n";
        }
        else if (a.has("selector")) {
            auto sel = gv::Selector::parse(a.get("selector"));
            auto sc  = gv::select(g, draftPtr, sel);
            if (sc.count() == 0) {
                std::cerr << "no elements matched\n";
                return 1;
            }
            int n = sc.count();
            sc.deleteAll();
            vm.saveDraft(id, draft);
            std::cout << "deleted " << n
                      << " element(s) matching: " << a.get("selector") << "\n";
        }
        else {
            usage("delete requires --node, --edge, or --selector");
            return 1;
        }
        return 0;
    }

    usage("unknown graph subcommand: " + a.subcommand);
    return 1;
}

// ─── cursor 命令族（游标磁盘持久化）─────────────────────────
int cmdCursor(Args& a, gs::Store& store)
{
    std::string id     = getGraphId(a);
    std::string action = a.subcommand;

    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }

    if (action == "open") {
        Json r = gv::openCursor(store, id, a.get("target", "nodes"));
        if (const Json* e = r.find("error")) {
            std::cerr << "error: " << e->s << "\n";
            return 4;
        }
        std::cout << r.dump(2) << "\n";
        return 0;
    }
    std::string cid = a.get("cursor");
    if (cid.empty()) {
        usage("cursor requires --cursor <id> (from open)");
        return 1;
    }

    Json r;
    if (action == "get")
        r = gv::getCursor(store, id, cid);
    else if (action == "next")
        r = gv::moveCursor(store, id, cid, 1);
    else if (action == "prev")
        r = gv::moveCursor(store, id, cid, -1);
    else if (action == "close")
        r = gv::closeCursor(store, id, cid);
    else {
        std::cerr << "cursor: unknown action '" << action
                  << "' (open|get|next|prev|close)\n";
        return 4;
    }
    if (const Json* e = r.find("error")) {
        std::cerr << "error: " << e->s << "\n";
        return 4;
    }
    std::cout << r.dump(2) << "\n";
    return 0;
}

// ─── draft 命令族（草稿状态 / 丢弃）─────────────────────────
int cmdDraft(Args& a, gs::Store& store)
{
    std::string id     = getGraphId(a);
    std::string action = a.subcommand;

    if (id.empty()) {
        usage("missing graph id");
        return 1;
    }
    gv::GraphVersionManager vm(store.root());

    if (action == "status") {
        Json r = vm.draftStatus(id);
        std::cout << r.dump(2) << "\n";
        return 0;
    }
    if (action == "discard") {
        vm.discardDraft(id);
        std::cout << "draft discarded for " << id << "\n";
        return 0;
    }

    usage("draft: unknown action '" + action + "' (status|discard)");
    return 1;
}

}  // namespace

// ===================================================================
// Windows UTF-8 argv 处理
// ===================================================================
#ifdef _WIN32
static std::vector<std::string> utf8Args()
{
    int                      n     = 0;
    wchar_t**                wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> out;
    for (int i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0,
                                      nullptr, nullptr);
        std::string s((size_t)(len > 0 ? len - 1 : 0), '\0');
        if (len > 0)
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], len, nullptr,
                                nullptr);
        out.push_back(s);
    }
    LocalFree(wargv);
    return out;
}
#endif

// ===================================================================
// main: CLI 主入口
// ===================================================================
// 关键步骤：参数解析 → 尝试旧版指令 → 新版命令族分发 → 统一异常处理
int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> uargs = utf8Args();
    std::vector<char*>       uptrs;
    for (auto& s : uargs)
        uptrs.push_back(const_cast<char*>(s.c_str()));
    if ((int)uptrs.size() == argc)
        argv = uptrs.data();
#endif
    Args a = parseArgs(argc, argv);

    if (a.family.empty() || a.family == "--help" || a.family == "-h" ||
        a.has("help"))
        return usage("", 0);

    gs::Store store(getStoreDir(a));

    try {
        // ── serve (特殊：直接启动 MCP，不经过 handleLegacyCommand) ──
        if (a.family == "serve" && a.subcommand.empty())
            return mcp::serve(store);

        // ── dump-tools：从 toolList() 导出 OpenAPI / JSON schema ──
        if (a.family == "dump-tools") {
            std::string fmt = a.get("format", "openapi");
            std::string text;
            if (fmt == "openapi") {
                text = mcp::dumpOpenApiYaml();
            }
            else if (fmt == "json") {
                text = mcp::toolList().dump(2) + "\n";
            }
            else {
                std::cerr << "error: unknown --format '" << fmt
                          << "' (expected openapi|json)\n";
                return 1;
            }
            if (a.has("output")) {
                if (!ge::writeFile(a.get("output"), text)) {
                    std::cerr << "error: cannot write " << a.get("output")
                              << "\n";
                    return 1;
                }
                return 0;
            }
            std::cout << text;
            return 0;
        }

        // ── 尝试旧版指令兼容 ──
        if (a.subcommand.empty() && handleLegacyCommand(a.family, a, store))
            return 0;

        // ── 新版命令族分发 ──
        if (a.family == "create")
            return cmdCreate(a, store);
        if (a.family == "convert")
            return cmdConvert(a);
        if (a.family == "export")
            return cmdExport(a, store);
        if (a.family == "edit")
            return cmdEdit(a, store);
        if (a.family == "import")
            return cmdImport(a, store);
        if (a.family == "layout")
            return cmdLayout(a, store);
        if (a.family == "validate")
            return cmdValidate(a, store);
        if (a.family == "store")
            return cmdStore(a, store);
        if (a.family == "table")
            return cmdTable(a, store);
        if (a.family == "version")
            return cmdVersion(a, store);
        if (a.family == "graph")
            return cmdGraph(a, store);
        if (a.family == "cursor")
            return cmdCursor(a, store);
        if (a.family == "draft")
            return cmdDraft(a, store);

        return usage("unknown command: " + a.family);
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
