// test_version.cpp - unit tests for version management module
// Tests: Draft/Stage/Commit lifecycle, VersionManager CRUD, diff, checkout
#include "../src/cursor_types.hpp"
#include "../src/version_manager.hpp"
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
using gm::Edge;
using gm::Graph;
using gm::Node;
using gv::Commit;
using gv::Draft;
using gv::FieldChange;
using gv::GraphVersionManager;
using gv::Operation;
using gv::OpType;
using gv::Selector;
using gv::Stage;
using gv::VersionMeta;

// cleanup helper
static void rmtree(const std::string& dir)
{
#ifdef _WIN32
    std::system(("rmdir /s /q \"" + dir + "\" >nul 2>nul").c_str());
#else
    std::system(("rm -rf \"" + dir + "\"").c_str());
#endif
}

// ─── Test 1: Operation serialization round-trip ──────────────
static void testOperationJson()
{
    Operation op;
    op.type       = OpType::NODE_UPDATE;
    op.targetId   = "A";
    op.targetType = "node";
    op.changes.push_back({"label", "old", "new"});
    op.changes.push_back({"shape", "rect", "diamond"});
    op.timestamp = "2026-07-08T10:00:00";

    Json      j   = op.toJson();
    Operation op2 = Operation::fromJson(j);

    CHECK(op2.type == OpType::NODE_UPDATE);
    CHECK(op2.targetId == "A");
    CHECK(op2.changes.size() == 2);
    CHECK(op2.changes[0].field == "label");
    CHECK(op2.changes[1].newValue == "diamond");
}

// ─── Test 2: Draft serialization round-trip ──────────────────
static void testDraftJson()
{
    Draft d;
    d.graphId     = "gtest";
    d.baseVersion = 1;
    d.updatedAt   = "2026-07-08T10:00:00";

    Operation op;
    op.type       = OpType::NODE_UPDATE;
    op.targetId   = "A";
    op.targetType = "node";
    op.changes.push_back({"label", "Start", "Begin"});
    op.timestamp = "2026-07-08T10:00:01";
    d.operations.push_back(op);

    Json  j  = d.toJson();
    Draft d2 = Draft::fromJson(j);

    CHECK(d2.graphId == "gtest");
    CHECK(d2.baseVersion == 1);
    CHECK(d2.operationCount() == 1);
    CHECK(d2.operations[0].targetId == "A");
}

// ─── Test 3: Stage serialization round-trip ──────────────────
static void testStageJson()
{
    Stage s;
    s.graphId         = "gtest";
    s.stagedOpIndices = {0, 2, 5};
    s.message         = "test commit";
    s.stagedAt        = "2026-07-08T10:00:00";

    Json  j  = s.toJson();
    Stage s2 = Stage::fromJson(j);

    CHECK(s2.graphId == "gtest");
    CHECK(s2.stagedOpIndices.size() == 3);
    CHECK(s2.stagedOpIndices[1] == 2);
    CHECK(s2.message == "test commit");
}

// ─── Test 4: Commit rebuild (apply patch) ────────────────────
static void testCommitRebuild()
{
    Graph base;
    base.id = "gtest";
    base.ensureNode("A", "Start");
    base.ensureNode("B", "Middle");
    base.addEdge("A", "B");

    std::vector<Operation> patch;

    // NODE_UPDATE: rename A
    {
        Operation op;
        op.type       = OpType::NODE_UPDATE;
        op.targetId   = "A";
        op.targetType = "node";
        op.changes.push_back({"label", "Start", "Begin"});
        patch.push_back(op);
    }

    // NODE_INSERT: add C
    {
        Operation op;
        op.type       = OpType::NODE_INSERT;
        op.targetId   = "C";
        op.targetType = "node";
        Json snap     = Json::obj();
        snap.set("id", "C");
        snap.set("label", "End");
        snap.set("shape", "round");
        snap.set("x", 100.0);
        snap.set("y", 200.0);
        snap.set("w", 120.0);
        snap.set("h", 44.0);
        op.snapshot = snap;
        patch.push_back(op);
    }

    // EDGE_DELETE: remove A->B
    {
        Operation op;
        op.type       = OpType::EDGE_DELETE;
        op.targetId   = base.edges[0].id;
        op.targetType = "edge";
        patch.push_back(op);
    }

    // EDGE_INSERT: add A->C
    {
        Operation op;
        op.type       = OpType::EDGE_INSERT;
        op.targetId   = "e_new";
        op.targetType = "edge";
        Json snap     = Json::obj();
        snap.set("id", "e_new");
        snap.set("from", "A");
        snap.set("to", "C");
        snap.set("label", "goes to");
        snap.set("style", "solid");
        snap.set("arrow", "arrow");
        op.snapshot = snap;
        patch.push_back(op);
    }

    Graph result = Commit::rebuild(base, patch);

    CHECK(result.findNode("A")->label == "Begin");
    const gm::Node* node_c = result.findNode("C");
    CHECK(node_c != nullptr && node_c->shape == "round");
    CHECK(result.edges.size() == 1);
    CHECK(result.edges[0].from == "A");
    CHECK(result.edges[0].to == "C");
    CHECK(result.edges[0].label == "goes to");
}

// ─── Test 5: VersionManager full lifecycle ───────────────────
static void testVersionManagerLifecycle()
{
    std::string storeDir = "test-vm-store";
    rmtree(storeDir);
    {
        GraphVersionManager vm(storeDir);

        // Create initial graph via store
        Graph g;
        g.id   = "gm1";
        g.name = "TestGraph";
        g.type = "flowchart";
        g.ensureNode("A", "Start");
        g.ensureNode("B", "End");
        g.addEdge("A", "B");
        int v1 = vm.store().save(g, "initial");
        CHECK(v1 == 1);

        // loadDraft → should be empty
        Draft draft = vm.loadDraft("gm1");
        CHECK(draft.isEmpty());
        CHECK(draft.baseVersion == 1);

        // status
        auto st = vm.status("gm1");
        CHECK(st.headVersion == 1);
        CHECK(!st.dirty);

        // materialize draft → should be same as base
        Graph mat = vm.materializeDraft("gm1");
        CHECK(mat.nodes.size() == 2);

        // Add operations to draft
        Operation op;
        op.type       = OpType::NODE_UPDATE;
        op.targetId   = "A";
        op.targetType = "node";
        op.changes.push_back({"label", "Start", "Beginning"});
        op.timestamp = gv::nowIso();
        draft.operations.push_back(op);
        draft.updatedAt = gv::nowIso();
        vm.saveDraft("gm1", draft);

        // status → dirty
        st = vm.status("gm1");
        CHECK(st.dirty);
        CHECK(st.draftOpCount == 1);

        // materialize → should reflect changes
        Graph mat2 = vm.materializeDraft("gm1");
        CHECK(mat2.findNode("A")->label == "Beginning");

        // stageAll → commit
        vm.stageAll("gm1");
        int v2 = vm.commit("gm1", "rename node A");
        CHECK(v2 == 2);

        // draft should be empty after commit
        draft = vm.loadDraft("gm1");
        CHECK(draft.isEmpty());

        // history
        auto hist = vm.history("gm1");
        CHECK(hist.size() == 2);
        CHECK(hist[0].version == 1);
        CHECK(hist[1].version == 2);

        // diff
        auto ops = vm.diff("gm1", 1, 2);
        CHECK(!ops.empty());

        // commitAll (skip stage)
        Operation op2;
        op2.type       = OpType::NODE_INSERT;
        op2.targetId   = "C";
        op2.targetType = "node";
        Json snap      = Json::obj();
        snap.set("id", "C");
        snap.set("label", "Extra");
        snap.set("shape", "rect");
        snap.set("x", 0.0);
        snap.set("y", 0.0);
        snap.set("w", 100.0);
        snap.set("h", 44.0);
        op2.snapshot  = snap;
        op2.timestamp = gv::nowIso();
        draft         = vm.loadDraft("gm1");
        draft.operations.push_back(op2);
        vm.saveDraft("gm1", draft);

        int v3 = vm.commitAll("gm1", "add node C");
        CHECK(v3 == 3);

        // checkout
        bool ok = vm.checkout("gm1", 1);
        CHECK(ok);
        st = vm.status("gm1");
        CHECK(st.headVersion == 1);

        // checkout with dirty draft should fail
        draft = vm.loadDraft("gm1");
        draft.operations.push_back(op);
        vm.saveDraft("gm1", draft);
        ok = vm.checkout("gm1", 2);
        CHECK(!ok);  // should fail

        // force checkout
        ok = vm.checkout("gm1", 2, true);
        CHECK(ok);
    }
    rmtree(storeDir);
}

// ─── Test 6: resetDraft ──────────────────────────────────────
static void testResetDraft()
{
    std::string storeDir = "test-reset-store";
    rmtree(storeDir);
    {
        GraphVersionManager vm(storeDir);

        Graph g;
        g.id = "gr1";
        g.ensureNode("X", "NodeX");
        vm.store().save(g, "first");

        // Add to draft
        Draft     draft = vm.loadDraft("gr1");
        Operation op;
        op.type       = OpType::NODE_UPDATE;
        op.targetId   = "X";
        op.targetType = "node";
        op.changes.push_back({"label", "NodeX", "Changed"});
        op.timestamp = gv::nowIso();
        draft.operations.push_back(op);
        vm.saveDraft("gr1", draft);

        CHECK(vm.status("gr1").dirty);

        // Reset
        vm.resetDraft("gr1");
        CHECK(!vm.status("gr1").dirty);
        CHECK(vm.loadDraft("gr1").isEmpty());
    }
    rmtree(storeDir);
}

// ─── Test 7: Selector parsing ────────────────────────────────
static void testSelectorParse()
{
    auto s1 = Selector::parse("shape=rect");
    CHECK(s1.kind == Selector::Kind::BY_TYPE);
    CHECK(s1.value == "rect");

    auto s2 = Selector::parse("label~=Step");
    CHECK(s2.kind == Selector::Kind::BY_LABEL);
    CHECK(s2.value == "Step");
    CHECK(s2.regex == true);

    auto s3 = Selector::parse("parent=g1");
    CHECK(s3.kind == Selector::Kind::BY_PARENT);
    CHECK(s3.value == "g1");

    auto s4 = Selector::parse("unrecognized");
    CHECK(s4.kind == Selector::Kind::BY_ID);
    CHECK(s4.value == "unrecognized");
}

// ─── Test 8: FieldChange get/set on Node ─────────────────────
static void testFieldAccess()
{
    Node n;
    n.id    = "test";
    n.label = "Hello";
    n.shape = "rect";
    n.x     = 10;
    n.y     = 20;
    n.w     = 100;
    n.h     = 50;

    CHECK(gv::getNodeField(n, "label") == "Hello");
    CHECK(gv::getNodeField(n, "shape") == "rect");

    gv::setNodeField(n, "label", "World");
    CHECK(n.label == "World");

    gv::setNodeField(n, "x", "99");
    CHECK(n.x == 99.0);
}

// ─── Test 9: Edge field access ───────────────────────────────
static void testEdgeFieldAccess()
{
    Edge e;
    e.id    = "e1";
    e.from  = "A";
    e.to    = "B";
    e.label = "connects";
    e.style = "dashed";
    e.arrow = "none";

    CHECK(gv::getEdgeField(e, "style") == "dashed");
    CHECK(gv::getEdgeField(e, "arrow") == "none");

    gv::setEdgeField(e, "style", "solid");
    CHECK(e.style == "solid");
}

int main()
{
    testOperationJson();
    testDraftJson();
    testStageJson();
    testCommitRebuild();
    testVersionManagerLifecycle();
    testResetDraft();
    testSelectorParse();
    testFieldAccess();
    testEdgeFieldAccess();

    std::cout << "version tests: " << g_passed << " passed, " << g_failed
              << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
