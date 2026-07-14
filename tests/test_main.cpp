// test_main.cpp - assertion-based unit tests for graphmcp core modules
// 依赖 mcp.hpp 间接引入 exporters / table 工具头文件
#include "../src/mcp.hpp"
#include "../src/parsers.hpp"
#include "../src/storage.hpp"
#include "../src/table_bridge.hpp"
#include "../src/table_model.hpp"
#include "../src/table_storage.hpp"
#include "../src/table_xml.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

// EnvGuard: 测试内临时设置环境变量，析构时恢复（避免污染后续用例）
struct EnvGuard {
    std::string name_;
    std::string old_;

    EnvGuard(const char* name, const char* value) : name_(name)
    {
        old_ = ge::getEnvVar(name);
#ifdef _WIN32
        _putenv_s(name_.c_str(), value);
#else
        setenv(name_.c_str(), value, 1);
#endif
    }

    ~EnvGuard()
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), old_.c_str());
#else
        if (old_.empty())
            unsetenv(name_.c_str());
        else
            setenv(name_.c_str(), old_.c_str(), 1);
#endif
    }

    EnvGuard(const EnvGuard&)            = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
};

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
using gm::Edge;
using gm::Graph;
using gm::Node;

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
    Json::parse("{broken", &err);
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

    // image + files 应在 parse / export / svg 渲染链路中完整保留
    std::string imageDoc = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"img1","type":"image","x":10,"y":10,"width":40,"height":20,
         "angle":0.5,
         "fileId":"f1","opacity":100,
         "scale":[-1,1],
         "crop":{"x":1,"y":1,"width":40,"height":20,"naturalWidth":80,"naturalHeight":40}}
      ],
      "files":{
        "f1":{
          "mimeType":"image/png",
          "id":"f1",
          "dataURL":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO2+2b8AAAAASUVORK5CYII=",
          "created":1,
          "lastRetrieved":1
        }
      }
    })";
    Graph       gImg     = gp::parseExcalidraw(imageDoc);
    CHECK(gImg.elements.size() == 1);
    CHECK(gImg.files.isObj());
    const Json* f1 = gImg.files.find("f1");
    CHECK(f1 != nullptr);
    if (f1)
        CHECK(f1->str("mimeType") == "image/png");
    std::string outImg = ge::toExcalidraw(gImg);
    std::string errImg;
    Json        jImg = Json::parse(outImg, &errImg);
    CHECK(errImg.empty());
    CHECK(jImg.find("files") && jImg.find("files")->isObj());
    CHECK(jImg.find("files") && jImg.find("files")->find("f1"));
    CHECK(jImg.find("files") &&
          jImg.find("files")->find("f1")->str("dataURL").find(
              "data:image/png;base64,") == 0);
    std::string imageSvg = ge::toSVG(gImg);
    CHECK(imageSvg.find("<image ") != std::string::npos);
    CHECK(imageSvg.find("data:image/png;base64,") != std::string::npos);
    CHECK(imageSvg.find("transform=\"matrix(") != std::string::npos);
    CHECK(imageSvg.find("overflow=\"hidden\"") != std::string::npos);
    CHECK(imageSvg.find("clipPath id=\"clip-img1\"") == std::string::npos);
    // 无 crop 应对走到 <image x=...> 直接输出；drawio/mermaid 不应崩溃
    std::string imageNoCropDoc = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"img2","type":"image","x":5,"y":6,"width":30,"height":15,
         "fileId":"f1","opacity":100}
      ],
      "files":{
        "f1":{
          "mimeType":"image/png",
          "id":"f1",
          "dataURL":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO2+2b8AAAAASUVORK5CYII=",
          "created":1,
          "lastRetrieved":1
        }
      }
    })";
    Graph       gImgNc         = gp::parseExcalidraw(imageNoCropDoc);
    std::string imgNcSvg       = ge::toSVG(gImgNc);
    CHECK(imgNcSvg.find("<image x=\"5\"") != std::string::npos ||
          imgNcSvg.find("<image x=\"5.0") != std::string::npos);
    CHECK(imgNcSvg.find("overflow=\"hidden\"") == std::string::npos);
    std::string imgDrawio = ge::toDrawio(gImg);
    CHECK(imgDrawio.find("<mxfile") != std::string::npos);
    std::string imgMmd = ge::toMermaid(gImg);
    CHECK(imgMmd.find("flowchart") != std::string::npos);

    // freedraw 在 Excalidraw 往返中应原样保留
    std::string withFreedraw = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"fd1","type":"freedraw","x":50,"y":60,"strokeColor":"#ff0000",
         "strokeWidth":3,"points":[[0,0],[30,10],[60,20],[90,15]]}
      ]})";
    Graph       gfd          = gp::parseExcalidraw(withFreedraw);
    CHECK(gfd.elements.size() == 1);
    std::string outFd = ge::toExcalidraw(gfd);
    std::string err;
    Json        jf = Json::parse(outFd, &err);
    CHECK(err.empty());
    CHECK(jf.find("elements") && jf.find("elements")->size() == 1);
    if (jf.find("elements") && jf.find("elements")->size() == 1)
        CHECK(jf.find("elements")->a->at(0).str("type") == "freedraw");

    // 折线箭头 + 嵌入文字应进入逻辑边
    std::string bendArrow = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"a","type":"rectangle","x":10,"y":10,"width":80,"height":40},
        {"id":"b","type":"rectangle","x":200,"y":120,"width":80,"height":40},
        {"id":"arr","type":"arrow","x":50,"y":50,"width":120,"height":80,
         "points":[[0,0],[60,20],[150,90]],
         "startBinding":{"elementId":"a"},"endBinding":{"elementId":"b"},
         "endArrowhead":"arrow"},
        {"id":"lbl","type":"text","x":70,"y":60,"width":20,"height":20,
         "text":"是","containerId":"arr","fontSize":16,"textAlign":"center"}
      ]})";
    Graph       gb        = gp::parseExcalidraw(bendArrow);
    CHECK(gb.edges.size() == 1);
    CHECK(gb.edges[0].label == "是");
    std::string bendSvg = ge::toSVG(gb);
    CHECK(bendSvg.find("x=\"130.0") != std::string::npos ||
          bendSvg.find("x=\"130.") != std::string::npos);
    CHECK(bendSvg.find("mask id=\"mask-arr\"") != std::string::npos);
    CHECK(bendSvg.find("是") != std::string::npos);

    // 仅 startArrowhead 时应交换逻辑方向，避免导出方向反转
    std::string startOnlyArrow = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"s","type":"rectangle","x":10,"y":10,"width":80,"height":40},
        {"id":"t","type":"rectangle","x":220,"y":10,"width":80,"height":40},
        {"id":"arr2","type":"arrow","x":90,"y":30,"width":130,"height":0,
         "points":[[0,0],[130,0]],
         "startBinding":{"elementId":"s"},"endBinding":{"elementId":"t"},
         "startArrowhead":"arrow","endArrowhead":null}
      ]})";
    Graph       gs             = gp::parseExcalidraw(startOnlyArrow);
    CHECK(gs.edges.size() == 1);
    CHECK(gs.edges[0].from == "t");
    CHECK(gs.edges[0].to == "s");
    std::string startOnlySvg = ge::toSVG(gs);
    // 白板 SVG 仍按元素几何保留 startArrowhead -> marker-start
    CHECK(startOnlySvg.find("marker-start=\"url(#arrow)\"") !=
          std::string::npos);
    CHECK(startOnlySvg.find("marker-end=\"url(#arrow)\"") == std::string::npos);

    // 箭头绑定 image 等非节点元素时，白板校验应通过（边端点为 element id）
    std::string arrowToImage = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"r1","type":"rectangle","x":10,"y":10,"width":80,"height":40},
        {"id":"img1","type":"image","x":200,"y":10,"width":50,"height":40},
        {"id":"arr","type":"arrow","x":50,"y":30,"width":160,"height":0,
         "startBinding":{"elementId":"r1"},"endBinding":{"elementId":"img1"},
         "endArrowhead":"arrow"}
      ]})";
    Graph       gai          = gp::parseExcalidraw(arrowToImage);
    CHECK(gai.edges.size() == 1);
    auto vi = gl::validate(gai);
    CHECK(!gl::hasErrors(vi));

    // 多行彩色文本应保留颜色、透明度与 Excalifont；CSS 单引号不得被 &#39; 化
    std::string multiText = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"e1","type":"ellipse","x":120,"y":10,"width":90,"height":50,"opacity":50},
        {"id":"t2","type":"text","x":120,"y":10,"width":60,"height":40,
         "text":"红\n蓝","strokeColor":"#ff0000","fontSize":16,"opacity":50}
      ]})";
    Graph       mt        = gp::parseExcalidraw(multiText);
    std::string multiSvg  = ge::toSVG(mt);
    CHECK(multiSvg.find("fill=\"#ff0000\"") != std::string::npos);
    CHECK(multiSvg.find("<tspan") != std::string::npos);
    CHECK(multiSvg.find("opacity=\"0.5\"") != std::string::npos);
    CHECK(multiSvg.find("Excalifont") != std::string::npos);
    CHECK(multiSvg.find("font-family=\"'Excalifont'") != std::string::npos);
    CHECK(multiSvg.find("font-family=\"&#39;") == std::string::npos);
    CHECK(multiSvg.find("@font-face{font-family:'Excalifont'") !=
          std::string::npos);
    size_t b64pos = multiSvg.find("base64,");
    CHECK(b64pos != std::string::npos);
    if (b64pos != std::string::npos) {
        size_t payload = b64pos + 7;
        // 须在索引前确认载荷字节存在，避免 CHECK 宏不构成运行时门控
        if (payload < multiSvg.size()) {
            char c = multiSvg[payload];
            CHECK(c != '\'' && c != '\"' && c != ')');
        }
        else {
            CHECK(false);
        }
    }

    // 不均匀折线的嵌字位置应按路径长度中点而不是按点序号取中
    std::string unevenArrow = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"a3","type":"rectangle","x":10,"y":10,"width":80,"height":40},
        {"id":"b3","type":"rectangle","x":320,"y":200,"width":80,"height":40},
        {"id":"arr3","type":"arrow","x":90,"y":30,"width":260,"height":200,
         "points":[[0,0],[200,0],[200,10],[260,200]],
         "startBinding":{"elementId":"a3"},"endBinding":{"elementId":"b3"},
         "endArrowhead":"arrow"},
        {"id":"lbl3","type":"text","x":0,"y":0,"width":20,"height":20,
         "text":"X","containerId":"arr3","fontSize":16,"textAlign":"center"}
      ]})";
    std::string unevenSvg   = ge::toSVG(gp::parseExcalidraw(unevenArrow));
    CHECK(unevenSvg.find("x=\"290\"") != std::string::npos ||
          unevenSvg.find("x=\"290.") != std::string::npos);
    // 嵌字位于折线中点时，viewBox 应覆盖该区域而非仅 JSON 中的 x/y=0
    {
        double vw = ge::svgDim(unevenSvg, "width");
        double vh = ge::svgDim(unevenSvg, "height");
        CHECK(vw > 200);
        CHECK(vh > 150);
        CHECK(unevenSvg.find("viewBox=\"") != std::string::npos);
    }

    // 旋转/镜像：rectangle/ellipse/diamond/text 都应带 matrix 变换
    std::string transformedDoc = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"rT","type":"rectangle","x":10,"y":10,"width":80,"height":40,"angle":0.2,"scale":[-1,1]},
        {"id":"eT","type":"ellipse","x":130,"y":10,"width":70,"height":40,"angle":-0.3,"scale":[1,-1]},
        {"id":"dT","type":"diamond","x":230,"y":10,"width":80,"height":50,"angle":0.4,"scale":[-1,-1]},
        {"id":"tT","type":"text","x":40,"y":90,"width":120,"height":30,"text":"旋转文本","angle":0.5,"scale":[-1,1],"fontSize":16}
      ]})";
    std::string transformedSvg = ge::toSVG(gp::parseExcalidraw(transformedDoc));
    size_t      matrixCount    = 0;
    size_t      scanPos        = 0;
    while (true) {
        size_t p = transformedSvg.find("transform=\"matrix(", scanPos);
        if (p == std::string::npos)
            break;
        matrixCount++;
        scanPos = p + 1;
    }
    CHECK(matrixCount >= 4);
    // scale=[-1,1] 的矩形：旋转后 matrix a 分量应为负（镜像）
    size_t rMat = transformedSvg.find("transform=\"matrix(");
    CHECK(rMat != std::string::npos);
    if (rMat != std::string::npos) {
        size_t numStart = rMat + std::string("transform=\"matrix(").size();
        CHECK(transformedSvg[numStart] == '-');
    }
}

static void testParseAnyAndModel()
{
    Graph g1 = gp::parseAny("flowchart TD\nA[One]-->B[Two]\n");
    CHECK(g1.nodes.size() == 2);

    Graph g2 = gp::parseAny("id,label,parent\nceo,CEO,\ncto,CTO,ceo\n", "auto");
    CHECK(g2.type == "orgchart");

    Graph g3 = gp::parseAny("from,to\nA,B\n", "csv", "architecture");
    CHECK(g3.type == "architecture");

    Graph src = gp::parseMermaid("flowchart TD\nA[Alpha]-->B[Beta]\n");
    src.name  = "roundtrip";
    std::string err;
    Graph       dst = gp::parseAny(src.toJson().dump(), "model");
    CHECK(dst.name == "roundtrip");
    CHECK(dst.nodes.size() == 2);
    CHECK(dst.findNode("A")->label == "Alpha");

    Json modelDetect = Json::parse(src.toJson().dump(), &err);
    CHECK(err.empty());
    CHECK(gp::detectFormat(modelDetect.dump()) == "model");
}

static void testValidateWarnings()
{
    Graph g;
    g.type = "flowchart";
    gm::Node n;
    n.id    = "solo";
    n.label = "";
    g.nodes.push_back(n);
    g.ensureNode("other", "Other");
    auto issues = gl::validate(g);
    CHECK(!gl::hasErrors(issues));
    bool emptyLabel = false, isolated = false;
    for (const auto& i : issues) {
        if (i.message.find("empty label") != std::string::npos)
            emptyLabel = true;
        if (i.message.find("isolated") != std::string::npos)
            isolated = true;
    }
    CHECK(emptyLabel);
    CHECK(isolated);
}

static void testOrgchartAndMindmapExport()
{
    Graph org = gp::parseCSV("id,label,parent\nceo,CEO,\ncto,CTO,ceo\n");
    gl::layout(org);
    CHECK(org.laidOut);
    const gm::Node* ceo = org.findNode("ceo");
    const gm::Node* cto = org.findNode("cto");
    CHECK(ceo != nullptr && cto != nullptr && ceo->y < cto->y);

    std::string orgMmd = ge::toMermaid(org);
    CHECK(orgMmd.find("flowchart TD") != std::string::npos);
    CHECK(orgMmd.find("CEO") != std::string::npos);

    Graph       mm    = gp::parseMarkdownOutline("# Root\n## Branch\n");
    std::string mmMmd = ge::toMermaid(mm);
    CHECK(mmMmd.find("mindmap") != std::string::npos);
    CHECK(mmMmd.find("Root") != std::string::npos);

    Graph arch = gp::parseXML(
        "<graph type=\"architecture\"><node id=\"a\" label=\"A\"/>"
        "<node id=\"b\" label=\"B\"/><edge from=\"a\" to=\"b\"/></graph>");
    gl::layout(arch);
    std::string archSvg = ge::toSVG(arch);
    CHECK(archSvg.find("<svg") != std::string::npos);
}

static void testExportGraphAndHelpers()
{
    Graph g = gp::parseMermaid("flowchart TD\nA[Start]-->B[End]\n");

    ge::ExportResult bad = ge::exportGraph(g, "unknown");
    CHECK(!bad.ok);
    CHECK(bad.message.find("unknown export format") != std::string::npos);

    ge::ExportResult url = ge::exportGraph(g, "url");
    CHECK(url.ok);
    CHECK(url.content.find("mermaid.live") != std::string::npos);

    ge::ExportResult model = ge::exportGraph(g, "model");
    CHECK(model.ok);
    CHECK(model.content.find("\"nodes\"") != std::string::npos);

    std::string      outPath = "test-store-tmp/ci-unit-export.mmd";
    ge::ExportResult file    = ge::exportGraph(g, "mermaid", outPath);
    CHECK(file.ok);
    CHECK(file.path == outPath);
    std::string saved = ge::readFile(outPath);
    CHECK(saved.find("flowchart TD") != std::string::npos);

    CHECK(ge::xmlEscape("a<b>&\"'") == "a&lt;b&gt;&amp;&quot;&#39;");
    {
        // raw = a<'b>&"
        std::string raw;
        raw.push_back('a');
        raw.push_back('<');
        raw.push_back('\'');
        raw.push_back('b');
        raw.push_back('>');
        raw.push_back('&');
        raw.push_back('"');
        CHECK(ge::xmlTextEscape(raw) == "a&lt;'b&gt;&amp;\"");
        CHECK(ge::xmlAttrEscape(raw) == "a&lt;'b>&amp;&quot;");
    }
    CHECK(ge::sanitizeMermaidId("a-b.c") == "a_b_c");
    CHECK(ge::sanitizeMermaidId("!!!") == "___");

    // GRAPHMCP_ASSETS 优先于 CWD 相对路径
    {
#ifdef _WIN32
        _putenv_s("GRAPHMCP_ASSETS", "third_party/excalidraw-assets");
#else
        setenv("GRAPHMCP_ASSETS", "third_party/excalidraw-assets", 1);
#endif
        std::string fontPath = ge::bundledAssetPath("Virgil.woff2");
        CHECK(fontPath.find("Virgil.woff2") != std::string::npos);
        CHECK(ge::fileReadable(fontPath));
#ifdef _WIN32
        _putenv_s("GRAPHMCP_ASSETS", "");
#else
        unsetenv("GRAPHMCP_ASSETS");
#endif
    }

    std::string svg = ge::toSVG(g);
    CHECK(ge::svgDim(svg, "width") > 0);
    CHECK(ge::svgDim(svg, "height") > 0);
    CHECK(ge::fileUrl("C:/tmp/a.svg").find("file://") == 0);
}

static void testExcalidrawShapes()
{
    std::string doc = R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"d1","type":"diamond","x":10,"y":10,"width":80,"height":60},
        {"id":"e1","type":"ellipse","x":120,"y":10,"width":90,"height":50},
        {"id":"l1","type":"line","x":0,"y":0,"width":50,"height":50,
         "points":[[0,0],[50,50]],"strokeStyle":"dashed"}
      ]})";
    Graph       g   = gp::parseExcalidraw(doc);
    std::string svg = ge::toSVG(g);
    CHECK(svg.find("<polygon") != std::string::npos);
    CHECK(svg.find("<ellipse") != std::string::npos);
    CHECK(svg.find("stroke-dasharray") != std::string::npos);
}

static void testMcpExtended()
{
    gs::Store   store("test-store-tmp");
    std::string err;
    Json        resp;

    Json convert = Json::parse(
        R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{
            "name":"graph_convert","arguments":{
              "content":"flowchart TD\nP-->Q","format":"mermaid","to":"mermaid"}}})",
        &err);
    CHECK(mcp::handleMessage(convert, store, resp));
    CHECK(resp.find("result")->find("content")->a->at(0).str("text").find(
              "flowchart TD") != std::string::npos);

    Json validate = Json::parse(
        R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{
            "name":"graph_validate","arguments":{
              "content":"flowchart TD\nX-->Y","format":"mermaid"}}})",
        &err);
    CHECK(mcp::handleMessage(validate, store, resp));
    std::string vtext =
        resp.find("result")->find("content")->a->at(0).str("text");
    Json vj = Json::parse(vtext, &err);
    CHECK(vj.boolean("valid", false));

    Json create = Json::parse(
        R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{
            "name":"graph_create","arguments":{
              "content":"flowchart TD\nM-->N","name":"mcp-ext"}}})",
        &err);
    CHECK(mcp::handleMessage(create, store, resp));
    std::string ctext =
        resp.find("result")->find("content")->a->at(0).str("text");
    Json        cj  = Json::parse(ctext, &err);
    std::string gid = cj.str("id");

    Json list = Json::parse(
        R"({"jsonrpc":"2.0","id":13,"method":"tools/call","params":{
            "name":"graph_list","arguments":{}}})",
        &err);
    CHECK(mcp::handleMessage(list, store, resp));
    CHECK(resp.find("result")->find("content")->a->at(0).str("text").find(
              gid) != std::string::npos);

    Json hist = Json::parse(
        R"({"jsonrpc":"2.0","id":14,"method":"tools/call","params":{
            "name":"graph_history","arguments":{"id":")" +
            gid + R"("}}})",
        &err);
    CHECK(mcp::handleMessage(hist, store, resp));
    CHECK(resp.find("result")->find("content")->a->at(0).str("text").find(
              "version") != std::string::npos);

    Json ping =
        Json::parse(R"({"jsonrpc":"2.0","id":15,"method":"ping"})", &err);
    CHECK(mcp::handleMessage(ping, store, resp));
    CHECK(resp.find("result") != nullptr);
}

static std::string mcpText(const Json& resp)
{
    const Json* result = resp.find("result");
    CHECK(result != nullptr);
    const Json* content = result ? result->find("content") : nullptr;
    CHECK(content != nullptr);
    CHECK(content && content->isArr());
    CHECK(content && content->a && !content->a->empty());
    if (content && content->isArr() && content->a && !content->a->empty())
        return content->a->at(0).str("text");
    return "";
}

static void testMcpToolsRemaining()
{
    gs::Store   store("test-store-tmp");
    std::string err;
    Json        resp;

    Json create = Json::parse(
        R"({"jsonrpc":"2.0","id":16,"method":"tools/call","params":{
            "name":"graph_create","arguments":{
              "content":"flowchart TD\nA[Start]-->B[End]","name":"mcp-remaining"}}})",
        &err);
    CHECK(mcp::handleMessage(create, store, resp));
    Json        created = Json::parse(mcpText(resp), &err);
    std::string gid     = created.str("id");
    CHECK(!gid.empty());

    Json ins = Json::parse(
        R"({"jsonrpc":"2.0","id":17,"method":"tools/call","params":{
            "name":"graph_insert","arguments":{"id":")" +
            gid +
            R"(","element":"node","type":"rect","label":"Inserted","position":"400 200"}}})",
        &err);
    CHECK(mcp::handleMessage(ins, store, resp));
    Json inserted = Json::parse(mcpText(resp), &err);
    CHECK(inserted.str("status") == "inserted");
    std::string insertedId = inserted.str("elementId");
    CHECK(!insertedId.empty());

    Json stage = Json::parse(
        R"({"jsonrpc":"2.0","id":18,"method":"tools/call","params":{
            "name":"graph_stage","arguments":{"id":")" +
            gid + R"(","action":"add"}}})",
        &err);
    CHECK(mcp::handleMessage(stage, store, resp));
    Json staged = Json::parse(mcpText(resp), &err);
    CHECK(staged.str("status") == "staged");
    CHECK(staged.num("count") >= 1);

    Json commit = Json::parse(
        R"({"jsonrpc":"2.0","id":19,"method":"tools/call","params":{
            "name":"graph_commit","arguments":{"id":")" +
            gid + R"(","message":"remaining commit"}}})",
        &err);
    CHECK(mcp::handleMessage(commit, store, resp));
    Json committed = Json::parse(mcpText(resp), &err);
    CHECK(committed.str("status") == "committed");
    CHECK(committed.num("version") >= 2);

    Json diff = Json::parse(
        R"({"jsonrpc":"2.0","id":20,"method":"tools/call","params":{
            "name":"graph_diff","arguments":{"id":")" +
            gid + R"(","v1":1,"v2":2,"format":"json"}}})",
        &err);
    CHECK(mcp::handleMessage(diff, store, resp));
    Json        dj         = Json::parse(mcpText(resp), &err);
    const Json* operations = dj.find("operations");
    CHECK(operations != nullptr);
    if (operations)
        CHECK(operations->size() >= 1);
    else
        CHECK(false);

    Json checkout = Json::parse(
        R"({"jsonrpc":"2.0","id":21,"method":"tools/call","params":{
            "name":"graph_checkout","arguments":{"id":")" +
            gid + R"(","version":1}}})",
        &err);
    CHECK(mcp::handleMessage(checkout, store, resp));
    Json co = Json::parse(mcpText(resp), &err);
    CHECK(co.str("status") == "checkout complete");
    CHECK((int)co.num("headVersion") == 1);

    Json rollback = Json::parse(
        R"({"jsonrpc":"2.0","id":22,"method":"tools/call","params":{
            "name":"graph_rollback","arguments":{"id":")" +
            gid + R"(","version":1}}})",
        &err);
    CHECK(mcp::handleMessage(rollback, store, resp));
    Json rb = Json::parse(mcpText(resp), &err);
    CHECK(rb.str("status") == "rolled back");
    CHECK((int)rb.num("newVersion") >= 3);

    Json delEl = Json::parse(
        R"({"jsonrpc":"2.0","id":23,"method":"tools/call","params":{
            "name":"graph_delete_element","arguments":{"id":")" +
            gid + R"(","node":"A"}}})",
        &err);
    CHECK(mcp::handleMessage(delEl, store, resp));
    Json de = Json::parse(mcpText(resp), &err);
    CHECK(de.str("status") == "deleted");
    CHECK(de.str("elementType") == "node");

    // ── graph_open MCP 冒烟（launch=false，不拉起外部编辑器）
    {
        // 1. browser 模式：验证生成 mermaid.live URL
        Json ob = Json::parse(
            R"({"jsonrpc":"2.0","id":24,"method":"tools/call","params":{
            "name":"graph_open","arguments":{"id":")" +
            gid + R"(","editor":"browser","launch":false}}})",
            &err);
        CHECK(mcp::handleMessage(ob, store, resp));
        Json obj = Json::parse(mcpText(resp), &err);
        CHECK(obj.str("target").find("https://mermaid.live/") == 0);
        CHECK(obj.str("editor") == "browser");
        CHECK(obj.boolean("launched") == false);

        // 2. drawio 模式：验证生成 .drawio 文件路径
        Json od = Json::parse(
            R"({"jsonrpc":"2.0","id":25,"method":"tools/call","params":{
            "name":"graph_open","arguments":{"id":")" +
            gid + R"(","editor":"drawio","launch":false}}})",
            &err);
        CHECK(mcp::handleMessage(od, store, resp));
        Json odj = Json::parse(mcpText(resp), &err);
        CHECK(odj.str("target").find("/open.drawio") != std::string::npos);
        CHECK(odj.str("editor") == "drawio");
        CHECK(odj.boolean("launched") == false);
        CHECK(odj.find("availableEditors") != nullptr);

        // 3. excalidraw 模式：验证生成 .excalidraw 文件路径
        Json oe = Json::parse(
            R"({"jsonrpc":"2.0","id":26,"method":"tools/call","params":{
            "name":"graph_open","arguments":{"id":")" +
            gid + R"(","editor":"excalidraw","launch":false}}})",
            &err);
        CHECK(mcp::handleMessage(oe, store, resp));
        Json oej = Json::parse(mcpText(resp), &err);
        CHECK(oej.str("target").find("/open.excalidraw") != std::string::npos);
        CHECK(oej.str("editor") == "excalidraw");
        CHECK(oej.boolean("launched") == false);

        // 4. svg 模式 + editorPath：验证显式编辑器路径
        Json os = Json::parse(
            R"({"jsonrpc":"2.0","id":27,"method":"tools/call","params":{
            "name":"graph_open","arguments":{"id":")" +
            gid +
            R"(","editor":"svg","editorPath":"/usr/bin/code","launch":false}}})",
            &err);
        CHECK(mcp::handleMessage(os, store, resp));
        Json osj = Json::parse(mcpText(resp), &err);
        CHECK(osj.str("target").find("/open.svg") != std::string::npos);
        CHECK(osj.str("editor") == "svg");
        CHECK(osj.str("editorPath") == "/usr/bin/code");
        CHECK(osj.boolean("launched") == false);

        // 5. 不存在的图：验证错误返回
        Json onf = Json::parse(
            R"({"jsonrpc":"2.0","id":28,"method":"tools/call","params":{
            "name":"graph_open","arguments":{"id":"nonexistent-id","launch":false}}})",
            &err);
        CHECK(mcp::handleMessage(onf, store, resp));
        CHECK(resp.find("result")->find("content")->a->at(0)
                  .str("text")
                  .find("graph not found") != std::string::npos);
    }

    // ── graph_import MCP 冒烟（编辑回导闭环）
    {
        // 1. 自动探测 open.drawio（由上面 graph_open drawio 生成）
        Json ia = Json::parse(
            R"({"jsonrpc":"2.0","id":29,"method":"tools/call","params":{
            "name":"graph_import","arguments":{"id":")" +
            gid + R"("}}})",
            &err);
        CHECK(mcp::handleMessage(ia, store, resp));
        Json iaj = Json::parse(mcpText(resp), &err);
        CHECK(iaj.str("status") == "imported");
        CHECK(iaj.str("id") == gid);
        CHECK((int)iaj.num("version") >= 2);
        CHECK(iaj.num("nodes") >= 1);

        // 2. 显式 content（mermaid 格式）
        Json ic = Json::parse(
            R"({"jsonrpc":"2.0","id":30,"method":"tools/call","params":{
            "name":"graph_import","arguments":{"id":")" +
            gid +
            R"(","content":"flowchart TD\nX-->Y-->Z","format":"mermaid"}}})",
            &err);
        CHECK(mcp::handleMessage(ic, store, resp));
        Json icj = Json::parse(mcpText(resp), &err);
        CHECK(icj.str("status") == "imported");
        CHECK((int)icj.num("version") >= 3);
        CHECK(icj.num("nodes") == 3);

        // 3. 不存在的 id：验证错误返回
        Json inf = Json::parse(
            R"({"jsonrpc":"2.0","id":31,"method":"tools/call","params":{
            "name":"graph_import","arguments":{"id":"nonexistent-id"}}})",
            &err);
        CHECK(mcp::handleMessage(inf, store, resp));
        CHECK(resp.find("result")->find("content")->a->at(0)
                  .str("text")
                  .find("graph not found") != std::string::npos);
    }

    Json copen = Json::parse(
        R"({"jsonrpc":"2.0","id":32,"method":"tools/call","params":{
            "name":"graph_cursor_open","arguments":{"id":")" +
            gid + R"(","target":"nodes"}}})",
        &err);
    CHECK(mcp::handleMessage(copen, store, resp));
    Json        copenj = Json::parse(mcpText(resp), &err);
    std::string cid    = copenj.str("cursor");
    CHECK(!cid.empty());

    Json cget = Json::parse(
        R"({"jsonrpc":"2.0","id":26,"method":"tools/call","params":{
            "name":"graph_cursor_get","arguments":{"id":")" +
            gid + R"(","cursor":")" + cid + R"("}}})",
        &err);
    CHECK(mcp::handleMessage(cget, store, resp));
    Json cgetj = Json::parse(mcpText(resp), &err);
    CHECK(cgetj.find("index") != nullptr);

    Json cmove1 = Json::parse(
        R"({"jsonrpc":"2.0","id":27,"method":"tools/call","params":{
            "name":"graph_cursor_move","arguments":{"id":")" +
            gid + R"(","cursor":")" + cid + R"(","delta":1}}})",
        &err);
    CHECK(mcp::handleMessage(cmove1, store, resp));
    Json move1j = Json::parse(mcpText(resp), &err);
    CHECK(move1j.find("index") != nullptr);

    Json cmove2 = Json::parse(
        R"({"jsonrpc":"2.0","id":28,"method":"tools/call","params":{
            "name":"graph_cursor_move","arguments":{"id":")" +
            gid + R"(","cursor":")" + cid + R"(","delta":-1}}})",
        &err);
    CHECK(mcp::handleMessage(cmove2, store, resp));
    Json move2j = Json::parse(mcpText(resp), &err);
    CHECK(move2j.find("index") != nullptr);

    Json cclose = Json::parse(
        R"({"jsonrpc":"2.0","id":29,"method":"tools/call","params":{
            "name":"graph_cursor_close","arguments":{"id":")" +
            gid + R"(","cursor":")" + cid + R"("}}})",
        &err);
    CHECK(mcp::handleMessage(cclose, store, resp));
    Json cclosej = Json::parse(mcpText(resp), &err);
    CHECK(cclosej.boolean("closed", false));

    Json del = Json::parse(
        R"({"jsonrpc":"2.0","id":30,"method":"tools/call","params":{
            "name":"graph_delete","arguments":{"id":")" +
            gid + R"(","force":true}}})",
        &err);
    CHECK(mcp::handleMessage(del, store, resp));
    CHECK(mcpText(resp).find("deleted") != std::string::npos);
}

static void testMcpErrors()
{
    gs::Store   store("test-store-tmp");
    std::string err;
    Json        resp;

    Json unknown = Json::parse(
        R"({"jsonrpc":"2.0","id":31,"method":"tools/call","params":{
            "name":"graph_nope","arguments":{}}})",
        &err);
    CHECK(mcp::handleMessage(unknown, store, resp));
    const Json* result = resp.find("result");
    CHECK(result != nullptr);
    if (result)
        CHECK(result->boolean("isError", false));
    else
        CHECK(false);
    CHECK(mcpText(resp).find("unknown tool") != std::string::npos);

    Json validateMissing = Json::parse(
        R"({"jsonrpc":"2.0","id":32,"method":"tools/call","params":{
            "name":"graph_validate","arguments":{}}})",
        &err);
    CHECK(mcp::handleMessage(validateMissing, store, resp));
    result = resp.find("result");
    CHECK(result != nullptr);
    if (result)
        CHECK(result->boolean("isError", false));
    else
        CHECK(false);
    CHECK(mcpText(resp).find("provide either 'id' or 'content'") !=
          std::string::npos);

    Json create = Json::parse(
        R"({"jsonrpc":"2.0","id":33,"method":"tools/call","params":{
            "name":"graph_create","arguments":{
              "content":"flowchart TD\nA-->B","name":"mcp-error"}}})",
        &err);
    CHECK(mcp::handleMessage(create, store, resp));
    Json        created = Json::parse(mcpText(resp), &err);
    std::string gid     = created.str("id");
    CHECK(!gid.empty());

    Json emptyCommit = Json::parse(
        R"({"jsonrpc":"2.0","id":34,"method":"tools/call","params":{
            "name":"graph_commit","arguments":{"id":")" +
            gid + R"(","message":"should fail"}}})",
        &err);
    CHECK(mcp::handleMessage(emptyCommit, store, resp));
    result = resp.find("result");
    CHECK(result != nullptr);
    if (result)
        CHECK(result->boolean("isError", false));
    else
        CHECK(false);
    CHECK(mcpText(resp).find("nothing to commit") != std::string::npos);
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

    // whiteboard freedraw: SVG/drawio 保留矢量视图，Mermaid 丢弃
    Graph       wb    = gp::parseExcalidraw(R"({
      "type":"excalidraw","version":2,"elements":[
        {"id":"fdA","type":"freedraw","x":100,"y":80,"strokeColor":"#00aa00",
         "strokeWidth":2,"points":[[0,0],[20,10],[40,6],[70,20]],
         "pressures":[0.2,0.5,0.9,0.4],"simulatePressure":true}
      ]})");
    std::string wbSvg = ge::toSVG(wb);
    CHECK(wbSvg.find("<path d=\"M ") != std::string::npos);
    CHECK(wbSvg.find("<polyline") == std::string::npos);
    CHECK(wbSvg.find("#00aa00") != std::string::npos);

    std::string wbDrawio = ge::toDrawio(wb);
    CHECK(wbDrawio.find("freedraw") != std::string::npos);
    CHECK(wbDrawio.find("sourcePoint") != std::string::npos);
    CHECK(wbDrawio.find("targetPoint") != std::string::npos);

    std::string wbMmd = ge::toMermaid(wb);
    CHECK(wbMmd.find("flowchart TD") != std::string::npos);
    CHECK(wbMmd.find("freedraw") == std::string::npos);
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
    CHECK(resp.find("result")->find("tools")->size() == 39);
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

    // tools/call graph_status
    Json st = Json::parse(
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{
            "name":"graph_status","arguments":{"id":")" +
            gid + R"("}}})",
        &err);
    CHECK(mcp::handleMessage(st, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("headVersion") != std::string::npos);

    // tools/call graph_layout
    Json lay = Json::parse(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{
            "name":"graph_layout","arguments":{"id":")" +
            gid + R"(","strategy":"auto"}}})",
        &err);
    CHECK(mcp::handleMessage(lay, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("layout applied") != std::string::npos);

    // tools/call graph_show
    Json sh = Json::parse(
        R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{
            "name":"graph_show","arguments":{"id":")" +
            gid + R"("}}})",
        &err);
    CHECK(mcp::handleMessage(sh, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("nodeList") != std::string::npos);

    // tools/call graph_update
    Json upd = Json::parse(
        R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{
            "name":"graph_update","arguments":{"id":")" +
            gid + R"(","node":"X","set":"label=MCP Updated"}}})",
        &err);
    CHECK(mcp::handleMessage(upd, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("updated") != std::string::npos);

    // tools/call graph_draft
    Json dr = Json::parse(
        R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{
            "name":"graph_draft","arguments":{"id":")" +
            gid + R"(","action":"show"}}})",
        &err);
    CHECK(mcp::handleMessage(dr, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("operationCount") != std::string::npos);

    // tools/call graph_commit (commit all)
    Json cmt = Json::parse(
        R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{
            "name":"graph_commit","arguments":{"id":")" +
            gid + R"(","message":"mcp test commit","all":true}}})",
        &err);
    CHECK(mcp::handleMessage(cmt, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("committed") != std::string::npos);

    // tools/call graph_diff
    Json df = Json::parse(
        R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{
            "name":"graph_diff","arguments":{"id":")" +
            gid + R"(","v1":1,"v2":2,"format":"json"}}})",
        &err);
    CHECK(mcp::handleMessage(df, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("operations") != std::string::npos);

    // tools/call graph_delete
    Json del = Json::parse(
        R"({"jsonrpc":"2.0","id":13,"method":"tools/call","params":{
            "name":"graph_delete","arguments":{"id":")" +
            gid + R"(","force":true}}})",
        &err);
    CHECK(mcp::handleMessage(del, store, resp));
    text = resp.find("result")->find("content")->a->at(0).str("text");
    CHECK(text.find("deleted") != std::string::npos);
}

static void testParseDrawio()
{
    std::string d =
        "<mxfile host=\"graphmcp\">\n"
        "  <diagram name=\"Test\" id=\"d1\">\n"
        "    <mxGraphModel>\n"
        "      <root>\n"
        "        <mxCell id=\"0\"/>\n"
        "        <mxCell id=\"1\" parent=\"0\"/>\n"
        "        <mxCell id=\"A\" value=\"Start\" style=\"rounded=1;\" "
        "vertex=\"1\" parent=\"1\">\n"
        "          <mxGeometry x=\"100\" y=\"50\" width=\"120\" height=\"60\" "
        "as=\"geometry\"/>\n"
        "        </mxCell>\n"
        "        <mxCell id=\"B\" value=\"End\" style=\"rhombus;\" "
        "vertex=\"1\" parent=\"1\">\n"
        "          <mxGeometry x=\"300\" y=\"50\" width=\"120\" height=\"60\" "
        "as=\"geometry\"/>\n"
        "        </mxCell>\n"
        "        <mxCell id=\"edge1\" value=\"ok\" style=\"dashed=1;\" "
        "edge=\"1\" parent=\"1\" source=\"A\" target=\"B\">\n"
        "          <mxGeometry relative=\"1\" as=\"geometry\"/>\n"
        "        </mxCell>\n"
        "      </root>\n"
        "    </mxGraphModel>\n"
        "  </diagram>\n"
        "</mxfile>";
    Graph g = gp::parseDrawio(d);
    CHECK(g.name == "Test");
    CHECK(g.nodes.size() == 2);
    CHECK(g.edges.size() == 1);
    CHECK(g.findNode("A")->shape == "round");
    CHECK(g.findNode("B")->shape == "diamond");
    CHECK(g.edges[0].from == "A");
    CHECK(g.edges[0].style == "dashed");
    Graph g2 = gp::parseAny(d);
    CHECK(g2.nodes.size() == 2);
}

static void testDrawioRoundTrip()
{
    Graph g;
    g.name   = "RoundTrip";
    g.type   = "flowchart";
    Node& n1 = g.ensureNode("n1", "Hello");
    n1.shape = "round";
    n1.x = 100; n1.y = 100; n1.w = 120; n1.h = 60;
    Node& n2 = g.ensureNode("n2", "World");
    n2.shape = "diamond";
    n2.x = 300; n2.y = 100; n2.w = 120; n2.h = 60;
    g.addEdge("n1", "n2", "Link", "solid");
    std::string dx = ge::toDrawio(g);
    CHECK(!dx.empty());
    CHECK(dx.find("<mxfile") != std::string::npos);
    Graph g2 = gp::parseDrawio(dx);
    CHECK(g2.name == "RoundTrip");
    CHECK(g2.nodes.size() >= 2);
    CHECK(g2.edges.size() >= 1);
}

static void testMcpGraphImport()
{
    gs::Store   store("test-store-tmp");
    std::string err;
    Json call1 = Json::parse(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"graph_create","arguments":{"content":"flowchart TD\nA-->B","name":"import-test"}}})", &err);
    CHECK(err.empty());
    Json resp1;
    CHECK(mcp::handleMessage(call1, store, resp1));
    const Json* res1 = resp1.find("result");
    CHECK(res1 != nullptr);
    const Json* ct1 = res1->find("content") ? &res1->find("content")->a->at(0) : nullptr;
    CHECK(ct1 != nullptr);
    Json j1 = Json::parse(ct1->str("text"), &err);
    std::string gid = j1.str("id");
    CHECK(!gid.empty());
    Graph g;
    CHECK(store.load(gid, g, 0, &err));
    std::string fpath = store.root() + "/" + gid + "/open.drawio";
    CHECK(ge::writeFile(fpath, ge::toDrawio(g)));
    Json call2 = Json::parse(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"graph_import","arguments":{"id":")" + gid + R"("}}})", &err);
    Json resp2;
    CHECK(mcp::handleMessage(call2, store, resp2));
    const Json* res2 = resp2.find("result");
    const Json* ct2 = res2 && res2->find("content") ? &res2->find("content")->a->at(0) : nullptr;
    Json j2 = Json::parse(ct2 ? ct2->str("text") : "", &err);
    CHECK(j2.str("status") == "imported");
    CHECK((int)j2.num("version") == 2);
}

static void testTableModel()
{
    gt::Table t = gt::Table::fromCsv(
        "from,to,label\n"
        "A,B,\"x,y\"\n"
        "B,C,\n");
    CHECK(t.columns.size() == 3);
    CHECK(t.rows.size() == 2);
    CHECK(t.cell(0, 2) == "x,y");
    t.addColumn("weight", "1");
    CHECK(t.columns.size() == 4);
    CHECK(t.cell(0, 3) == "1");
    t.setCell(1, 3, "9");
    CHECK(t.cell(1, 3) == "9");
    std::string csv = t.toCsv();
    gt::Table   t2  = gt::Table::fromCsv(csv);
    CHECK(t2.rows.size() == 2);
    CHECK(t2.cell(0, 2) == "x,y");
    Json j = t.toJson();
    gt::Table t3 = gt::Table::fromJson(j);
    CHECK(t3.columns.size() == 4);

    // BOM CSV（Excel CSV UTF-8）应能正确识别首列
    gt::Table bom = gt::Table::fromCsv("\xEF\xBB\xBF编号,名称\n1,爬虫\n");
    CHECK(bom.colIndex("编号") == 0);
    CHECK(bom.cell(0, 1) == "爬虫");
}

static void testTableStoreAndBridge()
{
#ifdef _WIN32
    _putenv_s("GRAPHMCP_STORE", "test-table-store-tmp");
#else
    setenv("GRAPHMCP_STORE", "test-table-store-tmp", 1);
#endif
    ge::removeDirectory("test-table-store-tmp");
    gts::TableStore ts("test-table-store-tmp");
    gt::Table       t = gt::Table::fromCsv("id,label,parent\nceo,CEO,\ncto,CTO,ceo\n");
    t.name            = "org";
    int v1            = ts.save(t, "init");
    CHECK(v1 == 1);
    gt::Table loaded;
    CHECK(ts.load(t.id, loaded));
    CHECK(loaded.rows.size() == 2);

    Graph g = gtb::graphFromTable(loaded);
    CHECK(g.type == "orgchart");
    CHECK(g.nodes.size() >= 2);
    CHECK(!g.edges.empty());

    gt::Table edges = gtb::tableFromGraph(g, "edgelist", false);
    CHECK(edges.columns.size() == 3);
    CHECK(!edges.rows.empty());

    gt::Table sk = gtb::tableFromGraph(g, "skeleton", true);
    CHECK(!sk.columns.empty());
    CHECK(sk.hasHintRow);

    // align
    gt::Table drop = gt::Table::fromCsv("enemy_id,item\n");
    drop.id        = gm::genId("t");
    ts.save(drop, "drop");
    gt::Table enemies = gt::Table::fromCsv("编号,名称\n1,爬虫\n2,肿瘤\n");
    enemies.id        = gm::genId("t");
    ts.save(enemies, "enemies");
    // reload drop
    ts.load(drop.id, drop);
    Json align = gtb::tableAlign(enemies, drop, "编号", "enemy_id");
    CHECK((int)align.num("added_rows") == 2);
    CHECK(drop.rows.size() == 2);

    Json allowed = Json::obj();
    Json vals    = Json::arr();
    vals.push(Json("小怪"));
    vals.push(Json("Boss"));
    allowed.set("层级", vals);
    gt::Table bad = gt::Table::fromCsv("层级\n小怪\n精英\n");
    gt::Table rep = gtb::tableCheck(bad, allowed, nullptr);
    CHECK(rep.rows.size() == 1);
    bad.hasHintRow = true;
    gt::Table rep2 = gtb::tableCheck(bad, allowed, nullptr, true);
    CHECK(rep2.rows.size() == 1);  // 忽略首行后仍有一条违规（第二数据行）

    CHECK(ts.remove(t.id));
    ge::removeDirectory("test-table-store-tmp");
}

// 解析 example_input 夹具路径（支持从仓库根或 bin/ 运行）
static std::string readExampleInput(const std::string& name)
{
    const std::string cands[] = {
        "examples/example_input/" + name,
        "../examples/example_input/" + name,
        "../../examples/example_input/" + name,
    };
    for (const auto& p : cands) {
        std::string s = ge::readFile(p);
        if (!s.empty())
            return s;
    }
    return {};
}

static void testTableFixtures()
{
    // enemy_sample：通用表列与枚举校验
    std::string enemy_csv = readExampleInput("enemy_sample.csv");
    CHECK(!enemy_csv.empty());
    if (!enemy_csv.empty()) {
        gt::Table enemy = gt::Table::fromCsv(enemy_csv);
        CHECK(enemy.colIndex("编号") >= 0);
        CHECK(enemy.colIndex("名称") >= 0);
        CHECK(enemy.colIndex("层级") >= 0);
        CHECK(enemy.rows.size() >= 4);

        Json allowed = Json::obj();
        Json vals    = Json::arr();
        vals.push(Json("小怪"));
        vals.push(Json("Boss"));
        allowed.set("层级", vals);
        gt::Table report = gtb::tableCheck(enemy, allowed, nullptr, false);
        CHECK(report.rows.size() == 0);
    }

    // skill_relations：边表可投影为 flowchart
    std::string skill_csv = readExampleInput("skill_relations.csv");
    CHECK(!skill_csv.empty());
    if (!skill_csv.empty()) {
        gt::Table skill = gt::Table::fromCsv(skill_csv);
        Graph     g     = gtb::graphFromTable(skill);
        CHECK(g.type == "flowchart");
        CHECK(g.edges.size() == skill.rows.size());
        CHECK(g.nodes.size() >= 2);
        // 端点去重后节点数应不超过 2*边数，且不少于去重端点下界
        CHECK(g.nodes.size() <= skill.rows.size() * 2);
    }
}

static void testTableMcpTools()
{
#ifdef _WIN32
    _putenv_s("GRAPHMCP_STORE", "test-table-mcp-tmp");
#else
    setenv("GRAPHMCP_STORE", "test-table-mcp-tmp", 1);
#endif
    ge::removeDirectory("test-table-mcp-tmp");
    gs::Store store("test-table-mcp-tmp");
    mcp::ToolRunner runner(store);
    Json            args = Json::obj();
    args.set("content", "from,to,label\nA,B,go\n");
    args.set("name", "edges");
    Json res = runner.call("table_create", args);
    CHECK(!res.boolean("isError", false));
    std::string err;
    Json        body = Json::parse(res.find("content")->a->at(0).str("text"), &err);
    CHECK(err.empty());
    std::string tid = body.str("id");
    CHECK(!tid.empty());

    // create 语义：同 id 重复创建应失败（无 force）
    Json dup = Json::obj();
    dup.set("id", tid);
    dup.set("content", "a,b\n1,2\n");
    Json dupRes = runner.call("table_create", dup);
    CHECK(dupRes.boolean("isError", false));

    // 向后兼容：LEGACY_UPSERT 允许同 id 覆盖（仅 1/true 生效）
    {
        EnvGuard legacy("GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT", "1");
        Json     legacyCreate = runner.call("table_create", dup);
        CHECK(!legacyCreate.boolean("isError", false));
        Json legacyBody = Json::parse(
            legacyCreate.find("content")->a->at(0).str("text"), &err);
        CHECK(legacyBody.find("compat_warnings") != nullptr);
    }
    {
        // "false" 不应启用 legacy upsert
        EnvGuard bogus("GRAPHMCP_TABLE_CREATE_LEGACY_UPSERT", "false");
        Json     stillFail = runner.call("table_create", dup);
        CHECK(stillFail.boolean("isError", false));
    }

    // 恢复边表内容供后续用例
    Json restore = Json::obj();
    restore.set("id", tid);
    restore.set("content", "from,to,label\nA,B,go\n");
    restore.set("force", true);
    CHECK(!runner.call("table_create", restore).boolean("isError", false));

    Json u = Json::obj();
    u.set("id", tid);
    u.set("add_rows", R"([["B","C","next"]])");
    Json ur = runner.call("table_update", u);
    CHECK(!ur.boolean("isError", false));

    // set_cells 语义：缺 column/col_index 应失败
    Json badUpdate = Json::obj();
    badUpdate.set("id", tid);
    badUpdate.set("set_cells", R"([{"row":0,"value":"x"}])");
    Json badRes = runner.call("table_update", badUpdate);
    CHECK(badRes.boolean("isError", false));

    // 向后兼容：旧字段 col 仍可用；批量时 warning 去重为 1 条
    Json colUpdate = Json::obj();
    colUpdate.set("id", tid);
    colUpdate.set(
        "set_cells",
        R"([{"row":0,"col":"from","value":"AA"},{"row":0,"col":"to","value":"BB"}])");
    Json colRes = runner.call("table_update", colUpdate);
    CHECK(!colRes.boolean("isError", false));
    Json colBody =
        Json::parse(colRes.find("content")->a->at(0).str("text"), &err);
    CHECK(colBody.find("compat_warnings") != nullptr);
    CHECK(colBody.find("compat_warnings")->size() == 1);

    Json gf = Json::obj();
    gf.set("table_id", tid);
    Json gr = runner.call("graph_from_table", gf);
    CHECK(!gr.boolean("isError", false));
    Json gb = Json::parse(gr.find("content")->a->at(0).str("text"), &err);
    CHECK(gb.str("status") == "ok");
    CHECK((int)gb.num("nodes") >= 2);

    // from_graph 预览截断
    Json tf = Json::obj();
    tf.set("graph_id", gb.str("id"));
    tf.set("mode", "nodelist");
    tf.set("preview_rows", 1);
    Json tfr = runner.call("table_from_graph", tf);
    CHECK(!tfr.boolean("isError", false));
    Json tfj = Json::parse(tfr.find("content")->a->at(0).str("text"), &err);
    CHECK(tfj.boolean("truncated", false) || (int)tfj.num("rows") <= 1);
    if (tfj.boolean("truncated", false))
        CHECK(tfj.str("hint").find("table_export") != std::string::npos);

    // table_check：LEGACY_HINT 使缺省不跳过 hint 行
    {
        Json hintCreate = Json::obj();
        hintCreate.set("content", "层级\n非法提示\n小怪\n");
        hintCreate.set("name", "hint-check");
        Json hc = runner.call("table_create", hintCreate);
        CHECK(!hc.boolean("isError", false));
        Json hcb =
            Json::parse(hc.find("content")->a->at(0).str("text"), &err);
        std::string hid = hcb.str("id");
        // 标记 hasHintRow：通过 from_graph skeleton 更自然，这里直接 update
        // 存盘字段——用 table_export model / 再 import 太重，改为底层 store
        gts::TableStore ts("test-table-mcp-tmp");
        gt::Table       ht;
        CHECK(ts.load(hid, ht));
        ht.hasHintRow = true;
        CHECK(ts.save(ht, "mark hint") > 0);

        Json allowed = Json::obj();
        Json vals    = Json::arr();
        vals.push(Json("小怪"));
        vals.push(Json("Boss"));
        allowed.set("层级", vals);

        // 默认（hasHintRow）：跳过首行 → 0 违规
        Json ck = Json::obj();
        ck.set("id", hid);
        ck.set("allowed", allowed.dump());
        Json ckr = runner.call("table_check", ck);
        CHECK(!ckr.boolean("isError", false));
        Json ckj =
            Json::parse(ckr.find("content")->a->at(0).str("text"), &err);
        CHECK((int)ckj.num("violations") == 0);

        EnvGuard legacyHint("GRAPHMCP_TABLE_CHECK_LEGACY_HINT", "1");
        Json     ckr2 = runner.call("table_check", ck);
        CHECK(!ckr2.boolean("isError", false));
        Json ckj2 =
            Json::parse(ckr2.find("content")->a->at(0).str("text"), &err);
        CHECK((int)ckj2.num("violations") == 1);
        CHECK(ckj2.find("compat_warnings") != nullptr);
        CHECK(ckj2.boolean("ignore_hint_row") == false);
    }

    // writeFileAtomic 轻量回环
    {
        std::string p = "test-table-mcp-tmp/atomic-probe.txt";
        CHECK(ge::writeFileAtomic(p, "hello-atomic"));
        CHECK(ge::readFile(p) == "hello-atomic");
        CHECK(ge::writeFileAtomic(p, "hello-atomic-2"));
        CHECK(ge::readFile(p) == "hello-atomic-2");
    }

    // table_import：同 id upsert 成功
    {
        Json imp = Json::obj();
        imp.set("id", tid);
        imp.set("content", "from,to,label\nX,Y,z\n");
        imp.set("name", "edges-imported");
        Json ir = runner.call("table_import", imp);
        CHECK(!ir.boolean("isError", false));
        Json ib =
            Json::parse(ir.find("content")->a->at(0).str("text"), &err);
        CHECK(ib.str("status") == "imported");
        CHECK(ib.str("id") == tid);
        CHECK((int)ib.num("version") >= 2);
    }

    // table_export：to=csv|model
    {
        Json ex = Json::obj();
        ex.set("id", tid);
        ex.set("to", "csv");
        Json er = runner.call("table_export", ex);
        CHECK(!er.boolean("isError", false));
        std::string csv_out = er.find("content")->a->at(0).str("text");
        CHECK(csv_out.find("from") != std::string::npos);
        CHECK(csv_out.find("X") != std::string::npos);

        ex.set("to", "model");
        Json em = runner.call("table_export", ex);
        CHECK(!em.boolean("isError", false));
        Json model =
            Json::parse(em.find("content")->a->at(0).str("text"), &err);
        CHECK(err.empty());
        CHECK(model.str("id") == tid);
        CHECK(model.find("columns") != nullptr);
    }

    // table_align：主键补行
    {
        Json pCreate = Json::obj();
        pCreate.set("content", "key,name\nA,alpha\nB,beta\nC,gamma\n");
        pCreate.set("name", "align-primary");
        Json pr = runner.call("table_create", pCreate);
        CHECK(!pr.boolean("isError", false));
        Json pb =
            Json::parse(pr.find("content")->a->at(0).str("text"), &err);
        std::string pid = pb.str("id");

        Json tCreate = Json::obj();
        tCreate.set("content", "key,extra\nA,keep\n");
        tCreate.set("name", "align-target");
        Json tr = runner.call("table_create", tCreate);
        CHECK(!tr.boolean("isError", false));
        Json tb =
            Json::parse(tr.find("content")->a->at(0).str("text"), &err);
        std::string tgt = tb.str("id");

        Json al = Json::obj();
        al.set("primary_id", pid);
        al.set("target_id", tgt);
        al.set("primary_key", "key");
        al.set("target_key", "key");
        Json ar = runner.call("table_align", al);
        CHECK(!ar.boolean("isError", false));
        Json ab =
            Json::parse(ar.find("content")->a->at(0).str("text"), &err);
        CHECK(ab.str("status") == "ok");
        CHECK((int)ab.find("align")->num("added_rows") == 2);
    }

    // table_show：limit 截断
    {
        Json multi = Json::obj();
        multi.set("content", "c\n1\n2\n3\n4\n5\n");
        multi.set("name", "show-limit");
        Json mr = runner.call("table_create", multi);
        CHECK(!mr.boolean("isError", false));
        Json mb =
            Json::parse(mr.find("content")->a->at(0).str("text"), &err);
        std::string mid = mb.str("id");

        Json sh = Json::obj();
        sh.set("id", mid);
        sh.set("limit", 2);
        Json sr = runner.call("table_show", sh);
        CHECK(!sr.boolean("isError", false));
        Json sb =
            Json::parse(sr.find("content")->a->at(0).str("text"), &err);
        CHECK(sb.boolean("truncated", false));
        CHECK((int)sb.num("total_rows") == 5);
        CHECK(sb.find("rows")->size() == 2);
    }

    // table_delete：无 force 失败，有 force 成功
    {
        Json del = Json::obj();
        del.set("id", tid);
        Json d1 = runner.call("table_delete", del);
        CHECK(d1.boolean("isError", false));
        del.set("force", true);
        Json d2 = runner.call("table_delete", del);
        CHECK(!d2.boolean("isError", false));
        Json db =
            Json::parse(d2.find("content")->a->at(0).str("text"), &err);
        CHECK(db.str("status") == "deleted");
    }

    ge::removeDirectory("test-table-mcp-tmp");
}

static void testTableXml()
{
    // 模式 A：显式列 + 命名子元素
    gt::Table t = gtx::fromXml(
        "<table id=\"t1\" name=\"demo\" hasHintRow=\"false\">"
        "<columns><col>编号</col><col>名称</col><col>层级</col></columns>"
        "<rows>"
        "<row><编号>1</编号><名称>爬虫</名称><层级>小怪</层级></row>"
        "<row><名称>游荡者</名称><编号>2</编号></row>"
        "</rows></table>");
    CHECK(t.id == "t1");
    CHECK(t.name == "demo");
    CHECK(!t.hasHintRow);
    CHECK(t.columns.size() == 3);
    CHECK(t.rows.size() == 2);
    CHECK(t.cell(0, 0) == "1");
    CHECK(t.cell(0, 1) == "爬虫");
    CHECK(t.cell(1, 0) == "2");
    CHECK(t.cell(1, 2) == "");  // 缺列补空

    // 一层嵌套 → 父.子；属性被同名子元素覆盖；属性出现序（z 在 a 前）
    gt::Table nest = gtx::fromXml(
        "<table><columns><col>id</col><col>动画.生成</col><col>动画.受击</col>"
        "<col>名</col></columns>"
        "<row 名=\"attr\" id=\"a\" z=\"1\"><名>elem</名>"
        "<动画><生成>x</生成><受击>y</受击></动画></row></table>");
    CHECK(nest.cell(0, 0) == "a");
    CHECK(nest.cell(0, 1) == "x");
    CHECK(nest.cell(0, 2) == "y");
    CHECK(nest.cell(0, 3) == "elem");

    // toXml 按父标签聚合
    std::string agg = gtx::toXml(nest);
    CHECK(agg.find("<动画>") != std::string::npos);
    CHECK(agg.find("<生成>x</生成>") != std::string::npos);
    CHECK(agg.find("<受击>y</受击>") != std::string::npos);
    // 同一父只出现一次开标签（粗检：第二个 <动画> 应不存在于聚合后）
    {
        size_t first = agg.find("<动画>");
        CHECK(first != std::string::npos);
        size_t second = agg.find("<动画>", first + 1);
        CHECK(second == std::string::npos);
    }

    // 无 columns 时属性按出现序（非字典序）：z 应在 a 前
    {
        gt::Table inferred = gtx::fromXml(
            "<table><row z=\"1\" a=\"2\"/></table>");
        CHECK(inferred.columns.size() == 2);
        CHECK(inferred.columns[0] == "z");
        CHECK(inferred.columns[1] == "a");
    }

    // 重复列名去重 + warning
    {
        std::vector<std::string> warns;
        gt::Table dup = gtx::fromXml(
            "<table><columns><col>a</col><col>a</col><col>b</col></columns>"
            "<row><a>1</a><b>2</b></row></table>",
            &warns);
        CHECK(dup.columns.size() == 2);
        CHECK(dup.columns[0] == "a");
        CHECK(dup.columns[1] == "b");
        CHECK(!warns.empty());
        CHECK(warns[0].find("duplicate") != std::string::npos);
    }

    // 不安全列名拒绝
    {
        bool bad = false;
        try {
            gtx::fromXml(
                "<table><columns><col>a b</col></columns><row/></table>");
        }
        catch (const gt::TableError&) {
            bad = true;
        }
        CHECK(bad);
    }

    // 未知 format / to 报错
    {
        bool bad = false;
        try {
            gtx::parseTableContent("a\n1\n", "yaml");
        }
        catch (const gt::TableError& e) {
            bad = true;
            CHECK(std::string(e.what()).find("unsupported") != std::string::npos);
        }
        CHECK(bad);
        bad = false;
        try {
            gtx::exportTableText(nest, "foo");
        }
        catch (const gt::TableError&) {
            bad = true;
        }
        CHECK(bad);
    }

    // 畸形 XML → TableError（包装 ParseError）
    {
        bool bad = false;
        try {
            gtx::fromXml("not-xml");
        }
        catch (const gt::TableError& e) {
            bad = true;
            CHECK(std::string(e.what()).find("table xml:") != std::string::npos);
        }
        CHECK(bad);
    }

    // XML ↔ JSON ↔ XML 往返
    Json      j  = nest.toJson();
    gt::Table j2 = gt::Table::fromJson(j);
    CHECK(j2.cell(0, 1) == "x");
    std::string xml2 = gtx::toXml(j2);
    gt::Table   back = gtx::fromXml(xml2);
    CHECK(back.columns.size() == nest.columns.size());
    CHECK(back.cell(0, 0) == "a");
    CHECK(back.cell(0, 1) == "x");
    CHECK(back.cell(0, 2) == "y");
    CHECK(back.cell(0, 3) == "elem");

    // 叶子列与嵌套父名冲突
    {
        gt::Table conflict;
        conflict.columns = {"动画", "动画.生成"};
        conflict.rows    = {{"leaf", "x"}};
        bool bad         = false;
        try {
            gtx::toXml(conflict);
        }
        catch (const gt::TableError&) {
            bad = true;
        }
        CHECK(bad);
    }

    // 更深嵌套应报错
    bool threw = false;
    try {
        gtx::fromXml(
            "<table><row><a><b><c>z</c></b></a></row></table>");
    }
    catch (const gt::TableError&) {
        threw = true;
    }
    CHECK(threw);

    // 与 enemy_sample.csv 内容对齐
    std::string enemy_csv = readExampleInput("enemy_sample.csv");
    std::string enemy_xml = readExampleInput("enemy_sample.xml");
    CHECK(!enemy_csv.empty());
    CHECK(!enemy_xml.empty());
    if (!enemy_csv.empty() && !enemy_xml.empty()) {
        gt::Table fromCsv = gt::Table::fromCsv(enemy_csv);
        gt::Table fromXml = gtx::fromXml(enemy_xml);
        CHECK(fromCsv.columns.size() == fromXml.columns.size());
        CHECK(fromCsv.rows.size() == fromXml.rows.size());
        for (size_t c = 0; c < fromCsv.columns.size(); c++)
            CHECK(fromCsv.columns[c] == fromXml.columns[c]);
        for (size_t r = 0; r < fromCsv.rows.size(); r++)
            for (size_t c = 0; c < fromCsv.columns.size(); c++)
                CHECK(fromCsv.cell(r, c) == fromXml.cell(r, c));
    }

    // parseTableContent + MCP format=xml
#ifdef _WIN32
    _putenv_s("GRAPHMCP_STORE", "test-table-xml-mcp-tmp");
#else
    setenv("GRAPHMCP_STORE", "test-table-xml-mcp-tmp", 1);
#endif
    ge::removeDirectory("test-table-xml-mcp-tmp");
    {
        gs::Store       store("test-table-xml-mcp-tmp");
        mcp::ToolRunner runner(store);
        std::string     err;
        Json            imp = Json::obj();
        imp.set("format", "xml");
        imp.set("content",
                "<table name=\"x\"><columns><col>a</col></columns>"
                "<row><a>1</a></row></table>");
        Json ir = runner.call("table_import", imp);
        CHECK(!ir.boolean("isError", false));
        Json ib =
            Json::parse(ir.find("content")->a->at(0).str("text"), &err);
        std::string tid = ib.str("id");
        CHECK(!tid.empty());

        Json ex = Json::obj();
        ex.set("id", tid);
        ex.set("to", "xml");
        Json er = runner.call("table_export", ex);
        CHECK(!er.boolean("isError", false));
        std::string xml_out = er.find("content")->a->at(0).str("text");
        CHECK(xml_out.find("<table") != std::string::npos);
        CHECK(xml_out.find("<a>") != std::string::npos);

        ex.set("to", "model");
        Json em = runner.call("table_export", ex);
        CHECK(!em.boolean("isError", false));
        Json model =
            Json::parse(em.find("content")->a->at(0).str("text"), &err);
        CHECK(err.empty());
        CHECK(model.find("columns") != nullptr);

        // 未知 to 应报错
        ex.set("to", "foo");
        Json ef = runner.call("table_export", ex);
        CHECK(ef.boolean("isError", false));
    }
    ge::removeDirectory("test-table-xml-mcp-tmp");
}

int runAll()
{
    // 防止 graph_open / export 等路径意外拉起浏览器或外部编辑器
#ifdef _WIN32
    _putenv_s("GRAPHMCP_NO_LAUNCH", "1");
#else
    setenv("GRAPHMCP_NO_LAUNCH", "1", 1);
#endif

    testJson();
    testMermaidFlowchart();
    testMermaidMindmapAndER();
    testMarkdown();
    testCSV();
    testXML();
    testExcalidraw();
    testParseAnyAndModel();
    testValidateWarnings();
    testOrgchartAndMindmapExport();
    testExportGraphAndHelpers();
    testExcalidrawShapes();
    testDetect();
    testValidate();
    testLayout();
    testExporters();
    testBase64();
    testStorage();
    testMcpProtocol();
    testTableModel();
    testTableStoreAndBridge();
    testTableFixtures();
    testTableMcpTools();
    testTableXml();
    testMcpExtended();
    testMcpToolsRemaining();
    testMcpErrors();
    testParseDrawio();
    testDrawioRoundTrip();
    testMcpGraphImport();
    testMermaidDeepParsing();
    testMermaidStructureValidation();
    testMermaidClass();
    testMermaidState();
    std::cout << "tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return g_failed == 0 ? 0 : 1;
}

int main()
{
    try {
        int code = runAll();
        return code;
    }
    catch (const std::exception& e) {
        std::cerr << "uncaught exception: " << e.what() << "\n";
        return 1;
    }
    catch (...) {
        std::cerr << "uncaught unknown exception\n";
        return 1;
    }
}
