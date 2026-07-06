// layout.hpp - 基础自动布局 + 图结构校验
#pragma once
#include "model.hpp"
#include <queue>
#include <functional>

namespace gl {

using gm::Graph;
using gm::Node;

// ------------------------------------------------------------- 校验 --

// Issue: 校验问题项（severity 表示级别，message 为可读描述）
struct Issue {
    std::string severity; // 问题级别："error" | "warning"
    std::string message;
};

// validate: 图结构校验入口
// 关键步骤：空图检查 -> 节点/父子关系检查 -> 边引用检查 -> 环与孤点检查
inline std::vector<Issue> validate(const Graph& g) {
    std::vector<Issue> issues;
    auto err  = [&](const std::string& m) { issues.push_back({"error", m}); };
    auto warn = [&](const std::string& m) { issues.push_back({"warning", m}); };

    if (g.nodes.empty() && g.elements.empty())
        err("graph has no nodes or whiteboard elements");

    std::set<std::string> ids;
    for (auto& n : g.nodes) {
        if (n.id.empty()) err("node with empty id");
        else if (!ids.insert(n.id).second) err("duplicate node id: " + n.id);
        if (n.label.empty() && n.shape != "group")
            warn("node '" + n.id + "' has empty label");
    }
    for (auto& n : g.nodes) {
        if (!n.parent.empty() && !ids.count(n.parent))
            err("node '" + n.id + "' references missing parent '" + n.parent + "'");
    }
    for (auto& e : g.edges) {
        if (!ids.count(e.from)) err("edge '" + e.id + "' references missing node '" + e.from + "'");
        if (!ids.count(e.to))   err("edge '" + e.id + "' references missing node '" + e.to + "'");
        if (e.from == e.to)     warn("edge '" + e.id + "' is a self-loop on '" + e.from + "'");
    }
    // 层级环检测（沿 parent 链向上追踪）
    for (auto& n : g.nodes) {
        std::set<std::string> seen;
        const Node* cur = &n;
        while (cur && !cur->parent.empty()) {
            if (!seen.insert(cur->id).second) {
                err("hierarchy cycle involving node '" + n.id + "'");
                break;
            }
            cur = g.findNode(cur->parent);
        }
    }
    // 孤立节点检测（仅对流程类图）
    if ((g.type == "flowchart" || g.type == "architecture") && g.nodes.size() > 1) {
        std::set<std::string> connected;
        for (auto& e : g.edges) { connected.insert(e.from); connected.insert(e.to); }
        for (auto& n : g.nodes)
            if (!connected.count(n.id) && n.shape != "group" && n.parent.empty())
                warn("node '" + n.id + "' is isolated (no edges)");
    }
    return issues;
}

// hasErrors: 快速判断问题列表里是否存在 error 级别
inline bool hasErrors(const std::vector<Issue>& issues) {
    for (auto& i : issues) if (i.severity == "error") return true;
    return false;
}

// ----------------------------------------------------------------- 布局 --

namespace detail {

// 树布局：递归计算子树宽度并放置节点
// horizontal=false 表示上下布局（组织图），true 表示左右布局（思维导图）
// TreeLayout: 树形布局辅助器
// 命名说明：depth 表示主轴深度，breadth 表示横向展开宽度
struct TreeLayout {
    Graph& g;
    std::map<std::string, std::vector<std::string>> children;
    double gapX = 40, gapY = 70;
    bool horizontal;

    TreeLayout(Graph& graph, bool horiz) : g(graph), horizontal(horiz) {
        for (auto& n : g.nodes)
            if (!n.parent.empty()) children[n.parent].push_back(n.id);
    }
    // 返回子树在“横向展开轴”（breadth）上的占用范围
    // place: 递归放置子树并返回该子树在 breadth 方向占用的总宽度
    double place(const std::string& id, double depthPos, double breadthPos) {
        Node* n = g.findNode(id);
        if (!n) return 0;
        double selfBreadth = horizontal ? n->h : n->w;
        auto& kids = children[id];
        if (kids.empty()) {
            setPos(*n, depthPos, breadthPos + selfBreadth / 2);
            return selfBreadth;
        }
        double childDepth = depthPos + (horizontal ? n->w + 90 : n->h + gapY);
        double total = 0;
        for (auto& k : kids) {
            double ext = place(k, childDepth, breadthPos + total);
            total += ext + gapX;
        }
        total -= gapX;
        total = std::max(total, selfBreadth);
        setPos(*n, depthPos, breadthPos + total / 2);
        return total;
    }
    // setPos: 根据横向/纵向布局模式统一设置节点坐标
    void setPos(Node& n, double depthPos, double breadthCenter) {
        if (horizontal) { n.x = depthPos; n.y = breadthCenter - n.h / 2; }
        else            { n.x = breadthCenter - n.w / 2; n.y = depthPos; }
    }
};

} // 命名空间 detail

// 分层布局（流程类图）：从入度为 0 的源点进行 BFS 分层
// layoutLayered: 分层布局（适合流程图）
// 关键步骤：Kahn 分层 -> 同层排布 -> 最后包裹 group 容器
inline void layoutLayered(Graph& g) {
    std::map<std::string, int> indeg;
    std::map<std::string, std::vector<std::string>> out;
    for (auto& n : g.nodes) indeg[n.id] = 0;
    for (auto& e : g.edges) {
        if (indeg.count(e.to) && e.from != e.to) {
            indeg[e.to]++;
            out[e.from].push_back(e.to);
        }
    }
    std::map<std::string, int> rank;
    std::queue<std::string> q;
    // Kahn 拓扑分层；剩余成环节点在后续兜底处理
    std::map<std::string, int> deg = indeg;
    for (auto& n : g.nodes) if (deg[n.id] == 0) { rank[n.id] = 0; q.push(n.id); }
    while (!q.empty()) {
        std::string u = q.front(); q.pop();
        for (auto& v : out[u]) {
            rank[v] = std::max(rank.count(v) ? rank[v] : 0, rank[u] + 1);
            if (--deg[v] == 0) q.push(v);
        }
    }
    int maxRank = 0;
    for (auto& n : g.nodes) {
        if (!rank.count(n.id)) rank[n.id] = 0; // 位于环中的节点
        maxRank = std::max(maxRank, rank[n.id]);
    }
    // group 容器节点后置处理，用于包裹子节点范围
    std::map<int, std::vector<Node*>> byRank;
    for (auto& n : g.nodes)
        if (n.shape != "group") byRank[rank[n.id]].push_back(&n);
    double y = 40;
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        if (row.empty()) continue;
        double totalW = 0;
        for (auto* n : row) totalW += n->w + 60;
        totalW -= 60;
        double x = -totalW / 2;
        double rowH = 0;
        for (auto* n : row) {
            n->x = x;
            n->y = y;
            x += n->w + 60;
            rowH = std::max(rowH, n->h);
        }
        y += rowH + 80;
    }
    // 根据成员节点边界回填 group 容器尺寸
    for (auto& grp : g.nodes) {
        if (grp.shape != "group") continue;
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        bool any = false;
        for (auto& n : g.nodes) {
            if (n.parent != grp.id || n.shape == "group") continue;
            any = true;
            minX = std::min(minX, n.x); minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x + n.w); maxY = std::max(maxY, n.y + n.h);
        }
        if (any) {
            grp.x = minX - 20; grp.y = minY - 36;
            grp.w = maxX - minX + 40; grp.h = maxY - minY + 56;
        }
    }
}

// layoutTree: 树布局入口（horizontal=true 为左右，false 为上下）
inline void layoutTree(Graph& g, bool horizontal) {
    detail::TreeLayout tl(g, horizontal);
    std::set<std::string> hasParent;
    for (auto& n : g.nodes) if (!n.parent.empty()) hasParent.insert(n.id);
    double offset = 40;
    for (auto& n : g.nodes) {
        if (hasParent.count(n.id)) continue; // 非根节点
        double ext = tl.place(n.id, 40, offset);
        offset += ext + 60;
    }
}

// 无结构图的网格兜底布局
// layoutGrid: 无边结构的兜底网格布局
inline void layoutGrid(Graph& g) {
    int cols = (int)std::max(1.0, std::ceil(std::sqrt((double)g.nodes.size())));
    int i = 0;
    for (auto& n : g.nodes) {
        n.x = 40 + (i % cols) * 220;
        n.y = 40 + (i / cols) * 120;
        i++;
    }
}

// 入口：按图类型选择布局策略，并先补齐节点尺寸
// layout: 总布局入口（按图类型选择策略）
// 关键步骤：补默认尺寸 -> 策略分发 -> 坐标归一化到正区间
inline void layout(Graph& g, bool force = false) {
    if (g.laidOut && !force) return;
    for (auto& n : g.nodes)
        if (n.w <= 0 || n.h <= 0) gm::defaultSize(n);
    if (g.type == "mindmap")       layoutTree(g, true);
    else if (g.type == "orgchart") layoutTree(g, false);
    else if (g.edges.empty())      layoutGrid(g);
    else                           layoutLayered(g);
    // 坐标归一化到正区间
    double minX = 1e18, minY = 1e18;
    for (auto& n : g.nodes) { minX = std::min(minX, n.x); minY = std::min(minY, n.y); }
    if (!g.nodes.empty() && (minX < 20 || minY < 20)) {
        double dx = 20 - minX, dy = 20 - minY;
        for (auto& n : g.nodes) { n.x += dx; n.y += dy; }
    }
    g.laidOut = true;
}

} // 命名空间 gl
