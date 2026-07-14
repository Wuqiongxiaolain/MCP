// table_xml.hpp - 表 XML（模式 A）↔ gt::Table
//
// 本阶段策略：多新增少修改——不改 Table::fromCsv，不搬迁 parsers 中的
// parseXmlDoc；本文件直接复用 gp::detail::parseXmlDoc 与现有 Table API。
//
// 已知债：
// 1) 装表样板（赋列/appendRow/normalize/meta）可能与 fromCsv 少量重复；
// 2) 表模块依赖 gp::detail（图解析头）。
//
// 抽离触发（出现任一即单独开重构 PR，抽出 xml_util + buildTable）：
// - CSV 与表 XML 在 normalize/缺列/meta 等行为上漂移；
// - 再增加第三种表交换格式；
// - 表侧需脱离 parsers.hpp。
// 详见 docs/APPLICATION_LOGIC.md「表 XML 与后续抽离约定」。
#pragma once
#include "exporters.hpp"
#include "parsers.hpp"
#include "table_model.hpp"
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace gtx {

using gj::Json;
using gt::Table;
using gt::TableError;

namespace detail {

    // isSafeXmlName: 列名/标签名是否可安全用作本项目迷你 XML 的元素名
    // 拒绝空白与 <>/&"'= ；允许中文、? 等（与 enemy_sample 一致）
    inline bool isSafeXmlName(const std::string& s)
    {
        if (s.empty())
            return false;
        for (unsigned char c : s) {
            if (std::isspace(c))
                return false;
            if (c == '<' || c == '>' || c == '/' || c == '&' || c == '"' ||
                c == '\'' || c == '=')
                return false;
        }
        return true;
    }

    // requireSafeXmlName: 不安全则拒绝
    inline void requireSafeXmlName(const std::string& name, const char* what)
    {
        if (!isSafeXmlName(name))
            throw TableError(std::string("table xml: unsafe ") + what +
                             " for XML tag: \"" + name + "\"");
    }

    // splitNestedCol: 恰有一个 '.' 则拆成 parent/child；否则视为扁列
    inline bool splitNestedCol(const std::string& col,
                               std::string&       parent,
                               std::string&       child)
    {
        size_t dot = col.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= col.size())
            return false;
        if (col.find('.', dot + 1) != std::string::npos)
            return false;
        parent = col.substr(0, dot);
        child  = col.substr(dot + 1);
        return true;
    }

    // collectRowFields: 从 <row> 收集
    // 列名→值（属性先按出现序，同名子元素覆盖；一层嵌套→父.子）
    inline void collectRowFields(const gp::detail::XmlNode&          row,
                                 std::map<std::string, std::string>& fields,
                                 std::vector<std::string>*           order)
    {
        auto noteKey = [&](const std::string& key) {
            if (!order)
                return;
            for (auto& k : *order)
                if (k == key)
                    return;
            order->push_back(key);
        };

        // 属性：按 attr_order（文档出现序），而非 map 字典序
        for (auto& aname : row.attr_order) {
            auto it = row.attrs.find(aname);
            if (it == row.attrs.end())
                continue;
            fields[aname] = it->second;
            noteKey(aname);
        }

        for (auto& c : row.children) {
            if (c.children.empty()) {
                fields[c.tag] = c.text;
                noteKey(c.tag);
                continue;
            }
            for (auto& gc : c.children) {
                if (!gc.children.empty())
                    throw TableError(
                        "table xml: nested deeper than one level under <" +
                        c.tag + ">");
                std::string key = c.tag + "." + gc.tag;
                fields[key]     = gc.text;
                noteKey(key);
            }
        }
    }

    // gatherRows: 收集根下所有 <row>（含嵌套在 <rows> 内）
    inline void gatherRows(const gp::detail::XmlNode&               root,
                           std::vector<const gp::detail::XmlNode*>& out)
    {
        for (auto& c : root.children) {
            if (c.tag == "row")
                out.push_back(&c);
            else if (c.tag == "rows") {
                for (auto& r : c.children)
                    if (r.tag == "row")
                        out.push_back(&r);
            }
        }
    }

    // appendColumnUnique: 追加列名；重复则跳过并写入 warning
    inline void appendColumnUnique(Table&                    t,
                                   const std::string&        name,
                                   std::vector<std::string>* warnings)
    {
        for (auto& c : t.columns) {
            if (c == name) {
                if (warnings)
                    warnings->push_back("duplicate column ignored: " + name);
                return;
            }
        }
        t.columns.push_back(name);
    }

}  // namespace detail

// fromXml: 模式 A 表 XML → Table
// 参数 text: 表 XML；warnings: 可选，接收去重等非致命告警
inline Table fromXml(const std::string&        text,
                     std::vector<std::string>* warnings = nullptr)
{
    gp::detail::XmlNode root;
    try {
        root = gp::detail::parseXmlDoc(text);
    }
    catch (const gp::ParseError& e) {
        throw TableError(std::string("table xml: ") + e.what());
    }

    if (root.tag != "table")
        throw TableError("table xml: root element must be <table>, got <" +
                         root.tag + ">");

    Table t;
    if (root.attrs.count("id"))
        t.id = root.attrs.at("id");
    if (root.attrs.count("name"))
        t.name = root.attrs.at("name");
    if (root.attrs.count("hasHintRow"))
        t.hasHintRow = ge::parseTruthy(root.attrs.at("hasHintRow"));

    for (auto& c : root.children) {
        if (c.tag != "columns")
            continue;
        for (auto& col : c.children) {
            if (col.tag != "col")
                continue;
            std::string name = col.text;
            if (name.empty() && col.attrs.count("name"))
                name = col.attrs.at("name");
            if (name.empty())
                continue;
            std::string p, ch;
            if (detail::splitNestedCol(name, p, ch)) {
                detail::requireSafeXmlName(p, "nested parent name");
                detail::requireSafeXmlName(ch, "nested child name");
            }
            else {
                detail::requireSafeXmlName(name, "column name");
            }
            detail::appendColumnUnique(t, name, warnings);
        }
    }

    std::vector<const gp::detail::XmlNode*> rows;
    detail::gatherRows(root, rows);

    if (t.columns.empty()) {
        std::vector<std::string> order;
        for (auto* rp : rows) {
            std::map<std::string, std::string> fields;
            detail::collectRowFields(*rp, fields, &order);
        }
        for (auto& name : order) {
            std::string p, ch;
            if (detail::splitNestedCol(name, p, ch)) {
                detail::requireSafeXmlName(p, "nested parent name");
                detail::requireSafeXmlName(ch, "nested child name");
            }
            else {
                detail::requireSafeXmlName(name, "column name");
            }
            detail::appendColumnUnique(t, name, warnings);
        }
    }

    if (t.columns.empty())
        throw TableError("table xml: no columns");

    for (auto* rp : rows) {
        std::map<std::string, std::string> fields;
        detail::collectRowFields(*rp, fields, nullptr);
        std::vector<std::string> row;
        row.reserve(t.columns.size());
        for (auto& col : t.columns) {
            auto it = fields.find(col);
            row.push_back(it == fields.end() ? "" : it->second);
        }
        t.appendRow(row);
    }
    t.normalize();
    return t;
}

// toXml: Table → 规范模式 A；嵌套列按父标签聚合成一个父元素
inline std::string toXml(const Table& t)
{
    // 预检列名，并检测叶子列与嵌套父名冲突
    std::set<std::string> nested_parents;
    for (auto& col : t.columns) {
        std::string p, ch;
        if (detail::splitNestedCol(col, p, ch)) {
            detail::requireSafeXmlName(p, "nested parent name");
            detail::requireSafeXmlName(ch, "nested child name");
            nested_parents.insert(p);
        }
        else {
            detail::requireSafeXmlName(col, "column name");
        }
    }
    for (auto& col : t.columns) {
        std::string p, ch;
        if (!detail::splitNestedCol(col, p, ch) && nested_parents.count(col))
            throw TableError(
                "table xml: column \"" + col +
                "\" conflicts with nested columns under the same parent");
    }

    std::ostringstream os;
    os << "<table";
    if (!t.id.empty())
        os << " id=\"" << ge::xmlAttrEscape(t.id) << "\"";
    if (!t.name.empty())
        os << " name=\"" << ge::xmlAttrEscape(t.name) << "\"";
    os << " hasHintRow=\"" << (t.hasHintRow ? "true" : "false") << "\">\n";
    os << "  <columns>\n";
    for (auto& c : t.columns)
        os << "    <col>" << ge::xmlTextEscape(c) << "</col>\n";
    os << "  </columns>\n";
    os << "  <rows>\n";

    for (auto& r : t.rows) {
        os << "    <row>\n";
        std::set<std::string> emitted_parents;
        for (size_t i = 0; i < t.columns.size(); i++) {
            const std::string& col = t.columns[i];
            std::string        parent, child;
            if (detail::splitNestedCol(col, parent, child)) {
                if (emitted_parents.count(parent))
                    continue;
                emitted_parents.insert(parent);
                os << "      <" << parent << ">\n";
                // 按列序写出该父下全部子列
                for (size_t j = 0; j < t.columns.size(); j++) {
                    std::string p2, c2;
                    if (!detail::splitNestedCol(t.columns[j], p2, c2) ||
                        p2 != parent)
                        continue;
                    std::string val = j < r.size() ? r[j] : "";
                    os << "        <" << c2 << ">" << ge::xmlTextEscape(val)
                       << "</" << c2 << ">\n";
                }
                os << "      </" << parent << ">\n";
            }
            else {
                std::string val = i < r.size() ? r[i] : "";
                os << "      <" << col << ">" << ge::xmlTextEscape(val) << "</"
                   << col << ">\n";
            }
        }
        os << "    </row>\n";
    }
    os << "  </rows>\n";
    os << "</table>\n";
    return os.str();
}

// parseTableContent: 按 format 选择解析器；默认 csv
// 参数 warnings: 仅 xml 路径可能写入去重告警
inline Table parseTableContent(const std::string&        content,
                               const std::string&        format   = "csv",
                               std::vector<std::string>* warnings = nullptr)
{
    std::string fmt = gm::toLower(format);
    if (fmt.empty())
        fmt = "csv";
    if (fmt == "csv")
        return Table::fromCsv(content);
    if (fmt == "xml")
        return fromXml(content, warnings);
    if (fmt == "model" || fmt == "json") {
        std::string err;
        Json        j = Json::parse(content, &err);
        if (!err.empty())
            throw TableError("invalid table model JSON: " + err);
        return Table::fromJson(j);
    }
    throw TableError("unsupported table format: " + format);
}

// exportTableText: 按 to 导出；未知 to 报错（不静默回退 csv）
inline std::string exportTableText(const Table& t, const std::string& to)
{
    std::string fmt = gm::toLower(to);
    if (fmt.empty())
        fmt = "csv";
    if (fmt == "csv")
        return t.toCsv();
    if (fmt == "model" || fmt == "json")
        return t.toJson().dump(2);
    if (fmt == "xml")
        return toXml(t);
    throw TableError("unsupported table export format: " + to +
                     " (expected csv|model|xml)");
}

}  // namespace gtx
