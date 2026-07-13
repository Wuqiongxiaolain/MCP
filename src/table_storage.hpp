// table_storage.hpp - 通用表的文件系统存储（简化版本，无 Draft/Stage）
// 磁盘结构：
//   <root>/tables/index.json
//   <root>/tables/<tableId>/latest.json
//   <root>/tables/<tableId>/versions/v<N>.json
//   <root>/tables/<tableId>/HEAD
#pragma once
#include "exporters.hpp"
#include "storage.hpp"  // gs::makeDir / gs::nowIso
#include "table_model.hpp"
#include "version_types.hpp"

namespace gts {

using gj::Json;
using gt::Table;

// TableStore: 表对象存储（与 gs::Store 并列，共享 GRAPHMCP_STORE 根）
class TableStore {
  public:
    explicit TableStore(std::string root = "")
    {
        if (root.empty()) {
            std::string env = ge::getEnvVar("GRAPHMCP_STORE");
            root            = env.empty() ? "graph-store" : env;
        }
        root_ = root;
        gs::makeDir(root_);
        gs::makeDir(tablesRoot());
    }

    const std::string& root() const { return root_; }

    static bool isValidTableId(const std::string& id)
    { return gv::isValidId(id); }

    std::string tablesRoot() const { return root_ + "/tables"; }

    Json loadIndex() const
    {
        std::string txt = ge::readFile(tablesRoot() + "/index.json");
        if (txt.empty()) {
            Json j = Json::obj();
            j.set("tables", Json::arr());
            return j;
        }
        std::string err;
        Json        j = Json::parse(txt, &err);
        if (!err.empty() || !j.find("tables")) {
            Json fresh = Json::obj();
            fresh.set("tables", Json::arr());
            return fresh;
        }
        return j;
    }

    bool saveIndex(const Json& idx) const
    { return ge::writeFile(tablesRoot() + "/index.json", idx.dump(2)); }

    bool exists(const std::string& id) const
    {
        if (!isValidTableId(id))
            return false;
        Json idx = loadIndex();
        for (auto& item : *idx["tables"].a)
            if (item.str("id") == id)
                return true;
        return false;
    }

    // save: 写 latest + 新版本快照；返回版本号，失败返回 -1
    int save(Table& t, const std::string& note = "", std::string* err = nullptr)
    {
        if (t.id.empty())
            t.id = gm::genId("t");
        if (!isValidTableId(t.id)) {
            if (err)
                *err = "invalid table id";
            return -1;
        }
        if (t.name.empty())
            t.name = t.id;
        t.normalize();

        std::string dir = tablesRoot() + "/" + t.id;
        gs::makeDir(dir);
        gs::makeDir(dir + "/versions");

        Json  idx   = loadIndex();
        Json* entry = nullptr;
        for (auto& item : *idx["tables"].a)
            if (item.str("id") == t.id) {
                entry = &item;
                break;
            }
        int version = 1;
        if (entry)
            version = (int)entry->num("versions") + 1;

        Json model = t.toJson();
        if (!ge::writeFile(dir + "/latest.json", model.dump(2))) {
            if (err)
                *err = "failed to write latest.json";
            return -1;
        }

        Json snap = Json::obj();
        snap.set("version", version);
        snap.set("savedAt", gs::nowIso());
        snap.set("note", note);
        snap.set("model", model);
        if (!ge::writeFile(dir + "/versions/v" + std::to_string(version) +
                               ".json",
                           snap.dump(2))) {
            if (err)
                *err = "failed to write version snapshot";
            return -1;
        }
        if (!ge::writeFile(dir + "/HEAD", std::to_string(version))) {
            if (err)
                *err = "failed to write HEAD";
            return -1;
        }

        if (entry) {
            entry->set("name", t.name);
            entry->set("columns", (double)t.columns.size());
            entry->set("rows", (double)t.rows.size());
            entry->set("versions", version);
            entry->set("updatedAt", gs::nowIso());
        }
        else {
            Json e = Json::obj();
            e.set("id", t.id);
            e.set("name", t.name);
            e.set("columns", (double)t.columns.size());
            e.set("rows", (double)t.rows.size());
            e.set("versions", version);
            e.set("createdAt", gs::nowIso());
            e.set("updatedAt", gs::nowIso());
            idx["tables"].push(e);
        }
        if (!saveIndex(idx)) {
            if (err)
                *err = "failed to write tables index";
            return -1;
        }
        return version;
    }

    bool load(const std::string& id,
              Table&             out,
              int                version = 0,
              std::string*       err     = nullptr) const
    {
        if (!isValidTableId(id)) {
            if (err)
                *err = "invalid table id";
            return false;
        }
        std::string path;
        if (version <= 0)
            path = tablesRoot() + "/" + id + "/latest.json";
        else
            path = tablesRoot() + "/" + id + "/versions/v" +
                   std::to_string(version) + ".json";
        std::string txt = ge::readFile(path);
        if (txt.empty()) {
            if (err)
                *err = "table not found: " + id;
            return false;
        }
        std::string perr;
        Json        j = Json::parse(txt, &perr);
        if (!perr.empty()) {
            if (err)
                *err = "corrupt table file: " + perr;
            return false;
        }
        if (version > 0) {
            const Json* m = j.find("model");
            if (!m) {
                if (err)
                    *err = "snapshot has no model";
                return false;
            }
            out = Table::fromJson(*m);
        }
        else {
            out = Table::fromJson(j);
        }
        return true;
    }

    Json history(const std::string& id) const
    {
        Json list     = Json::arr();
        Json idx      = loadIndex();
        int  versions = 0;
        for (auto& item : *idx["tables"].a)
            if (item.str("id") == id)
                versions = (int)item.num("versions");
        for (int v = 1; v <= versions; v++) {
            std::string txt =
                ge::readFile(tablesRoot() + "/" + id + "/versions/v" +
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
                e.set("columns",
                      (double)(m->find("columns") ? m->find("columns")->size()
                                                  : 0));
                e.set("rows",
                      (double)(m->find("rows") ? m->find("rows")->size() : 0));
            }
            list.push(e);
        }
        return list;
    }

    bool rollback(const std::string& id,
                  int                version,
                  int*               newVersion,
                  std::string*       err = nullptr)
    {
        Table t;
        if (!load(id, t, version, err))
            return false;
        t.id   = id;
        int nv = save(t, "rollback to v" + std::to_string(version));
        if (nv < 0) {
            if (err)
                *err = "rollback save failed";
            return false;
        }
        if (newVersion)
            *newVersion = nv;
        return true;
    }

    // remove: 删除表目录并更新索引；成功返回 true
    bool remove(const std::string& id, std::string* err = nullptr)
    {
        if (!isValidTableId(id)) {
            if (err)
                *err = "invalid table id";
            return false;
        }
        Table probe;
        if (!load(id, probe, 0, err))
            return false;
        std::string dir = tablesRoot() + "/" + id;
        ge::removeDirectory(dir);
        Json idx = loadIndex();
        Json neu = Json::arr();
        for (auto& item : *idx["tables"].a)
            if (item.str("id") != id)
                neu.push(item);
        idx.set("tables", neu);
        if (!saveIndex(idx)) {
            if (err)
                *err = "failed to write tables index";
            return false;
        }
        return true;
    }

  private:
    std::string root_;
};

}  // namespace gts
