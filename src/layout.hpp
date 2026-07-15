// layout.hpp - 基础自动布局 + 图结构校验
#pragma once
#include "model.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <queue>
#include <set>

namespace gl {

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
    // 状态图起始/终止标记 [*]：解析器不建 Node，但边端点可合法引用
    if (g.type == "stateDiagram")
        endpointIds.insert("[*]");
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

// ---- Sugiyama Phase 1: DFS 去环 -------------------------------------------
// CycleInfo: 记录去环操作以便后续恢复
struct LayoutCycleInfo
{
    // 被反转的边（from/to 已交换），key=(原from, 原to)
    std::set<std::pair<std::string, std::string>> reversed;
    std::set<std::string> loopNodes;  // 有自环的节点
};

// layoutRemoveCycles: DFS 检测环并反转边打破环路
// 关键步骤：DFS 三色标记 -> 收集反馈边 -> 反转边方向
inline LayoutCycleInfo layoutRemoveCycles(Graph& g)
{
    std::map<std::string, int> state;  // 0=unvisited, 1=in_progress, 2=done
    for (auto& n : g.nodes)
        state[n.id] = 0;

    // 构建出边索引：node_id -> 边在 g.edges 中的下标列表
    std::map<std::string, std::vector<size_t>> outIdx;
    for (size_t i = 0; i < g.edges.size(); i++)
        outIdx[g.edges[i].from].push_back(i);

    LayoutCycleInfo              info;
    std::vector<size_t>          toReverse;  // 待反转的边下标
    std::map<std::string, bool>  visited;

    std::function<void(const std::string&)> dfs = [&](const std::string& u) {
        state[u]      = 1;  // in_progress
        visited[u]    = true;
        for (size_t ei : outIdx[u]) {
            auto&       e = g.edges[ei];
            std::string v = e.to;
            if (u == v) {  // 自环
                info.loopNodes.insert(u);
                continue;
            }
            if (!visited.count(v)) {
                dfs(v);
            }
            else if (state.count(v) && state[v] == 1) {
                // 回边 -> 环，反转此边
                info.reversed.insert({u, v});
                toReverse.push_back(ei);
            }
        }
        state[u] = 2;  // done
    };

    for (auto& n : g.nodes)
        if (!visited.count(n.id))
            dfs(n.id);

    // 执行反转
    for (size_t ei : toReverse)
        std::swap(g.edges[ei].from, g.edges[ei].to);

    return info;
}

// layoutRestoreCycles: 布局完成后恢复被反转的边
inline void layoutRestoreCycles(Graph& g, LayoutCycleInfo& info)
{
    for (auto& e : g.edges) {
        auto rev = std::make_pair(e.to, e.from);
        if (info.reversed.count(rev)) {
            std::swap(e.from, e.to);
            info.reversed.erase(rev);
        }
    }
}

// ---- Sugiyama Phase 2: 最长路径分层 ----------------------------------------
// layoutLongestPathLayers: 将 DAG 节点分配到层（最长路径算法）
// 返回 (rank map, maxRank)
inline std::pair<std::map<std::string, int>, int>
layoutLongestPathLayers(Graph& g)
{
    // 构建入度 + 出边邻接
    std::map<std::string, int>                      indeg;
    std::map<std::string, std::vector<std::string>> out;
    for (auto& n : g.nodes) {
        indeg[n.id] = 0;
        out[n.id]   = {};
    }
    for (auto& e : g.edges) {
        if (indeg.count(e.to) && e.from != e.to) {
            indeg[e.to]++;
            out[e.from].push_back(e.to);
        }
    }

    std::map<std::string, int> rank;
    std::queue<std::string>    q;

    // 入度为 0 的源点 → rank 0
    for (auto& n : g.nodes)
        if (indeg[n.id] == 0) {
            rank[n.id] = 0;
            q.push(n.id);
        }

    // 如果没有入度为 0 的节点（全是环），取第一个节点作为起点
    if (q.empty() && !g.nodes.empty()) {
        rank[g.nodes[0].id] = 0;
        q.push(g.nodes[0].id);
    }

    int maxRank = 0;
    while (!q.empty()) {
        std::string u = q.front();
        q.pop();
        for (auto& v : out[u]) {
            int  cand = rank[u] + 1;
            auto it   = rank.find(v);
            if (it == rank.end() || cand > it->second) {
                rank[v] = cand;
                maxRank = std::max(maxRank, cand);
            }
            if (--indeg[v] == 0)
                q.push(v);
        }
    }

    // 处理仍未分配 rank 的节点（理论上不应该出现，兜底）
    for (auto& n : g.nodes)
        if (!rank.count(n.id))
            rank[n.id] = 0;

    return {rank, maxRank};
}

// ---- Sugiyama Phase 2.5: 层压缩（反向扫描，节点往前提）---------------------
// layoutCompactLayers: 对每个节点尝试前移，减少总层数 + 增加层密度
inline void layoutCompactLayers(std::map<std::string, int>& rank,
                                const Graph& g, int& maxRank)
{
    // 构建前驱索引
    std::map<std::string, std::vector<std::string>> pred;
    for (auto& e : g.edges)
        if (rank.count(e.from) && rank.count(e.to))
            pred[e.to].push_back(e.from);

    // 按 rank 排序节点，从低到高处理
    std::vector<std::pair<int, std::string>> order;
    for (auto& [id, r] : rank)
        order.push_back({r, id});
    std::sort(order.begin(), order.end());

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [r, id] : order) {
            int minR = 0;
            for (auto& p : pred[id]) {
                auto it = rank.find(p);
                if (it != rank.end())
                    minR = std::max(minR, it->second + 1);
            }
            if (minR < rank[id]) {
                rank[id] = minR;
                changed  = true;
            }
        }
    }

    // 重新规范化 rank 消除空层
    std::map<int, int> remap;
    int                nextR = 0;
    for (auto& [r, id] : order) {
        int rr = rank[id];
        if (!remap.count(rr))
            remap[rr] = nextR++;
        rank[id] = remap[rr];
    }
    maxRank = nextR - 1;

    // 合并稀疏相邻层
    if (maxRank > 1) {
        std::map<int, std::vector<std::string>> layers;
        for (auto& [id, r] : rank) layers[r].push_back(id);
        for (int r = maxRank; r > 0; r--) {
            int cntAbove = (int)layers[r - 1].size();
            int cntCur   = (int)layers[r].size();
            if (cntAbove > 5 || cntCur > 5) continue;
            bool canMerge = true;
            for (auto& id : layers[r]) {
                for (auto& e : g.edges)
                    if (e.to == id && rank.count(e.from) &&
                        rank[e.from] >= r - 1) { canMerge = false; break; }
                if (!canMerge) break;
            }
            if (canMerge) {
                for (auto& id : layers[r]) rank[id] = r - 1;
                layers[r - 1].insert(layers[r - 1].end(),
                                     layers[r].begin(), layers[r].end());
                layers.erase(r);
            }
        }
        remap.clear(); nextR = 0;
        for (auto& [r, ids] : layers) {
            remap[r] = nextR++;
            for (auto& id : ids) rank[id] = remap[r];
        }
        maxRank = nextR - 1;
    }
}

// ---- Sugiyama Phase 3: 重心启发式减交叉 ------------------------------------
// layoutReduceCrossings: 对已分层的节点按重心重排序以减少边交叉
// byRank: rank -> 该层节点指针列表（原地修改顺序）
// rank:   node_id -> rank
inline void layoutReduceCrossings(
    std::map<int, std::vector<gm::Node*>>& byRank,
    const std::map<std::string, int>&       rank,
    const Graph&                            g,
    int                                     maxRank)
{
    if (maxRank < 1)
        return;  // 单层无需减交叉

    // 计算节点 u 在相邻层的邻居平均位置（重心）
    auto barycenter = [&](const std::string& u, int adjRank,
                          bool useOut) -> double {
        double                 sum = 0;
        int                    cnt = 0;
        const auto&            adjRow = byRank[adjRank];
        std::set<std::string>  seen;
        for (auto& e : g.edges) {
            if (useOut && e.from == u && rank.count(e.to) &&
                rank.at(e.to) == adjRank) {
                if (seen.insert(e.to).second) {
                    // 找到 e.to 在 adjRow 中的位置
                    for (size_t i = 0; i < adjRow.size(); i++) {
                        if (adjRow[i]->id == e.to) {
                            sum += (double)i;
                            cnt++;
                            break;
                        }
                    }
                }
            }
            else if (!useOut && e.to == u && rank.count(e.from) &&
                     rank.at(e.from) == adjRank) {
                if (seen.insert(e.from).second) {
                    for (size_t i = 0; i < adjRow.size(); i++) {
                        if (adjRow[i]->id == e.from) {
                            sum += (double)i;
                            cnt++;
                            break;
                        }
                    }
                }
            }
        }
        if (cnt == 0)
            return -1.0;  // 无邻居，保持原位
        return sum / (double)cnt;
    };

    // 主体：交替扫描，最多 24 轮，连续 4 轮无改进则停止
    int  bestCross = INT_MAX;
    int  noImprove = 0;
    auto saveOrder = byRank;  // 保存最佳顺序

    for (int iter = 0; iter < 24 && noImprove < 4; iter++) {
        bool downward = (iter % 2 == 0);

        if (downward) {
            // 自上而下：用上层邻居排序当前层
            for (int r = 1; r <= maxRank; r++) {
                auto& row = byRank[r];
                std::stable_sort(row.begin(), row.end(),
                    [&](gm::Node* a, gm::Node* b) {
                        double ba = barycenter(a->id, r - 1, false);
                        double bb = barycenter(b->id, r - 1, false);
                        if (ba < 0)
                            ba = 1e9;
                        if (bb < 0)
                            bb = 1e9;
                        return ba < bb;
                    });
            }
        }
        else {
            // 自下而上：用下层邻居排序当前层
            for (int r = maxRank - 1; r >= 0; r--) {
                auto& row = byRank[r];
                std::stable_sort(row.begin(), row.end(),
                    [&](gm::Node* a, gm::Node* b) {
                        double ba = barycenter(a->id, r + 1, true);
                        double bb = barycenter(b->id, r + 1, true);
                        if (ba < 0)
                            ba = 1e9;
                        if (bb < 0)
                            bb = 1e9;
                        return ba < bb;
                    });
            }
        }

        // 简易交叉计数（相邻层间）
        int cross = 0;
        for (int r = 0; r < maxRank; r++) {
            auto& upper = byRank[r];
            auto& lower = byRank[r + 1];
            std::map<std::string, int> upPos;
            for (size_t i = 0; i < upper.size(); i++)
                upPos[upper[i]->id] = (int)i;
            for (size_t i = 0; i < lower.size(); i++) {
                for (size_t j = i + 1; j < lower.size(); j++) {
                    for (auto& e : g.edges) {
                        if (e.from == lower[i]->id &&
                            upPos.count(e.to)) {
                            for (auto& e2 : g.edges) {
                                if (e2.from == lower[j]->id &&
                                    upPos.count(e2.to) &&
                                    upPos[e2.to] < upPos[e.to]) {
                                    cross++;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (cross < bestCross) {
            bestCross = cross;
            noImprove = 0;
            saveOrder = byRank;
        }
        else {
            noImprove++;
        }
    }

    byRank = saveOrder;  // 恢复最佳顺序
}

// ---- Sugiyama Phase 4: 迭代力导向坐标精炼 --------------------------------
// layoutRefineCoords: 迭代将节点拉向邻居平均 x，自然对齐+减少长边
inline void layoutRefineCoords(
    std::map<int, std::vector<gm::Node*>>& byRank,
    const Graph&                            g,
    int                                     maxRank,
    double                                  gapX)
{
    if (maxRank < 0) return;

    // 第一步：初始 x-center 放置（左→右，每层居中）
    std::map<std::string, double> xCenter;
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        if (row.empty()) continue;
        double cursor = 0;
        for (auto* n : row) {
            xCenter[n->id] = cursor + n->w / 2;
            cursor += n->w + gapX;
        }
        double totalW = cursor - gapX;
        double offset = -totalW / 2;
        for (auto* n : row) xCenter[n->id] += offset;
    }

    // 第二步：迭代力导向精炼（阻尼因子 0.3，6轮保留水平展开）
    for (int iter = 0; iter < 6; iter++) {
        std::map<std::string, double> newX;
        for (int r = 0; r <= maxRank; r++) {
            for (auto* n : byRank[r]) {
                double sumX = 0;
                int    count = 0;
                for (auto& e : g.edges) {
                    if (e.from == n->id && xCenter.count(e.to))
                        { sumX += xCenter[e.to]; count++; }
                    if (e.to == n->id && xCenter.count(e.from))
                        { sumX += xCenter[e.from]; count++; }
                }
                if (count > 0) {
                    double target = sumX / (double)count;
                    newX[n->id] = xCenter[n->id] * 0.7 + target * 0.3;
                } else {
                    newX[n->id] = xCenter[n->id];
                }
            }
        }
        xCenter = newX;
    }

    // 第三步：层内去重叠，保持排序顺序
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        if (row.empty()) continue;
        // 按 xCenter 排序
        std::vector<std::pair<double, gm::Node*>> sorted;
        for (auto* n : row) sorted.push_back({xCenter[n->id], n});
        std::sort(sorted.begin(), sorted.end());
        // 去重叠：左→右推
        double cursor = sorted[0].first - sorted[0].second->w / 2;
        for (auto& [xc, n] : sorted) {
            double desired = xc - n->w / 2;
            n->x    = std::max(cursor, desired);
            cursor  = n->x + n->w + gapX;
        }
        if (row.size() == 1 && maxRank > 2)
            row[0]->x += (r % 2 == 0 ? -24.0 : 24.0);
    }
}

// ---- Phase 5: 1:1 边垂直对齐 + 边-节点重叠消解 --------------------------
// layoutAlignUniqueEdges: 对唯一的跨层边，强制垂直对齐
// 当 u 只有一条出边到下一层，且 v 只有一条入边来自上一层，则 center_x 对齐
inline void layoutAlignUniqueEdges(
    std::map<int, std::vector<gm::Node*>>& byRank,
    const std::map<std::string, int>&       rank,
    const Graph&                            g,
    double                                  gapX)
{
    // 统计每个节点的跨层出入度
    std::map<std::string, int> outDeg, inDeg;  // 向下一层的出度 / 向上一层的入度
    std::map<std::string, std::string> outTarget, inSource;  // 唯一邻居

    for (auto& e : g.edges) {
        if (!rank.count(e.from) || !rank.count(e.to)) continue;
        int dr = rank.at(e.to) - rank.at(e.from);
        if (dr == 1) {
            outDeg[e.from]++;
            outTarget[e.from] = e.to;
            inDeg[e.to]++;
            inSource[e.to] = e.from;
        }
    }

    // 找 1:1 边对
    std::vector<std::pair<gm::Node*, gm::Node*>> alignPairs;  // (upper, lower)
    for (auto& [u, od] : outDeg) {
        if (od != 1) continue;
        std::string v = outTarget[u];
        if (inDeg[v] != 1) continue;
        if (inSource[v] != u) continue;
        // u→v 是唯一的跨层边连接；用 rank 做 O(1) 层查找
        auto itUR = rank.find(u), itVR = rank.find(v);
        if (itUR == rank.end() || itVR == rank.end()) continue;
        Node* un = nullptr, *vn = nullptr;
        for (auto* n : byRank.at(itUR->second))
            if (n->id == u) { un = n; break; }
        for (auto* n : byRank.at(itVR->second))
            if (n->id == v) { vn = n; break; }
        if (un && vn) alignPairs.push_back({un, vn});
    }

    // 对齐中心 x（不移已经产生重叠）
    for (auto& [un, vn] : alignPairs) {
        double cx   = un->x + un->w / 2;
        double vcx  = vn->x + vn->w / 2;
        double diff = vcx - cx;
        if (std::abs(diff) < 1) continue;

        // 移动 v 层节点（保持该层不重叠）
        int vRank = rank.at(vn->id);
        auto& row = byRank[vRank];
        std::vector<std::pair<double, gm::Node*>> sorted;
        for (auto* n : row)
            sorted.push_back({n->x, n});
        std::sort(sorted.begin(), sorted.end());

        // 收集所有 x 值便于判断间距
        double newVx = cx - vn->w / 2;
        // 确保 v 不与同层节点重叠
        bool blocked = false;
        for (auto& [sx, sn] : sorted) {
            if (sn->id == vn->id) continue;
            if (newVx + vn->w + gapX > sn->x &&
                newVx < sn->x + sn->w + gapX) {
                blocked = true;
                break;
            }
        }
        if (!blocked) {
            vn->x = newVx;
            // 整层重新左→右推进（消除所有潜在重叠）
            double cursor = -1e18;
            for (auto& [sx, sn] : sorted) {
                double desired = sn->x;  // 保留现有位置（含刚修改的）
                sn->x   = std::max(cursor, desired);
                cursor  = sn->x + sn->w + gapX;
            }
        }
    }
}

// ---- 最终兜底：按 y 分组强制去重叠 ---------------------------------------
// layoutForceNoOverlap: 所有阶段结束后，按实际 y 坐标分组做左→右推进
// 确保无论前序逻辑如何，同层节点绝对不重叠
inline void layoutForceNoOverlap(Graph& g, double gapX)
{
    // 按 y 分组（取整容差 2px，同层节点 y 必定相等）
    std::map<int, std::vector<gm::Node*>> buckets;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        buckets[(int)std::round(n.y)].push_back(&n);
    }
    for (auto& [y, row] : buckets) {
        if (row.size() < 2)
            continue;
        // 按 x 排序
        std::sort(row.begin(), row.end(),
                  [](gm::Node* a, gm::Node* b) { return a->x < b->x; });
        // 左→右强制推进
        double cursor = row[0]->x;
        for (auto* n : row) {
            if (n->x < cursor)
                n->x = cursor;
            cursor = n->x + n->w + gapX;
        }
    }
}

// 分层布局（流程类图）：Sugiyama 算法（去环 + 最长路径分层 + 重心减交叉 + 坐标精炼 + 垂直对齐）
inline void layoutLayered(Graph& g)
{
    // Phase 1: DFS 去环
    LayoutCycleInfo cycleInfo = layoutRemoveCycles(g);

    // Phase 2: 最长路径分层
    auto [rank, maxRank] = layoutLongestPathLayers(g);

    // Phase 2.5: 层压缩（减少空层，增加层密度）
    layoutCompactLayers(rank, g, maxRank);

    // 按 rank 分组（排除 group 容器节点，它们后处理）
    std::map<int, std::vector<gm::Node*>> byRank;
    for (auto& n : g.nodes)
        if (n.shape != "group")
            byRank[rank[n.id]].push_back(&n);

    // Phase 3: 重心减交叉
    layoutReduceCrossings(byRank, rank, g, maxRank);

    // Phase 4: 迭代力导向坐标精炼（含初始排列+y 分配）
    layoutRefineCoords(byRank, g, maxRank, 60.0);

    // Phase 5: 1:1 跨层边垂直对齐（消除长斜边穿过节点）
    layoutAlignUniqueEdges(byRank, rank, g, 60.0);

    double gapY = (maxRank > 10) ? std::max(48.0, 80.0 - (maxRank - 10) * 3.0) : 80.0;
    double y = 40;
    for (int r = 0; r <= maxRank; r++) {
        auto& row = byRank[r];
        if (row.empty()) continue;
        double rowH = 0;
        for (auto* n : row) {
            n->y = y;
            rowH = std::max(rowH, n->h);
        }
        y += rowH + gapY;
    }

    // 最终兜底：按 y 分组强制去重叠
    layoutForceNoOverlap(g, 60.0);

    // Phase 6: 恢复被反转的边
    layoutRestoreCycles(g, cycleInfo);
}

// ---- Group 递归包裹定位（所有布局策略共用）------------------------------
// layoutPlaceGroups: 自底向上递归计算 group 边界框
// 叶 group 包裹非 group 子节点，父 group 包裹子 group
inline void layoutPlaceGroups(Graph& g)
{
    // 构建 parent → 直接子节点列表
    std::map<std::string, std::vector<gm::Node*>> children;
    for (auto& n : g.nodes) {
        if (!n.parent.empty())
            children[n.parent].push_back(&n);
    }

    // 收集所有 group 节点
    std::map<std::string, gm::Node*> groups;
    for (auto& n : g.nodes)
        if (n.shape == "group")
            groups[n.id] = &n;

    if (groups.empty())
        return;

    // 计算 group 深度（最长 parent 链长度），从叶到根排序
    std::map<std::string, int> depth;
    std::function<int(const std::string&)> calcDepth =
        [&](const std::string& id) -> int {
        if (depth.count(id))
            return depth[id];
        int d = 0;
        auto* n = g.findNode(id);
        if (n && !n->parent.empty() && groups.count(n->parent))
            d = calcDepth(n->parent) + 1;
        depth[id] = d;
        return d;
    };
    for (auto& [id, _] : groups)
        calcDepth(id);

    // 按深度降序排列 group（叶先处理）
    std::vector<std::pair<int, std::string>> order;
    for (auto& [id, d] : depth)
        order.push_back({d, id});
    std::sort(order.rbegin(), order.rend());

    // 自底向上：每个 group 包裹直接子节点 + 已就位的子 group
    for (auto& [d, gid] : order) {
        gm::Node* grp = groups[gid];
        double    minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        bool      any  = false;
        for (auto* c : children[gid]) {
            if (c->shape == "group") {
                // 子 group 已有尺寸，直接使用
                if (c->w <= 0 && c->h <= 0) continue;  // skip empty groups
                any  = true;
                minX = std::min(minX, c->x);
                minY = std::min(minY, c->y);
                maxX = std::max(maxX, c->x + c->w);
                maxY = std::max(maxY, c->y + c->h);
            } else {
                // 普通节点
                any  = true;
                minX = std::min(minX, c->x);
                minY = std::min(minY, c->y);
                maxX = std::max(maxX, c->x + c->w);
                maxY = std::max(maxY, c->y + c->h);
            }
        }
        if (any) {
            grp->x = minX - 20;
            grp->y = minY - 36;
            grp->w = maxX - minX + 40;
            grp->h = maxY - minY + 56;
        }
    }

    // 同 parent 的兄弟 group 去重叠（横向展开，子节点跟随位移）
    // 递归位移 group 的所有后代
    std::function<void(const std::string&, double, double)> shiftTree;
    shiftTree = [&](const std::string& gid, double dx, double dy) {
        for (auto* c : children[gid]) {
            c->x += dx;
            c->y += dy;
            if (c->shape == "group")
                shiftTree(c->id, dx, dy);
        }
    };

    std::function<void(const std::string&)> deoverlapSiblings =
        [&](const std::string& pid) {
        std::vector<gm::Node*> sibs;
        for (auto& n : g.nodes)
            if (n.shape == "group" && n.parent == pid)
                sibs.push_back(&n);
        if (sibs.size() < 2)
            return;
        // 先处理子 group 内部的兄弟去重叠
        for (auto* s : sibs)
            deoverlapSiblings(s->id);
        // 按 x 排序兄弟 group
        std::sort(sibs.begin(), sibs.end(),
            [](gm::Node* a, gm::Node* b) { return a->x < b->x; });
        // 水平去重叠：gap=40，子节点跟随位移
        double cursor = sibs[0]->x;
        for (auto* s : sibs) {
            if (s->x < cursor) {
                double dx = cursor - s->x;
                shiftTree(s->id, dx, 0);
                s->x = cursor;
            }
            cursor = s->x + s->w + 40;
        }
    };

    // 对根层级执行兄弟去重叠（一次调用处理所有根 group）
    deoverlapSiblings("");
}

// ---- 最终复查：全量节点重叠检测修复 ---------------------------------------
inline int layoutFinalOverlapCheck(Graph& g, double gapX)
{
    int fixes = 0;
    std::vector<gm::Node*> nodes;
    for (auto& n : g.nodes)
        if (n.shape != "group") nodes.push_back(&n);
    bool changed = true;
    while (changed) {
        changed = false;
        std::sort(nodes.begin(), nodes.end(), [](gm::Node* a, gm::Node* b) {
            if (std::abs(a->y - b->y) > 2) return a->y < b->y;
            return a->x < b->x;
        });
        for (size_t i = 0; i < nodes.size(); i++) {
            for (size_t j = i + 1; j < nodes.size(); j++) {
                gm::Node* a = nodes[i], *b = nodes[j];
                if (b->y - a->y > 64) break;
                if (std::abs(a->y - b->y) > 60) continue;
                if (a->x + a->w + gapX > b->x &&
                    b->x + b->w + gapX > a->x &&
                    a->y + a->h > b->y && b->y + b->h > a->y) {
                    b->x = a->x + a->w + gapX;
                    changed = true; fixes++;
                }
            }
        }
    }
    return fixes;
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
// layoutGrid: 无边结构的兜底网格布局（自适应列宽）
inline void layoutGrid(Graph& g)
{
    int cols = (int)std::max(1.0, std::ceil(std::sqrt((double)g.nodes.size())));
    // 第一遍：统计每列最大宽度
    std::map<int, double> colW;
    int                   idx = 0;
    for (auto& n : g.nodes) {
        int c = idx % cols;
        colW[c] = std::max(colW[c], n.w);
        idx++;
    }
    // 计算列起始 x
    std::map<int, double> colX;
    double                x = 40;
    for (int c = 0; c < cols; c++) {
        colX[c] = x;
        x += colW[c] + 60;
    }
    // 第二遍：放置节点
    idx = 0;
    for (auto& n : g.nodes) {
        n.x = colX[idx % cols];
        n.y = 40 + (idx / cols) * 120;
        idx++;
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
    // 递归 group 包裹定位（所有策略共用）
    layoutPlaceGroups(g);
    // 最终复查：全量节点重叠检测修复
    layoutFinalOverlapCheck(g, 60.0);
    // 宽高比平衡：过宽时纵向堆叠 group，过窄时水平扩展
    if (!g.nodes.empty()) {
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (auto& n : g.nodes) {
            minX = std::min(minX, n.x); minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x + n.w); maxY = std::max(maxY, n.y + n.h);
        }
        double w = maxX - minX, h = maxY - minY;

        if (h > 0 && w / h > 3.0) {
            // 过宽 → 收集根 group，纵向堆叠
            std::vector<gm::Node*> rootGrps;
            std::set<std::string> allGrps;
            for (auto& n : g.nodes)
                if (n.shape == "group") allGrps.insert(n.id);
            for (auto& n : g.nodes)
                if (n.shape == "group" &&
                    (n.parent.empty() || !allGrps.count(n.parent)))
                    rootGrps.push_back(&n);
            if (rootGrps.size() > 1) {
                std::sort(rootGrps.begin(), rootGrps.end(),
                    [](gm::Node* a, gm::Node* b) { return a->x < b->x; });
                int cols = (int)std::ceil(std::sqrt((double)rootGrps.size()));
                double colW = 0;
                for (auto* g : rootGrps) colW = std::max(colW, g->w);
                for (int i = 0; i < (int)rootGrps.size(); i++) {
                    int cr = i / cols, cc = i % cols;
                    double dx = minX + cc * (colW + 60) - rootGrps[i]->x;
                    double dy = minY + cr * (h + 40) - rootGrps[i]->y;
                    if (dx != 0 || dy != 0) {
                        // 递归位移整个 group 子树
                        for (auto& nd : g.nodes) {
                            // 检查 nd 是否是 rootGrps[i] 的后代
                            std::string p = nd.parent;
                            bool isDesc = (p == rootGrps[i]->id);
                            while (!isDesc && !p.empty()) {
                                gm::Node* pn = g.findNode(p);
                                if (!pn) break;
                                if (pn->parent == rootGrps[i]->id)
                                    { isDesc = true; break; }
                                p = pn->parent;
                            }
                            if (isDesc || nd.id == rootGrps[i]->id)
                                { nd.x += dx; nd.y += dy; }
                        }
                    }
                }
            }
        } else if (h > 0 && w / h < 0.5) {
            // 过窄 → 水平扩展（上限 2.5x 防止过宽）
            double targetRatio = 0.5;
            double scale = targetRatio * h / std::max(w, 1.0);
            if (scale > 2.5) scale = 2.5;
            double cx = (minX + maxX) / 2;
            for (auto& n : g.nodes) n.x = cx + (n.x - cx) * scale;
        }

        // 重新居中
        minX = 1e18; minY = 1e18; maxX = -1e18; maxY = -1e18;
        for (auto& n : g.nodes) {
            minX = std::min(minX, n.x); minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x + n.w); maxY = std::max(maxY, n.y + n.h);
        }
        double dx = 40 - minX, dy = 40 - minY;
        for (auto& n : g.nodes) { n.x += dx; n.y += dy; }
    }
    g.laidOut = true;
}

}  // namespace gl
