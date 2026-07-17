// version_types.hpp - graphmcp 版本管理核心类型
// Draft / Stage / Commit 三层模型 + Selector 选择器
#pragma once
#include "model.hpp"
#include <ctime>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace gv {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

// ─── 安全 ID 校验 ─────────────────────────────────────────────────
// isValidId: 拒绝含路径遍历字符的标识符，避免文件操作逃逸存储目录
inline bool isValidId(const std::string& id)
{
    if (id.empty())
        return false;
    if (id[0] == '.')
        return false;
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        if (c == '/' || c == '\\' || c == '\0')
            return false;
        if (c == '.' && i + 1 < id.size() && id[i + 1] == '.')
            return false;
    }
    return true;
}

// ─── nowIso: 本地 ISO 时间戳 ────────────────────────────────────
inline std::string nowIso()
{
    time_t    t   = time(nullptr);
    struct tm tmv = {};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
}

// ─── OpType: 原子修改操作类型 ────────────────────────────────────
enum class OpType {
    UNKNOWN = -1,     // 无效操作类型（解析错误）
    NODE_INSERT,      // 插入节点
    NODE_UPDATE,      // 更新节点属性
    NODE_DELETE,      // 删除节点（级联删除关联边）
    EDGE_INSERT,      // 插入边
    EDGE_UPDATE,      // 更新边属性
    EDGE_DELETE,      // 删除边
    META_UPDATE,      // 更新图级元数据（name / type）
    PROPERTY_SET,     // 设置 properties 中的值
    PROPERTY_INSERT,  // 向 properties 数组插入元素
    PROPERTY_DELETE   // 从 properties 中删除元素
};

inline const char* opTypeName(OpType t)
{
    switch (t) {
        case OpType::UNKNOWN: return "UNKNOWN";
        case OpType::NODE_INSERT: return "NODE_INSERT";
        case OpType::NODE_UPDATE: return "NODE_UPDATE";
        case OpType::NODE_DELETE: return "NODE_DELETE";
        case OpType::EDGE_INSERT: return "EDGE_INSERT";
        case OpType::EDGE_UPDATE: return "EDGE_UPDATE";
        case OpType::EDGE_DELETE: return "EDGE_DELETE";
        case OpType::META_UPDATE: return "META_UPDATE";
        case OpType::PROPERTY_SET: return "PROPERTY_SET";
        case OpType::PROPERTY_INSERT: return "PROPERTY_INSERT";
        case OpType::PROPERTY_DELETE: return "PROPERTY_DELETE";
    }
    return "UNKNOWN";
}

inline OpType opTypeFromString(const std::string& s)
{
    if (s == "NODE_INSERT")
        return OpType::NODE_INSERT;
    if (s == "NODE_UPDATE")
        return OpType::NODE_UPDATE;
    if (s == "NODE_DELETE")
        return OpType::NODE_DELETE;
    if (s == "EDGE_INSERT")
        return OpType::EDGE_INSERT;
    if (s == "EDGE_UPDATE")
        return OpType::EDGE_UPDATE;
    if (s == "EDGE_DELETE")
        return OpType::EDGE_DELETE;
    if (s == "META_UPDATE")
        return OpType::META_UPDATE;
    if (s == "PROPERTY_SET")
        return OpType::PROPERTY_SET;
    if (s == "PROPERTY_INSERT")
        return OpType::PROPERTY_INSERT;
    if (s == "PROPERTY_DELETE")
        return OpType::PROPERTY_DELETE;
    return OpType::UNKNOWN;
}

// ─── FieldChange: 单个字段的变更记录 ──────────────────────────────
struct FieldChange
{
    std::string field;     // 字段名：label / shape / style / x / y / w / h /
                           // parent / from / to / arrow
    std::string oldValue;  // 旧值（删除时为空）
    std::string newValue;  // 新值（插入时为空）

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("field", field);
        j.set("oldValue", oldValue);
        j.set("newValue", newValue);
        return j;
    }
    static FieldChange fromJson(const Json& j)
    {
        FieldChange fc;
        fc.field    = j.str("field");
        fc.oldValue = j.str("oldValue");
        fc.newValue = j.str("newValue");
        return fc;
    }
};

// ─── Operation: 一次原子修改操作 ─────────────────────────────────
struct Operation
{
    OpType                   type = OpType::NODE_UPDATE;
    std::string              targetId;             // 目标元素 id
    std::string              targetType = "node";  // "node" | "edge" | "graph"
    std::vector<FieldChange> changes;   // 字段变更明细（UPDATE 操作）
    Json                     snapshot;  // 插入时的完整元素快照（INSERT 操作）
    std::string path;       // JSON 路径（PROPERTY_SET/INSERT/DELETE 用）
    Json        value;      // 新值（PROPERTY_SET/INSERT 用）
    std::string timestamp;  // ISO 时间戳

    // 简短摘要（用于 CLI 展示）
    std::string summary() const
    {
        std::string s = std::string(opTypeName(type)) + " " + targetType;
        if (!targetId.empty())
            s += " " + targetId;
        if (!path.empty())
            s += " @ " + path;
        if (type == OpType::NODE_UPDATE || type == OpType::EDGE_UPDATE) {
            if (!changes.empty())
                s += " (" + std::to_string(changes.size()) + " fields)";
        }
        return s;
    }

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("type", opTypeName(type));
        j.set("targetId", targetId);
        j.set("targetType", targetType);
        if (!changes.empty()) {
            Json arr = Json::arr();
            for (auto& c : changes)
                arr.push(c.toJson());
            j.set("changes", arr);
        }
        if (snapshot.isObj() || snapshot.isArr())
            j.set("snapshot", snapshot);
        if (!path.empty())
            j.set("path", path);
        if (value.isObj() || value.isArr() || value.isStr() || value.isNum() ||
            value.isBool() || value.isNull())
            j.set("value", value);
        j.set("timestamp", timestamp);
        return j;
    }

    static Operation fromJson(const Json& j)
    {
        Operation op;
        op.type       = opTypeFromString(j.str("type"));
        op.targetId   = j.str("targetId");
        op.targetType = j.str("targetType", "node");
        if (const Json* ch = j.find("changes")) {
            if (ch->isArr())
                for (auto& c : *ch->a)
                    op.changes.push_back(FieldChange::fromJson(c));
        }
        if (const Json* sn = j.find("snapshot"))
            op.snapshot = *sn;
        op.path = j.str("path");
        if (const Json* v = j.find("value"))
            op.value = *v;
        op.timestamp = j.str("timestamp");
        return op;
    }
};

// ─── Draft: 工作草稿（增量操作序列）───────────────────────────────
// 不保存完整 Graph 副本，只保存从 base commit 到当前工作区的一系列 Operation。
struct Draft
{
    std::string            graphId;
    int                    baseVersion = 0;  // 基于哪个 commit 版本
    std::vector<Operation> operations;       // 修改操作序列
    std::string            updatedAt;
    // 崩溃恢复：save 前写入 inflight；若快照已含同 commitId 则裁剪草稿防重放
    std::string            inflightCommitId;
    std::vector<int>       inflightStagedIndices;

    bool isEmpty() const
    { return operations.empty(); }
    int operationCount() const
    { return (int)operations.size(); }

    // 从 base Graph 重建完整 Graph：base + apply(operations)
    Graph materialize(const Graph& base) const;

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("graphId", graphId);
        j.set("baseVersion", baseVersion);
        Json ops = Json::arr();
        for (auto& op : operations)
            ops.push(op.toJson());
        j.set("operations", ops);
        j.set("updatedAt", updatedAt);
        if (!inflightCommitId.empty()) {
            j.set("inflightCommitId", inflightCommitId);
            Json idx = Json::arr();
            for (int i : inflightStagedIndices)
                idx.push(Json((double)i));
            j.set("inflightStagedIndices", idx);
        }
        return j;
    }
    static Draft fromJson(const Json& j)
    {
        Draft d;
        d.graphId     = j.str("graphId");
        d.baseVersion = (int)j.num("baseVersion");
        if (const Json* ops = j.find("operations")) {
            if (ops->isArr())
                for (auto& o : *ops->a)
                    d.operations.push_back(Operation::fromJson(o));
        }
        d.updatedAt         = j.str("updatedAt");
        d.inflightCommitId  = j.str("inflightCommitId");
        if (const Json* idx = j.find("inflightStagedIndices")) {
            if (idx->isArr())
                for (auto& v : *idx->a)
                    d.inflightStagedIndices.push_back((int)v.n);
        }
        return d;
    }
};

// ─── Stage: 暂存区（operation 索引子集）───────────────────────────
struct Stage
{
    std::string      graphId;
    std::vector<int> stagedOpIndices;  // Draft.operations 中被选中的索引
    std::string      message;          // 预填的 commit message
    std::string      stagedAt;

    bool isEmpty() const
    { return stagedOpIndices.empty(); }

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("graphId", graphId);
        Json idx = Json::arr();
        for (int i : stagedOpIndices)
            idx.push(Json((double)i));
        j.set("stagedOpIndices", idx);
        j.set("message", message);
        j.set("stagedAt", stagedAt);
        return j;
    }
    static Stage fromJson(const Json& j)
    {
        Stage s;
        s.graphId = j.str("graphId");
        if (const Json* idx = j.find("stagedOpIndices")) {
            if (idx->isArr())
                for (auto& v : *idx->a)
                    s.stagedOpIndices.push_back((int)v.n);
        }
        s.message  = j.str("message");
        s.stagedAt = j.str("stagedAt");
        return s;
    }
};

// ─── Commit: 不可变版本快照 ──────────────────────────────────────
struct Commit
{
    int                    version = 0;
    int                    parent  = 0;  // 父版本号（0 = 根）
    std::string            commitId;     // 哈希（前 8 位）
    std::string            message;
    std::string            author;
    std::string            timestamp;
    std::vector<Operation> patch;          // 从 parent 到本版本的增量
    Json                   modelSnapshot;  // 完整模型快照（冗余，加速加载）

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("version", (double)version);
        j.set("parent", (double)parent);
        j.set("commitId", commitId);
        j.set("message", message);
        j.set("author", author);
        j.set("timestamp", timestamp);
        Json pa = Json::arr();
        for (auto& op : patch)
            pa.push(op.toJson());
        j.set("patch", pa);
        j.set("model", modelSnapshot);
        return j;
    }

    static Commit fromJson(const Json& j)
    {
        Commit c;
        c.version  = (int)j.num("version");
        c.parent   = (int)j.num("parent");
        c.commitId = j.str("commitId");
        c.message  = j.str("message");
        if (c.message.empty())
            c.message = j.str("note");  // storage 快照字段兼容
        c.author    = j.str("author");
        c.timestamp = j.str("timestamp");
        if (c.timestamp.empty())
            c.timestamp = j.str("savedAt");
        if (const Json* pa = j.find("patch")) {
            if (pa->isArr())
                for (auto& op : *pa->a)
                    c.patch.push_back(Operation::fromJson(op));
        }
        if (const Json* m = j.find("model"))
            c.modelSnapshot = *m;
        return c;
    }

    // 从 parent + patch 重建完整模型
    static Graph rebuild(const Graph&                  parentModel,
                         const std::vector<Operation>& patch);
};

// ─── VersionMeta: 轻量版本索引条目 ───────────────────────────────
struct VersionMeta
{
    int         version = 0;
    int         parent  = 0;
    std::string commitId;
    std::string message;
    std::string timestamp;
    int         nodeCount = 0;
    int         edgeCount = 0;
};

// ─── Selector: 元素筛选器 ────────────────────────────────────────
struct Selector
{
    enum class Kind {
        BY_ID,         // 按 id 精确匹配
        BY_TYPE,       // 按 shape 类型匹配（rect / diamond / circle ...）
        BY_LABEL,      // 按 label 精确或模糊匹配
        BY_PARENT,     // 按 parent id 匹配（选中子节点）
        ALL_NODES,     // 全部节点
        ALL_EDGES,     // 全部边
        CONNECTED_TO,  // 与指定节点相连的元素
    };

    Kind        kind = Kind::ALL_NODES;
    std::string value;          // 匹配值
    bool        regex = false;  // 模糊匹配（value 作为子串查找）

    // 工厂方法
    static Selector byId(const std::string& id)
    {
        Selector s;
        s.kind  = Kind::BY_ID;
        s.value = id;
        return s;
    }
    static Selector byType(const std::string& shape)
    {
        Selector s;
        s.kind  = Kind::BY_TYPE;
        s.value = shape;
        return s;
    }
    static Selector byLabel(const std::string& label, bool fuzzy = false)
    {
        Selector s;
        s.kind  = Kind::BY_LABEL;
        s.value = label;
        s.regex = fuzzy;
        return s;
    }
    static Selector byParent(const std::string& parentId)
    {
        Selector s;
        s.kind  = Kind::BY_PARENT;
        s.value = parentId;
        return s;
    }
    static Selector allNodes()
    {
        Selector s;
        s.kind = Kind::ALL_NODES;
        return s;
    }
    static Selector allEdges()
    {
        Selector s;
        s.kind = Kind::ALL_EDGES;
        return s;
    }
    static Selector connectedTo(const std::string& nodeId)
    {
        Selector s;
        s.kind  = Kind::CONNECTED_TO;
        s.value = nodeId;
        return s;
    }

    // 从 CLI 条件字符串解析：shape=rect | parent=g1 | label~=Step | id=A
    static Selector parse(const std::string& cond);
};

// ─── 辅助: 读取 Node 字段值 / 设置 Node 字段值 ──────────────────────

// formatCoord: 坐标/尺寸草稿序列化（最多 6 位有效小数，避免 to_string 噪声）
// 参数 v：数值；声明/实现均在本头文件
inline std::string formatCoord(double v)
{
    std::ostringstream oss;
    oss << std::setprecision(6) << std::defaultfloat << v;
    return oss.str();
}

inline std::string getNodeField(const Node& n, const std::string& field)
{
    if (field == "id")
        return n.id;
    if (field == "label")
        return n.label;
    if (field == "shape")
        return n.shape;
    if (field == "parent")
        return n.parent;
    if (field == "style")
        return n.style;
    if (field == "fillColor")
        return n.fillColor;
    if (field == "strokeColor")
        return n.strokeColor;
    if (field == "x")
        return formatCoord(n.x);
    if (field == "y")
        return formatCoord(n.y);
    if (field == "w")
        return formatCoord(n.w);
    if (field == "h")
        return formatCoord(n.h);
    return "";
}

// setNodeField: 设置节点字段；未知字段返回 false（不修改）
inline bool setNodeField(Node& n, const std::string& field, const std::string& val)
{
    if (field == "label")
        n.label = val;
    else if (field == "shape")
        n.shape = val;
    else if (field == "parent")
        n.parent = val;
    else if (field == "style")
        n.style = val;
    else if (field == "fillColor")
        n.fillColor = val;
    else if (field == "strokeColor")
        n.strokeColor = val;
    else if (field == "x")
        n.x = std::strtod(val.c_str(), nullptr);
    else if (field == "y")
        n.y = std::strtod(val.c_str(), nullptr);
    else if (field == "w")
        n.w = std::strtod(val.c_str(), nullptr);
    else if (field == "h")
        n.h = std::strtod(val.c_str(), nullptr);
    else
        return false;
    return true;
}

// waypointsToJsonString: 将边折点序列化为 JSON 数组字符串（草稿 FieldChange 用）
inline std::string waypointsToJsonString(
    const std::vector<std::pair<double, double>>& wps)
{
    Json arr = Json::arr();
    for (auto& wp : wps) {
        Json o = Json::obj();
        o.set("x", wp.first);
        o.set("y", wp.second);
        arr.push(o);
    }
    return arr.dump();
}

// parseWaypointsJson: 解析折点 JSON；成功返回 true 并写入 out
// 参数 err：可选错误信息输出（声明/实现均在本头文件）
inline bool parseWaypointsJson(const std::string& val,
                               std::vector<std::pair<double, double>>& out,
                               std::string* err = nullptr)
{
    out.clear();
    std::string perr;
    Json         j = Json::parse(val, &perr);
    if (!perr.empty() || !j.isArr()) {
        if (err)
            *err = perr.empty() ? "waypoints must be a JSON array" : perr;
        return false;
    }
    for (auto& item : *j.a) {
        if (item.isObj()) {
            out.push_back({item.num("x", 0.0), item.num("y", 0.0)});
        }
        else if (item.isArr() && item.a && item.a->size() >= 2) {
            const Json& jx = (*item.a)[0];
            const Json& jy = (*item.a)[1];
            double      x  = jx.isNum() ? jx.n : 0.0;
            double      y  = jy.isNum() ? jy.n : 0.0;
            out.push_back({x, y});
        }
        else {
            if (err)
                *err = "each waypoint must be {x,y} or [x,y]";
            out.clear();
            return false;
        }
    }
    return true;
}

// syncArrowFromHeads: 根据 headStart/headEnd 回填粗粒度兼容字段 arrow
// 说明：open/cross 等细粒度装饰映射为 arrow/both，仅供旧导出器；不反向抹掉 heads
inline void syncArrowFromHeads(Edge& e)
{
    if (e.headStart == "none" && e.headEnd == "none")
        e.arrow = "none";
    else if (e.headStart != "none" && e.headEnd != "none")
        e.arrow = "both";
    else
        e.arrow = "arrow";
}

// isDecoratedHead: 端点是否有非 none 装饰（含 open/cross/arrow）
inline bool isDecoratedHead(const std::string& h)
{
    return !h.empty() && h != "none";
}

// syncHeadsFromArrow: 由 legacy arrow 同步 heads；保留已有 open/cross，避免有损往返
inline void syncHeadsFromArrow(Edge& e)
{
    if (e.arrow == "none") {
        e.headStart = "none";
        e.headEnd   = "none";
        return;
    }
    if (e.arrow == "both") {
        // 仅补齐缺失端，不把 open/cross 降级为 arrow
        if (!isDecoratedHead(e.headStart))
            e.headStart = "arrow";
        if (!isDecoratedHead(e.headEnd))
            e.headEnd = "arrow";
        return;
    }
    // 单向箭头：末端有装饰、起点默认 none；保留已有 open/cross
    if (!isDecoratedHead(e.headEnd))
        e.headEnd = "arrow";
    if (e.headStart == "arrow" || e.headStart.empty())
        e.headStart = "none";
}

inline std::string getEdgeField(const Edge& e, const std::string& field)
{
    if (field == "id")
        return e.id;
    if (field == "from")
        return e.from;
    if (field == "to")
        return e.to;
    if (field == "label")
        return e.label;
    if (field == "style")
        return e.style;
    if (field == "arrow")
        return e.arrow;
    if (field == "strokeColor")
        return e.strokeColor;
    if (field == "headStart")
        return e.headStart;
    if (field == "headEnd")
        return e.headEnd;
    if (field == "labelX")
        return formatCoord(e.labelX);
    if (field == "labelY")
        return formatCoord(e.labelY);
    if (field == "waypoints")
        return waypointsToJsonString(e.waypoints);
    return "";
}

// setEdgeField: 设置边字段；未知字段或非法 waypoints 返回 false（不修改该字段）
// 参数 field/val：字段名与字符串值
inline bool setEdgeField(Edge& e, const std::string& field, const std::string& val)
{
    if (field == "from")
        e.from = val;
    else if (field == "to")
        e.to = val;
    else if (field == "label")
        e.label = val;
    else if (field == "style")
        e.style = val;
    else if (field == "arrow") {
        e.arrow = val;
        syncHeadsFromArrow(e);
    }
    else if (field == "strokeColor")
        e.strokeColor = val;
    else if (field == "headStart") {
        e.headStart = val;
        syncArrowFromHeads(e);
    }
    else if (field == "headEnd") {
        e.headEnd = val;
        syncArrowFromHeads(e);
    }
    else if (field == "labelX")
        e.labelX = std::strtod(val.c_str(), nullptr);
    else if (field == "labelY")
        e.labelY = std::strtod(val.c_str(), nullptr);
    else if (field == "waypoints") {
        std::vector<std::pair<double, double>> wps;
        if (!parseWaypointsJson(val, wps))
            return false;
        e.waypoints = std::move(wps);
    }
    else
        return false;
    return true;
}

// ─── Selector 匹配逻辑 ───────────────────────────────────────────

inline bool selectorMatchesNode(const Node& n, const Selector& sel)
{
    switch (sel.kind) {
        case Selector::Kind::BY_ID: return n.id == sel.value;
        case Selector::Kind::BY_TYPE: return n.shape == sel.value;
        case Selector::Kind::BY_LABEL:
            return sel.regex ? (n.label.find(sel.value) != std::string::npos) :
                               (n.label == sel.value);
        case Selector::Kind::BY_PARENT: return n.parent == sel.value;
        case Selector::Kind::ALL_NODES: return true;
        case Selector::Kind::CONNECTED_TO:
            return false;  // 关联匹配由外部边遍历完成
        default: return false;
    }
}

inline bool selectorMatchesEdge(const Edge& e, const Selector& sel)
{
    switch (sel.kind) {
        case Selector::Kind::BY_ID: return e.id == sel.value;
        case Selector::Kind::BY_LABEL:
            return sel.regex ? (e.label.find(sel.value) != std::string::npos) :
                               (e.label == sel.value);
        case Selector::Kind::ALL_EDGES: return true;
        case Selector::Kind::CONNECTED_TO:
            return e.from == sel.value || e.to == sel.value;
        case Selector::Kind::BY_TYPE: {
            // 边的"类型"按 style 匹配：solid / dashed / thick
            return e.style == sel.value;
        }
        default: return false;
    }
}

// ─── Selector::parse: 从 CLI 条件字符串解析 ────────────────────────
// 格式：key=value（精确） 或  key~=value（模糊）
// 示例：shape=rect  |  parent=g1  |  label~=Step  |  id=A
inline Selector Selector::parse(const std::string& cond)
{
    Selector sel;
    // 检测 ~= (模糊匹配)
    size_t opPos = cond.find("~=");
    bool   fuzzy = (opPos != std::string::npos);
    if (!fuzzy)
        opPos = cond.find('=');

    if (opPos == std::string::npos) {
        // 没有操作符，按 id 精确匹配
        sel.kind  = Kind::BY_ID;
        sel.value = cond;
        return sel;
    }

    std::string key = cond.substr(0, opPos);
    std::string val = cond.substr(opPos + (fuzzy ? 2 : 1));
    sel.regex       = fuzzy;

    if (key == "id" || key == "node" || key == "edge") {
        sel.kind  = Kind::BY_ID;
        sel.value = val;
    }
    else if (key == "shape" || key == "type") {
        sel.kind  = Kind::BY_TYPE;
        sel.value = val;
    }
    else if (key == "label") {
        sel.kind  = Kind::BY_LABEL;
        sel.value = val;
    }
    else if (key == "parent") {
        sel.kind  = Kind::BY_PARENT;
        sel.value = val;
    }
    else {
        sel.kind  = Kind::BY_LABEL;
        sel.value = cond;
    }
    return sel;
}

// ─── Draft::materialize ──────────────────────────────────────────
inline Graph Draft::materialize(const Graph& base) const
{ return Commit::rebuild(base, operations); }

// ─── Commit::rebuild ─────────────────────────────────────────────
inline Graph Commit::rebuild(const Graph&                  parentModel,
                             const std::vector<Operation>& patch)
{
    Graph g = parentModel;
    for (auto& op : patch) {
        if (op.type == OpType::NODE_INSERT) {
            // 从 snapshot JSON 重建 Node
            Node n;
            n.id          = op.snapshot.str("id");
            n.label       = op.snapshot.str("label");
            n.shape       = op.snapshot.str("shape", "rect");
            n.parent      = op.snapshot.str("parent");
            n.style       = op.snapshot.str("style");
            n.fillColor   = op.snapshot.str("fillColor");
            n.strokeColor = op.snapshot.str("strokeColor");
            n.x           = op.snapshot.num("x");
            n.y           = op.snapshot.num("y");
            n.w           = op.snapshot.num("w");
            n.h           = op.snapshot.num("h");
            if (const Json* attrs = op.snapshot.find("attrs")) {
                if (attrs->isArr())
                    for (auto& a : *attrs->a)
                        if (a.isStr())
                            n.attrs.push_back(a.s);
            }
            g.nodes.push_back(n);
        }
        else if (op.type == OpType::NODE_UPDATE) {
            Node* n = g.findNode(op.targetId);
            if (n)
                for (auto& ch : op.changes)
                    setNodeField(*n, ch.field, ch.newValue);
        }
        else if (op.type == OpType::NODE_DELETE) {
            g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                         [&](const Node& n) {
                                             return n.id == op.targetId;
                                         }),
                          g.nodes.end());
            g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                                         [&](const Edge& e) {
                                             return e.from == op.targetId ||
                                                    e.to == op.targetId;
                                         }),
                          g.edges.end());
        }
        else if (op.type == OpType::EDGE_INSERT) {
            Edge e;
            e.id          = op.snapshot.str("id");
            e.from        = op.snapshot.str("from");
            e.to          = op.snapshot.str("to");
            e.label       = op.snapshot.str("label");
            e.style       = op.snapshot.str("style", "solid");
            e.arrow       = op.snapshot.str("arrow", "arrow");
            e.strokeColor = op.snapshot.str("strokeColor");
            g.edges.push_back(e);
        }
        else if (op.type == OpType::EDGE_UPDATE) {
            for (auto& e : g.edges) {
                if (e.id == op.targetId) {
                    for (auto& ch : op.changes)
                        setEdgeField(e, ch.field, ch.newValue);
                    break;
                }
            }
        }
        else if (op.type == OpType::EDGE_DELETE) {
            g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                                         [&](const Edge& e) {
                                             return e.id == op.targetId;
                                         }),
                          g.edges.end());
        }
        else if (op.type == OpType::META_UPDATE) {
            for (auto& ch : op.changes) {
                if (ch.field == "name")
                    g.name = ch.newValue;
                else if (ch.field == "type")
                    g.type = ch.newValue;
            }
        }
        else if (op.type == OpType::PROPERTY_SET) {
            if (!op.path.empty())
                gj::pathSet(g.properties, op.path, op.value);
        }
        else if (op.type == OpType::PROPERTY_INSERT) {
            if (!op.path.empty())
                gj::pathInsert(g.properties, op.path, op.value);
        }
        else if (op.type == OpType::PROPERTY_DELETE) {
            if (!op.path.empty())
                gj::pathDelete(g.properties, op.path);
        }
    }
    return g;
}

// ─── 辅助：从 Node 构建 snapshot JSON（用于 INSERT 操作）────────────
inline Json nodeToSnapshot(const Node& n)
{
    Json j = Json::obj();
    j.set("id", n.id);
    j.set("label", n.label);
    j.set("shape", n.shape);
    if (!n.parent.empty())
        j.set("parent", n.parent);
    if (!n.style.empty())
        j.set("style", n.style);
    if (!n.fillColor.empty())
        j.set("fillColor", n.fillColor);
    if (!n.strokeColor.empty())
        j.set("strokeColor", n.strokeColor);
    if (!n.attrs.empty()) {
        Json arr = Json::arr();
        for (auto& a : n.attrs)
            arr.push(Json(a));
        j.set("attrs", arr);
    }
    j.set("x", n.x);
    j.set("y", n.y);
    j.set("w", n.w);
    j.set("h", n.h);
    return j;
}

inline Json edgeToSnapshot(const Edge& e)
{
    Json j = Json::obj();
    j.set("id", e.id);
    j.set("from", e.from);
    j.set("to", e.to);
    if (!e.label.empty())
        j.set("label", e.label);
    j.set("style", e.style);
    j.set("arrow", e.arrow);
    if (!e.strokeColor.empty())
        j.set("strokeColor", e.strokeColor);
    return j;
}

}  // namespace gv
