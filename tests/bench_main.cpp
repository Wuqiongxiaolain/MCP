// bench_main.cpp - 微基准测试套件，输出 JSON 供 CI 回归检测
// 编译: g++ -std=c++17 -O2 -o bin/graphmcp_bench tests/bench_main.cpp
#include "../src/mcp.hpp"
#include "../src/mcp_table_tools.hpp"
#include "../src/parsers.hpp"
#include "../src/exporters.hpp"
#include "../src/storage.hpp"
#include "../src/table_bridge.hpp"
#include "../src/table_model.hpp"
#include "../src/table_storage.hpp"
#include "../src/table_xml.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <direct.h>
#    include <windows.h>
#    include <psapi.h>
#else
#    include <sys/stat.h>
#endif

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

// ── 基准用临时存储根：优先 TMPDIR/TEMP，降低 Jenkins 工作区磁盘噪声 ──
static std::string benchTempRoot(const std::string& name)
{
    const char* tmp = std::getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = std::getenv("TEMP");
    if (!tmp || !*tmp)
        tmp = std::getenv("TMP");
#ifdef _WIN32
    if (!tmp || !*tmp)
        tmp = ".";
#else
    if (!tmp || !*tmp)
        tmp = "/tmp";
#endif
    std::string root = std::string(tmp) + "/graphmcp-" + name;
    return root;
}

static void benchSetStoreEnv(const std::string& root)
{
#ifdef _WIN32
    _putenv_s("GRAPHMCP_STORE", root.c_str());
    _putenv_s("GRAPHMCP_NO_LAUNCH", "1");
#else
    setenv("GRAPHMCP_STORE", root.c_str(), 1);
    setenv("GRAPHMCP_NO_LAUNCH", "1", 1);
#endif
}

// 删除指定版本快照（及 meta），避免反复 save 时 versions 线性膨胀主导耗时
static void benchUnlinkVersion(const std::string& dir, int version)
{
    if (version <= 0)
        return;
    std::string base =
        dir + "/versions/v" + std::to_string(version);
    std::remove((base + ".json").c_str());
    std::remove((base + ".meta.json").c_str());
}

// ── 轻量 JSON 输出辅助（不依赖 json.hpp 避免干扰被测对象） ──
static void jsonStr(FILE* f, const std::string& s)
{
    fputc('"', f);
    for (char c : s) {
        if (c == '"' || c == '\\') fputc('\\', f);
        fputc(c, f);
    }
    fputc('"', f);
}

struct BenchResult {
    std::string name;
    std::string unit;    // "us" | "ms"
    double      value;   // 单次调用均值
    double      p50;
    double      p95;
    int         iterations;
};

static std::vector<BenchResult> g_results;

// Timer: 高精度计时 + 百分位统计
struct Timer {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    std::vector<double> samples;  // microseconds

    void lap(TimePoint& t) {
        auto now = Clock::now();
        samples.push_back(
            std::chrono::duration<double, std::micro>(now - t).count());
        t = now;
    }
    double mean() const {
        double sum = 0;
        for (auto v : samples) sum += v;
        return sum / samples.size();
    }
    double percentile(double p) const {
        if (samples.empty()) return 0;
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (size_t)(p / 100.0 * (sorted.size() - 1));
        return sorted[idx];
    }
};

// 运行一次基准测试
static void bench(const std::string&          name,
                  int                         iterations,
                  std::function<void()>       setup,
                  std::function<void()>       fn,
                  std::function<void()>       teardown = {})
{
    Timer timer;
    for (int i = 0; i < iterations; i++) {
        if (setup) setup();
        auto t = Timer::Clock::now();
        fn();
        timer.lap(t);
        if (teardown) teardown();
    }
    // 原理：unit 只按均值选一次，value/p50/p95 必须同一量纲；
    // 否则均值≥1ms 而中位仍 <1ms 时会出现「p50>p95」的假象。
    double avg_us = timer.mean();
    double p50_us = timer.percentile(50);
    double p95_us = timer.percentile(95);
    const bool use_ms = avg_us >= 1000.0;
    const double scale = use_ms ? 1000.0 : 1.0;
    BenchResult r{name,
                  use_ms ? "ms" : "us",
                  avg_us / scale,
                  p50_us / scale,
                  p95_us / scale,
                  iterations};
    g_results.push_back(r);
}

// ══════════════════════════════════════════════════════════════ 测试用例 ══

// ── 1. Parser 吞吐（19 种 Mermaid 类型） ──
struct ParserCase {
    const char* name;
    const char* text;
};
static ParserCase s_parserCases[] = {
    {"flowchart", "flowchart TD\nA[Start] --> B[Process]\nB --> C[End]\n"
                  "C -->|ok| D([Done])\nD --> E((Circle))\n"
                  "subgraph sg1 [Group]\nF{Decision} --> G[[Stadium]]\nend\n"
                  "E --> F"},
    {"sequence",
     "sequenceDiagram\nparticipant Alice\nactor Bob\n"
     "Alice->>Bob: Hello\nBob-->>Alice: Hi\n"
     "loop every day\nAlice->>Bob: ping\nBob-->>Alice: pong\nend\n"
     "Note right of Alice: text"},
    {"classDiagram",
     "classDiagram\nclass Animal {\n+int age\n+String name\n+eat()\n}\n"
     "class Dog {\n+bark()\n}\nAnimal <|-- Dog\n"
     "class Cat {\n+meow()\n}\nAnimal <|-- Cat"},
    {"stateDiagram",
     "stateDiagram-v2\n[*] --> Idle\nIdle --> Processing : start\n"
     "state Processing {\n[*] --> Sub1\nSub1 --> Sub2\n--\n}\n"
     "Processing --> Done : finish\nDone --> [*]\n"},
    {"erDiagram",
     "erDiagram\nCUSTOMER ||--o{ ORDER : places\n"
     "ORDER ||--|{ LINE-ITEM : contains\n"
     "CUSTOMER {\nint id PK\nstring name\n}\nORDER {\nint id PK\n}"},
    {"gantt",
     "gantt\ndateFormat YYYY-MM-DD\ntitle Project\n"
     "section Dev\nTask A :a1, 2024-01-01, 30d\nTask B :after a1, 15d\n"
     "section QA\nTest :2024-02-01, 14d"},
    {"pie", "pie title Pets\n\"Dogs\": 50\n\"Cats\": 30\n\"Birds\": 15\n\"Fish\": 5"},
    {"gitGraph",
     "gitGraph\ncommit\nbranch dev\ncheckout dev\ncommit\n"
     "checkout main\ncommit\nmerge dev\n"},
    {"mindmap",
     "mindmap\nroot((Root))\n  A\n    A1\n    A2\n  B\n    B1\n      B1a\n    B2"},
    {"journey",
     "journey\ntitle My Day\nsection Morning\nWake: 5: Me, Cat\n"
     "Coffee: 3: Me\nsection Afternoon\nWork: 4: Me\n"},
    {"timeline",
     "timeline\ntitle History\nsection Era1\n"
     "2000 : Event A : Event B\n2001 : Event C\nsection Era2\n2005 : Event D"},
    {"kanban",
     "kanban\ntodo[To Do]\n  task1[Setup]\ndoing[In Progress]\n"
     "  task2[Code]@{ assigned: 'dev' }\ndone[Done]\n  task3[Deploy]"},
    {"quadrantChart",
     "quadrantChart\ntitle Market\nx-axis Low --> High\n"
     "y-axis Low --> High\nA: [0.3, 0.5]\nB: [0.7, 0.2]\nC: [0.2, 0.8]"},
    {"xychart",
     "xychart-beta\ntitle Sales\nx-axis [Q1, Q2, Q3, Q4]\n"
     "y-axis Revenue\nbar [10, 20, 15, 30]\nline [5, 10, 8, 12]"},
    {"requirementDiagram",
     "requirementDiagram\nrequirement REQ1 { id: 1\ntext: Login\n}\n"
     "element ELEM1 { type: software\ndocRef: DOC1\n}\n"
     "REQ1 - satisfies -> ELEM1"},
    {"sankey",
     "sankey-beta\nSource,Target,100\nTarget,Sink,50\n"
     "Source,Sink,30\nSink,Final,80"},
    {"block",
     "block-beta\ncolumns 2\nA[Hello]\nB[World]\nA --> B\nC[Test]\nD[Pass]\nC --> D"},
    {"architecture",
     "architecture-beta\ngroup api(cloud)[API]\n"
     "service db(database)[DB] in api\nservice web(server)[Web] in api\n"
     "web:T --> db:T"},
    {"packet",
     "packet-beta\ntitle Frame\n0-15: Header\n16-31: Payload\n32-47: CRC"},
};

static void benchAllParsers()
{
    for (auto& pc : s_parserCases) {
        std::string name = std::string("parseMermaid_") + pc.name;
        std::string text = pc.text;
        bench(name, 500, {},
              [text]() {
                  Graph g = gp::parseMermaid(text);
                  (void)g;
              });
    }
}

// ── 2. 大图解析（100/500/1000 节点 Flowchart） ──

static std::string makeBigFlowchart(int n)
{
    std::string s = "flowchart TD\n";
    for (int i = 0; i < n - 1; i++)
        s += "N" + std::to_string(i) + " --> N" + std::to_string(i + 1) + "\n";
    return s;
}

static void benchLargeGraphs()
{
    for (int n : {100, 500, 1000}) {
        std::string text = makeBigFlowchart(n);
        bench("parseMermaid_flowchart_n" + std::to_string(n), 50, {},
              [text]() {
                  Graph g = gp::parseMermaid(text);
                  (void)g;
              });
    }
}

// ── 3. JSON 序列化/反序列化开销 ──

static void benchJsonRoundTrip()
{
    // 中等规模图 -> JSON 往返
    std::string text = makeBigFlowchart(200);
    std::string err;
    for (int n : {200}) {
        std::string label = "jsonRoundTrip_flowchart_n" + std::to_string(n);
        bench(label, 100,
              [&text]() {
                  // setup: parse once, produce JSON
              },
              [&text, &err]() {
                  Graph g    = gp::parseMermaid(text);
                  Json  j    = g.toJson();
                  std::string dumped = j.dump();
                  Json  j2   = Json::parse(dumped, &err);
                  Graph g2   = Graph::fromJson(j2);
                  (void)g2;
              });
    }

    // 纯 dump 性能
    std::string ft200 = makeBigFlowchart(200);
    Graph       g200  = gp::parseMermaid(ft200);
    Json        j200  = g200.toJson();
    bench("jsonDump_n200", 200, {},
          [&j200]() {
              std::string s = j200.dump();
              (void)s;
          });

    // 纯 parse 性能
    std::string dumped200 = j200.dump();
    bench("jsonParse_n200", 200, {},
          [&dumped200, &err]() {
              Json j = Json::parse(dumped200, &err);
              (void)j;
          });
}

// ── 4. 导出性能 (Mermaid / Drawio / SVG) ──

static void benchExporters()
{
    std::string text = makeBigFlowchart(200);
    Graph       g    = gp::parseMermaid(text);
    gl::layout(g);

    bench("toMermaid_n200", 100, {},
          [&g]() {
              std::string s = ge::toMermaid(g);
              (void)s;
          });

    bench("toDrawio_n200", 100, {},
          [&g]() {
              std::string s = ge::toDrawio(g);
              (void)s;
          });

    bench("toSVG_n200", 20, {},  // SVG 较重，减少迭代
          [&g]() {
              std::string s = ge::toSVG(g);
              (void)s;
          });
}

// ── 5. Storage I/O ──

static void benchStorage()
{
    std::string text = makeBigFlowchart(100);
    Graph       g    = gp::parseMermaid(text);
    g.name           = "bench-storage";
    g.id             = "bench-store-fixed";

    std::string root = benchTempRoot("bench-store-tmp");
    benchSetStoreEnv(root);
    ge::removeDirectory(root);
    gs::Store store(root);

    // 原理：固定 id 反复 save；teardown 丢掉上一版快照，测热路径而非历史堆积
    int last_store_ver = 0;
    bench("store_save", 100, {},
          [&store, &g, &last_store_ver]() {
              last_store_ver = store.save(g, "bench save");
          },
          [&g, &root, &last_store_ver]() {
              if (last_store_ver > 1)
                  benchUnlinkVersion(root + "/" + g.id, last_store_ver - 1);
          });

    // 先保存一次以测试 load
    store.save(g, "pre-save for load test");
    std::string savedId = g.id;

    bench("store_load", 100, {},
          [&store, &savedId]() {
              Graph loaded;
              bool  ok = store.load(savedId, loaded);
              (void)ok;
          });

    ge::removeDirectory(root);
}

// ── 6. MCP 工具调用模拟 (tools/list / graph_create) ──

static void benchMcpTools()
{
    std::string root = benchTempRoot("bench-mcp-tmp");
    benchSetStoreEnv(root);
    ge::removeDirectory(root);
    gs::Store store(root);

    // tools/list
    bench("mcp_tools_list", 200, {},
          [&store]() {
              Json resp;
              Json req = Json::parse(
                  R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})", nullptr);
              mcp::handleMessage(req, store, resp);
          });

    // graph_create：固定 id upsert，避免 100 次 genId 撑爆 index.json
    const std::string gid = "bench-mcp-graph";
    bench(
        "mcp_graph_create", 100, {},
        [&store]() {
            Json resp;
            Json req = Json::parse(
                R"({"jsonrpc":"2.0","id":2,"method":"tools/call",
              "params":{"name":"graph_create","arguments":{
              "content":"flowchart TD\nA-->B\nB-->C","name":"bench",
              "id":"bench-mcp-graph"}}})",
                nullptr);
            mcp::handleMessage(req, store, resp);
        },
        [&store, &root, &gid]() {
            // teardown 不计时
            Json idx = store.loadIndex();
            for (auto& item : *idx["graphs"].a) {
                if (item.str("id") == gid) {
                    int head = (int)item.num("head", item.num("versions", 0));
                    if (head > 1)
                        benchUnlinkVersion(root + "/" + gid, head - 1);
                    break;
                }
            }
        });

    ge::removeDirectory(root);
}

// ── 7. Table 模型专项 ──

// 构造 N 行 CSV 的辅助函数
static std::string makeCsv(int rows, int cols)
{
    std::string csv;
    // 表头
    for (int c = 0; c < cols; c++) {
        if (c > 0) csv += ",";
        csv += "col" + std::to_string(c);
    }
    csv += "\n";
    // 数据行
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (c > 0) csv += ",";
            csv += "v" + std::to_string(r) + "_" + std::to_string(c);
        }
        csv += "\n";
    }
    return csv;
}

static void benchTableModel()
{
    // ── CSV 解析规模测试 ──
    for (int n : {100, 500, 2000}) {
        std::string csv = makeCsv(n, 8);
        bench("table_fromCsv_n" + std::to_string(n), 100, {},
              [&csv]() {
                  gt::Table t = gt::Table::fromCsv(csv);
                  (void)t;
              });
    }

    // ── 大 CSV + toJson 往返 ──
    {
        std::string csv500 = makeCsv(500, 8);
        gt::Table   t500   = gt::Table::fromCsv(csv500);
        bench("table_toJson_n500", 100, {},
              [&t500]() {
                  Json j = t500.toJson();
                  (void)j;
              });
        Json j500 = t500.toJson();
        bench("table_fromJson_n500", 100, {},
              [&j500]() {
                  gt::Table t = gt::Table::fromJson(j500);
                  (void)t;
              });
    }

    // ── 单元格访问性能 ──
    {
        std::string csv = makeCsv(200, 10);
        gt::Table   t   = gt::Table::fromCsv(csv);
        bench("table_cell_n200", 5000, {},
              [&t]() {
                  // 随机访问散布的单元格
                  for (int r = 0; r < 200; r++)
                      for (int c = 0; c < 10; c++)
                          t.cell(r, c);
              });
    }
}

static void benchTableXml()
{
    // SpreadsheetML 2003 往返（默认 toXml/fromXml）。
    // 相对旧 <table> 方言：写出更轻，读入因 Cell/Data 节点更多而更重——属格式成本。
    for (int n : {50, 200, 500}) {
        std::string csv    = makeCsv(n, 5);
        gt::Table   t      = gt::Table::fromCsv(csv);
        std::string xml    = gtx::toXml(t);
        std::string labelN = std::to_string(n);
        bench("table_toXml_n" + labelN, 50, {},
              [&t]() {
                  std::string x = gtx::toXml(t);
                  (void)x;
              });
        bench("table_fromXml_n" + labelN, 50, {},
              [&xml]() {
                  gt::Table t2 = gtx::fromXml(xml);
                  (void)t2;
              });
    }
}

static void benchTableBridge()
{
    // ── 边表 → Graph ──
    {
        // 构造边表: from,to,label (N 条边)
        std::string csv = "from,to,label\n";
        for (int i = 0; i < 500; i++)
            csv += "N" + std::to_string(i) + ",N" + std::to_string(i + 1) + ",e" +
                   std::to_string(i) + "\n";
        gt::Table t = gt::Table::fromCsv(csv);
        bench("graphFromTable_n500", 100, {},
              [&t]() {
                  Graph g = gtb::graphFromTable(t);
                  (void)g;
              });
    }

    // ── Graph → 边表 ──
    {
        std::string mmd = "flowchart TD\n";
        for (int i = 0; i < 200; i++)
            mmd += "N" + std::to_string(i) + " --> N" + std::to_string(i + 1) +
                   " |edge" + std::to_string(i) + "|\n";
        Graph g = gp::parseMermaid(mmd);
        bench("tableFromGraph_edgelist_n200", 100, {},
              [&g]() {
                  gt::Table t = gtb::tableFromGraph(g, "edgelist", false);
                  (void)t;
              });
        bench("tableFromGraph_skeleton_n200", 100, {},
              [&g]() {
                  gt::Table t = gtb::tableFromGraph(g, "skeleton", true);
                  (void)t;
              });
    }

    // ── tableCheck 枚举校验 ──
    {
        std::string csv = "type,status,value\n";
        for (int i = 0; i < 300; i++)
            csv += "T" + std::to_string(i % 5) + ",S" + std::to_string(i % 3) +
                   "," + std::to_string(i) + "\n";
        gt::Table t = gt::Table::fromCsv(csv);
        Json      allowed = Json::obj();
        Json      types   = Json::arr();
        for (int i = 0; i < 5; i++)
            types.push(Json("T" + std::to_string(i)));
        allowed.set("type", types);
        bench("tableCheck_n300_1rule", 50, {},
              [&t, &allowed]() {
                  gt::Table report = gtb::tableCheck(t, allowed, nullptr, false);
                  (void)report;
              });
    }

    // ── tableAlign 表对齐 ──
    {
        std::string csv1 = "id,name\n";
        std::string csv2 = "enemy_id,item\n";
        for (int i = 0; i < 100; i++) {
            csv1 += std::to_string(i) + ",Name" + std::to_string(i) + "\n";
            csv2 += std::to_string(i) + ",Item" + std::to_string(i) + "\n";
        }
        gt::Table primary = gt::Table::fromCsv(csv1);
        gt::Table ref     = gt::Table::fromCsv(csv2);
        bench("tableAlign_n100", 50, {},
              [&primary, &ref]() {
                  Json result = gtb::tableAlign(primary, ref, "id", "enemy_id");
                  (void)result;
              });
    }
}

static void benchTableStorage()
{
    std::string root = benchTempRoot("bench-table-tmp");
    benchSetStoreEnv(root);
    ge::removeDirectory(root);
    gts::TableStore ts(root);

    std::string csv = makeCsv(200, 6);
    gt::Table   t   = gt::Table::fromCsv(csv);
    t.name          = "bench-table";
    t.id            = "bench-table-fixed";

    int last_tbl_ver = 0;
    bench("tableStore_save_n200", 50, {},
          [&ts, &t, &last_tbl_ver]() {
              last_tbl_ver = ts.save(t, "bench");
          },
          [&t, &root, &last_tbl_ver]() {
              if (last_tbl_ver > 1) {
                  std::string base = root + "/tables/" + t.id + "/versions/v" +
                                     std::to_string(last_tbl_ver - 1);
                  std::remove((base + ".json").c_str());
              }
          });

    // 先存一个用于 load 测试
    ts.save(t, "pre-save");
    std::string savedId = t.id;

    bench("tableStore_load_n200", 50, {},
          [&ts, &savedId]() {
              gt::Table loaded;
              bool     ok = ts.load(savedId, loaded);
              (void)ok;
          });

    bench("tableStore_list", 100, {},
          [&ts]() {
              Json idx = ts.loadIndex();
              (void)idx;
          });

    ge::removeDirectory(root);
}

static void benchTableMcpTools()
{
    std::string root = benchTempRoot("bench-table-mcp-tmp");
    benchSetStoreEnv(root);
    ge::removeDirectory(root);
    gs::Store       store(root);
    mcp::ToolRunner runner(store);

    // table_create：固定 id + force upsert，避免 index 膨胀
    std::string csv200 = makeCsv(200, 5);
    const std::string tid = "bench-mcp-table";
    bench(
        "mcp_table_create_n200", 20, {},
        [&runner, &csv200, &tid]() {
            Json args = Json::obj();
            args.set("content", csv200);
            args.set("name", "bench-tbl");
            args.set("id", tid);
            args.set("force", true);
            (void)runner.call("table_create", args);
        },
        [&root, &tid]() {
            Json idx = gts::TableStore(root).loadIndex();
            for (auto& item : *idx["tables"].a) {
                if (item.str("id") == tid) {
                    int ver = (int)item.num("versions", 0);
                    if (ver > 1) {
                        std::string base = root + "/tables/" + tid +
                                           "/versions/v" +
                                           std::to_string(ver - 1);
                        std::remove((base + ".json").c_str());
                    }
                    break;
                }
            }
        });

    ge::removeDirectory(root);
}

// ── 8. 内存稳定性：固定 graphId 重复 save，检测泄漏而非 index 膨胀 ──
//
// 原 memory_RSS_5000iter 每次 genId 导致 index 涨到 5000 条，RSS 反映容量而非泄漏。
// 现改为：warmup → 同一 id 重复 save；并分段对比后半 delta，避免对古董相对基线过敏。

static size_t getCurrentRSS()
{
#ifdef _WIN32
    // Windows：WorkingSet；失败返回 0（报告侧可借 abs_before=0 识别）
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (size_t)pmc.WorkingSetSize;
    return 0;
#else
    // 优先 RssAnon（更贴近堆泄漏）；否则回退 statm RSS
    std::string status = ge::readFile("/proc/self/status");
    if (!status.empty()) {
        const char* key = "RssAnon:";
        size_t      pos = status.find(key);
        if (pos != std::string::npos) {
            pos += strlen(key);
            while (pos < status.size() &&
                   (status[pos] == ' ' || status[pos] == '\t'))
                pos++;
            size_t kb = 0;
            while (pos < status.size() && status[pos] >= '0' &&
                   status[pos] <= '9') {
                kb = kb * 10 + (size_t)(status[pos] - '0');
                pos++;
            }
            if (kb > 0)
                return kb * 1024;
        }
    }
    std::string line = ge::readFile("/proc/self/statm");
    if (line.empty())
        return 0;
    size_t pos = line.find(' ');
    if (pos == std::string::npos)
        return 0;
    std::string rssStr = line.substr(pos + 1);
    size_t      pos2   = rssStr.find(' ');
    if (pos2 != std::string::npos)
        rssStr = rssStr.substr(0, pos2);
    return std::stoull(rssStr) * 4096;
#endif
}

static void benchMemoryStability()
{
#ifndef _WIN32
    // Linux：无 /proc 则无法采样，直接跳过（避免写出全 0 假指标）
    if (ge::readFile("/proc/self/statm").empty() &&
        ge::readFile("/proc/self/status").empty())
        return;
#endif

    std::string text = makeBigFlowchart(50);
    ge::removeDirectory("bench-leak-tmp");
    gs::Store   store("bench-leak-tmp");
    const char* fixedId = "bench-rss-fixed";

    auto runSave = [&]() {
        Graph g = gp::parseMermaid(text);
        g.id    = fixedId;
        g.name  = fixedId;
        Json        j = g.toJson();
        std::string s = j.dump();
        (void)s;
        store.save(g, "rss-stability");
    };

    // warmup：目录/锁/首次分配不计入 delta
    for (int i = 0; i < 50; i++)
        runSave();

    size_t before = getCurrentRSS();
    for (int i = 0; i < 2500; i++)
        runSave();
    size_t mid = getCurrentRSS();
    for (int i = 0; i < 2500; i++)
        runSave();
    size_t after = getCurrentRSS();

    auto toMB = [](size_t a, size_t b) -> double {
        if (b <= a)
            return 0.0;
        return (double)(b - a) / (1024.0 * 1024.0);
    };
    auto absMB = [](size_t bytes) -> double {
        return (double)bytes / (1024.0 * 1024.0);
    };
    double totalMB  = toMB(before, after);
    double firstMB  = toMB(before, mid);
    double secondMB = toMB(mid, after);
    double beforeMB = absMB(before);
    double afterMB  = absMB(after);

    g_results.push_back(BenchResult{"memory_RSS_repeat_save_same_id", "MB",
                                    totalMB, totalMB, totalMB, 1});
    g_results.push_back(BenchResult{"memory_RSS_repeat_save_2nd_half", "MB",
                                    secondMB, secondMB, secondMB, 1});
    // 附带第一半便于人工看分段；CI 对 memory_* 用绝对阈值，不用旧相对基线
    g_results.push_back(BenchResult{"memory_RSS_repeat_save_1st_half", "MB",
                                    firstMB, firstMB, firstMB, 1});
    // 绝对 RSS：区分「增量=0（无泄漏）」与「采样失败=0」；不对基线做相对比对
    g_results.push_back(BenchResult{"memory_RSS_abs_before", "MB", beforeMB,
                                    beforeMB, beforeMB, 1});
    g_results.push_back(BenchResult{"memory_RSS_abs_after", "MB", afterMB,
                                    afterMB, afterMB, 1});

    ge::removeDirectory("bench-leak-tmp");
}

// ═══════════════════════════════════════════════════════════════ 主程序 ══

int main()
{
    // 抑制 stdout 噪声（只输出 JSON 到 stderr 标记通道）
    std::cout.sync_with_stdio(false);

    benchAllParsers();
    benchLargeGraphs();
    benchJsonRoundTrip();
    benchExporters();
    benchStorage();
    benchMcpTools();
    benchTableModel();
    benchTableXml();
    benchTableBridge();
    benchTableStorage();
    benchTableMcpTools();
    benchMemoryStability();

    // ── 输出 JSON 结果 ──
    // 格式与 GitHub Actions benchmark 工具兼容，也便于自定义解析
    fprintf(stdout, "{\n  \"benchmarks\": [\n");
    for (size_t i = 0; i < g_results.size(); i++) {
        auto& r = g_results[i];
        fprintf(stdout, "    {");
        jsonStr(stdout, "name");
        fprintf(stdout, ": ");
        jsonStr(stdout, r.name);
        fprintf(stdout, ", ");
        jsonStr(stdout, "unit");
        fprintf(stdout, ": ");
        jsonStr(stdout, r.unit);
        fprintf(stdout, ", ");
        jsonStr(stdout, "value");
        fprintf(stdout, ": %.4f", r.value);
        fprintf(stdout, ", ");
        jsonStr(stdout, "p50");
        fprintf(stdout, ": %.4f", r.p50);
        fprintf(stdout, ", ");
        jsonStr(stdout, "p95");
        fprintf(stdout, ": %.4f", r.p95);
        fprintf(stdout, ", ");
        jsonStr(stdout, "iterations");
        fprintf(stdout, ": %d", r.iterations);
        fprintf(stdout, "}%s\n", i + 1 < g_results.size() ? "," : "");
    }
    fprintf(stdout, "  ]\n}\n");

    return 0;
}
