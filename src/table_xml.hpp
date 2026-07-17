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
#include <cstdio>
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

    // utf8PrefixByCodepoints: 按 UTF-8 码点截断（不切开多字节字符）
    inline std::string utf8PrefixByCodepoints(const std::string& s,
                                              size_t             max_cp)
    {
        size_t i = 0, cps = 0;
        while (i < s.size() && cps < max_cp) {
            unsigned char c   = static_cast<unsigned char>(s[i]);
            size_t        len = 1;
            if ((c & 0x80) == 0)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;
            if (i + len > s.size())
                break;
            i += len;
            cps++;
        }
        return s.substr(0, i);
    }

    // sanitizeSheetName: Excel 工作表名限制（≤31 码点，禁 \ / * ? [ ] :）
    inline std::string sanitizeSheetName(const std::string& name)
    {
        std::string s = name.empty() ? "Sheet1" : name;
        for (char& c : s) {
            if (c == '\\' || c == '/' || c == '*' || c == '?' || c == '[' ||
                c == ']' || c == ':')
                c = '_';
        }
        s = utf8PrefixByCodepoints(s, 31);
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

    // parsePositiveSize: 解析正整数；失败返回 0
    inline size_t parsePositiveSize(const std::string& s)
    {
        if (s.empty())
            return 0;
        size_t v = 0;
        for (unsigned char c : s) {
            if (!std::isdigit(c))
                return 0;
            v = v * 10 + static_cast<size_t>(c - '0');
        }
        return v;
    }

    // cellColumnIndex0: SpreadsheetML Cell 列下标（0-based）；支持 ss:Index
    // 参数 next_1based: 下一默认列号（1-based），读写更新
    inline size_t cellColumnIndex0(const gp::detail::XmlNode& cell,
                                   size_t&                    next_1based)
    {
        size_t      col1 = next_1based;
        std::string idx  = attrGet(cell, "Index");
        if (!idx.empty()) {
            size_t parsed = parsePositiveSize(idx);
            if (parsed > 0)
                col1 = parsed;
        }
        next_1based = col1 + 1;
        return col1 - 1;
    }

    // kwEscape: Keywords 值转义（避免 ; = % 打断键值对）
    inline std::string kwEscape(const std::string& s)
    {
        std::ostringstream os;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' ||
                c == '/') {
                os << static_cast<char>(c);
            }
            else {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%%%02X",
                              static_cast<unsigned>(c));
                os << buf;
            }
        }
        return os.str();
    }

    // kwUnescape: Keywords 百分号解码
    inline std::string kwUnescape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            if (s[i] == '%' && i + 2 < s.size() &&
                std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return 0;
                };
                out.push_back(static_cast<char>((hex(s[i + 1]) << 4) |
                                                hex(s[i + 2])));
                i += 3;
            }
            else {
                out.push_back(s[i]);
                i++;
            }
        }
        return out;
    }

    // parseXmlRoot: 解析 XML，失败包装为 TableError
    inline gp::detail::XmlNode parseXmlRoot(const std::string& text)
    {
        try {
            return gp::detail::parseXmlDoc(text);
        }
        catch (const gp::ParseError& e) {
            throw TableError(std::string("table xml: ") + e.what());
        }
    }

    // applyKeywordsMeta: 解析 graphmcp-* Keywords
    inline void applyKeywordsMeta(Table& t, const std::string& kw_text)
    {
        size_t p = 0;
        while (p < kw_text.size()) {
            size_t      semi = kw_text.find(';', p);
            std::string part = gm::trim(kw_text.substr(
                p, semi == std::string::npos ? std::string::npos : semi - p));
            if (part.compare(0, 12, "graphmcp-id=") == 0)
                t.id = kwUnescape(part.substr(12));
            else if (part.compare(0, 20, "graphmcp-hasHintRow=") == 0) {
                std::string v = part.substr(20);
                t.hasHintRow =
                    (v == "true" || v == "1" || ge::parseTruthy(v));
            }
            if (semi == std::string::npos)
                break;
            p = semi + 1;
        }
    }

    // readSparseRow: 按 ss:Index 读一行单元格到定长向量（缺位 ""）
    inline std::vector<std::string> readSparseRow(
        const gp::detail::XmlNode& row_node, size_t width)
    {
        std::vector<std::string> row(width, "");
        size_t                   next_1based = 1;
        for (auto& cell : row_node.children) {
            if (localName(cell.tag) != "Cell")
                continue;
            size_t ci = cellColumnIndex0(cell, next_1based);
            if (ci >= width)
                continue;
            row[ci] = cellDataText(cell);
        }
        return row;
    }

    // readHeaderRow: 读表头（支持 ss:Index 空隙 → col_N）
    inline void readHeaderRow(const gp::detail::XmlNode& row_node, Table& t,
                              std::vector<std::string>* warnings)
    {
        struct Slot
        {
            size_t      index0 = 0;
            std::string text;
        };
        std::vector<Slot> slots;
        size_t            next_1based = 1;
        size_t            max_index0  = 0;
        bool              any         = false;
        for (auto& cell : row_node.children) {
            if (localName(cell.tag) != "Cell")
                continue;
            size_t ci = cellColumnIndex0(cell, next_1based);
            any       = true;
            if (ci > max_index0)
                max_index0 = ci;
            slots.push_back(Slot{ci, cellDataText(cell)});
        }
        if (!any)
            return;
        size_t width = max_index0 + 1;
        std::vector<std::string> headers(width, "");
        for (auto& s : slots)
            headers[s.index0] = s.text;
        for (size_t i = 0; i < width; i++) {
            std::string h = headers[i];
            if (h.empty())
                h = "col_" + std::to_string(i);
            appendColumnUnique(t, h, warnings);
        }
    }

}  // namespace detail

// ---------------------------------------------------------------------------
// 旧方言：根 <table>，命名字段行（历史兼容）
// ---------------------------------------------------------------------------

// fromLegacyTableXmlNode: 已解析的旧方言根节点 → Table
inline Table fromLegacyTableXmlNode(const gp::detail::XmlNode& root,
                                    std::vector<std::string>*  warnings =
                                        nullptr)
{
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

// fromLegacyTableXml: 旧 graphmcp 表 XML → Table
inline Table fromLegacyTableXml(const std::string&        text,
                                std::vector<std::string>* warnings = nullptr)
{
    return fromLegacyTableXmlNode(detail::parseXmlRoot(text), warnings);
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
// 仅写第一个工作表；读入同样只取第一个 Worksheet。
inline std::string toSpreadsheetMl(const Table& t)
{
    std::string sheet = detail::sanitizeSheetName(
        t.name.empty() ? (t.id.empty() ? "Sheet1" : t.id) : t.name);

    std::ostringstream kw;
    if (!t.id.empty())
        kw << "graphmcp-id=" << detail::kwEscape(t.id);
    if (kw.tellp() > 0)
        kw << ';';
    kw << "graphmcp-hasHintRow=" << (t.hasHintRow ? "true" : "false");

    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<?mso-application progid=\"Excel.Sheet\"?>\n";
    os << "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\"\n";
    os << " xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n";
    os << " <DocumentProperties "
          "xmlns=\"urn:schemas-microsoft-com:office:office\">\n";
    if (!t.name.empty())
        os << "  <Title>" << ge::xmlTextEscape(t.name) << "</Title>\n";
    os << "  <Keywords>" << ge::xmlTextEscape(kw.str()) << "</Keywords>\n";
    os << " </DocumentProperties>\n";
    os << " <Worksheet ss:Name=\"" << ge::xmlAttrEscape(sheet) << "\">\n";
    os << "  <Table>\n";

    // 表头行（密铺；Excel 另存可能改成带 ss:Index 的稀疏行）
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

// fromSpreadsheetMlNode: 已解析 SpreadsheetML 根 → Table（首个 Worksheet）
inline Table fromSpreadsheetMlNode(const gp::detail::XmlNode& root,
                                   std::vector<std::string>*  warnings =
                                       nullptr)
{
    if (detail::localName(root.tag) != "Workbook")
        throw TableError("table xml: SpreadsheetML root must be <Workbook>, "
                         "got <" +
                         root.tag + ">");

    Table t;

    if (const auto* props = detail::findChild(root, "DocumentProperties")) {
        if (const auto* title = detail::findChild(*props, "Title"))
            t.name = title->text;
        if (const auto* kw = detail::findChild(*props, "Keywords"))
            detail::applyKeywordsMeta(t, kw->text);
    }

    const gp::detail::XmlNode* ws = detail::findChild(root, "Worksheet");
    if (!ws)
        throw TableError("table xml: SpreadsheetML missing <Worksheet> "
                         "(only first sheet is read)");

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

    detail::readHeaderRow(*rows[0], t, warnings);
    if (t.columns.empty())
        throw TableError("table xml: SpreadsheetML header row empty");

    for (size_t ri = 1; ri < rows.size(); ri++)
        t.appendRow(detail::readSparseRow(*rows[ri], t.columns.size()));
    t.normalize();
    return t;
}

// fromSpreadsheetMl: SpreadsheetML 2003 → Table
inline Table fromSpreadsheetMl(const std::string&        text,
                               std::vector<std::string>* warnings = nullptr)
{
    return fromSpreadsheetMlNode(detail::parseXmlRoot(text), warnings);
}

// toXml: 默认写出 SpreadsheetML 2003
inline std::string toXml(const Table& t)
{ return toSpreadsheetMl(t); }

// fromXml: 自动识别 SpreadsheetML 或旧 <table> 方言（只 parse 一次）
inline Table fromXml(const std::string&        text,
                     std::vector<std::string>* warnings = nullptr)
{
    gp::detail::XmlNode root = detail::parseXmlRoot(text);
    std::string         ln   = detail::localName(root.tag);
    if (ln == "Workbook")
        return fromSpreadsheetMlNode(root, warnings);
    if (ln == "table")
        return fromLegacyTableXmlNode(root, warnings);
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
