// test_cursor.cpp - unit tests for Cursor operations
// Tests: NodeCursor, EdgeCursor, SelectionCursor, insert/delete helpers
#include "../src/cursor_types.hpp"
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

using gm::Edge;
using gm::Graph;
using gm::Node;
using gv::deleteEdge;
using gv::deleteNode;
using gv::Draft;
using gv::EdgeCursor;
using gv::insertEdge;
using gv::insertNode;
using gv::NodeCursor;
using gv::select;
using gv::selectEdge;
using gv::selectEdges;
using gv::SelectionCursor;
using gv::selectNode;
using gv::selectNodes;
using gv::Selector;

// setup helper
static Graph makeTestGraph()
{
    Graph g;
    g.id   = "gtest";
    g.name = "Test";
    g.type = "flowchart";
    // Create nodes
    g.ensureNode("A", "Start");
    g.findNode("A")->shape = "round";
    g.findNode("A")->x     = 40;
    g.findNode("A")->y     = 40;
    g.findNode("A")->w     = 100;
    g.findNode("A")->h     = 44;

    g.ensureNode("B", "Process");
    g.findNode("B")->shape = "rect";
    g.findNode("B")->x     = 200;
    g.findNode("B")->y     = 40;

    g.ensureNode("C", "End");
    g.findNode("C")->shape = "round";
    g.findNode("C")->x     = 360;
    g.findNode("C")->y     = 40;

    g.ensureNode("D", "Decision");
    g.findNode("D")->shape = "diamond";
    g.findNode("D")->x     = 200;
    g.findNode("D")->y     = 160;

    // Create edges
    g.addEdge("A", "B", "go");
    g.addEdge("B", "D", "check");
    g.addEdge("D", "C", "done");
    return g;
}

// ─── Test 1: NodeCursor exact match ──────────────────────────
static void testNodeCursorExact()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    auto nc = selectNode(g, &draft, "A");
    CHECK(nc.valid());
    CHECK(nc.get()->label == "Start");
    CHECK(nc.get()->shape == "round");
    CHECK(nc.nodeId() == "A");

    // Invalid id
    auto bad = selectNode(g, &draft, "nonexistent");
    CHECK(!bad.valid());
}

// ─── Test 2: NodeCursor chained update ───────────────────────
static void testNodeCursorUpdate()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    auto nc = selectNode(g, &draft, "B");
    nc.updateLabel("Processing")
        .updateShape("diamond")
        .updatePosition(300, 100);

    CHECK(g.findNode("B")->label == "Processing");
    CHECK(g.findNode("B")->shape == "diamond");
    CHECK(g.findNode("B")->x == 300.0);
    CHECK(g.findNode("B")->y == 100.0);

    // Draft should record the operations
    CHECK(draft.operationCount() == 1);  // Merged into single NODE_UPDATE
    CHECK(draft.operations[0].targetId == "B");
    CHECK(draft.operations[0].changes.size() == 4);  // label, shape, x, y
}

// ─── Test 3: NodeCursor selector matching ────────────────────
static void testNodeCursorSelector()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    // Select all round nodes — NodeCursor::updateShape 仅影响当前元素
    auto nc = selectNodes(g, &draft, Selector::byType("round"));
    CHECK(nc.count() == 2);  // A and C are round
    for (int i = 0; i < nc.count(); i++) {
        nc.at(i);
        nc.updateShape("rect");
    }
    CHECK(g.findNode("A")->shape == "rect");
    CHECK(g.findNode("C")->shape == "rect");

    // Select by label
    auto nc2 = selectNodes(g, &draft, Selector::byLabel("Process"));
    CHECK(nc2.count() == 1);
}

// ─── Test 4: EdgeCursor operations ───────────────────────────
static void testEdgeCursor()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    // Exact match
    std::string eid = g.edges[0].id;
    auto        ec  = selectEdge(g, &draft, eid);
    CHECK(ec.valid());
    CHECK(ec.get()->from == "A");
    CHECK(ec.get()->label == "go");

    // Update edge
    ec.updateLabel("proceed").updateStyle("dashed").updateArrow("none");

    CHECK(g.edges[0].label == "proceed");
    CHECK(g.edges[0].style == "dashed");
    CHECK(g.edges[0].arrow == "none");

    // Reconnect
    ec.reconnect("A", "D");
    CHECK(g.edges[0].from == "A");
    CHECK(g.edges[0].to == "D");

    // Select all edges
    auto allEdges = selectEdges(g, &draft, Selector::allEdges());
    CHECK(allEdges.count() == 3);
}

// ─── Test 5: SelectionCursor batch operations ────────────────
static void testSelectionCursor()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    // Select all nodes with parent="" (all root nodes in this test)
    auto sc = select(g, &draft, Selector::byParent(""));
    // All nodes have empty parent initially
    CHECK(sc.nodeCount() == 4);
    CHECK(sc.edgeCount() == 0);

    // Set parent on all nodes to "root"
    sc.setAll("parent", "root");
    CHECK(g.findNode("A")->parent == "root");
    CHECK(g.findNode("B")->parent == "root");
    CHECK(g.findNode("D")->parent == "root");
}

// ─── Test 6: NodeCursor navigation ───────────────────────────
static void testNodeCursorNavigation()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    auto nc = selectNodes(g, &draft, Selector::allNodes());
    CHECK(nc.count() == 4);

    nc.first();
    CHECK(nc.nodeId() == "A");

    nc.next();
    CHECK(nc.nodeId() == "B");

    nc.last();
    CHECK(nc.nodeId() == "D");

    nc.prev();
    CHECK(nc.nodeId() == "C");

    nc.at(0);
    CHECK(nc.nodeId() == "A");
}

// ─── Test 7: insert helpers ──────────────────────────────────
static void testInsertHelpers()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    // Insert node
    std::string nid =
        insertNode(g, &draft, "rect", "NewNode", 500, 200, 120, 44);
    const gm::Node* inserted = g.findNode(nid);
    CHECK(inserted != nullptr && inserted->label == "NewNode");

    // Insert edge
    std::string eid       = insertEdge(g, &draft, "A", nid, "to new");
    bool        foundEdge = false;
    for (auto& e : g.edges)
        if (e.id == eid && e.label == "to new")
            foundEdge = true;
    CHECK(foundEdge);

    // Draft records
    CHECK(draft.operationCount() == 2);
}

// ─── Test 8: delete helpers ──────────────────────────────────
static void testDeleteHelpers()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    int initialNodes = (int)g.nodes.size();
    int initialEdges = (int)g.edges.size();

    // Delete node A → should cascade delete edges connected to A
    bool ok = deleteNode(g, &draft, "A");
    CHECK(ok);
    CHECK(g.findNode("A") == nullptr);
    CHECK((int)g.nodes.size() == initialNodes - 1);
    // Edge A->B should be deleted too
    for (auto& e : g.edges)
        CHECK(e.from != "A");

    // Delete edge by id
    std::string eid = g.edges[0].id;
    bool        ok2 = deleteEdge(g, &draft, eid);
    CHECK(ok2);
    CHECK((int)g.edges.size() ==
          initialEdges - 2);  // one from cascade, one from direct

    // Delete non-existent
    CHECK(!deleteNode(g, &draft, "nonexistent"));
    CHECK(!deleteEdge(g, &draft, "nonexistent"));
}

// ─── Test 9: Draft-less cursor (nullptr) ─────────────────────
static void testCursorNoDraft()
{
    Graph g = makeTestGraph();

    // Should work without draft (just no recording)
    auto nc = selectNode(g, nullptr, "B");
    nc.updateLabel("NoDraft");
    CHECK(g.findNode("B")->label == "NoDraft");

    auto ec = selectEdge(g, nullptr, g.edges[1].id);
    ec.updateStyle("thick");
    CHECK(g.edges[1].style == "thick");
}

// ─── Test 10: Selector connectedTo ───────────────────────────
static void testSelectorConnectedTo()
{
    Graph g = makeTestGraph();

    // Nodes connected to A: only B
    auto nc = selectNodes(g, nullptr, Selector::connectedTo("A"));
    CHECK(nc.count() == 1);
    CHECK(nc.nodeId() == "B");

    // Nodes connected to D: B and C
    auto nc2 = selectNodes(g, nullptr, Selector::connectedTo("D"));
    CHECK(nc2.count() == 2);
}

// ─── Test 11: SelectionCursor deleteAll ──────────────────────
static void testSelectionCursorDeleteAll()
{
    Graph g = makeTestGraph();
    Draft draft;
    draft.graphId = "gtest";

    // Delete all diamond-shaped nodes
    auto sc           = select(g, &draft, Selector::byType("diamond"));
    int  diamondCount = sc.count();  // D is diamond
    CHECK(diamondCount == 1);

    sc.deleteAll();
    CHECK(g.findNode("D") == nullptr);
    // Edges connected to D should also be deleted
    for (auto& e : g.edges) {
        CHECK(e.from != "D");
        CHECK(e.to != "D");
    }
}

int main()
{
    testNodeCursorExact();
    testNodeCursorUpdate();
    testNodeCursorSelector();
    testEdgeCursor();
    testSelectionCursor();
    testNodeCursorNavigation();
    testInsertHelpers();
    testDeleteHelpers();
    testCursorNoDraft();
    testSelectorConnectedTo();
    testSelectionCursorDeleteAll();

    std::cout << "cursor tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
