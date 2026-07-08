// cursor.hpp - 游标式逐项修改 + 草稿差异（类比数据库游标）
// 打开游标指向某张图的节点/边集合，next/get/update/insert/delete 逐项操作，
// 所有改动落在可变草稿（draft.json），显式 commit 才固化为正式版本。
// 语义层集中于此；底层文件存取由 gs::Store 提供。
#pragma once
#include "model.hpp"
#include "storage.hpp"
#include <array>

namespace gc {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

namespace detail {

// errJson: 统一的失败返回（带 error 字段，供 MCP/CLI 层识别）
inline Json errJson(const std::string& msg)
{
    Json j = Json::obj();
    j.set("error", msg);
    return j;
}

// nodeToJson: 节点的逻辑视图（不含坐标，坐标由布局决定，对改图无意义）
inline Json nodeToJson(const Node& n)
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

// edgeToJson: 边的逻辑视图
inline Json edgeToJson(const Edge& e)
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

// nodeEq / edgeEq: 逻辑相等判断（忽略坐标），用于草稿差异统计
inline bool nodeEq(const Node& a, const Node& b)
{
    return a.label == b.label && a.shape == b.shape && a.parent == b.parent &&
           a.style == b.style && a.attrs == b.attrs;
}
inline bool edgeEq(const Edge& a, const Edge& b)
{
    return a.from == b.from && a.to == b.to && a.label == b.label &&
           a.style == b.style && a.arrow == b.arrow;
}

// targetSize: 目标集合的元素个数
inline int targetSize(const Graph& g, const std::string& target)
{
    if (target == "nodes")
        return (int)g.nodes.size();
    if (target == "edges")
        return (int)g.edges.size();
    return 0;
}

// currentItem: 返回 index 指向的项；越界返回 null
inline Json currentItem(const Graph& g, const std::string& target, int idx)
{
    if (idx < 0 || idx >= targetSize(g, target))
        return Json();
    return target == "nodes" ? nodeToJson(g.nodes[(size_t)idx]) :
                               edgeToJson(g.edges[(size_t)idx]);
}

// loadCtx: 载入游标上下文（草稿图 + 游标状态）
// 前置：草稿必须存在（commit/discard 后游标失效，须重新 open）
inline bool loadCtx(gs::Store&         s,
                    const std::string& id,
                    const std::string& cid,
                    Graph&             g,
                    std::string&       target,
                    int&               idx,
                    Json&              errOut)
{
    if (!s.hasDraft(id)) {
        errOut = errJson("no active draft for '" + id +
                         "'; run cursor open first");
        return false;
    }
    Json st;
    if (!s.loadCursorState(id, cid, st)) {
        errOut = errJson("cursor not found: " + cid);
        return false;
    }
    std::string lerr;
    if (!s.loadDraft(id, g, &lerr)) {
        errOut = errJson(lerr);
        return false;
    }
    target = st.str("target", "nodes");
    idx    = (int)st.num("index");
    return true;
}

}  // namespace detail

// cursorOpen: 打开游标，指向 nodes 或 edges，index 归零
// 若图尚无草稿，从 latest 派生一份并落盘；图不存在则报错
inline Json cursorOpen(gs::Store&         s,
                       const std::string& id,
                       const std::string& targetIn)
{
    std::string target = targetIn.empty() ? "nodes" : targetIn;
    if (target != "nodes" && target != "edges")
        return detail::errJson("target must be 'nodes' or 'edges'");

    Graph       g;
    std::string err;
    if (!s.loadDraft(id, g, &err))
        return detail::errJson(err);
    s.saveDraft(id, g);  // 确保 draft.json 落盘，后续游标操作可用

    std::string cid = gm::genId("cur");
    Json        st  = Json::obj();
    st.set("graphId", id);
    st.set("target", target);
    st.set("index", 0);
    s.writeCursor(id, cid, st);

    Json out = Json::obj();
    out.set("cursor", cid);
    out.set("graphId", id);
    out.set("target", target);
    out.set("index", 0);
    out.set("count", (double)detail::targetSize(g, target));
    out.set("item", detail::currentItem(g, target, 0));
    return out;
}

// cursorGet: 读取当前项
inline Json cursorGet(gs::Store& s, const std::string& id, const std::string& cid)
{
    Graph       g;
    std::string target;
    int         idx = 0;
    Json        e;
    if (!detail::loadCtx(s, id, cid, g, target, idx, e))
        return e;
    int  count = detail::targetSize(g, target);
    Json out   = Json::obj();
    out.set("target", target);
    out.set("index", (double)idx);
    out.set("count", (double)count);
    if (idx < 0 || idx >= count)
        out.set("atEnd", true);
    else
        out.set("item", detail::currentItem(g, target, idx));
    return out;
}

// cursorMove: 相对移动游标（next=+1 / prev=-1），越界钳制到边界
inline Json cursorMove(gs::Store&         s,
                       const std::string& id,
                       const std::string& cid,
                       int                delta)
{
    Graph       g;
    std::string target;
    int         idx = 0;
    Json        e;
    if (!detail::loadCtx(s, id, cid, g, target, idx, e))
        return e;
    int count  = detail::targetSize(g, target);
    int newIdx = idx + delta;
    if (newIdx < 0)
        newIdx = 0;
    if (newIdx > count)
        newIdx = count;  // count 表示尾后位置

    Json st = Json::obj();
    st.set("graphId", id);
    st.set("target", target);
    st.set("index", (double)newIdx);
    s.writeCursor(id, cid, st);

    Json out = Json::obj();
    out.set("target", target);
    out.set("index", (double)newIdx);
    out.set("count", (double)count);
    if (newIdx < 0 || newIdx >= count)
        out.set("atEnd", true);
    else
        out.set("item", detail::currentItem(g, target, newIdx));
    return out;
}

// cursorUpdate: 修改当前项字段并写回草稿
// 节点可改 label/shape/parent/style；边可改 label/style/arrow/from/to
inline Json cursorUpdate(gs::Store&         s,
                         const std::string& id,
                         const std::string& cid,
                         const Json&        fields)
{
    Graph       g;
    std::string target;
    int         idx = 0;
    Json        e;
    if (!detail::loadCtx(s, id, cid, g, target, idx, e))
        return e;
    int count = detail::targetSize(g, target);
    if (idx < 0 || idx >= count)
        return detail::errJson("cursor not on a valid item (index " +
                               std::to_string(idx) + ", count " +
                               std::to_string(count) + ")");

    auto pick = [&](const char* key, std::string& dst) {
        if (const Json* v = fields.find(key))
            if (v->isStr())
                dst = v->s;
    };
    if (target == "nodes") {
        Node& n = g.nodes[(size_t)idx];
        pick("label", n.label);
        pick("shape", n.shape);
        pick("parent", n.parent);
        pick("style", n.style);
    }
    else {
        Edge& ed = g.edges[(size_t)idx];
        pick("label", ed.label);
        pick("style", ed.style);
        pick("arrow", ed.arrow);
        pick("from", ed.from);
        pick("to", ed.to);
    }
    g.laidOut = false;  // 内容变更，坐标失效，导出时重新布局
    s.saveDraft(id, g);

    Json out = Json::obj();
    out.set("updated", true);
    out.set("target", target);
    out.set("index", (double)idx);
    out.set("item", detail::currentItem(g, target, idx));
    return out;
}

// cursorInsert: 在当前项之后插入一个新项，游标移到新项
// 节点缺 id 时自动生成；边必须提供 from 和 to
inline Json cursorInsert(gs::Store&         s,
                         const std::string& id,
                         const std::string& cid,
                         const Json&        fields)
{
    Graph       g;
    std::string target;
    int         idx = 0;
    Json        e;
    if (!detail::loadCtx(s, id, cid, g, target, idx, e))
        return e;
    int count = detail::targetSize(g, target);
    int pos   = idx + 1;  // 当前项之后
    if (pos < 0)
        pos = 0;
    if (pos > count)
        pos = count;

    if (target == "nodes") {
        Node n;
        n.id = fields.str("newId");  // 新节点 id（区别于图 id）
        if (n.id.empty())
            n.id = gm::genId("n");
        n.label = fields.str("label", n.id);
        n.shape = fields.str("shape", "rect");
        n.parent = fields.str("parent");
        n.style  = fields.str("style");
        g.nodes.insert(g.nodes.begin() + pos, n);
    }
    else {
        std::string from = fields.str("from");
        std::string to   = fields.str("to");
        if (from.empty() || to.empty())
            return detail::errJson("edge insert requires 'from' and 'to'");
        Edge ed;
        ed.id = fields.str("newId");  // 新边 id（区别于图 id）
        if (ed.id.empty())
            ed.id = gm::genId("e");
        ed.from  = from;
        ed.to    = to;
        ed.label = fields.str("label");
        ed.style = fields.str("style", "solid");
        ed.arrow = fields.str("arrow", "arrow");
        g.edges.insert(g.edges.begin() + pos, ed);
    }
    g.laidOut = false;
    s.saveDraft(id, g);

    Json st = Json::obj();
    st.set("graphId", id);
    st.set("target", target);
    st.set("index", (double)pos);
    s.writeCursor(id, cid, st);

    Json out = Json::obj();
    out.set("inserted", true);
    out.set("target", target);
    out.set("index", (double)pos);
    out.set("count", (double)detail::targetSize(g, target));
    out.set("item", detail::currentItem(g, target, pos));
    return out;
}

// cursorDelete: 删除当前项，游标停在后续项（或前一项）
inline Json cursorDelete(gs::Store&         s,
                         const std::string& id,
                         const std::string& cid)
{
    Graph       g;
    std::string target;
    int         idx = 0;
    Json        e;
    if (!detail::loadCtx(s, id, cid, g, target, idx, e))
        return e;
    int count = detail::targetSize(g, target);
    if (idx < 0 || idx >= count)
        return detail::errJson("cursor not on a valid item (index " +
                               std::to_string(idx) + ", count " +
                               std::to_string(count) + ")");

    if (target == "nodes")
        g.nodes.erase(g.nodes.begin() + idx);
    else
        g.edges.erase(g.edges.begin() + idx);
    g.laidOut = false;

    int newCount = count - 1;
    int newIdx   = idx;
    if (newIdx >= newCount)
        newIdx = newCount - 1;  // 删掉末项则回退一格
    if (newIdx < 0)
        newIdx = 0;
    s.saveDraft(id, g);

    Json st = Json::obj();
    st.set("graphId", id);
    st.set("target", target);
    st.set("index", (double)newIdx);
    s.writeCursor(id, cid, st);

    Json out = Json::obj();
    out.set("deleted", true);
    out.set("target", target);
    out.set("index", (double)newIdx);
    out.set("count", (double)newCount);
    if (newCount > 0)
        out.set("item", detail::currentItem(g, target, newIdx));
    return out;
}

// cursorClose: 释放游标状态文件（幂等）
inline Json cursorClose(gs::Store&         s,
                        const std::string& id,
                        const std::string& cid)
{
    s.removeCursor(id, cid);
    Json out = Json::obj();
    out.set("closed", true);
    out.set("cursor", cid);
    return out;
}

// draftStatus: 汇报草稿相对 latest 的增删改（git status 风格）
inline Json draftStatus(gs::Store& s, const std::string& id)
{
    Json out = Json::obj();
    if (!s.hasDraft(id)) {
        out.set("draft", false);
        out.set("clean", true);
        return out;
    }
    Graph       draft, base;
    std::string err;
    if (!s.loadDraft(id, draft, &err))
        return detail::errJson(err);
    s.load(id, base, 0);  // latest 作为对比基线（不存在则视为空图）

    auto countDiff = [](const auto& cur, const auto& old,
                        auto eq) -> std::array<int, 3> {
        int added = 0, removed = 0, modified = 0;
        for (auto& c : cur) {
            bool found = false;
            for (auto& x : old)
                if (x.id == c.id) {
                    found = true;
                    if (!eq(c, x))
                        modified++;
                    break;
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
        return {added, removed, modified};
    };

    auto nd = countDiff(draft.nodes, base.nodes, detail::nodeEq);
    auto ed = countDiff(draft.edges, base.edges, detail::edgeEq);

    auto block = [](const std::array<int, 3>& d) {
        Json j = Json::obj();
        j.set("added", (double)d[0]);
        j.set("removed", (double)d[1]);
        j.set("modified", (double)d[2]);
        return j;
    };
    out.set("draft", true);
    out.set("clean", nd[0] + nd[1] + nd[2] + ed[0] + ed[1] + ed[2] == 0);
    out.set("nodes", block(nd));
    out.set("edges", block(ed));
    return out;
}

}  // namespace gc
