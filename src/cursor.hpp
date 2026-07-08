// cursor.hpp - draft/cursor 语义封装（基于 storage.hpp 的薄存取能力）
#pragma once
#include "storage.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace gc {

using gj::Json;
using gm::Graph;

inline Json nodeToJson(const gm::Node& n)
{
    Json j = Json::obj();
    j.set("id", n.id);
    j.set("label", n.label);
    j.set("shape", n.shape);
    j.set("parent", n.parent);
    j.set("style", n.style);
    return j;
}

inline Json edgeToJson(const gm::Edge& e)
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

inline int targetSize(const Graph& g, const std::string& target)
{
    return target == "nodes" ? (int)g.nodes.size() : (int)g.edges.size();
}

inline Json currentItem(const Graph& g, const std::string& target, int index)
{
    Json out = Json::obj();
    out.set("target", target);
    out.set("index", index);
    int sz = targetSize(g, target);
    if (index < 0 || index >= sz) {
        out.set("atEnd", true);
        return out;
    }
    out.set("atEnd", false);
    out.set("item",
            target == "nodes" ? nodeToJson(g.nodes[(size_t)index]) :
                                edgeToJson(g.edges[(size_t)index]));
    return out;
}

inline void ensureTarget(const std::string& target)
{
    if (target != "nodes" && target != "edges")
        throw std::runtime_error("target must be 'nodes' or 'edges'");
}

inline void ensureDraft(gs::Store& store, const std::string& id)
{
    if (store.hasDraft(id))
        return;
    Graph       g;
    std::string err;
    if (!store.load(id, g, 0, &err))
        throw std::runtime_error(err);
    store.saveDraft(id, g);
}

inline Json cursorOpen(gs::Store&      store,
                       const std::string& id,
                       const std::string& target)
{
    ensureTarget(target);
    ensureDraft(store, id);
    Graph       g;
    std::string err;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    std::string cid = gm::genId("cur");
    Json        st  = Json::obj();
    st.set("graphId", id);
    st.set("target", target);
    st.set("index", 0);
    store.writeCursor(id, cid, st);
    Json out = currentItem(g, target, 0);
    out.set("cursor", cid);
    return out;
}

inline Json cursorGet(gs::Store& store, const std::string& id, const std::string& cid)
{
    Json        st;
    std::string err;
    if (!store.loadCursorState(id, cid, st, &err))
        throw std::runtime_error(err);
    Graph g;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    Json out = currentItem(g, st.str("target"), (int)st.num("index"));
    out.set("cursor", cid);
    return out;
}

inline Json cursorMove(gs::Store&      store,
                       const std::string& id,
                       const std::string& cid,
                       int                delta)
{
    Json        st;
    std::string err;
    if (!store.loadCursorState(id, cid, st, &err))
        throw std::runtime_error(err);
    Graph g;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    std::string target = st.str("target");
    int         index  = (int)st.num("index");
    int         next   = index + delta;
    int         sz     = targetSize(g, target);
    if (next < 0 || next >= sz) {
        Json out = Json::obj();
        out.set("cursor", cid);
        out.set("target", target);
        out.set("index", index);
        out.set("atEnd", true);
        return out;
    }
    st.set("index", next);
    store.writeCursor(id, cid, st);
    Json out = currentItem(g, target, next);
    out.set("cursor", cid);
    return out;
}

inline Json cursorUpdate(gs::Store& store,
                         const std::string& id,
                         const std::string& cid,
                         const Json&        fields)
{
    Json        st;
    std::string err;
    if (!store.loadCursorState(id, cid, st, &err))
        throw std::runtime_error(err);
    Graph g;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    std::string target = st.str("target");
    int         index  = (int)st.num("index");
    int         sz     = targetSize(g, target);
    if (index < 0 || index >= sz) {
        Json out = Json::obj();
        out.set("cursor", cid);
        out.set("atEnd", true);
        return out;
    }

    if (target == "nodes") {
        gm::Node& n = g.nodes[(size_t)index];
        if (fields.find("label"))
            n.label = fields.str("label");
        if (fields.find("shape"))
            n.shape = fields.str("shape");
        if (fields.find("parent"))
            n.parent = fields.str("parent");
        if (fields.find("style"))
            n.style = fields.str("style");
    }
    else {
        gm::Edge& e = g.edges[(size_t)index];
        if (fields.find("label"))
            e.label = fields.str("label");
        if (fields.find("style"))
            e.style = fields.str("style");
        if (fields.find("arrow"))
            e.arrow = fields.str("arrow");
        if (fields.find("from"))
            e.from = fields.str("from");
        if (fields.find("to"))
            e.to = fields.str("to");
    }
    store.saveDraft(id, g);
    Json out = currentItem(g, target, index);
    out.set("cursor", cid);
    out.set("updated", true);
    return out;
}

inline Json cursorInsert(gs::Store& store,
                         const std::string& id,
                         const std::string& cid,
                         const Json&        fields)
{
    Json        st;
    std::string err;
    if (!store.loadCursorState(id, cid, st, &err))
        throw std::runtime_error(err);
    Graph g;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    std::string target = st.str("target");
    int         index  = (int)st.num("index");
    int         sz     = targetSize(g, target);
    int         pos    = sz == 0 ? 0 : std::min(index + 1, sz);

    if (target == "nodes") {
        gm::Node n;
        n.id     = fields.str("itemId", fields.str("item_id"));
        if (n.id.empty())
            n.id = gm::genId("n");
        if (g.findNode(n.id))
            throw std::runtime_error("node id already exists: " + n.id);
        n.label  = fields.str("label", n.id);
        n.shape  = fields.str("shape", "rect");
        n.parent = fields.str("parent");
        n.style  = fields.str("style");
        g.nodes.insert(g.nodes.begin() + pos, n);
    }
    else {
        gm::Edge e;
        e.id = fields.str("itemId", fields.str("item_id"));
        if (e.id.empty())
            e.id = gm::genId("e");
        e.from = fields.str("from");
        e.to   = fields.str("to");
        if (e.from.empty() || e.to.empty())
            throw std::runtime_error("edge insert requires 'from' and 'to'");
        e.label = fields.str("label");
        e.style = fields.str("style", "solid");
        e.arrow = fields.str("arrow", "arrow");
        g.edges.insert(g.edges.begin() + pos, e);
    }

    st.set("index", pos);
    store.writeCursor(id, cid, st);
    store.saveDraft(id, g);
    Json out = currentItem(g, target, pos);
    out.set("cursor", cid);
    out.set("inserted", true);
    return out;
}

inline Json cursorDelete(gs::Store& store, const std::string& id, const std::string& cid)
{
    Json        st;
    std::string err;
    if (!store.loadCursorState(id, cid, st, &err))
        throw std::runtime_error(err);
    Graph g;
    if (!store.loadDraft(id, g, &err))
        throw std::runtime_error(err);
    std::string target = st.str("target");
    int         index  = (int)st.num("index");
    int         sz     = targetSize(g, target);
    if (index < 0 || index >= sz) {
        Json out = Json::obj();
        out.set("cursor", cid);
        out.set("atEnd", true);
        return out;
    }

    if (target == "nodes")
        g.nodes.erase(g.nodes.begin() + index);
    else
        g.edges.erase(g.edges.begin() + index);

    int after = targetSize(g, target);
    if (after == 0)
        index = 0;
    else if (index >= after)
        index = after - 1;
    st.set("index", index);
    store.writeCursor(id, cid, st);
    store.saveDraft(id, g);
    Json out = currentItem(g, target, index);
    out.set("cursor", cid);
    out.set("deleted", true);
    return out;
}

inline Json cursorClose(gs::Store& store, const std::string& id, const std::string& cid)
{
    store.removeCursor(id, cid);
    Json out = Json::obj();
    out.set("cursor", cid);
    out.set("closed", true);
    return out;
}

inline Json draftStatus(gs::Store& store, const std::string& id)
{
    Graph       latest;
    std::string err;
    if (!store.load(id, latest, 0, &err))
        throw std::runtime_error(err);
    Graph draft = latest;
    bool  has   = store.hasDraft(id);
    if (has && !store.loadDraft(id, draft, &err))
        throw std::runtime_error(err);

    auto countNodeDiff = [](const std::vector<gm::Node>& before,
                            const std::vector<gm::Node>& after) {
        int added = 0, removed = 0, modified = 0;
        std::map<std::string, gm::Node> bm;
        std::map<std::string, gm::Node> am;
        for (auto& it : before)
            bm[it.id] = it;
        for (auto& it : after)
            am[it.id] = it;
        for (auto& kv : am) {
            auto it = bm.find(kv.first);
            if (it == bm.end())
                added++;
            else if (!(it->second.label == kv.second.label &&
                       it->second.shape == kv.second.shape &&
                       it->second.parent == kv.second.parent &&
                       it->second.style == kv.second.style &&
                       it->second.attrs == kv.second.attrs))
                modified++;
        }
        for (auto& kv : bm)
            if (am.find(kv.first) == am.end())
                removed++;
        Json out = Json::obj();
        out.set("added", added);
        out.set("removed", removed);
        out.set("modified", modified);
        return out;
    };

    auto countEdgeDiff = [](const std::vector<gm::Edge>& before,
                            const std::vector<gm::Edge>& after) {
        int added = 0, removed = 0, modified = 0;
        std::map<std::string, gm::Edge> bm;
        std::map<std::string, gm::Edge> am;
        for (auto& it : before)
            bm[it.id] = it;
        for (auto& it : after)
            am[it.id] = it;
        for (auto& kv : am) {
            auto it = bm.find(kv.first);
            if (it == bm.end())
                added++;
            else if (!(it->second.from == kv.second.from &&
                       it->second.to == kv.second.to &&
                       it->second.label == kv.second.label &&
                       it->second.style == kv.second.style &&
                       it->second.arrow == kv.second.arrow))
                modified++;
        }
        for (auto& kv : bm)
            if (am.find(kv.first) == am.end())
                removed++;
        Json out = Json::obj();
        out.set("added", added);
        out.set("removed", removed);
        out.set("modified", modified);
        return out;
    };

    Json nodes = countNodeDiff(latest.nodes, draft.nodes);
    Json edges = countEdgeDiff(latest.edges, draft.edges);

    Json out = Json::obj();
    out.set("id", id);
    out.set("hasDraft", has);
    out.set("nodes", nodes);
    out.set("edges", edges);
    out.set("changed",
            nodes.num("added") + nodes.num("removed") + nodes.num("modified") +
                    edges.num("added") + edges.num("removed") +
                    edges.num("modified") >
                0);
    return out;
}

}  // namespace gc
