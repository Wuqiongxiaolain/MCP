// test_main.cpp - assertion-based unit tests for graphmcp core modules
#include "../src/exporters.hpp"
#include "../src/mcp.hpp"
#include "../src/parsers.hpp"
#include "../src/storage.hpp"
#include <cstdio>
#include <iostream>


static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            g_passed++;                                                        \
        }                                                                      \
        else {                                                                 \
            g_failed++;                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__                \
                      << "  " #cond "\n";                                      \
        }                                                                      \
    } while (0)

using gj::Json;
using gm::Graph;

static void testJson()
{
    std::string err;
    Json        j =
        Json::parse(R"({"a":1,"b":[true,null,"x\n中"],"c":{"d":-2.5}})", &err);
    CHECK(err.empty());
    CHECK(j.num("a") == 1);
    CHECK(j.find("b")->isArr());
    CHECK(j.find("b")->a->at(2).s == "x\n\xe4\xb8\xad");
    CHECK(j.find("c")->num("d") == -2.5);
    // round-trip
    Json j2 = Json::parse(j.dump(), &err);
    CHECK(err.empty());
    CHECK(j2.dump() == j.dump());
    // error reporting
    Json bad = Json::parse("{broken", &err);
    CHECK(!err.empty());
}

static void testMermaidFlowchart()
{
    Graph g = gp::parseMermaid("flowchart TD\n"
                               "  A[Start] --> B{Check?}\n"
                               "  B -->|yes| C(Done)\n"
                               "  B -.->|no| A\n"
                               "  subgraph S1 [Backend]\n"
                               "    D[API] --> E[(DB)]\n"
                               "  end\n"
                               "  C --> D\n");
    CHECK(g.type == "flowchart");
    const gm::Node* nodeA  = g.findNode("A");
    const gm::Node* nodeB  = g.findNode("B");
    const gm::Node* nodeC  = g.findNode("C");
    const gm::Node* nodeS1 = g.findNode("S1");
    const gm::Node* nodeD  = g.findNode("D");
    CHECK(nodeA != nullptr);
    CHECK(nodeB != nullptr);
    CHECK(nodeC != nullptr);
    CHECK(nodeS1 != nullptr);
    CHECK(nodeD != nullptr);
    if (nodeA)
        CHECK(nodeA->label == "Start");
    if (nodeB)
        CHECK(nodeB->shape == "diamond");
    if (nodeC)
        CHECK(nodeC->shape == "round");
    if (nodeS1)
        CHECK(nodeS1->shape == "group");
    if (nodeD)
        CHECK(nodeD->parent == "S1");
    CHECK(g.edges.size() == 5);
    bool foundYes = false, foundDashed = false;
    for (auto& e : g.edges) {
        if (e.label == "yes")
            foundYes = true;
        if (e.style == "dashed" && e.label == "no")
            foundDashed = true;
    }
    CHECK(foundYes);
    CHECK(foundDashed);
}

static void testMermaidMindmapAndER()
{
    Graph mm = gp::parseMermaid("mindmap\n"
                                "  root((Project))\n"
                                "    Design\n"
                                "      UI\n"
                                "    Build\n");
    CHECK(mm.type == "mindmap");
    CHECK(mm.nodes.size() == 4);
    CHECK(mm.nodes[0].label == "Project");
    CHECK(mm.nodes[1].parent == mm.nodes[0].id);
    CHECK(mm.nodes[2].parent == mm.nodes[1].id);

    Graph er = gp::parseMermaid("erDiagram\n"
                                "  CUSTOMER ||--o{ ORDER : places\n"
                                "  CUSTOMER {\n"
                                "    string name\n"
                                "    int id\n"
                                "  }\n");
    CHECK(er.type == "er");
    CHECK(er.findNode("CUSTOMER") != nullptr);
    CHECK(er.findNode("ORDER") != nullptr);
    CHECK(er.findNode("CUSTOMER")->attrs.size() == 2);
    CHECK(er.edges.size() == 1);
    CHECK(er.edges[0].label == "places");
}

static void testMarkdown()
{
    Graph g = gp::parseMarkdownOutline("# Root\n"
                                       "## Alpha\n"
                                       "- item1\n"
                                       "  - item1a\n"
                                       "## Beta\n");
    CHECK(g.type == "mindmap");
    CHECK(g.nodes.size() == 5);
    CHECK(g.nodes[0].label == "Root");
    CHECK(g.nodes[1].label == "Alpha");
    CHECK(g.nodes[1].parent == g.nodes[0].id);
    CHECK(g.nodes[2].parent == g.nodes[1].id);  // item1 under Alpha
    CHECK(g.nodes[3].parent == g.nodes[2].id);  // item1a under item1
    CHECK(g.nodes[4].parent == g.nodes[0].id);  // Beta under Root
}

static void testCSV()
{
    Graph edges = gp::parseCSV("from,to,label\nA,B,go\nB,C,\n");
    CHECK(edges.type == "flowchart");
    CHECK(edges.nodes.size() == 3);
    CHECK(edges.edges.size() == 2);
    CHECK(edges.edges[0].label == "go");

    Graph tree = gp::parseCSV(
        "id,label,parent\nceo,CEO,\ncto,CTO,ceo\ndev1,\"Dev, One\",cto\n");
    CHECK(tree.type == "orgchart");
    CHECK(tree.findNode("dev1")->label == "Dev, One");  // quoted comma
    CHECK(tree.findNode("cto")->parent == "ceo");

    bool threw = false;
    try {
        gp::parseCSV("x,y\n1,2\n");
    }
    catch (gp::ParseError&) {
        threw = true;
    }
    CHECK(threw);
}

static void testXML()
{
    Graph g = gp::parseXML(
        "<?xml version=\"1.0\"?>\n"
        "<graph type=\"architecture\" name=\"sys\">\n"
        "  <node id=\"web\" label=\"Web &amp; App\"/>\n"
        "  <node id=\"cluster\" label=\"Cluster\">\n"
        "    <node id=\"svc\" label=\"Service\" shape=\"round\"/>\n"
        "  </node>\n"
        "  <edge from=\"web\" to=\"svc\" label=\"http\"/>\n"
        "</graph>");
    CHECK(g.type == "architecture");
    CHECK(g.name == "sys");
    CHECK(g.findNode("web")->label == "Web & App");
    CHECK(g.findNode("svc")->parent == "cluster");
    CHECK(g.findNode("cluster")->shape == "group");
    CHECK(g.edges.size() == 1);
}

static void testExcalidraw()
{
    std::string doc = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"r1","type":"rectangle","x":10,"y":20,"width":100,"height":50},
        {"id":"t1","type":"text","x":15,"y":30,"width":80,"height":20,
         "text":"Box A","containerId":"r1"},
        {"id":"r2","type":"ellipse","x":300,"y":20,"width":90,"height":60},
        {"id":"a1","type":"arrow","x":110,"y":45,"width":190,"height":5,
         "startBinding":{"elementId":"r1"},"endBinding":{"elementId":"r2"}}
      ]})";
    Graph       g   = gp::parseExcalidraw(doc);
    CHECK(g.type == "whiteboard");
    CHECK(g.elements.size() == 4);
    CHECK(g.nodes.size() == 2);
    CHECK(g.findNode("r1")->label == "Box A");
    CHECK(g.edges.size() == 1);
    CHECK(g.edges[0].from == "r1" && g.edges[0].to == "r2");
    // round-trip keeps raw elements
    std::string out = ge::toExcalidraw(g);
    Graph       g2  = gp::parseExcalidraw(out);
    CHECK(g2.elements.size() == 4);
}

static void testDetect()
{
    CHECK(gp::detectFormat("flowchart TD\nA-->B") == "mermaid");
    CHECK(gp::detectFormat("mindmap\n  root((x))") == "mermaid");
    CHECK(gp::detectFormat("# Title\n- a") == "markdown");
    CHECK(gp::detectFormat("from,to\nA,B") == "csv");
    CHECK(gp::detectFormat("<graph><node id=\"a\"/></graph>") == "xml");
    CHECK(gp::detectFormat("{\"type\":\"excalidraw\",\"elements\":[]}") ==
          "excalidraw");
}

static void testValidate()
{
    Graph g;
    g.ensureNode("a", "A");
    g.ensureNode("b", "B");
    g.addEdge("a", "missing");
    auto issues = gl::validate(g);
    CHECK(gl::hasErrors(issues));
    bool dangling = false;
    for (auto& i : issues)
        if (i.message.find("missing node 'missing'") != std::string::npos)
            dangling = true;
    CHECK(dangling);

    Graph cyc;
    cyc.ensureNode("x", "X").parent = "y";
    cyc.ensureNode("y", "Y").parent = "x";
    auto ci                         = gl::validate(cyc);
    bool cycle                      = false;
    for (auto& i : ci)
        if (i.message.find("cycle") != std::string::npos)
            cycle = true;
    CHECK(cycle);
}

static void testLayout()
{
    Graph g = gp::parseMermaid("flowchart TD\nA[Start]-->B[Mid]\nB-->C[End]\n");
    gl::layout(g);
    CHECK(g.laidOut);
    const gm::Node* a = g.findNode("A");
    const gm::Node* b = g.findNode("B");
    const gm::Node* c = g.findNode("C");
    CHECK(a->w > 0 && a->h > 0);
    CHECK(a->y < b->y && b->y < c->y);  // layered top-down ranks

    Graph mm = gp::parseMarkdownOutline("# R\n## A\n## B\n");
    gl::layout(mm);
    const gm::Node* root = &mm.nodes[0];
    CHECK(root->x < mm.nodes[1].x);  // mindmap grows left->right
}

static void testExporters()
{
    Graph g = gp::parseMermaid(
        "flowchart TD\nA[Start] --> B{OK?}\nB -->|yes| C[Done]\n");
    std::string drawio = ge::toDrawio(g);
    CHECK(drawio.find("<mxfile") != std::string::npos);
    CHECK(drawio.find("mxGraphModel") != std::string::npos);
    CHECK(drawio.find("rhombus") != std::string::npos);
    CHECK(drawio.find("value=\"Start\"") != std::string::npos);

    std::string mmd = ge::toMermaid(g);
    CHECK(mmd.find("flowchart TD") != std::string::npos);
    CHECK(mmd.find("A[\"Start\"]") != std::string::npos);
    CHECK(mmd.find("|yes|") != std::string::npos);

    std::string svg = ge::toSVG(g);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("polygon") != std::string::npos);  // diamond
    CHECK(svg.find("Start") != std::string::npos);

    std::string ex = ge::toExcalidraw(g);
    std::string err;
    Json        j = Json::parse(ex, &err);
    CHECK(err.empty());
    CHECK(j.str("type") == "excalidraw");
    CHECK(j.find("elements")->size() >= 7);  // 3 shapes + 3 labels + 2 arrows

    std::string url = ge::toMermaidLiveUrl(g);
    CHECK(url.find("https://mermaid.live/edit#base64:") == 0);

    // ER export round-trip
    Graph er = gp::parseMermaid(
        "erDiagram\n  A ||--o{ B : owns\n  A {\n    int id\n  }\n");
    std::string ermmd = ge::toMermaid(er);
    CHECK(ermmd.find("erDiagram") != std::string::npos);
    CHECK(ermmd.find("int id") != std::string::npos);
}

static void testBase64()
{
    CHECK(ge::base64Encode("") == "");
    CHECK(ge::base64Encode("f") == "Zg==");
    CHECK(ge::base64Encode("fo") == "Zm8=");
    CHECK(ge::base64Encode("foo") == "Zm9v");
    CHECK(ge::base64Encode("foobar") == "Zm9vYmFy");
}

static void testStorage()
{
    std::string root = "test-store-tmp";
    gs::Store   store(root);
    Graph       g = gp::parseMermaid("flowchart TD\nA[One]-->B[Two]\n");
    g.name        = "test graph";
    int v1        = store.save(g, "first");
    CHECK(v1 == 1);
    // second version with an extra node
    g.ensureNode("C", "Three");
    g.addEdge("B", "C");
    int v2 = store.save(g, "second");
    CHECK(v2 == 2);

    Graph       loaded;
    std::string err;
    CHECK(store.load(g.id, loaded, 0, &err));
    CHECK(loaded.nodes.size() == 3);
    CHECK(store.load(g.id, loaded, 1, &err));
    CHECK(loaded.nodes.size() == 2);  // old snapshot

    Json h = store.history(g.id);
    CHECK(h.size() == 2);
    CHECK((int)h.a->at(0).num("version") == 1);

    int nv = 0;
    CHECK(store.rollback(g.id, 1, &nv, &err));
    CHECK(nv == 3);
    CHECK(store.load(g.id, loaded, 0, &err));
    CHECK(loaded.nodes.size() == 2);  // latest == rolled back content

    CHECK(!store.load("no-such-graph", loaded, 0, &err));
    CHECK(!err.empty());
}

static void testMcpProtocol()
{
    gs::Store store("test-store-tmp");
    // initialize
    std::string err;
    Json        init = Json::parse(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", &err);
    Json resp;
    CHECK(mcp::handleMessage(init, store, resp));
    CHECK(resp.find("result")->str("protocolVersion") == mcp::PROTOCOL_VERSION);
    // notification -> no response
    Json note = Json::parse(
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})", &err);
    CHECK(!mcp::handleMessage(note, store, resp));
    // tools/list
    Json tl =
        Json::parse(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", &err);
    CHECK(mcp::handleMessage(tl, store, resp));
    CHECK(resp.find("result")->find("tools")->size() == 10);
    // tools/call graph_create
    Json call = Json::parse(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{
            "name":"graph_create","arguments":{
              "content":"flowchart TD\nX[Hello]-->Y[World]","name":"mcp test"}}})",
        &err);
    CHECK(err.empty());
    CHECK(mcp::handleMessage(call, store, resp));
    const Json* result = resp.find("result");
    CHECK(result != nullptr);
    const Json* content = result ? result->find("content") : nullptr;
    CHECK(content != nullptr);
    CHECK(content && content->isArr());
    CHECK(content && content->a && !content->a->empty());
    std::string text;
    if (content && content->isArr() && content->a && !content->a->empty())
        text = content->a->at(0).str("text");
    Json created = Json::parse(text, &err);
    CHECK(created.str("status") == "created");
    std::string gid = created.str("id");
    CHECK(!gid.empty());
    // tools/call graph_export (mermaid inline)
    Json exp = Json::parse(
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{
            "name":"graph_export","arguments":{"id":")" +
            gid + R"(","to":"mermaid"}}})",
        &err);
    CHECK(mcp::handleMessage(exp, store, resp));
    result = resp.find("result");
    CHECK(result != nullptr);
    content = result ? result->find("content") : nullptr;
    CHECK(content != nullptr);
    CHECK(content && content->isArr());
    CHECK(content && content->a && !content->a->empty());
    if (content && content->isArr() && content->a && !content->a->empty())
        text = content->a->at(0).str("text");
    CHECK(text.find("flowchart TD") != std::string::npos);
    CHECK(text.find("Hello") != std::string::npos);
    // unknown method -> error
    Json bad = Json::parse(R"({"jsonrpc":"2.0","id":5,"method":"nope"})", &err);
    CHECK(mcp::handleMessage(bad, store, resp));
    CHECK(resp.find("error") != nullptr);
}

static void testDraftCommit()
{
    gs::Store store("test-store-tmp");
    Graph     g  = gp::parseMermaid("flowchart TD\nA[Alpha]-->B[Beta]\n");
    g.name       = "draft graph";
    int v1       = store.save(g, "v1");
    CHECK(v1 == 1);
    std::string id = g.id;
    store.discardDraft(id);  // 从干净状态开始

    // 初始无草稿：clean
    Json st0 = gc::draftStatus(store, id);
    CHECK(st0.boolean("clean"));
    CHECK(!store.hasDraft(id));

    // 打开游标（派生草稿），改第一个节点 label
    Json        op  = gc::cursorOpen(store, id, "nodes");
    std::string cid = op.str("cursor");
    CHECK(!cid.empty());
    CHECK(store.hasDraft(id));
    Json up = gc::cursorUpdate(store, id, cid, Json::parse(R"({"label":"A2"})"));
    CHECK(up.boolean("updated"));

    // status 显示 modified=1，且 latest 尚未变化
    Json st1 = gc::draftStatus(store, id);
    CHECK(!st1.boolean("clean"));
    CHECK((int)st1.find("nodes")->num("modified") == 1);
    Graph latest;
    CHECK(store.load(id, latest, 0));
    CHECK(latest.findNode("A")->label == "Alpha");

    // commit -> v2，草稿清除，latest 生效
    int         nv = 0;
    std::string cerr;
    CHECK(store.commitDraft(id, "rename A", &nv, &cerr));
    CHECK(nv == 2);
    CHECK(!store.hasDraft(id));
    CHECK(store.load(id, latest, 0));
    CHECK(latest.findNode("A")->label == "A2");
    CHECK(store.history(id).size() == 2);

    // 无草稿时 commit 失败
    CHECK(!store.commitDraft(id, "noop", &nv, &cerr));
}

static void testCursor()
{
    gs::Store store("test-store-tmp");
    Graph     g = gp::parseMermaid("flowchart TD\nN1[One]-->N2[Two]\n");
    g.name      = "cursor graph";
    store.save(g, "v1");
    std::string id = g.id;
    store.discardDraft(id);

    Json        op  = gc::cursorOpen(store, id, "nodes");
    std::string cid = op.str("cursor");
    CHECK(op.str("target") == "nodes");
    CHECK((int)op.num("count") == 2);
    CHECK(op.find("item")->str("id") == "N1");

    // next -> 第二个节点
    Json nx = gc::cursorMove(store, id, cid, 1);
    CHECK((int)nx.num("index") == 1);
    CHECK(nx.find("item")->str("id") == "N2");

    // insert 在当前项之后插入 N3
    Json ins = gc::cursorInsert(store, id, cid,
                                Json::parse(R"({"newId":"N3","label":"Three"})"));
    CHECK(ins.boolean("inserted"));
    CHECK((int)ins.num("count") == 3);
    CHECK(ins.find("item")->str("id") == "N3");

    // update 当前项(N3) label
    Json up = gc::cursorUpdate(store, id, cid,
                               Json::parse(R"({"label":"Three-mod"})"));
    CHECK(up.find("item")->str("label") == "Three-mod");

    // 草稿内容校验
    Graph d;
    CHECK(store.loadDraft(id, d));
    CHECK(d.nodes.size() == 3);
    CHECK(d.findNode("N3") != nullptr);
    CHECK(d.findNode("N3")->label == "Three-mod");

    // delete 当前项(N3)
    Json del = gc::cursorDelete(store, id, cid);
    CHECK(del.boolean("deleted"));
    CHECK((int)del.num("count") == 2);
    CHECK(store.loadDraft(id, d));
    CHECK(d.findNode("N3") == nullptr);

    // close 后游标失效
    Json cl = gc::cursorClose(store, id, cid);
    CHECK(cl.boolean("closed"));
    Json after = gc::cursorGet(store, id, cid);
    CHECK(after.find("error") != nullptr);

    store.discardDraft(id);
}

int runAll()
{
    testJson();
    testMermaidFlowchart();
    testMermaidMindmapAndER();
    testMarkdown();
    testCSV();
    testXML();
    testExcalidraw();
    testDetect();
    testValidate();
    testLayout();
    testExporters();
    testBase64();
    testStorage();
    testMcpProtocol();
    testDraftCommit();
    testCursor();
    std::cout << "tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return g_failed == 0 ? 0 : 1;
}

int main()
{ return runAll(); }
