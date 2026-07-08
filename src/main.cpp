// main.cpp - graphmcp 命令行入口
// 这是一个图设计与绘制 MCP 工具：将结构化图描述（Mermaid / Markdown 大纲 /
// CSV / XML / Excalidraw JSON）解析为统一图模型，再执行校验、自动布局、
// 带版本历史的存储、导出（drawio / Mermaid / Excalidraw / SVG / PNG / PDF /
// 浏览器 URL）、外部编辑器打开，以及通过 MCP 协议提供服务。
#include <iostream>

#include "parsers.hpp"

#include "exporters.hpp"

#include "storage.hpp"

#include "mcp.hpp"

#ifdef _WIN32
#    include <windows.h>

#    include <shellapi.h>
#endif

using gj::Json;
using gm::Graph;

namespace {

// Args: 命令行参数结果（command=主命令，opts=--key value 形式选项表）
struct Args
{
    std::string                        command;
    std::map<std::string, std::string> opts;
    bool                               has(const std::string& k) const
    { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const
    {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
};

// parseArgs: 解析命令行参数
// 关键步骤：读取 command -> 处理 --k=v / --k v / -o 三种参数写法
Args parseArgs(int argc, char** argv)
{
    Args a;
    if (argc >= 2)
        a.command = argv[1];
    for (int i = 2; i < argc; i++) {
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
            a.opts[key] = val;
        }
        else if (s == "-o" && i + 1 < argc) {
            a.opts["output"] = argv[++i];
        }
    }
    return a;
}

// readInput: 输入源统一入口（input 文件 > content 直传 > stdin）
std::string readInput(const Args& a)
{
    if (a.has("input")) {
        std::string txt = ge::readFile(a.get("input"));
        if (txt.empty()) {
            std::cerr << "error: cannot read input file: " << a.get("input")
                      << "\n";
            exit(2);
        }
        return txt;
    }
    if (a.has("content"))
        return a.get("content");
    // 从标准输入读取内容
    std::ostringstream os;
    os << std::cin.rdbuf();
    return os.str();
}

// printIssues: 统一打印校验问题列表
void printIssues(const std::vector<gl::Issue>& issues)
{
    for (auto& i : issues)
        std::cout << "  [" << i.severity << "] " << i.message << "\n";
}

// usage: 输出帮助信息
int usage()
{
    std::cout
        << "graphmcp " << mcp::SERVER_VERSION
        << " - graph design & drawing MCP tool\n"
           "\n"
           "usage: graphmcp <command> [options]\n"
           "\n"
           "commands:\n"
           "  create    --input <file> [--format auto] [--type auto] [--name "
           "X] [--id X]\n"
           "            parse + validate + layout + save to store; prints "
           "graph id\n"
           "  update    --id <id> --input <file> [--format auto] [--type X] "
           "[--name X]\n"
           "            update stored graph from new input (version "
           "auto-increments)\n"
           "  convert   --input <file> --to <fmt> [-o out]     one-shot "
           "conversion\n"
           "  export    --id <id> --to <fmt> [-o out] [--version N]\n"
           "  open      --id <id> [--editor browser|drawio|excalidraw|svg] "
           "[--editor-path <path>]\n"
           "  validate  --input <file> | --id <id>\n"
           "  list                                            list stored "
           "graphs\n"
           "  history   --id <id>                             show version "
           "history\n"
           "  rollback  --id <id> --version N                 restore old "
           "version\n"
           "  serve                                           run MCP stdio "
           "server\n"
           "\n"
           "formats:\n"
           "  input : mermaid | markdown | csv | xml | excalidraw | model | "
           "auto\n"
           "  output: drawio | mermaid | excalidraw | svg | png | pdf | url | "
           "model\n"
           "\n"
           "environment:\n"
           "  GRAPHMCP_STORE   store directory (default ./graph-store)\n"
           "  GRAPHMCP_EDITOR  default external editor for open command\n";
    return 1;
}

}  // namespace

#ifdef _WIN32
// Windows 传入的 argv 使用 ANSI 代码页；这里重新按 UTF-8 获取，
// 以保证中文名称等内容写入 JSON 存储时不乱码。
// utf8Args: Windows 下将系统参数转为 UTF-8，避免中文路径/名称乱码
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

// main: CLI 主入口
// 关键步骤：参数解析 -> 命令分发 -> 统一异常处理
int main(int argc, char** argv)
{
    srand((unsigned)time(nullptr));
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> uargs = utf8Args();
    std::vector<char*>       uptrs;
    for (auto& s : uargs)
        uptrs.push_back(const_cast<char*>(s.c_str()));
    if ((int)uptrs.size() == argc)
        argv = uptrs.data();
#endif
    Args      a = parseArgs(argc, argv);
    gs::Store store(a.get("store"));

    try {
        if (a.command == "serve") {
            return mcp::serve(store);
        }
        if (a.command == "create") {
            Graph g = gp::parseAny(readInput(a), a.get("format", "auto"),
                                   a.get("type"));
            if (a.has("name"))
                g.name = a.get("name");
            if (a.has("id"))
                g.id = a.get("id");
            auto issues = gl::validate(g);
            if (!issues.empty())
                printIssues(issues);
            if (gl::hasErrors(issues)) {
                std::cerr << "rejected: graph has structural errors\n";
                return 3;
            }
            gl::layout(g);
            int v = store.save(g, a.get("note", "created via CLI"));
            std::cout << "created graph '" << g.name << "' id=" << g.id << " v"
                      << v << " (" << g.nodes.size() << " nodes, "
                      << g.edges.size() << " edges, type=" << g.type << ")\n";
            return 0;
        }
        if (a.command == "convert") {
            if (!a.has("to"))
                return usage();
            Graph g = gp::parseAny(readInput(a), a.get("format", "auto"),
                                   a.get("type"));
            ge::ExportResult r =
                ge::exportGraph(g, a.get("to"), a.get("output"));
            if (!r.ok) {
                std::cerr << "error: " << r.message << "\n";
                return 4;
            }
            if (!r.content.empty())
                std::cout << r.content << "\n";
            else
                std::cout << r.message << "\n";
            return 0;
        }
        if (a.command == "export") {
            if (!a.has("id") || !a.has("to"))
                return usage();
            Graph       g;
            std::string err;
            if (!store.load(a.get("id"), g, atoi(a.get("version", "0").c_str()),
                            &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
            }
            ge::ExportResult r =
                ge::exportGraph(g, a.get("to"), a.get("output"));
            if (!r.ok) {
                std::cerr << "error: " << r.message << "\n";
                return 4;
            }
            if (!r.content.empty())
                std::cout << r.content << "\n";
            else
                std::cout << r.message << "\n";
            return 0;
        }
        if (a.command == "open") {
            if (!a.has("id"))
                return usage();
            Graph       g;
            std::string err;
            if (!store.load(a.get("id"), g, 0, &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
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
                    return 4;
                }
            }
            std::string editorPath = a.get("editor-path");
            if (editorPath.empty())
                editorPath = ge::editorFromEnv();
            std::cout << "opening: " << target << "\n";
            if (!ge::openExternal(target, editorPath))
                std::cerr
                    << "warning: could not launch handler; open manually\n";
            return 0;
        }
        if (a.command == "update") {
            if (!a.has("id"))
                return usage();
            Graph       g;
            std::string err;
            if (!store.load(a.get("id"), g, 0, &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
            }
            std::string newContent = readInput(a);
            if (newContent.empty()) {
                std::cerr
                    << "error: no input provided (use --input, --content, "
                       "or pipe)\n";
                return 1;
            }
            Graph updated = gp::parseAny(newContent, a.get("format", "auto"),
                                         a.get("type", g.type));
            updated.id    = g.id;
            if (a.has("name"))
                updated.name = a.get("name");
            else
                updated.name = g.name;
            auto issues = gl::validate(updated);
            if (!issues.empty())
                printIssues(issues);
            if (gl::hasErrors(issues)) {
                std::cerr << "rejected: updated graph has structural errors\n";
                return 3;
            }
            gl::layout(updated);
            int v = store.save(updated, a.get("note", "updated via CLI"));
            std::cout << "updated graph '" << updated.name
                      << "' id=" << updated.id << " v" << v << " ("
                      << updated.nodes.size() << " nodes, "
                      << updated.edges.size() << " edges)\n";
            return 0;
        }
        if (a.command == "validate") {
            Graph g;
            if (a.has("id")) {
                std::string err;
                if (!store.load(a.get("id"), g, 0, &err)) {
                    std::cerr << "error: " << err << "\n";
                    return 5;
                }
            }
            else {
                g = gp::parseAny(readInput(a), a.get("format", "auto"));
            }
            auto issues = gl::validate(g);
            if (issues.empty()) {
                std::cout << "valid: no issues found\n";
                return 0;
            }
            printIssues(issues);
            return gl::hasErrors(issues) ? 3 : 0;
        }
        if (a.command == "list") {
            Json idx = store.loadIndex();
            for (auto& e : *idx["graphs"].a) {
                std::cout << e.str("id") << "  " << e.str("type") << "  v"
                          << (int)e.num("versions") << "  "
                          << e.str("updatedAt") << "  " << e.str("name")
                          << "\n";
            }
            if (idx["graphs"].size() == 0)
                std::cout << "(store is empty)\n";
            return 0;
        }
        if (a.command == "history") {
            if (!a.has("id"))
                return usage();
            Json h = store.history(a.get("id"));
            if (h.size() == 0) {
                std::cerr << "no history for graph: " << a.get("id") << "\n";
                return 5;
            }
            for (auto& e : *h.a) {
                std::cout << "v" << (int)e.num("version") << "  "
                          << e.str("savedAt")
                          << "  nodes=" << (int)e.num("nodes")
                          << " edges=" << (int)e.num("edges");
                if (!e.str("note").empty())
                    std::cout << "  (" << e.str("note") << ")";
                std::cout << "\n";
            }
            return 0;
        }
        if (a.command == "rollback") {
            if (!a.has("id") || !a.has("version"))
                return usage();
            int         nv = 0;
            std::string err;
            if (!store.rollback(a.get("id"), atoi(a.get("version").c_str()),
                                &nv, &err)) {
                std::cerr << "error: " << err << "\n";
                return 5;
            }
            std::cout << "rolled back " << a.get("id") << " to v"
                      << a.get("version") << " (saved as v" << nv << ")\n";
            return 0;
        }
        return usage();
    }
    catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
