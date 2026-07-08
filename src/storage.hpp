// storage.hpp - 基于 JSON 文件的图存储（支持版本历史与回滚）
// 磁盘结构：
//   <root>/index.json                     - 图索引目录
//   <root>/<graphId>/latest.json          - 当前模型版本
//   <root>/<graphId>/versions/v<N>.json   - 不可变历史快照
#pragma once
#include "exporters.hpp"  // 复用 writeFile / readFile
#include "model.hpp"
#include <cstdio>  // std::remove
#include <ctime>

#ifdef _WIN32
#    include <direct.h>
#else
#    include <sys/stat.h>
#    include <sys/types.h>
#endif

namespace gs {

using gj::Json;
using gm::Graph;

// makeDir: 平台无关目录创建封装
inline void makeDir(const std::string& path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

// nowIso: 生成本地时间 ISO 字符串，用于版本时间戳
inline std::string nowIso()
{
    time_t    t = time(nullptr);
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

// Store: 文件系统版图存储服务（索引 + latest + versions）
class Store {
  public:
    // root 为空时回退到环境变量 GRAPHMCP_STORE 或默认 graph-store
    explicit Store(std::string root = "")
    {
        if (root.empty()) {
            const char* env = getenv("GRAPHMCP_STORE");
            root            = env ? env : "graph-store";
        }
        root_ = root;
        makeDir(root_);
    }

    const std::string& root() const
    { return root_; }

    // ---- 索引 ----
    // loadIndex: 读取索引；文件缺失或损坏时返回空索引结构
    Json loadIndex() const
    {
        std::string txt = ge::readFile(root_ + "/index.json");
        if (txt.empty()) {
            Json j = Json::obj();
            j.set("graphs", Json::arr());
            return j;
        }
        std::string err;
        Json        j = Json::parse(txt, &err);
        if (!err.empty() || !j.find("graphs")) {
            Json fresh = Json::obj();
            fresh.set("graphs", Json::arr());
            return fresh;
        }
        return j;
    }
    // saveIndex: 将索引写回 index.json
    void saveIndex(const Json& idx) const
    { ge::writeFile(root_ + "/index.json", idx.dump(2)); }

    // ---- 保存：写 latest.json + 新的不可变版本快照 ----
    // 返回新版本号
    // save: 保存当前图并创建不可变快照版本
    // 关键步骤：补齐 id/name -> 写 latest -> 写 versions/vN -> 更新索引版本计数
    int save(Graph& g, const std::string& note = "")
    {
        if (g.id.empty())
            g.id = gm::genId();
        if (g.name.empty())
            g.name = g.id;
        std::string dir = root_ + "/" + g.id;
        makeDir(dir);
        makeDir(dir + "/versions");

        Json  idx   = loadIndex();
        Json* entry = nullptr;
        for (auto& item : *idx["graphs"].a)
            if (item.str("id") == g.id) {
                entry = &item;
                break;
            }
        int version = 1;
        if (entry)
            version = (int)entry->num("versions") + 1;

        std::string modelJson = g.toJson().dump(2);
        ge::writeFile(dir + "/latest.json", modelJson);

        Json snap = Json::obj();
        snap.set("version", version);
        snap.set("savedAt", nowIso());
        snap.set("note", note);
        snap.set("model", g.toJson());
        ge::writeFile(dir + "/versions/v" + std::to_string(version) + ".json",
                      snap.dump(2));

        if (entry) {
            entry->set("name", g.name);
            entry->set("type", g.type);
            entry->set("versions", version);
            entry->set("updatedAt", nowIso());
        }
        else {
            Json e = Json::obj();
            e.set("id", g.id);
            e.set("name", g.name);
            e.set("type", g.type);
            e.set("versions", version);
            e.set("createdAt", nowIso());
            e.set("updatedAt", nowIso());
            idx["graphs"].push(e);
        }
        saveIndex(idx);
        return version;
    }

    // ---- 加载 latest 或指定版本 ----
    // load: 读取最新版本或指定版本；version<=0 表示 latest
    bool load(const std::string& id,
              Graph&             out,
              int                version = 0,
              std::string*       err     = nullptr) const
    {
        std::string path;
        if (version <= 0) {
            path = root_ + "/" + id + "/latest.json";
        }
        else {
            path = root_ + "/" + id + "/versions/v" + std::to_string(version) +
                   ".json";
        }
        std::string txt = ge::readFile(path);
        if (txt.empty()) {
            if (err)
                *err = "graph not found: " + id +
                       (version > 0 ? " v" + std::to_string(version) : "") +
                       " (looked in " + path + ")";
            return false;
        }
        std::string perr;
        Json        j = Json::parse(txt, &perr);
        if (!perr.empty()) {
            if (err)
                *err = "corrupt graph file " + path + ": " + perr;
            return false;
        }
        if (version > 0) {
            const Json* m = j.find("model");
            if (!m) {
                if (err)
                    *err = "snapshot has no model: " + path;
                return false;
            }
            out = Graph::fromJson(*m);
        }
        else {
            out = Graph::fromJson(j);
        }
        return true;
    }

    // ---- 历史列表 ----
    // history: 聚合指定图的版本元数据（时间、备注、节点/边数量）
    Json history(const std::string& id) const
    {
        Json list     = Json::arr();
        Json idx      = loadIndex();
        int  versions = 0;
        for (auto& item : *idx["graphs"].a)
            if (item.str("id") == id)
                versions = (int)item.num("versions");
        for (int v = 1; v <= versions; v++) {
            std::string txt = ge::readFile(root_ + "/" + id + "/versions/v" +
                                           std::to_string(v) + ".json");
            if (txt.empty())
                continue;
            std::string perr;
            Json        j = Json::parse(txt, &perr);
            if (!perr.empty())
                continue;
            Json e = Json::obj();
            e.set("version", v);
            e.set("savedAt", j.str("savedAt"));
            e.set("note", j.str("note"));
            const Json* m = j.find("model");
            if (m) {
                e.set(
                    "nodes",
                    (double)(m->find("nodes") ? m->find("nodes")->size() : 0));
                e.set(
                    "edges",
                    (double)(m->find("edges") ? m->find("edges")->size() : 0));
            }
            list.push(e);
        }
        return list;
    }

    // ---- 回滚：将旧快照重新保存为最新版本 ----
    // rollback: 基于旧版本重新 save 成新版本（非破坏式回滚）
    bool rollback(const std::string& id,
                  int                version,
                  int*               newVersion,
                  std::string*       err = nullptr)
    {
        Graph g;
        if (!load(id, g, version, err))
            return false;
        g.id   = id;
        int nv = save(g, "rollback to v" + std::to_string(version));
        if (newVersion)
            *newVersion = nv;
        return true;
    }

    // ---- 草稿（draft）：可变工作副本，不占版本号 ----
    // 语义类比 git 工作区：改动先落草稿，显式 commit 才固化为正式版本。

    // draftPath: 草稿文件路径 <root>/<id>/draft.json
    std::string draftPath(const std::string& id) const
    { return root_ + "/" + id + "/draft.json"; }

    // hasDraft: 是否存在未提交草稿
    bool hasDraft(const std::string& id) const
    { return !ge::readFile(draftPath(id)).empty(); }

    // loadDraft: 载入草稿；无草稿时回退到 latest 作为草稿基线
    // 图不存在（latest 也没有）时返回 false
    bool loadDraft(const std::string& id,
                   Graph&             out,
                   std::string*       err = nullptr) const
    {
        std::string txt = ge::readFile(draftPath(id));
        if (txt.empty())
            return load(id, out, 0, err);  // 无草稿 -> 从正式版派生基线
        std::string perr;
        Json        j = Json::parse(txt, &perr);
        if (!perr.empty()) {
            if (err)
                *err = "corrupt draft file " + draftPath(id) + ": " + perr;
            return false;
        }
        out = Graph::fromJson(j);
        return true;
    }

    // saveDraft: 写入草稿（整图），不产生版本快照
    void saveDraft(const std::string& id, const Graph& g) const
    {
        makeDir(root_ + "/" + id);
        ge::writeFile(draftPath(id), g.toJson().dump(2));
    }

    // discardDraft: 丢弃草稿（删除 draft.json）
    void discardDraft(const std::string& id) const
    { std::remove(draftPath(id).c_str()); }

    // commitDraft: 将草稿固化为新的正式版本，随后清除草稿
    // 复用 save() 的版本化逻辑；无草稿时报错
    bool commitDraft(const std::string& id,
                     const std::string& note,
                     int*               newVersion,
                     std::string*       err = nullptr)
    {
        if (!hasDraft(id)) {
            if (err)
                *err = "nothing to commit: no draft for graph " + id;
            return false;
        }
        Graph g;
        if (!loadDraft(id, g, err))
            return false;
        g.id   = id;
        int nv = save(g, note.empty() ? "commit draft" : note);
        discardDraft(id);
        if (newVersion)
            *newVersion = nv;
        return true;
    }

    // ---- 游标状态文件：<root>/<id>/cursors/<cid>.json ----
    // CLI 每次调用是独立进程，游标位置必须落盘才能跨调用复用。

    // cursorPath: 游标状态文件路径
    std::string cursorPath(const std::string& id, const std::string& cid) const
    { return root_ + "/" + id + "/cursors/" + cid + ".json"; }

    // writeCursor: 持久化游标状态（graphId / target / index）
    void writeCursor(const std::string& id,
                     const std::string& cid,
                     const Json&        state) const
    {
        makeDir(root_ + "/" + id + "/cursors");
        ge::writeFile(cursorPath(id, cid), state.dump(2));
    }

    // loadCursorState: 读取游标状态；不存在或损坏返回 false
    bool loadCursorState(const std::string& id,
                         const std::string& cid,
                         Json&              out) const
    {
        std::string txt = ge::readFile(cursorPath(id, cid));
        if (txt.empty())
            return false;
        std::string perr;
        out = Json::parse(txt, &perr);
        return perr.empty();
    }

    // removeCursor: 删除游标状态文件
    void removeCursor(const std::string& id, const std::string& cid) const
    { std::remove(cursorPath(id, cid).c_str()); }

  private:
    std::string root_;
};

}  // namespace gs
