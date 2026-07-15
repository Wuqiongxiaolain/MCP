// layout.hpp - 基础自动布局 + 图结构校验
#pragma once
#include "model.hpp"
#include <queue>

namespace gl {

using gm::Edge;
using gm::Graph;
using gm::Node;

// ------------------------------------------------------------- 校验 --

// Issue: 校验问题项（severity 表示级别，message 为可读描述）
struct Issue
{
    std::string severity;  // 问题级别："error" | "warning"
    std::string message;
};

// validate: 图结构校验入口
// 关键步骤：空图检查 -> 节点/父子关系检查 -> 边引用检查 -> 环与孤点检查
inline std::vector<Issue> validate(const Graph& g)
{
    std::vector<Issue> issues;
    auto err  = [&](const std::string& m) { issues.push_back({"error", m}); };
    auto warn = [&](const std::string& m) { issues.push_back({"warning", m}); };

    // rawMermaid 透传类型没有可验证的节点/边结构，跳过校验
    if (!g.rawMermaid.empty())
        return issues;

    // properties 类型：有类型特定的结构化数据，不要求必须有 nodes
    bool hasProperties = g.properties.isObj() && g.properties.o &&
                         !g.properties.o->empty();

    if (g.nodes.empty() && g.elements.empty() && !hasProperties)
        err("graph has no nodes or whiteboard elements");

    std::set<std::string> ids;
    for (auto& n : g.nodes) {
        if (n.id.empty())
            err("node with empty id");
        else if (!ids.insert(n.id).second)
            err("duplicate node id: " + n.id);
        if (n.label.empty() && n.shape != "group")
            warn("node '" + n.id + "' has empty label");
    }
    // 白板边可绑定未提升为节点的 Excalidraw 元素 id
    std::set<std::string> endpointIds = ids;
    if (g.type == "whiteboard") {
        for (auto& el : g.elements) {
            std::string eid = el.str("id");
            if (!eid.empty())
                endpointIds.insert(eid);
        }
    }
    for (auto& n : g.nodes) {
        if (!n.parent.empty() && !ids.count(n.parent))
            err("node '" + n.id + "' references missing parent '" + n.parent +
                "'");
    }
    for (auto& e : g.edges) {
        if (!endpointIds.count(e.from))
            err("edge '" + e.id + "' references missing node '" + e.from + "'");
        if (!endpointIds.count(e.to))
            err("edge '" + e.id + "' references missing node '" + e.to + "'");
        if (e.from == e.to)
            warn("edge '" + e.id + "' is a self-loop on '" + e.from + "'");
    }
    // 层级环检测（沿 parent 链向上追踪）
    for (auto& n : g.nodes) {
        std::set<std::string> seen;
        const Node*           cur = &n;
        while (cur && !cur->parent.empty()) {
            if (!seen.insert(cur->id).second) {
                err("hierarchy cycle involving node '" + n.id + "'");
                break;
            }
            cur = g.findNode(cur->parent);
        }
    }
    // 孤立节点检测（仅对流程类图）
    if ((g.type == "flowchart" || g.type == "architecture") &&
        g.nodes.size() > 1) {
        std::set<std::string> connected;
        for (auto& e : g.edges) {
            connected.insert(e.from);
            connected.insert(e.to);
        }
        for (auto& n : g.nodes)
            if (!connected.count(n.id) && n.shape != "group" &&
                n.parent.empty())
                warn("node '" + n.id + "' is isolated (no edges)");
    }
    return issues;
}

// hasErrors: 快速判断问题列表里是否存在 error 级别
inline bool hasErrors(const std::vector<Issue>& issues)
{
    for (auto& i : issues)
        if (i.severity == "error")
            return true;
    return false;
}

// ----------------------------------------------------------------- 布局 --

namespace detail {

    // 树布局：递归计算子树宽度并放置节点
    // horizontal=false 表示上下布局（组织图），true 表示左右布局（思维导图）
    // TreeLayout: 树形布局辅助器
    // 命名说明：depth 表示主轴深度，breadth 表示横向展开宽度
    struct TreeLayout
    {
        Graph&                                          g;
        std::map<std::string, std::vector<std::string>> children;
        double                                          gapX = 40, gapY = 70;
        bool                                            horizontal;

        TreeLayout(Graph& graph, bool horiz) : g(graph), horizontal(horiz)
        {
            for (auto& n : g.nodes)
                if (!n.parent.empty())
                    children[n.parent].push_back(n.id);
        }
        // 返回子树在“横向展开轴”（breadth）上的占用范围
        // place: 递归放置子树并返回该子树在 breadth 方向占用的总宽度
        double place(const std::string& id, double depthPos, double breadthPos)
        {
            Node* n = g.findNode(id);
            if (!n)
                return 0;
            double selfBreadth = horizontal ? n->h : n->w;
            auto&  kids        = children[id];
            if (kids.empty()) {
                setPos(*n, depthPos, breadthPos + selfBreadth / 2);
                return selfBreadth;
            }
            double childDepth =
                depthPos + (horizontal ? n->w + 90 : n->h + gapY);
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
        void setPos(Node& n, double depthPos, double breadthCenter)
        {
            if (horizontal) {
                n.x = depthPos;
                n.y = breadthCenter - n.h / 2;
            }
            else {
                n.x = breadthCenter - n.w / 2;
                n.y = depthPos;
            }
        }
    };

}  // namespace detail

// 分层布局（流程类图）：从入度为 0 的源点进行 BFS 分层
// layoutLayered: 分层布局（适合流程图）
// 关键步骤：Kahn 分层 -> 同层排布 -> 最后包裹 group 容器
inline void layoutLayered(Graph& g)
{
    std::map<std::string, int>                      indeg;
    std::map<std::string, std::vector<std::string>> out;
    for (auto& n : g.nodes)
        indeg[n.id] = 0;
    for (auto& e : g.edges) {
        if (indeg.count(e.to) && e.from != e.to) {
            indeg[e.to]++;
            out[e.from].push_back(e.to);
        }
    }
    std::map<std::string, int> rank;
    std::queue<std::string>    q;
    // Kahn 拓扑分层；剩余成环节点在后续兜底处理
    std::map<std::string, int> deg = indeg;
    for (auto& n : g.nodes)
        if (deg[n.id] == 0) {
            rank[n.id] = 0;
            q.push(n.id);
        }
    while (!q.empty()) {
        std::string u = q.front();
        q.pop();
        for (auto& v : out[u]) {
            rank[v] = std::max(rank.count(v) ? rank[v] : 0, rank[u] + 1);
            if (--deg[v] == 0)
                q.push(v);
        }
    }
    // ---- 成环节点层级分配（迭代松弛 + 全环引导） ----
    // 构建前驱邻接表
    std::map<std::string, std::vector<std::string>> preds;
    for (auto& e : g.edges) {
        if (e.from != e.to && indeg.count(e.to))
            preds[e.to].push_back(e.from);
    }
    {
        // 若 Kahn 后 rank 完全为空（全部节点处于环中），选"净出度"最大
        // 的节点作为锚点（偏好自然的信息源/枢纽），赋予 rank 0 引导迭代
        if (rank.empty() && !g.nodes.empty()) {
            const Node* seed = &g.nodes[0];
            int         bestNet = (int)out[seed->id].size() - indeg[seed->id];
            for (auto& n : g.nodes) {
                int net = (int)out[n.id].size() - indeg[n.id];
                if (net > bestNet) { bestNet = net; seed = &n; }
            }
            rank[seed->id] = 0;
        }
        // 反复扫描：未分层节点若存在已分层前驱/后继，则取其最大值 + 1
        bool changed = true;
        int  fallbackGuard = 0;
        while (changed && fallbackGuard < 100) {
            changed = false;
            fallbackGuard++;
            for (auto& n : g.nodes) {
                if (rank.count(n.id)) continue;
                int best = -1;
                auto it = preds.find(n.id);
                if (it != preds.end()) {
                    for (auto& pid : it->second) {
                        auto ri = rank.find(pid);
                        if (ri != rank.end() && ri->second > best)
                            best = ri->second;
                    }
                }
                auto oi = out.find(n.id);
                if (oi != out.end()) {
                    for (auto& sid : oi->second) {
                        auto ri = rank.find(sid);
                        if (ri != rank.end() && ri->second > best)
                            best = ri->second;
                    }
                }
                if (best >= 0) {
                    rank[n.id] = best + 1;
                    changed = true;
                }
            }
        }
        for (auto& n : g.nodes)
            if (!rank.count(n.id))
                rank[n.id] = 0;
    }
    int maxRank = 0;
    for (auto& n : g.nodes)
        maxRank = std::max(maxRank, rank[n.id]);

    // ---- 层次均衡：将超宽层次的多余节点向下推，避免单行过长 ----
    const int MAX_NODES_PER_LAYER = 6;
    {
        // 统计每层节点数
        std::map<int, int> layerCount;
        for (auto& n : g.nodes)
            if (n.shape != "group")
                layerCount[rank[n.id]]++;

        // 自底向上扫描，把超限层节点向下推（支持级联：后继在 r+1 的也一起下推）
        bool changed = true;
        while (changed) {
            changed = false;
            for (int r = maxRank; r >= 0; r--) {
                if (layerCount[r] <= MAX_NODES_PER_LAYER) continue;
                int excess = layerCount[r] - MAX_NODES_PER_LAYER;
                if (excess <= 0) continue;

                // 收集当前层所有节点，分为"可直接下移"和"需级联"
                // 按优先级排序：低出度 → 对下游影响小
                std::vector<Node*> cands;
                for (auto& n : g.nodes) {
                    if (n.shape == "group") continue;
                    if (rank[n.id] != r) continue;
                    cands.push_back(&n);
                }
                std::sort(cands.begin(), cands.end(),
                    [&](Node* a, Node* b) {
                        int oa = out.count(a->id) ? (int)out[a->id].size() : 0;
                        int ob = out.count(b->id) ? (int)out[b->id].size() : 0;
                        if (oa != ob) return oa < ob;
                        return false;
                    });

                int moved = 0;
                for (auto* n : cands) {
                    if (moved >= excess) break;

                    // 检查后继：如果有后继在 r+1，级联下推它们
                    bool needCascade = false;
                    auto oi = out.find(n->id);
                    if (oi != out.end()) {
                        for (auto& sid : oi->second) {
                            auto ri = rank.find(sid);
                            if (ri != rank.end() && ri->second == r + 1)
                                needCascade = true;
                        }
                    }

                    if (needCascade) {
                        // 级联：先把 r+1 的后继推到 r+2
                        std::set<std::string> cascaded;
                        if (oi != out.end()) {
                            for (auto& sid : oi->second) {
                                auto ri = rank.find(sid);
                                if (ri != rank.end() && ri->second == r + 1 &&
                                    cascaded.insert(sid).second) {
                                    rank[sid] = r + 2;
                                    layerCount[r + 1]--;
                                    layerCount[r + 2]++;
                                }
                            }
                        }
                    }

                    // 下移当前节点
                    rank[n->id] = r + 1;
                    layerCount[r]--;
                    layerCount[r + 1]++;
                    moved++;
                    changed = true;
                }
            }
            // 更新 maxRank
            maxRank = 0;
            for (auto& kv : layerCount)
                if (kv.second > 0 && kv.first > maxRank)
                    maxRank = kv.first;
        }
    }

    // group 容器节点后置处理，用于包裹子节点范围
    std::map<int, std::vector<Node*>> byRank;
    for (auto& n : g.nodes)
        if (n.shape != "group")
            byRank[rank[n.id]].push_back(&n);

    // ---- 虚拟节点：长边（跨度 > 1 层）在中间层插入临时节点 ----
    // 临时虚拟节点存于局部向量，用 Node* 参与后续流程，布局完成后丢弃
    std::vector<Node> dummyNodes;
    // 记录每条原始边的中间虚拟节点 id 列表（从上到下），供导出器做折线路由
    std::map<std::string, std::vector<std::string>> edgeDummies;

    // 预分配避免 push_back 触发 realloc 导致指针失效
    {
        int est = 0;
        for (auto& e : g.edges)
            est += std::max(0, std::abs(rank[e.from] - rank[e.to]) - 1);
        dummyNodes.reserve((size_t)est);
    }
    for (auto& e : g.edges) {
        int rFrom = rank[e.from], rTo = rank[e.to];
        if (std::abs(rFrom - rTo) <= 1) continue;
        int lo = std::min(rFrom, rTo), hi = std::max(rFrom, rTo);
        std::string prevId = e.from;
        for (int r = lo + 1; r < hi; r++) {
            Node d;
            d.id = "$$d_" + e.id + "_" + std::to_string(r);
            d.shape = "dummy";
            d.w = 1; d.h = 1;
            dummyNodes.push_back(d);
            Node* dp = &dummyNodes.back();
            byRank[r].push_back(dp);

            // 更新交叉最小化所需的邻接表
            preds[d.id].push_back(prevId);
            out[prevId].push_back(d.id);
            std::string nextId = (r + 1 == hi) ? e.to : ("$$d_" + e.id + "_" + std::to_string(r + 1));
            preds[nextId].push_back(d.id);
            out[d.id].push_back(nextId);

            edgeDummies[e.id].push_back(d.id);
            prevId = d.id;
        }
    }
    // 虚拟节点插入后重算 maxRank（可能没有变化，但保持一致性）
    maxRank = 0;
    for (auto& kv : byRank)
        if (!kv.second.empty() && kv.first > maxRank)
            maxRank = kv.first;

    // ---- crossing minimization (barycenter heuristic) ----
    // 10 轮交替扫描（maxRank>=3 时）；每轮后追加贪心相邻交换优化
    const int MAX_PASSES = (maxRank >= 3) ? 10 : (maxRank >= 2 ? 4 : 2);
    for (int pass = 0; pass < MAX_PASSES; pass++) {
        bool downward = (pass % 2 == 0);
        if (downward) {
            for (int r = 1; r <= maxRank; r++) {
                auto& layer = byRank[r];
                if (layer.size() <= 1) continue;
                std::map<std::string, int> upperPos;
                auto& upper = byRank[r - 1];
                for (size_t j = 0; j < upper.size(); j++)
                    upperPos[upper[j]->id] = (int)j;
                std::vector<std::pair<double, Node*>> scored;
                for (auto* n : layer) {
                    double sum = 0; int cnt = 0;
                    auto it = preds.find(n->id);
                    if (it != preds.end()) {
                        for (auto& pid : it->second) {
                            auto pi = upperPos.find(pid);
                            if (pi != upperPos.end()) { sum += pi->second; cnt++; }
                        }
                    }
                    double bary = cnt ? sum / cnt : layer.size() / 2.0;
                    scored.push_back({bary, n});
                }
                std::stable_sort(scored.begin(), scored.end(),
                    [](auto& a, auto& b) { return a.first < b.first; });
                layer.clear();
                for (auto& s : scored) layer.push_back(s.second);

                // 贪心相邻交换：尝试交换减少与上层之间的交叉
                for (int sweep = 0; sweep < 2; sweep++) {
                    for (size_t i = 0; i + 1 < layer.size(); i++) {
                        Node* a = layer[i];
                        Node* b = layer[i + 1];
                        double crossBefore = 0, crossAfter = 0;
                        auto itA = preds.find(a->id), itB = preds.find(b->id);
                        if (itA != preds.end() && itB != preds.end()) {
                            for (auto& pa : itA->second) {
                                auto paPos = upperPos.find(pa);
                                if (paPos == upperPos.end()) continue;
                                for (auto& pb : itB->second) {
                                    auto pbPos = upperPos.find(pb);
                                    if (pbPos == upperPos.end()) continue;
                                    if (paPos->second > pbPos->second) crossBefore++;
                                    if (paPos->second < pbPos->second) crossAfter++;
                                }
                            }
                        }
                        if (crossAfter < crossBefore) {
                            std::swap(layer[i], layer[i + 1]);
                        }
                    }
                }
            }
        } else {
            for (int r = maxRank - 1; r >= 0; r--) {
                auto& layer = byRank[r];
                if (layer.size() <= 1) continue;
                std::map<std::string, int> lowerPos;
                auto& lower = byRank[r + 1];
                for (size_t j = 0; j < lower.size(); j++)
                    lowerPos[lower[j]->id] = (int)j;
                std::vector<std::pair<double, Node*>> scored;
                for (auto* n : layer) {
                    double sum = 0; int cnt = 0;
                    auto it = out.find(n->id);
                    if (it != out.end()) {
                        for (auto& sid : it->second) {
                            auto si = lowerPos.find(sid);
                            if (si != lowerPos.end()) { sum += si->second; cnt++; }
                        }
                    }
                    double bary = cnt ? sum / cnt : layer.size() / 2.0;
                    scored.push_back({bary, n});
                }
                std::stable_sort(scored.begin(), scored.end(),
                    [](auto& a, auto& b) { return a.first < b.first; });
                layer.clear();
                for (auto& s : scored) layer.push_back(s.second);

                // 贪心相邻交换：尝试交换减少与下层之间的交叉
                for (int sweep = 0; sweep < 2; sweep++) {
                    for (size_t i = 0; i + 1 < layer.size(); i++) {
                        Node* a = layer[i];
                        Node* b = layer[i + 1];
                        double crossBefore = 0, crossAfter = 0;
                        auto itA = out.find(a->id), itB = out.find(b->id);
                        if (itA != out.end() && itB != out.end()) {
                            for (auto& sa : itA->second) {
                                auto saPos = lowerPos.find(sa);
                                if (saPos == lowerPos.end()) continue;
                                for (auto& sb : itB->second) {
                                    auto sbPos = lowerPos.find(sb);
                                    if (sbPos == lowerPos.end()) continue;
                                    if (saPos->second > sbPos->second) crossBefore++;
                                    if (saPos->second < sbPos->second) crossAfter++;
                                }
                            }
                        }
                        if (crossAfter < crossBefore) {
                            std::swap(layer[i], layer[i + 1]);
                        }
                    }
                }
            }
        }
    }

    // ---- Brandes-Köpf 风格坐标分配 ----
    // 先用 barycenter 顺序做初始间距排放，再迭代优化使长边保持竖直
    double y = 40;
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        if (row.empty()) { y += 80; continue; }
        double totalW = 0;
        for (auto* n : row)
            totalW += (n->shape == "dummy" ? 0.0 : n->w) + 60;
        totalW -= 60;
        if (totalW < 0) totalW = 0;
        double x = -totalW / 2;
        for (auto* n : row) {
            n->x = x;
            n->y = y;
            x += (n->shape == "dummy" ? 0.0 : n->w) + 60;
        }
        double rowH = 0;
        for (auto* n : row)
            if (n->shape != "dummy")
                rowH = std::max(rowH, n->h);
        y += (rowH > 0 ? rowH : 44) + 80;
    }

    // 迭代优化：每个节点向其上下游邻居的加权中心靠拢，再扫一遍保证不重叠
    for (int iter = 0; iter < 5; iter++) {
        std::map<std::string, double> targetX;
        // 逐层计算目标位置
        for (int r = 0; r <= maxRank; r++) {
            for (auto* n : byRank[r]) {
                if (n->shape == "dummy") continue;
                double sum = 0; int cnt = 0;
                auto pi = preds.find(n->id);
                if (pi != preds.end()) {
                    for (auto& pid : pi->second) {
                        Node* pn = g.findNode(pid);
                        if (!pn) {  // 可能是虚拟节点
                            for (auto& dn : dummyNodes)
                                if (dn.id == pid) { pn = &dn; break; }
                        }
                        if (pn) { sum += pn->x + pn->w / 2; cnt++; }
                    }
                }
                auto si = out.find(n->id);
                if (si != out.end()) {
                    for (auto& sid : si->second) {
                        Node* sn = g.findNode(sid);
                        if (!sn) {
                            for (auto& dn : dummyNodes)
                                if (dn.id == sid) { sn = &dn; break; }
                        }
                        if (sn) { sum += sn->x + sn->w / 2; cnt++; }
                    }
                }
                if (cnt > 0) targetX[n->id] = sum / cnt - n->w / 2;
            }
        }
        // 应用目标位置
        for (int r = 0; r <= maxRank; r++) {
            for (auto* n : byRank[r]) {
                auto tx = targetX.find(n->id);
                if (tx != targetX.end())
                    n->x = n->x * 0.4 + tx->second * 0.6;  // blend
            }
        }
        // 左到右扫描保证最小间距
        for (int r = 0; r <= maxRank; r++) {
            auto& row = byRank[r];
            if (row.size() <= 1) continue;
            std::sort(row.begin(), row.end(),
                [](Node* a, Node* b) { return a->x < b->x; });
            for (size_t i = 1; i < row.size(); i++) {
                double minSep = (row[i-1]->shape == "dummy") ? 1.0 :
                                row[i-1]->w + 60;
                double need = row[i-1]->x + minSep;
                if (row[i]->x < need) row[i]->x = need;
            }
        }
    }

    // 虚拟节点 x 坐标用线性插值回填（始终位于源和目标之间的直线上）
    for (auto& e : g.edges) {
        auto dit = edgeDummies.find(e.id);
        if (dit == edgeDummies.end()) continue;
        const Node* src = g.findNode(e.from);
        const Node* dst = g.findNode(e.to);
        if (!src || !dst) continue;
        auto& dummies = dit->second;
        int   span    = (int)dummies.size() + 1;
        for (size_t k = 0; k < dummies.size(); k++) {
            double t = (double)(k + 1) / span;
            double ix = src->x + src->w / 2 + (dst->x + dst->w / 2 - src->x - src->w / 2) * t;
            for (auto& dn : dummyNodes) {
                if (dn.id == dummies[k]) {
                    dn.x = ix - dn.w / 2;
                    break;
                }
            }
        }
    }

    // 将虚拟节点坐标持久化到 Edge.waypoints，供 SVG 导出器做折线路由
    for (auto& e : g.edges) {
        auto dit = edgeDummies.find(e.id);
        if (dit == edgeDummies.end()) continue;
        e.waypoints.clear();
        e.waypoints.reserve(dit->second.size());
        for (auto& dummyId : dit->second) {
            for (auto& dn : dummyNodes) {
                if (dn.id == dummyId) {
                    e.waypoints.push_back({dn.x + dn.w / 2.0, dn.y});
                    break;
                }
            }
        }
    }

    // 清理：从 byRank 中踢掉虚拟节点（group 回填等步骤不应该看到它们）
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        row.erase(std::remove_if(row.begin(), row.end(),
            [](Node* n) { return n->shape == "dummy"; }), row.end());
    }
    // 根据成员节点边界回填 group 容器尺寸
    for (auto& grp : g.nodes) {
        if (grp.shape != "group")
            continue;
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        bool   any = false;
        for (auto& n : g.nodes) {
            if (n.parent != grp.id || n.shape == "group")
                continue;
            any  = true;
            minX = std::min(minX, n.x);
            minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x + n.w);
            maxY = std::max(maxY, n.y + n.h);
        }
        if (any) {
            grp.x = minX - 20;
            grp.y = minY - 36;
            grp.w = maxX - minX + 40;
            grp.h = maxY - minY + 56;
        }
    }
}

// layoutTree: 树布局入口（horizontal=true 为左右，false 为上下）
inline void layoutTree(Graph& g, bool horizontal)
{
    detail::TreeLayout    tl(g, horizontal);
    std::set<std::string> hasParent;
    for (auto& n : g.nodes)
        if (!n.parent.empty())
            hasParent.insert(n.id);
    double offset = 40;
    for (auto& n : g.nodes) {
        if (hasParent.count(n.id))
            continue;  // 非根节点
        double ext = tl.place(n.id, 40, offset);
        offset += ext + 60;
    }
}

// 无结构图的网格兜底布局
// layoutGrid: 无边结构的兜底网格布局
inline void layoutGrid(Graph& g)
{
    int cols = (int)std::max(1.0, std::ceil(std::sqrt((double)g.nodes.size())));
    int i    = 0;
    for (auto& n : g.nodes) {
        n.x = 40 + (i % cols) * 220;
        n.y = 40 + (i / cols) * 120;
        i++;
    }
}

// 入口：按图类型选择布局策略，并先补齐节点尺寸
// layout: 总布局入口（按图类型或显式策略选择）
// 关键步骤：补默认尺寸 -> 策略分发 -> 坐标归一化到正区间
inline void layout(Graph& g, bool force = false,
                   const std::string& strategy = "") {
    // rawMermaid 透传类型无节点坐标可布局，直接标记完成
    if (!g.rawMermaid.empty()) {
        g.laidOut = true;
        return;
    }
    if (g.laidOut && !force)
        return;
    for (auto& n : g.nodes)
        if (n.w <= 0 || n.h <= 0)
            gm::defaultSize(n);
    if (strategy == "layered")
        layoutLayered(g);
    else if (strategy == "tree-h")
        layoutTree(g, true);
    else if (strategy == "tree-v")
        layoutTree(g, false);
    else if (strategy == "grid")
        layoutGrid(g);
    else if (strategy == "auto" || strategy.empty()) {
        if (g.type == "mindmap")
            layoutTree(g, true);
        else if (g.type == "orgchart")
            layoutTree(g, false);
        else if (g.edges.empty())
            layoutGrid(g);
        else
            layoutLayered(g);
    }
    else {
        layoutLayered(g);  // 未知策略回退
    }
    // 坐标归一化到正区间
    double minX = 1e18, minY = 1e18;
    for (auto& n : g.nodes) {
        minX = std::min(minX, n.x);
        minY = std::min(minY, n.y);
    }
    if (!g.nodes.empty() && (minX < 20 || minY < 20)) {
        double dx = 20 - minX, dy = 20 - minY;
        for (auto& n : g.nodes) {
            n.x += dx;
            n.y += dy;
        }
    }
    g.laidOut = true;
}

}  // namespace gl
