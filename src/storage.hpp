// storage.hpp - 基于 JSON 文件的图存储（支持版本历史与回滚）
// 磁盘结构：
//   <root>/index.json                     - 图索引目录
//   <root>/<graphId>/latest.json          - 当前模型版本
//   <root>/<graphId>/versions/v<N>.json   - 不可变历史快照
//   <root>/<graphId>/versions/v<N>.meta.json - 轻量历史元数据
#pragma once
#include "exporters.hpp"  // writeFileAtomic / StoreLock / readFile
#include "model.hpp"
#include "version_types.hpp"  // gv::nowIso / gv::isValidId
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

// nowIso 已统一到 gv::nowIso (version_types.hpp)，避免维护分歧
using gv::nowIso;

// Store: 文件系统版图存储服务（索引 + latest + versions）
class Store {
  public:
    // root 为空时回退到环境变量 GRAPHMCP_STORE 或默认 graph-store
    explicit Store(std::string root = "")
    {
        if (root.empty()) {
            std::string env = ge::getEnvVar("GRAPHMCP_STORE");
            root            = env.empty() ? "graph-store" : env;
        }
        root_ = root;
        makeDir(root_);
    }

    const std::string& root() const
    { return root_; }

    // isValidGraphId: 委托统一的 gv::isValidId (version_types.hpp)
    static bool isValidGraphId(const std::string& id)
    { return gv::isValidId(id); }

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

    // saveIndex: 原子写回 index.json（紧凑序列化）
    bool saveIndex(const Json& idx) const
    { return ge::writeFileAtomic(root_ + "/index.json", idx.dump()); }

    // removeGraphFromIndex: 持锁删除索引条目（供 graph_delete 使用）
    bool removeGraphFromIndex(const std::string& id, std::string* err = nullptr)
    {
        ge::StoreLock lock(root_);
        if (!lock.locked()) {
            if (err)
                *err = "failed to acquire store lock";
            return false;
        }
        Json idx = loadIndex();
        if (!idx.find("graphs") || !idx["graphs"].isArr()) {
            Json fresh = Json::obj();
            fresh.set("graphs", Json::arr());
            idx = fresh;
        }
        Json filtered = Json::arr();
        for (auto& item : *idx["graphs"].a) {
            if (item.str("id") != id)
                filtered.push(item);
        }
        idx.set("graphs", filtered);
        if (!saveIndex(idx)) {
            if (err)
                *err = "failed to write index.json";
            return false;
        }
        return true;
    }

    // ---- 保存：写 latest.json + 新的不可变版本快照 ----
    // 返回新版本号；失败返回 -1
    // 关键步骤：持锁 -> 合并读 index -> 写 latest/snapshot/meta/HEAD -> 写 index
    int save(Graph&             g,
             const std::string& note          = "",
             int                parentVersion = 0,
             const std::string& commitId      = "")
    {
        if (g.id.empty())
            g.id = gm::genId();
        if (!isValidGraphId(g.id))
            return -1;
        if (g.name.empty())
            g.name = g.id;

        ge::StoreLock lock(root_);
        if (!lock.locked())
            return -1;

        std::string dir = root_ + "/" + g.id;
        makeDir(dir);
        makeDir(dir + "/versions");

        // 持锁后重新读 index，避免并发写互相覆盖
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

        // 大文件白板含大量 base64：toJson 一次，dump 一次，供 latest + snapshot 复用
        Json        model     = g.toJson();
        std::string modelDump = model.dump();
        if (!ge::writeFileAtomic(dir + "/latest.json", modelDump))
            return -1;

        std::string savedAt = nowIso();
        Json        snap    = Json::obj();
        snap.set("version", version);
        snap.set("savedAt", savedAt);
        snap.set("note", note);
        snap.set("parent", parentVersion);
        if (!commitId.empty())
            snap.set("commitId", commitId);
        // 复用 model 对象挂入快照；序列化时再 dump 整棵树一次（无法避免含 model）
        snap.set("model", model);
        std::string verPath =
            dir + "/versions/v" + std::to_string(version) + ".json";
        if (!ge::writeFileAtomic(verPath, snap.dump()))
            return -1;

        // 轻量 meta：history 优先读，避免全量解析大 snapshot
        Json meta = Json::obj();
        meta.set("version", version);
        meta.set("savedAt", savedAt);
        meta.set("note", note);
        meta.set("nodes", (double)g.nodes.size());
        meta.set("edges", (double)g.edges.size());
        ge::writeFileAtomic(dir + "/versions/v" + std::to_string(version) +
                                ".meta.json",
                            meta.dump());

        if (entry) {
            entry->set("name", g.name);
            entry->set("type", g.type);
            entry->set("versions", version);
            entry->set("updatedAt", savedAt);
        }
        else {
            Json e = Json::obj();
            e.set("id", g.id);
            e.set("name", g.name);
            e.set("type", g.type);
            e.set("versions", version);
            e.set("createdAt", savedAt);
            e.set("updatedAt", savedAt);
            idx["graphs"].push(e);
        }
        if (!saveIndex(idx))
            return -1;

        // 与 GraphVersionManager::writeHead 对齐
        if (!ge::writeFileAtomic(dir + "/HEAD", std::to_string(version)))
            return -1;
        return version;
    }

    // ---- 加载 latest 或指定版本 ----
    bool load(const std::string& id,
              Graph&             out,
              int                version = 0,
              std::string*       err     = nullptr) const
    {
        if (!isValidGraphId(id)) {
            if (err)
                *err = "invalid graph id";
            return false;
        }
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
    // history: 优先读 vN.meta.json；缺失时回退全量 snapshot parse
    Json history(const std::string& id) const
    {
        Json list     = Json::arr();
        Json idx      = loadIndex();
        int  versions = 0;
        for (auto& item : *idx["graphs"].a)
            if (item.str("id") == id)
                versions = (int)item.num("versions");
        for (int v = 1; v <= versions; v++) {
            std::string metaPath = root_ + "/" + id + "/versions/v" +
                                   std::to_string(v) + ".meta.json";
            std::string metaTxt  = ge::readFile(metaPath);
            if (!metaTxt.empty()) {
                std::string perr;
                Json        mj = Json::parse(metaTxt, &perr);
                if (perr.empty()) {
                    Json e = Json::obj();
                    e.set("version", v);
                    e.set("savedAt", mj.str("savedAt"));
                    e.set("note", mj.str("note"));
                    e.set("nodes", mj.num("nodes"));
                    e.set("edges", mj.num("edges"));
                    list.push(e);
                    continue;
                }
            }
            // 兼容旧数据：全量 parse snapshot
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
        if (nv < 0) {
            if (err)
                *err = "rollback save failed";
            return false;
        }
        if (newVersion)
            *newVersion = nv;
        return true;
    }

  private:
    std::string root_;
};

}  // namespace gs
