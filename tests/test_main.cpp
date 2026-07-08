// test_main.cpp - assertion-based unit tests for graphmcp core modules
#include "../src/exporters.hpp"
#include "../src/mcp.hpp"
#include "../src/parsers.hpp"
#include "../src/storage.hpp"
#include <cstdlib>
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
    CHECK(resp.find("result")->find("tools")->size() == 8);
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

int runAll()
{
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
    testMcpExtended();
    std::cout << "tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return g_failed == 0 ? 0 : 1;
}

int main()
{ return runAll(); }
