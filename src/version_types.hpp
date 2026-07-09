// version_types.hpp - graphmcp 版本管理核心类型
// Draft / Stage / Commit 三层模型 + Selector 选择器
#pragma once
#include "model.hpp"
#include <ctime>
#include <functional>
#include <optional>
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
    UNKNOWN = -1,  // 无效操作类型（解析错误）
    NODE_INSERT,   // 插入节点
    NODE_UPDATE,   // 更新节点属性
    NODE_DELETE,   // 删除节点（级联删除关联边）
    EDGE_INSERT,   // 插入边
    EDGE_UPDATE,   // 更新边属性
    EDGE_DELETE,   // 删除边
    META_UPDATE    // 更新图级元数据（name / type）
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
    return OpType::UNKNOWN;
}

// ─── FieldChange: 单个字段的变更记录 ──────────────────────────────
struct FieldChange
{
    std::string field;  // 字段名：label / shape / style / x / y / w / h /
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
    std::vector<FieldChange> changes;    // 字段变更明细（UPDATE 操作）
    Json                     snapshot;   // 插入时的完整元素快照（INSERT 操作）
    std::string              timestamp;  // ISO 时间戳

    // 简短摘要（用于 CLI 展示）
    std::string summary() const
    {
        std::string s =
            std::string(opTypeName(type)) + " " + targetType + " " + targetId;
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
        d.updatedAt = j.str("updatedAt");
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
        c.version   = (int)j.num("version");
        c.parent    = (int)j.num("parent");
        c.commitId  = j.str("commitId");
        c.message   = j.str("message");
        c.author    = j.str("author");
        c.timestamp = j.str("timestamp");
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
    if (field == "x")
        return std::to_string(n.x);
    if (field == "y")
        return std::to_string(n.y);
    if (field == "w")
        return std::to_string(n.w);
    if (field == "h")
        return std::to_string(n.h);
    return "";
}

inline void
setNodeField(Node& n, const std::string& field, const std::string& val)
{
    if (field == "label")
        n.label = val;
    else if (field == "shape")
        n.shape = val;
    else if (field == "parent")
        n.parent = val;
    else if (field == "style")
        n.style = val;
    else if (field == "x")
        n.x = std::strtod(val.c_str(), nullptr);
    else if (field == "y")
        n.y = std::strtod(val.c_str(), nullptr);
    else if (field == "w")
        n.w = std::strtod(val.c_str(), nullptr);
    else if (field == "h")
        n.h = std::strtod(val.c_str(), nullptr);
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
    return "";
}

inline void
setEdgeField(Edge& e, const std::string& field, const std::string& val)
{
    if (field == "from")
        e.from = val;
    else if (field == "to")
        e.to = val;
    else if (field == "label")
        e.label = val;
    else if (field == "style")
        e.style = val;
    else if (field == "arrow")
        e.arrow = val;
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
            n.id     = op.snapshot.str("id");
            n.label  = op.snapshot.str("label");
            n.shape  = op.snapshot.str("shape", "rect");
            n.parent = op.snapshot.str("parent");
            n.style  = op.snapshot.str("style");
            n.x      = op.snapshot.num("x");
            n.y      = op.snapshot.num("y");
            n.w      = op.snapshot.num("w");
            n.h      = op.snapshot.num("h");
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
            e.id    = op.snapshot.str("id");
            e.from  = op.snapshot.str("from");
            e.to    = op.snapshot.str("to");
            e.label = op.snapshot.str("label");
            e.style = op.snapshot.str("style", "solid");
            e.arrow = op.snapshot.str("arrow", "arrow");
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
    return j;
}

}  // namespace gv
