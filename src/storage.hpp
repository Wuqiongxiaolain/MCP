// storage.hpp - 基于 JSON 文件的图存储（支持版本历史与回滚）
// 磁盘结构：
//   <root>/index.json                     - 图索引目录
//   <root>/<graphId>/latest.json          - 当前模型版本
//   <root>/<graphId>/versions/v<N>.json   - 不可变历史快照
#pragma once
#include "model.hpp"
#include "exporters.hpp" // 复用 writeFile / readFile
#include <ctime>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace gs {

using gm::Graph;
using gj::Json;

// makeDir: 平台无关目录创建封装
inline void makeDir(const std::string& path) {
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

// nowIso: 生成本地时间 ISO 字符串，用于版本时间戳
inline std::string nowIso() {
    time_t t = time(nullptr);
    struct tm tmv;
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
    explicit Store(std::string root = "") {
        if (root.empty()) {
            const char* env = getenv("GRAPHMCP_STORE");
            root = env ? env : "graph-store";
        }
        root_ = root;
        makeDir(root_);
    }

    const std::string& root() const { return root_; }

    // ---- 索引 ----
    // loadIndex: 读取索引；文件缺失或损坏时返回空索引结构
    Json loadIndex() const {
        std::string txt = ge::readFile(root_ + "/index.json");
        if (txt.empty()) {
            Json j = Json::obj();
            j.set("graphs", Json::arr());
            return j;
        }
        std::string err;
        Json j = Json::parse(txt, &err);
        if (!err.empty() || !j.find("graphs")) {
            Json fresh = Json::obj();
            fresh.set("graphs", Json::arr());
            return fresh;
        }
        return j;
    }
    // saveIndex: 将索引写回 index.json
    void saveIndex(const Json& idx) const {
        ge::writeFile(root_ + "/index.json", idx.dump(2));
    }

    // ---- 保存：写 latest.json + 新的不可变版本快照 ----
    // 返回新版本号
    // save: 保存当前图并创建不可变快照版本
    // 关键步骤：补齐 id/name -> 写 latest -> 写 versions/vN -> 更新索引版本计数
    int save(Graph& g, const std::string& note = "") {
        if (g.id.empty()) g.id = gm::genId();
        if (g.name.empty()) g.name = g.id;
        std::string dir = root_ + "/" + g.id;
        makeDir(dir);
        makeDir(dir + "/versions");

        Json idx = loadIndex();
        Json* entry = nullptr;
        for (auto& item : *idx["graphs"].a)
            if (item.str("id") == g.id) { entry = &item; break; }
        int version = 1;
        if (entry) version = (int)entry->num("versions") + 1;

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
        } else {
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
    bool load(const std::string& id, Graph& out, int version = 0,
              std::string* err = nullptr) const {
        std::string path;
        if (version <= 0) {
            path = root_ + "/" + id + "/latest.json";
        } else {
            path = root_ + "/" + id + "/versions/v" + std::to_string(version) + ".json";
        }
        std::string txt = ge::readFile(path);
        if (txt.empty()) {
            if (err) *err = "graph not found: " + id +
                            (version > 0 ? " v" + std::to_string(version) : "") +
                            " (looked in " + path + ")";
            return false;
        }
        std::string perr;
        Json j = Json::parse(txt, &perr);
        if (!perr.empty()) {
            if (err) *err = "corrupt graph file " + path + ": " + perr;
            return false;
        }
        if (version > 0) {
            const Json* m = j.find("model");
            if (!m) { if (err) *err = "snapshot has no model: " + path; return false; }
            out = Graph::fromJson(*m);
        } else {
            out = Graph::fromJson(j);
        }
        return true;
    }

    // ---- 历史列表 ----
    // history: 聚合指定图的版本元数据（时间、备注、节点/边数量）
    Json history(const std::string& id) const {
        Json list = Json::arr();
        Json idx = loadIndex();
        int versions = 0;
        for (auto& item : *idx["graphs"].a)
            if (item.str("id") == id) versions = (int)item.num("versions");
        for (int v = 1; v <= versions; v++) {
            std::string txt = ge::readFile(root_ + "/" + id + "/versions/v" +
                                           std::to_string(v) + ".json");
            if (txt.empty()) continue;
            std::string perr;
            Json j = Json::parse(txt, &perr);
            if (!perr.empty()) continue;
            Json e = Json::obj();
            e.set("version", v);
            e.set("savedAt", j.str("savedAt"));
            e.set("note", j.str("note"));
            const Json* m = j.find("model");
            if (m) {
                e.set("nodes", (double)(m->find("nodes") ? m->find("nodes")->size() : 0));
                e.set("edges", (double)(m->find("edges") ? m->find("edges")->size() : 0));
            }
            list.push(e);
        }
        return list;
    }

    // ---- 回滚：将旧快照重新保存为最新版本 ----
    // rollback: 基于旧版本重新 save 成新版本（非破坏式回滚）
    bool rollback(const std::string& id, int version, int* newVersion,
                  std::string* err = nullptr) {
        Graph g;
        if (!load(id, g, version, err)) return false;
        g.id = id;
        int nv = save(g, "rollback to v" + std::to_string(version));
        if (newVersion) *newVersion = nv;
        return true;
    }

private:
    std::string root_;
};

} // 命名空间 gs
