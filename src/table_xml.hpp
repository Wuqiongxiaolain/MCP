// table_xml.hpp - 表 XML ↔ gt::Table
//
// 默认交换面：SpreadsheetML 2003（单文件，Excel 可打开），零第三方依赖。
// 兼容读入：旧版 graphmcp「命名字段行」方言（根 <table>）。
// 显式旧格式：format/to = table-xml | graphmcp-table-xml
//
// 复用 gp::detail::parseXmlDoc；不改 Table::fromCsv。
// 详见 docs/APPLICATION_LOGIC.md「表 XML」。
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

    // localName: 去掉命名空间前缀（ss:Name → Name）
    inline std::string localName(const std::string& tag)
    {
        size_t p = tag.find(':');
        return p == std::string::npos ? tag : tag.substr(p + 1);
    }

    // attrGet: 按完整名或去前缀后的本地名取属性
    inline std::string attrGet(const gp::detail::XmlNode& n,
                               const std::string&         local)
    {
        auto it = n.attrs.find(local);
        if (it != n.attrs.end())
            return it->second;
        std::string pref = "ss:" + local;
        it               = n.attrs.find(pref);
        if (it != n.attrs.end())
            return it->second;
        for (auto& kv : n.attrs) {
            if (localName(kv.first) == local)
                return kv.second;
        }
        return "";
    }

    // findChild: 按本地标签名找第一个子节点
    inline const gp::detail::XmlNode* findChild(const gp::detail::XmlNode& n,
                                                const std::string& local)
    {
        for (auto& c : n.children)
            if (localName(c.tag) == local)
                return &c;
        return nullptr;
    }

    // isSafeXmlName: 旧方言列名/标签名安全检查
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

    inline void requireSafeXmlName(const std::string& name, const char* what)
    {
        if (!isSafeXmlName(name))
            throw TableError(std::string("table xml: unsafe ") + what +
                             " for XML tag: \"" + name + "\"");
    }

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

    // sanitizeSheetName: Excel 工作表名限制（≤31，禁 \ / * ? [ ]）
    inline std::string sanitizeSheetName(const std::string& name)
    {
        std::string s = name.empty() ? "Sheet1" : name;
        for (char& c : s) {
            if (c == '\\' || c == '/' || c == '*' || c == '?' || c == '[' ||
                c == ']' || c == ':')
                c = '_';
        }
        if (s.size() > 31)
            s.resize(31);
        if (s.empty())
            s = "Sheet1";
        return s;
    }

    // cellDataText: 取 Cell 下 Data 文本（或 Cell 自身文本）
    inline std::string cellDataText(const gp::detail::XmlNode& cell)
    {
        const gp::detail::XmlNode* data = findChild(cell, "Data");
        if (data)
            return data->text;
        return cell.text;
    }

}  // namespace detail

// ---------------------------------------------------------------------------
// 旧方言：根 <table>，命名字段行（历史兼容）
// ---------------------------------------------------------------------------

// fromLegacyTableXml: 旧 graphmcp 表 XML → Table
inline Table fromLegacyTableXml(const std::string&        text,
                                std::vector<std::string>* warnings = nullptr)
{
    gp::detail::XmlNode root;
    try {
        root = gp::detail::parseXmlDoc(text);
    }
    catch (const gp::ParseError& e) {
        throw TableError(std::string("table xml: ") + e.what());
    }

    if (detail::localName(root.tag) != "table")
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

// toLegacyTableXml: Table → 旧命名字段行方言
inline std::string toLegacyTableXml(const Table& t)
{
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

// ---------------------------------------------------------------------------
// SpreadsheetML 2003（默认）
// ---------------------------------------------------------------------------

// toSpreadsheetMl: Table → SpreadsheetML 2003 单文件（Excel 可打开）
inline std::string toSpreadsheetMl(const Table& t)
{
    std::string sheet = detail::sanitizeSheetName(
        t.name.empty() ? (t.id.empty() ? "Sheet1" : t.id) : t.name);

    std::ostringstream kw;
    if (!t.id.empty())
        kw << "graphmcp-id=" << t.id;
    if (t.hasHintRow) {
        if (kw.tellp() > 0)
            kw << ';';
        kw << "graphmcp-hasHintRow=true";
    }

    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<?mso-application progid=\"Excel.Sheet\"?>\n";
    os << "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\n";
    os << " xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n";
    os << " <DocumentProperties "
          "xmlns=\"urn:schemas-microsoft-com:office:office\">\n";
    if (!t.name.empty())
        os << "  <Title>" << ge::xmlTextEscape(t.name) << "</Title>\n";
    if (kw.tellp() > 0)
        os << "  <Keywords>" << ge::xmlTextEscape(kw.str()) << "</Keywords>\n";
    os << " </DocumentProperties>\n";
    os << " <Worksheet ss:Name=\"" << ge::xmlAttrEscape(sheet) << "\">\n";
    os << "  <Table>\n";

    // 表头行
    os << "   <Row>\n";
    for (auto& c : t.columns) {
        os << "    <Cell><Data ss:Type=\"String\">" << ge::xmlTextEscape(c)
           << "</Data></Cell>\n";
    }
    os << "   </Row>\n";

    for (auto& r : t.rows) {
        os << "   <Row>\n";
        for (size_t i = 0; i < t.columns.size(); i++) {
            std::string val = i < r.size() ? r[i] : "";
            os << "    <Cell><Data ss:Type=\"String\">"
               << ge::xmlTextEscape(val) << "</Data></Cell>\n";
        }
        os << "   </Row>\n";
    }

    os << "  </Table>\n";
    os << " </Worksheet>\n";
    os << "</Workbook>\n";
    return os.str();
}

// fromSpreadsheetMl: SpreadsheetML 2003 → Table（支持本工具导出的子集）
inline Table fromSpreadsheetMl(const std::string&        text,
                               std::vector<std::string>* warnings = nullptr)
{
    (void)warnings;
    gp::detail::XmlNode root;
    try {
        root = gp::detail::parseXmlDoc(text);
    }
    catch (const gp::ParseError& e) {
        throw TableError(std::string("table xml: ") + e.what());
    }

    if (detail::localName(root.tag) != "Workbook")
        throw TableError("table xml: SpreadsheetML root must be <Workbook>, "
                         "got <" +
                         root.tag + ">");

    Table t;

    if (const auto* props = detail::findChild(root, "DocumentProperties")) {
        if (const auto* title = detail::findChild(*props, "Title"))
            t.name = title->text;
        if (const auto* kw = detail::findChild(*props, "Keywords")) {
            // graphmcp-id=...;graphmcp-hasHintRow=true
            std::string s = kw->text;
            size_t      p = 0;
            while (p < s.size()) {
                size_t      semi = s.find(';', p);
                std::string part =
                    gm::trim(s.substr(p, semi == std::string::npos
                                             ? std::string::npos
                                             : semi - p));
                if (part.compare(0, 12, "graphmcp-id=") == 0)
                    t.id = part.substr(12);
                else if (part == "graphmcp-hasHintRow=true" ||
                         part == "graphmcp-hasHintRow=1")
                    t.hasHintRow = true;
                if (semi == std::string::npos)
                    break;
                p = semi + 1;
            }
        }
    }

    const gp::detail::XmlNode* ws = detail::findChild(root, "Worksheet");
    if (!ws)
        throw TableError("table xml: SpreadsheetML missing <Worksheet>");

    std::string sheetName = detail::attrGet(*ws, "Name");
    if (t.name.empty() && !sheetName.empty() && sheetName != "Sheet1")
        t.name = sheetName;

    const gp::detail::XmlNode* table = detail::findChild(*ws, "Table");
    if (!table)
        throw TableError("table xml: SpreadsheetML missing <Table>");

    std::vector<const gp::detail::XmlNode*> rows;
    for (auto& c : table->children) {
        if (detail::localName(c.tag) == "Row")
            rows.push_back(&c);
    }
    if (rows.empty())
        throw TableError("table xml: SpreadsheetML has no rows");

    // 首行 = 表头
    for (auto& cell : rows[0]->children) {
        if (detail::localName(cell.tag) != "Cell")
            continue;
        std::string h = detail::cellDataText(cell);
        if (h.empty())
            h = "col_" + std::to_string(t.columns.size());
        detail::appendColumnUnique(t, h, warnings);
    }
    if (t.columns.empty())
        throw TableError("table xml: SpreadsheetML header row empty");

    for (size_t ri = 1; ri < rows.size(); ri++) {
        std::vector<std::string> row;
        row.reserve(t.columns.size());
        size_t ci = 0;
        for (auto& cell : rows[ri]->children) {
            if (detail::localName(cell.tag) != "Cell")
                continue;
            if (ci >= t.columns.size())
                break;
            row.push_back(detail::cellDataText(cell));
            ci++;
        }
        while (row.size() < t.columns.size())
            row.push_back("");
        t.appendRow(row);
    }
    t.normalize();
    return t;
}

// toXml: 默认写出 SpreadsheetML 2003
inline std::string toXml(const Table& t)
{ return toSpreadsheetMl(t); }

// fromXml: 自动识别 SpreadsheetML 或旧 <table> 方言
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

    std::string ln = detail::localName(root.tag);
    if (ln == "Workbook")
        return fromSpreadsheetMl(text, warnings);
    if (ln == "table")
        return fromLegacyTableXml(text, warnings);
    throw TableError(
        "table xml: unrecognized root <" + root.tag +
        "> (expected SpreadsheetML <Workbook> or legacy <table>)");
}

// parseTableContent: 按 format 选择解析器；默认 csv
// xml → 自动识别；table-xml / graphmcp-table-xml → 仅旧方言
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
    if (fmt == "table-xml" || fmt == "graphmcp-table-xml")
        return fromLegacyTableXml(content, warnings);
    if (fmt == "model" || fmt == "json") {
        std::string err;
        Json        j = Json::parse(content, &err);
        if (!err.empty())
            throw TableError("invalid table model JSON: " + err);
        return Table::fromJson(j);
    }
    throw TableError("unsupported table format: " + format);
}

// exportTableText: 按 to 导出
// xml → SpreadsheetML；table-xml → 旧方言；excelCsv 仅影响 csv
inline std::string exportTableText(const Table& t, const std::string& to,
                                   bool excelCsv = true)
{
    std::string fmt = gm::toLower(to);
    if (fmt.empty())
        fmt = "csv";
    if (fmt == "csv")
        return excelCsv ? t.toCsv() : t.toCsvRaw();
    if (fmt == "model" || fmt == "json")
        return t.toJson().dump();
    if (fmt == "xml")
        return toSpreadsheetMl(t);
    if (fmt == "table-xml" || fmt == "graphmcp-table-xml")
        return toLegacyTableXml(t);
    throw TableError("unsupported table export format: " + to +
                     " (expected csv|model|xml|table-xml)");
}

}  // namespace gtx
