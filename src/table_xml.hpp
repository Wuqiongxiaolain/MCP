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
#include <map>
#include <sstream>

namespace gtx {

using gt::Table;
using gt::TableError;
using gj::Json;

namespace detail {

// cellText: 取节点直接文本；无则空串
inline std::string cellText(const gp::detail::XmlNode& n)
{
    return n.text;
}

// collectRowFields: 从 <row> 收集 列名→值（属性先，同名子元素覆盖；一层嵌套→父.子）
// 关键步骤：属性写入 -> 子元素扁平/一层拍扁 -> 同名覆盖
inline void collectRowFields(const gp::detail::XmlNode& row,
                             std::map<std::string, std::string>& fields,
                             std::vector<std::string>* order)
{
    auto noteKey = [&](const std::string& key) {
        if (!order)
            return;
        for (auto& k : *order)
            if (k == key)
                return;
        order->push_back(key);
    };

    for (auto& kv : row.attrs) {
        fields[kv.first] = kv.second;
        noteKey(kv.first);
    }

    for (auto& c : row.children) {
        if (c.children.empty()) {
            fields[c.tag] = cellText(c);
            noteKey(c.tag);
            continue;
        }
        // 一层嵌套：子节点必须是叶子，否则过深
        for (auto& gc : c.children) {
            if (!gc.children.empty())
                throw TableError(
                    "table xml: nested deeper than one level under <" + c.tag +
                    ">");
            std::string key = c.tag + "." + gc.tag;
            fields[key]     = cellText(gc);
            noteKey(key);
        }
        // 父节点自身文本若存在且无叶子映射需求，忽略（拍扁以子孙为准）
        (void)c.text;
    }
}

// gatherRows: 收集根下所有 <row>（含嵌套在 <rows> 内）
inline void gatherRows(const gp::detail::XmlNode& root,
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

}  // namespace detail

// fromXml: 模式 A 表 XML → Table
// 参数 text: 表 XML 文本；根须为 <table>
inline Table fromXml(const std::string& text)
{
    gp::detail::XmlNode root = gp::detail::parseXmlDoc(text);
    if (root.tag != "table")
        throw TableError("table xml: root element must be <table>, got <" +
                         root.tag + ">");

    Table t;
    if (root.attrs.count("id"))
        t.id = root.attrs.at("id");
    if (root.attrs.count("name"))
        t.name = root.attrs.at("name");
    if (root.attrs.count("hasHintRow")) {
        std::string v = gm::toLower(root.attrs.at("hasHintRow"));
        t.hasHintRow  = (v == "1" || v == "true" || v == "yes");
    }

    // 显式列区
    for (auto& c : root.children) {
        if (c.tag != "columns")
            continue;
        for (auto& col : c.children) {
            if (col.tag != "col")
                continue;
            std::string name = col.text;
            if (name.empty() && col.attrs.count("name"))
                name = col.attrs.at("name");
            if (!name.empty())
                t.columns.push_back(name);
        }
    }

    std::vector<const gp::detail::XmlNode*> rows;
    detail::gatherRows(root, rows);

    // 无 <columns>：按首次出现顺序并集
    if (t.columns.empty()) {
        std::vector<std::string> order;
        for (auto* rp : rows) {
            std::map<std::string, std::string> fields;
            detail::collectRowFields(*rp, fields, &order);
        }
        t.columns = order;
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

// toXml: Table → 规范模式 A 表 XML（矩形空单元格也输出空标签）
inline std::string toXml(const Table& t)
{
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
        for (size_t i = 0; i < t.columns.size(); i++) {
            std::string col = t.columns[i];
            // 含 '.' 的列写回一层嵌套
            size_t dot = col.find('.');
            std::string val = i < r.size() ? r[i] : "";
            if (dot != std::string::npos && col.find('.', dot + 1) == std::string::npos) {
                std::string parent = col.substr(0, dot);
                std::string child  = col.substr(dot + 1);
                os << "      <" << parent << "><" << child << ">"
                   << ge::xmlTextEscape(val) << "</" << child << "></" << parent
                   << ">\n";
            }
            else {
                // 列名作标签：若含非法空白则仍按原名写出（与 fromXml 对称）
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

// parseTableContent: 按 format 选择解析器；默认 csv（保持现网行为）
// 参数 format: csv | xml | model（json 同 model）
inline Table parseTableContent(const std::string& content,
                               const std::string& format = "csv")
{
    std::string fmt = gm::toLower(format);
    if (fmt.empty())
        fmt = "csv";
    if (fmt == "csv")
        return Table::fromCsv(content);
    if (fmt == "xml")
        return fromXml(content);
    if (fmt == "model" || fmt == "json") {
        std::string err;
        Json        j = Json::parse(content, &err);
        if (!err.empty())
            throw TableError("invalid table model JSON: " + err);
        return Table::fromJson(j);
    }
    throw TableError("unsupported table format: " + format);
}

}  // namespace gtx
