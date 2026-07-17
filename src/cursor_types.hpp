// cursor_types.hpp - Cursor 游标操作类型
// 提供对图模型元素的定位、遍历和链式修改能力。
// NodeCursor / EdgeCursor 用于单元素精确定位；
// SelectionCursor 用于批量选择操作。
#pragma once
#include "model.hpp"
#include "version_types.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace gv {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

// ─── CursorPosition: 游标当前定位点 ───────────────────────────────
struct CursorPosition
{
    std::string elementId;
    std::string elementType;  // "node" | "edge" | "graph"
    std::string fieldName;
    int         fieldIndex = -1;
};

// ==================================================================
// NodeCursor: 节点游标
// ==================================================================
class NodeCursor {
  public:
    // 构造：精确定位到指定节点
    NodeCursor(Graph& g, Draft* draft, const std::string& nodeId)
        : graph_(g), draft_(draft)
    {
        for (size_t i = 0; i < g.nodes.size(); i++) {
            if (g.nodes[i].id == nodeId) {
                matched_.push_back(nodeId);
                current_ = 0;
                return;
            }
        }
        current_ = -1;
    }

    // 构造：从 Selector 筛选结果中取第 index 个
    NodeCursor(Graph& g, Draft* draft, const Selector& sel, int index = 0)
        : graph_(g), draft_(draft)
    {
        for (auto& n : g.nodes) {
            if (selectorMatchesNode(n, sel))
                matched_.push_back(n.id);
        }
        // 额外处理 CONNECTED_TO
        if (sel.kind == Selector::Kind::CONNECTED_TO) {
            std::set<std::string> connected;
            for (auto& e : g.edges) {
                if (e.from == sel.value)
                    connected.insert(e.to);
                if (e.to == sel.value)
                    connected.insert(e.from);
            }
            for (auto& n : g.nodes) {
                if (connected.count(n.id) &&
                    std::find(matched_.begin(), matched_.end(), n.id) ==
                        matched_.end())
                    matched_.push_back(n.id);
            }
        }
        if (index >= 0 && index < (int)matched_.size())
            current_ = index;
        else if (!matched_.empty())
            current_ = 0;
        else
            current_ = -1;
    }

    // ── 导航方法（链式调用）──
    NodeCursor& first()
    {
        current_ = matched_.empty() ? -1 : 0;
        return *this;
    }
    NodeCursor& last()
    {
        current_ = matched_.empty() ? -1 : (int)matched_.size() - 1;
        return *this;
    }
    NodeCursor& next()
    {
        if (current_ >= 0 && current_ + 1 < (int)matched_.size())
            current_++;
        else
            current_ = -1;
        return *this;
    }
    NodeCursor& prev()
    {
        if (current_ > 0)
            current_--;
        return *this;
    }
    NodeCursor& at(int index)
    {
        if (index >= 0 && index < (int)matched_.size())
            current_ = index;
        else
            current_ = -1;
        return *this;
    }

    // ── 字段导航 ──
    NodeCursor& field(const std::string& name)
    {
        currentField_ = name;
        return *this;
    }

    // ── 状态查询 ──
    bool valid() const
    { return current_ >= 0 && current_ < (int)matched_.size(); }
    int count() const
    { return (int)matched_.size(); }
    std::string nodeId() const
    { return valid() ? matched_[current_] : ""; }
    std::vector<std::string> idList() const
    { return matched_; }

    // ── 读取 ──
    Node* get()
    {
        if (!valid())
            return nullptr;
        return graph_.findNode(matched_[current_]);
    }
    const Node* get() const
    {
        if (!valid())
            return nullptr;
        return graph_.findNode(matched_[current_]);
    }

    std::string value() const
    {
        const Node* n = get();
        if (!n)
            return "";
        if (!currentField_.empty())
            return getNodeField(*n, currentField_);
        return n->label;
    }

    // ── 通用 set ──
    // set: 写入节点字段；未知字段时不记草稿并返回 false
    bool set(const std::string& fieldName, const std::string& val)
    {
        Node* n = get();
        if (!n)
            return false;
        std::string oldVal = getNodeField(*n, fieldName);
        if (!setNodeField(*n, fieldName, val))
            return false;
        if (draft_)
            recordChange(fieldName, oldVal, val);
        return true;
    }

    // ── 便捷修改方法 ──
    NodeCursor& updateLabel(const std::string& label)
    {
        set("label", label);
        return *this;
    }
    NodeCursor& updateShape(const std::string& shape)
    {
        set("shape", shape);
        return *this;
    }
    NodeCursor& updateStyle(const std::string& style)
    {
        set("style", style);
        return *this;
    }
    NodeCursor& setParent(const std::string& parentId)
    {
        set("parent", parentId);
        return *this;
    }

    NodeCursor& updatePosition(double x, double y)
    {
        Node* n = get();
        if (!n)
            return *this;
        std::string ox = formatCoord(n->x), oy = formatCoord(n->y);
        n->x = x;
        n->y = y;
        if (draft_) {
            recordChange("x", ox, formatCoord(x));
            recordChange("y", oy, formatCoord(y));
        }
        return *this;
    }
    NodeCursor& updateSize(double w, double h)
    {
        Node* n = get();
        if (!n)
            return *this;
        std::string ow = formatCoord(n->w), oh = formatCoord(n->h);
        n->w = w;
        n->h = h;
        if (draft_) {
            recordChange("w", ow, formatCoord(w));
            recordChange("h", oh, formatCoord(h));
        }
        return *this;
    }

  private:
    Graph&                   graph_;
    Draft*                   draft_;
    std::vector<std::string> matched_;
    int                      current_ = -1;
    std::string              currentField_;

    void recordChange(const std::string& field,
                      const std::string& oldVal,
                      const std::string& newVal)
    {
        // 查找是否已有本节点的 UPDATE 操作
        std::string nid = matched_[current_];
        for (auto& op : draft_->operations) {
            if (op.type == OpType::NODE_UPDATE && op.targetId == nid) {
                op.changes.push_back({field, oldVal, newVal});
                op.timestamp = nowIso();
                return;
            }
        }
        // 新建操作
        Operation op;
        op.type       = OpType::NODE_UPDATE;
        op.targetId   = nid;
        op.targetType = "node";
        op.changes.push_back({field, oldVal, newVal});
        op.timestamp = nowIso();
        draft_->operations.push_back(op);
        draft_->updatedAt = nowIso();
    }
};

// ==================================================================
// EdgeCursor: 边游标
// ==================================================================
class EdgeCursor {
  public:
    EdgeCursor(Graph& g, Draft* draft, const std::string& edgeId)
        : graph_(g), draft_(draft)
    {
        for (size_t i = 0; i < g.edges.size(); i++) {
            if (g.edges[i].id == edgeId) {
                matched_.push_back(edgeId);
                current_ = 0;
                return;
            }
        }
        current_ = -1;
    }

    EdgeCursor(Graph& g, Draft* draft, const Selector& sel, int index = 0)
        : graph_(g), draft_(draft)
    {
        for (auto& e : g.edges) {
            if (selectorMatchesEdge(e, sel))
                matched_.push_back(e.id);
        }
        if (index >= 0 && index < (int)matched_.size())
            current_ = index;
        else if (!matched_.empty())
            current_ = 0;
        else
            current_ = -1;
    }

    // ── 导航 ──
    EdgeCursor& first()
    {
        current_ = matched_.empty() ? -1 : 0;
        return *this;
    }
    EdgeCursor& last()
    {
        current_ = matched_.empty() ? -1 : (int)matched_.size() - 1;
        return *this;
    }
    EdgeCursor& next()
    {
        if (current_ >= 0 && current_ + 1 < (int)matched_.size())
            current_++;
        else
            current_ = -1;
        return *this;
    }
    EdgeCursor& prev()
    {
        if (current_ > 0)
            current_--;
        else
            current_ = -1;
        return *this;
    }
    EdgeCursor& at(int index)
    {
        if (index >= 0 && index < (int)matched_.size())
            current_ = index;
        else
            current_ = -1;
        return *this;
    }

    EdgeCursor& field(const std::string& name)
    {
        currentField_ = name;
        return *this;
    }

    // ── 状态 ──
    bool valid() const
    { return current_ >= 0 && current_ < (int)matched_.size(); }
    int count() const
    { return (int)matched_.size(); }
    std::string edgeId() const
    { return valid() ? matched_[current_] : ""; }
    std::vector<std::string> idList() const
    { return matched_; }

    Edge* get()
    {
        if (!valid())
            return nullptr;
        for (auto& e : graph_.edges)
            if (e.id == matched_[current_])
                return &e;
        return nullptr;
    }
    const Edge* get() const
    {
        if (!valid())
            return nullptr;
        for (auto& e : graph_.edges)
            if (e.id == matched_[current_])
                return &e;
        return nullptr;
    }

    std::string value() const
    {
        const Edge* e = get();
        if (!e)
            return "";
        if (!currentField_.empty())
            return getEdgeField(*e, currentField_);
        return e->label;
    }

    // set: 写入边字段；waypoints 等解析失败时不记草稿并返回 false
    bool set(const std::string& fieldName, const std::string& val)
    {
        Edge* e = get();
        if (!e)
            return false;
        std::string oldVal = getEdgeField(*e, fieldName);
        if (!setEdgeField(*e, fieldName, val))
            return false;
        if (draft_)
            recordChange(fieldName, oldVal, val);
        return true;
    }

    EdgeCursor& updateLabel(const std::string& label)
    {
        set("label", label);
        return *this;
    }
    EdgeCursor& updateStyle(const std::string& style)
    {
        set("style", style);
        return *this;
    }
    EdgeCursor& updateArrow(const std::string& arrow)
    {
        set("arrow", arrow);
        return *this;
    }
    EdgeCursor& reconnect(const std::string& from, const std::string& to)
    {
        set("from", from);
        set("to", to);
        return *this;
    }

  private:
    Graph&                   graph_;
    Draft*                   draft_;
    std::vector<std::string> matched_;
    int                      current_ = -1;
    std::string              currentField_;

    void recordChange(const std::string& field,
                      const std::string& oldVal,
                      const std::string& newVal)
    {
        std::string eid = matched_[current_];
        for (auto& op : draft_->operations) {
            if (op.type == OpType::EDGE_UPDATE && op.targetId == eid) {
                op.changes.push_back({field, oldVal, newVal});
                op.timestamp = nowIso();
                return;
            }
        }
        Operation op;
        op.type       = OpType::EDGE_UPDATE;
        op.targetId   = eid;
        op.targetType = "edge";
        op.changes.push_back({field, oldVal, newVal});
        op.timestamp = nowIso();
        draft_->operations.push_back(op);
        draft_->updatedAt = nowIso();
    }
};

// ==================================================================
// SelectionCursor: 批量选择游标
// ==================================================================
class SelectionCursor {
  public:
    SelectionCursor(Graph& g, Draft* draft, const Selector& sel)
        : graph_(g), draft_(draft), selector_(sel)
    {
        for (auto& n : g.nodes)
            if (selectorMatchesNode(n, sel))
                nodeIds_.push_back(n.id);
        for (auto& e : g.edges)
            if (selectorMatchesEdge(e, sel))
                edgeIds_.push_back(e.id);

        // CONNECTED_TO 补充
        if (sel.kind == Selector::Kind::CONNECTED_TO) {
            std::set<std::string> connected;
            for (auto& e : g.edges) {
                if (e.from == sel.value)
                    connected.insert(e.to);
                if (e.to == sel.value)
                    connected.insert(e.from);
            }
            for (auto& n : g.nodes) {
                if (connected.count(n.id) &&
                    std::find(nodeIds_.begin(), nodeIds_.end(), n.id) ==
                        nodeIds_.end())
                    nodeIds_.push_back(n.id);
            }
        }
    }

    int count() const
    { return (int)(nodeIds_.size() + edgeIds_.size()); }
    int nodeCount() const
    { return (int)nodeIds_.size(); }
    int edgeCount() const
    { return (int)edgeIds_.size(); }

    std::vector<std::string> nodeIds() const
    { return nodeIds_; }
    std::vector<std::string> edgeIds() const
    { return edgeIds_; }
    std::vector<std::string> allIds() const
    {
        auto ids = nodeIds_;
        ids.insert(ids.end(), edgeIds_.begin(), edgeIds_.end());
        return ids;
    }

    Selector selector() const
    { return selector_; }

    // 转换为子游标
    NodeCursor asNodeCursor(int index = 0)
    { return NodeCursor(graph_, draft_, selector_, index); }
    EdgeCursor asEdgeCursor(int index = 0)
    { return EdgeCursor(graph_, draft_, selector_, index); }

    // 批量设置：对选中集合的每个元素设置同一字段值；任一失败则返回 false
    bool setAll(const std::string& field, const std::string& value)
    {
        for (auto& nid : nodeIds_) {
            NodeCursor nc(graph_, draft_, nid);
            if (!nc.set(field, value))
                return false;
        }
        for (auto& eid : edgeIds_) {
            EdgeCursor ec(graph_, draft_, eid);
            if (!ec.set(field, value))
                return false;
        }
        return true;
    }

    // 批量删除
    SelectionCursor& deleteAll()
    {
        // 先删边
        for (auto& eid : edgeIds_) {
            if (draft_) {
                Operation op;
                op.type       = OpType::EDGE_DELETE;
                op.targetId   = eid;
                op.targetType = "edge";
                op.timestamp  = nowIso();
                draft_->operations.push_back(op);
                draft_->updatedAt = nowIso();
            }
            graph_.edges.erase(
                std::remove_if(graph_.edges.begin(), graph_.edges.end(),
                               [&](const Edge& e) { return e.id == eid; }),
                graph_.edges.end());
        }
        // 再删节点（级联删边）
        for (auto& nid : nodeIds_) {
            if (draft_) {
                Operation op;
                op.type       = OpType::NODE_DELETE;
                op.targetId   = nid;
                op.targetType = "node";
                op.timestamp  = nowIso();
                draft_->operations.push_back(op);
                draft_->updatedAt = nowIso();
            }
            graph_.edges.erase(
                std::remove_if(graph_.edges.begin(), graph_.edges.end(),
                               [&](const Edge& e) {
                                   return e.from == nid || e.to == nid;
                               }),
                graph_.edges.end());
            graph_.nodes.erase(
                std::remove_if(graph_.nodes.begin(), graph_.nodes.end(),
                               [&](const Node& n) { return n.id == nid; }),
                graph_.nodes.end());
        }
        return *this;
    }

  private:
    Graph&                   graph_;
    Draft*                   draft_;
    Selector                 selector_;
    std::vector<std::string> nodeIds_;
    std::vector<std::string> edgeIds_;
};

// ==================================================================
// 工厂函数（对标 SQL 语义：select * from graph where ...）
// ==================================================================
inline NodeCursor selectNode(Graph& g, Draft* draft, const std::string& id)
{ return NodeCursor(g, draft, id); }
inline NodeCursor selectNodes(Graph& g, Draft* draft, const Selector& sel)
{ return NodeCursor(g, draft, sel); }
inline EdgeCursor selectEdge(Graph& g, Draft* draft, const std::string& id)
{ return EdgeCursor(g, draft, id); }
inline EdgeCursor selectEdges(Graph& g, Draft* draft, const Selector& sel)
{ return EdgeCursor(g, draft, sel); }
inline SelectionCursor select(Graph& g, Draft* draft, const Selector& sel)
{ return SelectionCursor(g, draft, sel); }

// ─── 便捷：插入节点并记录 Draft ──────────────────────────────────
// 返回新节点的 id
inline std::string insertNode(Graph&             g,
                              Draft*             draft,
                              const std::string& shape       = "rect",
                              const std::string& label       = "",
                              double             x           = 0,
                              double             y           = 0,
                              double             w           = 0,
                              double             h           = 0,
                              const std::string& parent      = "",
                              const std::string& style       = "",
                              const std::string& fillColor   = "",
                              const std::string& strokeColor = "")
{
    Node n;
    n.id          = "n" + std::to_string(++g.nodeCounter_);
    n.label       = label.empty() ? n.id : label;
    n.shape       = shape;
    n.x           = x;
    n.y           = y;
    n.w           = (w <= 0) ? 120 : w;
    n.h           = (h <= 0) ? 44 : h;
    n.parent      = parent;
    n.style       = style;
    n.fillColor   = fillColor;
    n.strokeColor = strokeColor;
    if (w <= 0 || h <= 0)
        gm::defaultSize(n);
    g.nodes.push_back(n);

    if (draft) {
        Operation op;
        op.type       = OpType::NODE_INSERT;
        op.targetId   = n.id;
        op.targetType = "node";
        op.snapshot   = nodeToSnapshot(n);
        op.timestamp  = nowIso();
        draft->operations.push_back(op);
        draft->updatedAt = nowIso();
    }
    return n.id;
}

// ─── 便捷：插入边并记录 Draft ────────────────────────────────────
inline std::string insertEdge(Graph&             g,
                              Draft*             draft,
                              const std::string& from,
                              const std::string& to,
                              const std::string& label       = "",
                              const std::string& style       = "solid",
                              const std::string& arrow       = "arrow",
                              const std::string& strokeColor = "")
{
    // 确保端点节点存在
    g.ensureNode(from);
    g.ensureNode(to);

    Edge e;
    e.id          = "e" + std::to_string(++g.edgeCounter_);
    e.from        = from;
    e.to          = to;
    e.label       = label;
    e.style       = style;
    e.arrow       = arrow;
    e.strokeColor = strokeColor;
    g.edges.push_back(e);

    if (draft) {
        Operation op;
        op.type       = OpType::EDGE_INSERT;
        op.targetId   = e.id;
        op.targetType = "edge";
        op.snapshot   = edgeToSnapshot(e);
        op.timestamp  = nowIso();
        draft->operations.push_back(op);
        draft->updatedAt = nowIso();
    }
    return e.id;
}

// ─── 便捷：删除节点（级联删边）并记录 Draft ──────────────────────
inline bool deleteNode(Graph& g, Draft* draft, const std::string& nodeId)
{
    if (!g.findNode(nodeId))
        return false;

    if (draft) {
        Operation op;
        op.type       = OpType::NODE_DELETE;
        op.targetId   = nodeId;
        op.targetType = "node";
        op.timestamp  = nowIso();
        draft->operations.push_back(op);
        draft->updatedAt = nowIso();
    }

    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                                 [&](const Edge& e) {
                                     return e.from == nodeId || e.to == nodeId;
                                 }),
                  g.edges.end());
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                 [&](const Node& n) { return n.id == nodeId; }),
                  g.nodes.end());
    return true;
}

// ─── 便捷：删除边并记录 Draft ────────────────────────────────────
inline bool deleteEdge(Graph& g, Draft* draft, const std::string& edgeId)
{
    auto it = std::find_if(g.edges.begin(), g.edges.end(),
                           [&](const Edge& e) { return e.id == edgeId; });
    if (it == g.edges.end())
        return false;

    if (draft) {
        Operation op;
        op.type       = OpType::EDGE_DELETE;
        op.targetId   = edgeId;
        op.targetType = "edge";
        op.timestamp  = nowIso();
        draft->operations.push_back(op);
        draft->updatedAt = nowIso();
    }

    g.edges.erase(it);
    return true;
}

}  // namespace gv
