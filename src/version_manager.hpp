// version_manager.hpp - 图版本管理器
// Draft → Stage → Commit 三层模型的核心引擎。
// 基于现有 gs::Store 构建，在其上增加草稿/暂存/签出能力。
#pragma once
#include "storage.hpp"
#include "version_types.hpp"
#include <cstdio>
#include <set>
#include <tuple>

namespace gv {

using gj::Json;
using gm::Graph;

// isValidId 已统一到 gv::isValidId (version_types.hpp)

// ─── 简单的字符串哈希（用于 commitId）─────────────────────────────
inline std::string shortHash(const std::string& input)
{
    // FNV-1a 32-bit
    unsigned hash = 2166136261u;
    for (unsigned char c : input) {
        hash ^= c;
        hash *= 16777619u;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%08x", hash);
    return buf;
}

// ==================================================================
// GraphVersionManager: 图版本生命周期管理器
// ==================================================================
class GraphVersionManager {
  public:
    explicit GraphVersionManager(const std::string& storeRoot)
        : store_(storeRoot)
    {
    }

    // 获取底层 Store（供 CLI 直接操作）
    gs::Store& store()
    { return store_; }
    const std::string& root() const
    { return store_.root(); }

    // ==============================================================
    // Draft 操作
    // ==============================================================

    // loadDraft: 加载草稿；不存在则创建空草稿（基于 latest 版本）
    // 若存在未完成的 inflight commit，先做崩溃恢复，避免重复回放已入库 ops。
    Draft loadDraft(const std::string& graphId)
    {
        std::string path = draftPath(graphId);
        std::string txt  = ge::readFile(path);
        if (!txt.empty()) {
            std::string err;
            Json        j = Json::parse(txt, &err);
            if (err.empty()) {
                Draft d = Draft::fromJson(j);
                healInflightCommit(d);
                return d;
            }
        }
        // 新建空草稿
        Draft d;
        d.graphId     = graphId;
        d.baseVersion = readHead(graphId);
        d.updatedAt   = nowIso();
        return d;
    }

    // saveDraft: 保存草稿到磁盘
    bool saveDraft(const std::string& graphId, const Draft& draft)
    {
        makeGraphDir(graphId);
        // 草稿热路径用紧凑 dump，减少编辑态 I/O
        return ge::writeFile(draftPath(graphId), draft.toJson().dump());
    }

    // resetDraft: 丢弃草稿（删除 draft.json）
    bool resetDraft(const std::string& graphId)
    {
        std::string path = draftPath(graphId);
        return std::remove(path.c_str()) == 0 || !ge::fileReadable(path);
    }

    // materializeDraft: 从 Draft 重建工作区 Graph（base + operations）
    Graph materializeDraft(const std::string& graphId)
    {
        Draft unused;
        return materializeDraftWithDraft(graphId, unused);
    }

    // materializeDraftWithDraft: 一次读盘同时返回 Graph 与 Draft，避免二次 loadDraft
    Graph materializeDraftWithDraft(const std::string& graphId, Draft& outDraft)
    {
        outDraft = loadDraft(graphId);
        if (outDraft.isEmpty()) {
            // 无修改，直接返回 HEAD 版本
            std::string err;
            Graph       g;
            store_.load(graphId, g,
                        outDraft.baseVersion > 0 ? outDraft.baseVersion : 0,
                        &err);
            return g;
        }
        Graph       base;
        std::string err;
        if (!store_.load(graphId, base,
                         outDraft.baseVersion > 0 ? outDraft.baseVersion : 0,
                         &err)) {
            // base 不存在，从空图开始
            base    = Graph();
            base.id = graphId;
        }
        return outDraft.materialize(base);
    }

    // ==============================================================
    // Stage 操作
    // ==============================================================

    Stage loadStage(const std::string& graphId)
    {
        std::string txt = ge::readFile(stagePath(graphId));
        if (!txt.empty()) {
            std::string err;
            Json        j = Json::parse(txt, &err);
            if (err.empty())
                return Stage::fromJson(j);
        }
        Stage s;
        s.graphId = graphId;
        return s;
    }

    bool saveStage(const std::string& graphId, const Stage& stage)
    {
        makeGraphDir(graphId);
        return ge::writeFile(stagePath(graphId), stage.toJson().dump());
    }

    // stageAll: 暂存 Draft 中全部操作
    Stage stageAll(const std::string& graphId)
    {
        Draft draft = loadDraft(graphId);
        Stage stage;
        stage.graphId = graphId;
        for (int i = 0; i < draft.operationCount(); i++)
            stage.stagedOpIndices.push_back(i);
        stage.stagedAt = nowIso();
        saveStage(graphId, stage);
        return stage;
    }

    // stageSelected: 暂存指定索引的操作
    Stage stageSelected(const std::string&      graphId,
                        const std::vector<int>& indices)
    {
        Draft draft = loadDraft(graphId);
        Stage stage;
        stage.graphId = graphId;
        for (int idx : indices) {
            if (idx >= 0 && idx < draft.operationCount())
                stage.stagedOpIndices.push_back(idx);
        }
        stage.stagedAt = nowIso();
        saveStage(graphId, stage);
        return stage;
    }

    // clearStage: 清空暂存区
    bool clearStage(const std::string& graphId)
    {
        std::string path = stagePath(graphId);
        return std::remove(path.c_str()) == 0 || !ge::fileReadable(path);
    }

    // ==============================================================
    // Commit 操作
    // ==============================================================

    // commit: 将暂存区提交为新版本
    int commit(const std::string& graphId,
               const std::string& message,
               const std::string& author = "cli")
    {
        (void)author;  // 预留：提交者信息，暂未写入存储
        Draft draft = loadDraft(graphId);
        Stage stage = loadStage(graphId);

        if (stage.isEmpty())
            return -1;  // 无事可提交

        // 获取 parent 版本
        int         parentVersion = draft.baseVersion;
        Graph       base;
        std::string err;
        if (!store_.load(graphId, base, parentVersion > 0 ? parentVersion : 0,
                         &err))
            return -2;  // 父版本损坏，中止提交避免数据丢失

        // 从 stage 索引构建此版本的 patch
        std::vector<Operation> patch;
        for (int idx : stage.stagedOpIndices) {
            if (idx >= 0 && idx < draft.operationCount())
                patch.push_back(draft.operations[idx]);
        }

        // 重建完整模型
        Graph committedModel = Commit::rebuild(base, patch);
        committedModel.id    = graphId;

        // 写入存储，附带 parent 与 commitId
        committedModel.name = base.name;
        std::string cid =
            shortHash(graphId + ":" + std::to_string(parentVersion) + ":" +
                      message + ":" + nowIso());
        // 先持久化 inflight 意图：若随后 save 成功而裁剪草稿失败/崩溃，
        // 下次 loadDraft 可按 commitId 识别已入库并裁剪，避免重复回放。
        draft.inflightCommitId      = cid;
        draft.inflightStagedIndices = stage.stagedOpIndices;
        draft.updatedAt             = nowIso();
        if (!saveDraft(graphId, draft))
            return -3;

        int newVersion =
            store_.save(committedModel, message, parentVersion, cid);
        if (newVersion < 0) {
            draft.inflightCommitId.clear();
            draft.inflightStagedIndices.clear();
            saveDraft(graphId, draft);
            return -3;  // 存储锁或 IO 失败；保留 draft/stage 供重试
        }

        // save 已原子更新快照/latest/HEAD/index，此处裁剪已提交操作。
        // 若裁剪写盘失败：磁盘仍保留带 inflightCommitId 的旧 draft，heal 可恢复。
        std::set<int>          staged(stage.stagedOpIndices.begin(),
                                      stage.stagedOpIndices.end());
        std::vector<Operation> remaining;
        for (int i = 0; i < draft.operationCount(); i++) {
            if (!staged.count(i))
                remaining.push_back(draft.operations[i]);
        }
        draft.operations            = remaining;
        draft.baseVersion           = newVersion;
        draft.updatedAt             = nowIso();
        draft.inflightCommitId.clear();
        draft.inflightStagedIndices.clear();
        if (!saveDraft(graphId, draft))
            return -4;

        // 清空暂存区
        clearStage(graphId);

        return newVersion;
    }

    // commitAll: 跳过暂存，直接提交全部 Draft
    int commitAll(const std::string& graphId,
                  const std::string& message,
                  const std::string& author = "cli")
    {
        Draft draft = loadDraft(graphId);
        if (draft.isEmpty())
            return -1;

        // 自动暂存全部
        stageAll(graphId);
        return commit(graphId, message, author);
    }

    // ==============================================================
    // 版本查询
    // ==============================================================

    // loadVersion: 加载指定版本
    Graph loadVersion(const std::string& graphId, int version)
    {
        Graph       g;
        std::string err;
        store_.load(graphId, g, version, &err);
        return g;
    }

    // loadLatest: 加载最新版本
    Graph loadLatest(const std::string& graphId)
    {
        Graph       g;
        std::string err;
        store_.load(graphId, g, 0, &err);
        return g;
    }

    // history: 版本历史
    std::vector<VersionMeta> history(const std::string& graphId)
    {
        std::vector<VersionMeta> result;
        Json                     h = store_.history(graphId);
        for (auto& e : *h.a) {
            VersionMeta m;
            m.version   = (int)e.num("version");
            m.nodeCount = (int)e.num("nodes");
            m.edgeCount = (int)e.num("edges");
            m.timestamp = e.str("savedAt");
            m.message   = e.str("note");
            result.push_back(m);
        }
        return result;
    }

    // show: 查看版本详情
    Commit show(const std::string& graphId, int version)
    {
        Commit c;
        c.version = version;

        Graph       g;
        std::string err;
        if (!store_.load(graphId, g, version, &err))
            return c;

        c.modelSnapshot = g.toJson();
        c.timestamp     = "";

        // 读取 versions/vN.json 获取 commit 元数据
        std::string vpath = versionPath(graphId, version);
        std::string txt   = ge::readFile(vpath);
        if (!txt.empty()) {
            std::string perr;
            Json        j = Json::parse(txt, &perr);
            if (perr.empty()) {
                c = Commit::fromJson(j);
            }
        }
        return c;
    }

    // diff: 对比两个版本，返回 v1→v2 的操作序列
    std::vector<Operation> diff(const std::string& graphId, int v1, int v2)
    {
        Graph       g1, g2;
        std::string err;
        if (!store_.load(graphId, g1, v1, &err))
            return {};
        if (!store_.load(graphId, g2, v2, &err))
            return {};

        std::vector<Operation> ops;

        // 构建节点 id 集合
        std::set<std::string> ids1, ids2;
        for (auto& n : g1.nodes)
            ids1.insert(n.id);
        for (auto& n : g2.nodes)
            ids2.insert(n.id);

        // 删除的节点（在 v1 不在 v2）
        for (auto& nid : ids1) {
            if (!ids2.count(nid)) {
                Operation op;
                op.type       = OpType::NODE_DELETE;
                op.targetId   = nid;
                op.targetType = "node";
                ops.push_back(op);
            }
        }
        // 新增的节点（在 v2 不在 v1）
        for (auto& nid : ids2) {
            if (!ids1.count(nid)) {
                Operation op;
                op.type       = OpType::NODE_INSERT;
                op.targetId   = nid;
                op.targetType = "node";
                op.snapshot   = nodeToSnapshot(*g2.findNode(nid));
                ops.push_back(op);
            }
        }
        // 修改的节点
        for (auto& nid : ids1) {
            if (!ids2.count(nid))
                continue;
            const Node* n1 = g1.findNode(nid);
            const Node* n2 = g2.findNode(nid);
            if (!n1 || !n2)
                continue;
            std::vector<FieldChange> changes;
            if (n1->label != n2->label)
                changes.push_back({"label", n1->label, n2->label});
            if (n1->shape != n2->shape)
                changes.push_back({"shape", n1->shape, n2->shape});
            if (n1->style != n2->style)
                changes.push_back({"style", n1->style, n2->style});
            if (n1->parent != n2->parent)
                changes.push_back({"parent", n1->parent, n2->parent});
            if (std::abs(n1->x - n2->x) > 0.01)
                changes.push_back(
                    {"x", std::to_string(n1->x), std::to_string(n2->x)});
            if (std::abs(n1->y - n2->y) > 0.01)
                changes.push_back(
                    {"y", std::to_string(n1->y), std::to_string(n2->y)});
            if (!changes.empty()) {
                Operation op;
                op.type       = OpType::NODE_UPDATE;
                op.targetId   = nid;
                op.targetType = "node";
                op.changes    = changes;
                ops.push_back(op);
            }
        }

        // 边级别 diff
        std::set<std::string> eids1, eids2;
        for (auto& e : g1.edges)
            eids1.insert(e.id);
        for (auto& e : g2.edges)
            eids2.insert(e.id);

        for (auto& eid : eids1)
            if (!eids2.count(eid)) {
                Operation op;
                op.type       = OpType::EDGE_DELETE;
                op.targetId   = eid;
                op.targetType = "edge";
                ops.push_back(op);
            }
        for (auto& eid : eids2) {
            if (!eids1.count(eid)) {
                const Edge* e2 = nullptr;
                for (auto& e : g2.edges)
                    if (e.id == eid) {
                        e2 = &e;
                        break;
                    }
                Operation op;
                op.type       = OpType::EDGE_INSERT;
                op.targetId   = eid;
                op.targetType = "edge";
                if (e2)
                    op.snapshot = edgeToSnapshot(*e2);
                ops.push_back(op);
            }
        }
        for (auto& eid : eids1) {
            if (!eids2.count(eid))
                continue;
            const Edge *e1 = nullptr, *e2 = nullptr;
            for (auto& e : g1.edges)
                if (e.id == eid) {
                    e1 = &e;
                    break;
                }
            for (auto& e : g2.edges)
                if (e.id == eid) {
                    e2 = &e;
                    break;
                }
            if (!e1 || !e2)
                continue;
            std::vector<FieldChange> changes;
            if (e1->from != e2->from)
                changes.push_back({"from", e1->from, e2->from});
            if (e1->to != e2->to)
                changes.push_back({"to", e1->to, e2->to});
            if (e1->label != e2->label)
                changes.push_back({"label", e1->label, e2->label});
            if (e1->style != e2->style)
                changes.push_back({"style", e1->style, e2->style});
            if (e1->arrow != e2->arrow)
                changes.push_back({"arrow", e1->arrow, e2->arrow});
            if (!changes.empty()) {
                Operation op;
                op.type       = OpType::EDGE_UPDATE;
                op.targetId   = eid;
                op.targetType = "edge";
                op.changes    = changes;
                ops.push_back(op);
            }
        }
        return ops;
    }

    // ==============================================================
    // Checkout
    // ==============================================================

    bool checkout(const std::string& graphId, int version, bool force = false)
    {
        Draft draft = loadDraft(graphId);
        if (!draft.isEmpty() && !force)
            return false;  // 有未保存的修改

        // 丢弃草稿和暂存
        resetDraft(graphId);
        clearStage(graphId);

        // 移动 HEAD
        writeHead(graphId, version);
        return true;
    }

    // ==============================================================
    // 状态查询
    // ==============================================================

    struct Status
    {
        int         headVersion   = 0;
        int         draftOpCount  = 0;
        int         stagedOpCount = 0;
        bool        dirty         = false;
        std::string graphName;
        std::string graphType;
    };

    Status status(const std::string& graphId)
    {
        Status s;
        s.headVersion = readHead(graphId);

        Draft draft    = loadDraft(graphId);
        s.draftOpCount = draft.operationCount();
        s.dirty        = !draft.isEmpty();

        Stage stage     = loadStage(graphId);
        s.stagedOpCount = (int)stage.stagedOpIndices.size();

        // 加载图元信息
        Graph       g;
        std::string err;
        if (store_.load(graphId, g, 0, &err)) {
            s.graphName = g.name;
            s.graphType = g.type;
        }
        return s;
    }

    // ─── hasDraft / discardDraft: 草稿便捷操作 ──────────────────
    bool hasDraft(const std::string& graphId)
    { return !ge::readFile(draftPath(graphId)).empty(); }

    void discardDraft(const std::string& graphId)
    {
        std::remove(draftPath(graphId).c_str());
        std::remove(stagePath(graphId).c_str());
    }

    // ─── draftStatus: 草稿相对 latest 的增删改统计 ──────────────
    // 返回包含 nodes/edges 各 added/removed/modified 计数的 JSON
    Json draftStatus(const std::string& graphId)
    {
        Json out = Json::obj();
        if (!hasDraft(graphId)) {
            out.set("draft", false);
            out.set("clean", true);
            return out;
        }
        Graph       latest;
        std::string ign;
        store_.load(graphId, latest, 0, &ign);
        Graph draft = materializeDraft(graphId);

        auto countDiff = [](const auto& cur, const auto& old, auto eq) {
            int added = 0, removed = 0, modified = 0;
            for (auto& c : cur) {
                bool found = false;
                for (auto& x : old) {
                    if (x.id == c.id) {
                        found = true;
                        if (!eq(c, x))
                            modified++;
                        break;
                    }
                }
                if (!found)
                    added++;
            }
            for (auto& x : old) {
                bool present = false;
                for (auto& c : cur)
                    if (c.id == x.id) {
                        present = true;
                        break;
                    }
                if (!present)
                    removed++;
            }
            return std::make_tuple(added, removed, modified);
        };

        auto nodeEq = [](const gm::Node& a, const gm::Node& b) {
            return a.label == b.label && a.shape == b.shape &&
                   a.parent == b.parent && a.style == b.style &&
                   a.attrs == b.attrs;
        };
        auto edgeEq = [](const gm::Edge& a, const gm::Edge& b) {
            return a.from == b.from && a.to == b.to && a.label == b.label &&
                   a.style == b.style && a.arrow == b.arrow;
        };

        auto [na, nr, nm] = countDiff(draft.nodes, latest.nodes, nodeEq);
        auto [ea, er, em] = countDiff(draft.edges, latest.edges, edgeEq);

        auto block = [](int a, int r, int m) {
            Json j = Json::obj();
            j.set("added", (double)a);
            j.set("removed", (double)r);
            j.set("modified", (double)m);
            return j;
        };
        out.set("draft", true);
        out.set("clean", na + nr + nm + ea + er + em == 0);
        out.set("graphId", graphId);
        out.set("headVersion", (double)readHead(graphId));
        out.set("nodes", block(na, nr, nm));
        out.set("edges", block(ea, er, em));
        return out;
    }

  private:
    gs::Store store_;

    std::string graphDir(const std::string& id) const
    { return store_.root() + "/" + id; }
    std::string draftPath(const std::string& id) const
    { return graphDir(id) + "/draft.json"; }
    std::string stagePath(const std::string& id) const
    { return graphDir(id) + "/stage.json"; }
    std::string headPath(const std::string& id) const
    { return graphDir(id) + "/HEAD"; }
    std::string versionPath(const std::string& id, int version) const
    { return graphDir(id) + "/versions/v" + std::to_string(version) + ".json"; }

    void makeGraphDir(const std::string& id)
    {
        std::string dir = graphDir(id);
        gs::makeDir(dir);
        gs::makeDir(dir + "/versions");
    }

    int readHead(const std::string& id)
    {
        // 新格式优先从受锁保护的 index.head 读取。
        Json idx = store_.loadIndex();
        for (auto& item : *idx["graphs"].a) {
            if (item.str("id") == id) {
                int head = (int)item.num("head");
                if (head > 0)
                    return head;
                break;
            }
        }

        // 兼容旧存储：回退读取独立 HEAD 文件。
        std::string txt = ge::readFile(headPath(id));
        if (txt.empty()) {
            // 回退到 index.json 中的版本数
            for (auto& item : *idx["graphs"].a) {
                if (item.str("id") == id)
                    return (int)item.num("versions");
            }
            return 0;
        }
        return std::atoi(txt.c_str());
    }

    void writeHead(const std::string& id, int version)
    {
        makeGraphDir(id);
        std::string err;
        if (!store_.setHead(id, version, &err)) {
            // 兼容损坏/旧索引：至少保留独立 HEAD，供 readHead 回退。
            ge::writeFileAtomic(headPath(id), std::to_string(version));
        }
    }

    // snapshotCommitId: 读取版本快照中的 commitId（缺失则空串）
    std::string snapshotCommitId(const std::string& id, int version) const
    {
        std::string txt = ge::readFile(versionPath(id, version));
        if (txt.empty())
            return "";
        std::string err;
        Json        j = Json::parse(txt, &err);
        if (!err.empty())
            return "";
        return j.str("commitId");
    }

    // healInflightCommit: 若 inflight 对应快照已存在则裁剪草稿，防止重复回放
    void healInflightCommit(Draft& draft)
    {
        if (draft.inflightCommitId.empty() || draft.graphId.empty())
            return;

        const std::string& cid = draft.inflightCommitId;
        int                head = readHead(draft.graphId);
        int                foundVer = 0;
        int                start =
            draft.baseVersion > 0 ? draft.baseVersion + 1 : 1;
        for (int v = start; v <= head; ++v) {
            if (snapshotCommitId(draft.graphId, v) == cid) {
                foundVer = v;
                break;
            }
        }
        if (foundVer == 0) {
            // save 未成功落地：仅清除 inflight 标记，保留原 ops
            draft.inflightCommitId.clear();
            draft.inflightStagedIndices.clear();
            saveDraft(draft.graphId, draft);
            return;
        }

        std::set<int> staged(draft.inflightStagedIndices.begin(),
                             draft.inflightStagedIndices.end());
        // 若未记录索引（旧数据），保守地清空全部 ops，避免重放已入库变更
        std::vector<Operation> remaining;
        if (!staged.empty()) {
            for (int i = 0; i < draft.operationCount(); i++) {
                if (!staged.count(i))
                    remaining.push_back(draft.operations[i]);
            }
        }
        draft.operations = remaining;
        draft.baseVersion = foundVer;
        draft.updatedAt   = nowIso();
        draft.inflightCommitId.clear();
        draft.inflightStagedIndices.clear();
        saveDraft(draft.graphId, draft);
        clearStage(draft.graphId);
    }
};

// ==================================================================
// 游标磁盘持久化 (类比 PR #15 cursor.hpp)
// 游标状态写入 <storeRoot>/<id>/cursors/<cid>.json，
// 使游标位置在多次 CLI 调用间可复用。
// 放在此处而非 cursor_types.hpp 是为了避免与 GraphVersionManager
// 产生循环 include 依赖。
// ==================================================================

namespace detail {

    inline std::string cursorStatePath(const std::string& storeRoot,
                                       const std::string& graphId,
                                       const std::string& cursorId)
    { return storeRoot + "/" + graphId + "/cursors/" + cursorId + ".json"; }

    inline Json nodeToCursorJson(const gm::Node& n)
    {
        Json j = Json::obj();
        j.set("id", n.id);
        j.set("label", n.label);
        j.set("shape", n.shape);
        if (!n.parent.empty())
            j.set("parent", n.parent);
        if (!n.style.empty())
            j.set("style", n.style);
        return j;
    }

    inline Json edgeToCursorJson(const gm::Edge& e)
    {
        Json j = Json::obj();
        j.set("id", e.id);
        j.set("from", e.from);
        j.set("to", e.to);
        j.set("label", e.label);
        j.set("style", e.style);
        j.set("arrow", e.arrow);
        return j;
    }

}  // namespace detail

// ─── cursorOpen ──────────────────────────────────────────────────
inline Json openCursor(gs::Store&         store,
                       const std::string& graphId,
                       const std::string& targetIn)
{
    if (!isValidId(graphId)) {
        Json e = Json::obj();
        e.set("error", "invalid graph id");
        return e;
    }
    std::string target = targetIn.empty() ? "nodes" : targetIn;
    if (target != "nodes" && target != "edges") {
        Json e = Json::obj();
        e.set("error", "target must be 'nodes' or 'edges'");
        return e;
    }
    Graph       g;
    std::string err;
    if (!store.load(graphId, g, 0, &err)) {
        Json e = Json::obj();
        e.set("error", "graph not found: " + graphId);
        return e;
    }
    GraphVersionManager vm(store.root());
    // 不主动创建空草稿——游标仅读取,不应产生写入副作用
    (void)vm.loadDraft(graphId);

    std::string cid = gm::genId("cur");
    int count = target == "nodes" ? (int)g.nodes.size() : (int)g.edges.size();

    Json st = Json::obj();
    st.set("graphId", graphId);
    st.set("target", target);
    st.set("index", 0);
    std::string dir = store.root() + "/" + graphId + "/cursors";
    gs::makeDir(dir);
    ge::writeFile(detail::cursorStatePath(store.root(), graphId, cid),
                  st.dump(2));

    Json out = Json::obj();
    out.set("cursor", cid);
    out.set("graphId", graphId);
    out.set("target", target);
    out.set("index", 0);
    out.set("count", (double)count);
    if (target == "nodes" && !g.nodes.empty())
        out.set("item", detail::nodeToCursorJson(g.nodes[0]));
    else if (target == "edges" && !g.edges.empty())
        out.set("item", detail::edgeToCursorJson(g.edges[0]));
    return out;
}

// ─── cursorGet ───────────────────────────────────────────────────
inline Json getCursor(gs::Store&         store,
                      const std::string& graphId,
                      const std::string& cursorId)
{
    if (!isValidId(graphId) || !isValidId(cursorId)) {
        Json e = Json::obj();
        e.set("error", "invalid graph id or cursor id");
        return e;
    }
    std::string txt =
        ge::readFile(detail::cursorStatePath(store.root(), graphId, cursorId));
    if (txt.empty()) {
        Json e = Json::obj();
        e.set("error", "cursor not found: " + cursorId);
        return e;
    }
    std::string perr;
    Json        st = Json::parse(txt, &perr);
    if (!perr.empty()) {
        Json e = Json::obj();
        e.set("error", "corrupt cursor state: " + perr);
        return e;
    }
    std::string target = st.str("target", "nodes");
    int         idx    = (int)st.num("index");

    GraphVersionManager vm(store.root());
    Graph               g = vm.materializeDraft(graphId);
    if (g.id.empty()) {
        std::string ign;
        store.load(graphId, g, 0, &ign);
    }

    int  count = target == "nodes" ? (int)g.nodes.size() : (int)g.edges.size();
    Json out   = Json::obj();
    out.set("target", target);
    out.set("index", (double)idx);
    out.set("count", (double)count);
    if (idx < 0 || idx >= count) {
        out.set("atEnd", true);
    }
    else if (target == "nodes") {
        out.set("item", detail::nodeToCursorJson(g.nodes[(size_t)idx]));
    }
    else {
        out.set("item", detail::edgeToCursorJson(g.edges[(size_t)idx]));
    }
    return out;
}

// ─── cursorMove ──────────────────────────────────────────────────
inline Json moveCursor(gs::Store&         store,
                       const std::string& graphId,
                       const std::string& cursorId,
                       int                delta)
{
    if (!isValidId(graphId) || !isValidId(cursorId)) {
        Json e = Json::obj();
        e.set("error", "invalid graph id or cursor id");
        return e;
    }
    std::string txt =
        ge::readFile(detail::cursorStatePath(store.root(), graphId, cursorId));
    if (txt.empty()) {
        Json e = Json::obj();
        e.set("error", "cursor not found: " + cursorId);
        return e;
    }
    std::string perr;
    Json        st = Json::parse(txt, &perr);
    if (!perr.empty()) {
        Json e = Json::obj();
        e.set("error", "corrupt cursor state: " + perr);
        return e;
    }
    std::string target = st.str("target", "nodes");
    int         idx    = (int)st.num("index");

    GraphVersionManager vm(store.root());
    Graph               g = vm.materializeDraft(graphId);
    if (g.id.empty()) {
        std::string ign;
        store.load(graphId, g, 0, &ign);
    }

    int count  = target == "nodes" ? (int)g.nodes.size() : (int)g.edges.size();
    int newIdx = idx + delta;
    if (newIdx < 0)
        newIdx = 0;
    if (newIdx > count)
        newIdx = count;

    st.set("index", (double)newIdx);
    ge::writeFile(detail::cursorStatePath(store.root(), graphId, cursorId),
                  st.dump(2));

    Json out = Json::obj();
    out.set("target", target);
    out.set("index", (double)newIdx);
    out.set("count", (double)count);
    if (newIdx < 0 || newIdx >= count) {
        out.set("atEnd", true);
    }
    else if (target == "nodes") {
        out.set("item", detail::nodeToCursorJson(g.nodes[(size_t)newIdx]));
    }
    else {
        out.set("item", detail::edgeToCursorJson(g.edges[(size_t)newIdx]));
    }
    return out;
}

// ─── cursorClose ─────────────────────────────────────────────────
inline Json closeCursor(gs::Store&         store,
                        const std::string& graphId,
                        const std::string& cursorId)
{
    if (!isValidId(graphId) || !isValidId(cursorId)) {
        Json e = Json::obj();
        e.set("error", "invalid graph id or cursor id");
        return e;
    }
    int rc = std::remove(
        detail::cursorStatePath(store.root(), graphId, cursorId).c_str());
    Json out = Json::obj();
    out.set("closed", rc == 0);
    out.set("cursor", cursorId);
    return out;
}

}  // namespace gv
