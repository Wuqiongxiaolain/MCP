// 快速验证所有 Mermaid 图表类型支持情况
#include "../src/parsers.hpp"
#include "../src/exporters.hpp"
#include "../src/layout.hpp"
#include <iostream>
#include <iomanip>

struct TestCase {
    const char* name;
    const char* mermaid;
    bool        expectDeepParse;  // true=深度解析, false=rawMermaid透传
};

int main() {
    TestCase cases[] = {
        // 原有3种深度解析
        {"flowchart",        "flowchart TD\n  A[Start] --> B[End]", true},
        {"mindmap",          "mindmap\n  Root\n    Child1\n    Child2", true},
        {"erDiagram",        "erDiagram\n  CUSTOMER ||--o{ ORDER : places", true},
        // 阶段2新增深度解析
        {"classDiagram",     "classDiagram\n  class Animal {\n    +int age\n  }\n  Animal <|-- Dog", true},
        {"stateDiagram-v2",  "stateDiagram-v2\n  [*] --> Idle\n  Idle --> Running : start\n  Running --> [*]", true},
        // 透传模式
        {"sequenceDiagram",  "sequenceDiagram\n  Alice->>Bob: Hello\n  Bob-->>Alice: Hi", false},
        {"gantt",            "gantt\n  title Project\n  dateFormat YYYY-MM-DD\n  section Dev\n  Task : 5d", false},
        {"pie",              "pie title Pets\n  \"Dogs\" : 50\n  \"Cats\" : 30", false},
        {"gitGraph",         "gitGraph\n  commit\n  branch dev\n  checkout dev\n  commit", false},
        {"journey",          "journey\n  title My Day\n  section Morning\n  Wake up: 5: Me", false},
        {"timeline",         "timeline\n  title History\n  section Era\n  Event : detail", false},
        {"kanban",           "kanban\n  columns\n    column \"Todo\"\n    column \"Done\"", false},
        {"quadrantChart",    "quadrantChart\n  title Quad\n  x-axis Low --> High\n  y-axis Low --> High\n  A: [0.3, 0.5]", false},
        {"xychart-beta",     "xychart-beta\n  title Sales\n  x-axis [A, B]\n  y-axis 0 --> 100\n  bar [10, 20]", false},
        {"architecture-beta","architecture-beta\n  group api(cloud)[API]\n  service db(database)[DB]", false},
        {"packet-beta",      "packet-beta\n  title Frame\n  0-7: Header\n  8-15: Data", false},
    };

    int passed = 0, failed = 0;
    std::cout << std::left;
    std::cout << std::setw(20) << "Diagram Type"
              << std::setw(10) << "Parse"
              << std::setw(10) << "Export"
              << std::setw(12) << "Validate"
              << std::setw(10) << "Detect"
              << std::setw(12) << "LiveURL"
              << "\n";
    std::cout << std::string(74, '-') << "\n";

    for (auto& tc : cases) {
        std::cout << std::setw(20) << tc.name;
        bool allOk = true;

        // 1. 解析测试
        try {
            gm::Graph g = gp::parseMermaid(tc.mermaid);
            bool deepOk = (tc.expectDeepParse == g.rawMermaid.empty());
            std::cout << std::setw(10) << (deepOk ? "OK" : "FAIL");
            if (!deepOk) { allOk = false; }

            // 2. Mermaid 导出（无损往返）
            std::string out = ge::toMermaid(g);
            bool exportOk = !out.empty();
            std::cout << std::setw(10) << (exportOk ? "OK" : "FAIL");
            if (!exportOk) { allOk = false; }

            // 3. 验证（不应崩溃）
            auto issues = gl::validate(g);
            std::cout << std::setw(12) << "OK";

            // 4. 格式检测
            std::string fmt = gp::detectFormat(tc.mermaid);
            bool detectOk = (fmt == "mermaid");
            std::cout << std::setw(10) << (detectOk ? "OK" : "FAIL");
            if (!detectOk) { allOk = false; }

            // 5. Live URL 生成
            std::string url = ge::toMermaidLiveUrl(g);
            bool urlOk = (!url.empty() && url.find("mermaid.live") != std::string::npos);
            std::cout << std::setw(12) << (urlOk ? "OK" : "FAIL");
            if (!urlOk) { allOk = false; }

            std::cout << "\n";
        } catch (std::exception& e) {
            std::cout << std::setw(10) << "ERROR"
                      << std::setw(10) << "ERROR"
                      << std::setw(12) << "ERROR"
                      << std::setw(10) << "ERROR"
                      << std::setw(12) << "ERROR"
                      << "  " << e.what() << "\n";
            allOk = false;
        }

        if (allOk) passed++; else failed++;
    }

    std::cout << std::string(74, '-') << "\n";
    std::cout << "Result: " << passed << " passed, " << failed << " failed (total " << (passed+failed) << ")\n";
    return failed == 0 ? 0 : 1;
}
