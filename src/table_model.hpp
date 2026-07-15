// table_model.hpp - 通用 CSV 记录表（与 Graph 并列的一等对象）
// 权威交换格式为 CSV；仓库内以 JSON（columns + rows）持久化。
#pragma once
#include "csv_util.hpp"
#include "model.hpp"
#include <algorithm>
#include <stdexcept>

namespace gt {

using gj::Json;
using gm::splitLines;
using gm::toLower;
using gm::trim;

// TableError: 表解析/编辑异常
struct TableError : std::runtime_error
{
    explicit TableError(const std::string& m) : std::runtime_error(m) {}
};

using gcsv::escapeCsvField;
using gcsv::splitCsvLine;

// Table: 通用记录矩阵（列名 + 矩形行）
struct Table
{
    std::string                           id;
    std::string                           name;
    bool                                  hasHintRow = false;
    std::vector<std::string>              columns;
    std::vector<std::vector<std::string>> rows;

    // normalize: 将所有行补齐到 columns.size()
    void normalize()
    {
        size_t w = columns.size();
        for (auto& r : rows) {
            if (r.size() < w)
                r.resize(w);
            else if (r.size() > w)
                r.resize(w);
        }
    }

    // colIndex: 按列名查找（大小写不敏感）；找不到返回 -1
    int colIndex(const std::string& name) const
    {
        std::string key = toLower(name);
        for (size_t i = 0; i < columns.size(); i++)
            if (toLower(columns[i]) == key)
                return (int)i;
        return -1;
    }

    // cell: 读取单元格；越界返回空串
    std::string cell(size_t row, size_t col) const
    {
        if (row >= rows.size() || col >= rows[row].size())
            return "";
        return rows[row][col];
    }

    // setCell: 设置单元格；必要时扩展行列
    void setCell(size_t row, size_t col, const std::string& value)
    {
        if (col >= columns.size())
            throw TableError("column index out of range");
        while (rows.size() <= row)
            rows.push_back(std::vector<std::string>(columns.size()));
        if (rows[row].size() < columns.size())
            rows[row].resize(columns.size());
        rows[row][col] = value;
    }

    void appendRow(const std::vector<std::string>& row)
    {
        std::vector<std::string> r = row;
        if (r.size() < columns.size())
            r.resize(columns.size());
        else if (r.size() > columns.size())
            r.resize(columns.size());
        rows.push_back(std::move(r));
    }

    void insertRow(size_t index, const std::vector<std::string>& row)
    {
        if (index > rows.size())
            index = rows.size();
        std::vector<std::string> r = row;
        if (r.size() < columns.size())
            r.resize(columns.size());
        else if (r.size() > columns.size())
            r.resize(columns.size());
        rows.insert(rows.begin() + (std::ptrdiff_t)index, std::move(r));
    }

    void deleteRow(size_t index)
    {
        if (index >= rows.size())
            throw TableError("row index out of range");
        rows.erase(rows.begin() + (std::ptrdiff_t)index);
    }

    void addColumn(const std::string& name, const std::string& defaultVal = "")
    {
        if (colIndex(name) >= 0)
            throw TableError("column already exists: " + name);
        columns.push_back(name);
        for (auto& r : rows)
            r.push_back(defaultVal);
    }

    void renameColumn(const std::string& oldName, const std::string& newName)
    {
        int i = colIndex(oldName);
        if (i < 0)
            throw TableError("column not found: " + oldName);
        if (colIndex(newName) >= 0 && toLower(oldName) != toLower(newName))
            throw TableError("column already exists: " + newName);
        columns[(size_t)i] = newName;
    }

    void deleteColumn(const std::string& name)
    {
        int i = colIndex(name);
        if (i < 0)
            throw TableError("column not found: " + name);
        columns.erase(columns.begin() + i);
        for (auto& r : rows) {
            if ((size_t)i < r.size())
                r.erase(r.begin() + i);
        }
    }

    // ensureColumns: 若列不存在则追加
    void ensureColumns(const std::vector<std::string>& names)
    {
        for (auto& n : names)
            if (colIndex(n) < 0)
                addColumn(n);
    }

    // toCsv: 导出 CSV 文本（含表头）
    std::string toCsv() const
    {
        std::string out;
        for (size_t i = 0; i < columns.size(); i++) {
            if (i)
                out += ',';
            out += escapeCsvField(columns[i]);
        }
        out += '\n';
        for (auto& r : rows) {
            for (size_t i = 0; i < columns.size(); i++) {
                if (i)
                    out += ',';
                std::string v = i < r.size() ? r[i] : "";
                out += escapeCsvField(v);
            }
            out += '\n';
        }
        return out;
    }

    // fromCsv: 从 CSV 文本构建表（首行为表头）
    static Table fromCsv(const std::string& text)
    {
        auto lines = splitLines(text);
        // 去掉尾部空行
        while (!lines.empty() && trim(lines.back()).empty())
            lines.pop_back();
        if (lines.empty())
            throw TableError("empty csv input");
        Table       t;
        std::string header = lines[0];
        // Excel“CSV UTF-8”常带 BOM，需剥离避免首列匹配失败
        if (header.size() >= 3 && (unsigned char)header[0] == 0xEF &&
            (unsigned char)header[1] == 0xBB &&
            (unsigned char)header[2] == 0xBF) {
            header = header.substr(3);
        }
        t.columns = splitCsvLine(header);
        if (t.columns.empty())
            throw TableError("csv has no columns");
        for (size_t li = 1; li < lines.size(); li++) {
            if (trim(lines[li]).empty())
                continue;
            t.appendRow(splitCsvLine(lines[li]));
        }
        t.normalize();
        return t;
    }

    Json toJson() const
    {
        Json j = Json::obj();
        j.set("id", id);
        j.set("name", name);
        j.set("hasHintRow", hasHintRow);
        Json cols = Json::arr();
        for (auto& c : columns)
            cols.push(Json(c));
        j.set("columns", cols);
        Json rs = Json::arr();
        for (auto& r : rows) {
            Json jr = Json::arr();
            for (size_t i = 0; i < columns.size(); i++)
                jr.push(Json(i < r.size() ? r[i] : ""));
            rs.push(jr);
        }
        j.set("rows", rs);
        return j;
    }

    static Table fromJson(const Json& j)
    {
        Table t;
        t.id         = j.str("id");
        t.name       = j.str("name");
        t.hasHintRow = j.boolean("hasHintRow", false);
        if (const Json* cols = j.find("columns")) {
            if (cols->isArr())
                for (auto& c : *cols->a)
                    if (c.isStr())
                        t.columns.push_back(c.s);
        }
        if (const Json* rs = j.find("rows")) {
            if (rs->isArr())
                for (auto& r : *rs->a) {
                    std::vector<std::string> row;
                    if (r.isArr())
                        for (auto& cell : *r.a)
                            row.push_back(cell.isStr() ? cell.s : cell.dump());
                    t.rows.push_back(std::move(row));
                }
        }
        t.normalize();
        return t;
    }
};

// tableDiff: 比较两表，返回摘要 JSON（列增删、行数变化、逐格差异计数）
inline Json tableDiff(const Table& a, const Table& b)
{
    Json out = Json::obj();
    out.set("rows_a", (double)a.rows.size());
    out.set("rows_b", (double)b.rows.size());
    out.set("cols_a", (double)a.columns.size());
    out.set("cols_b", (double)b.columns.size());

    Json addedCols   = Json::arr();
    Json removedCols = Json::arr();
    for (auto& c : b.columns)
        if (a.colIndex(c) < 0)
            addedCols.push(Json(c));
    for (auto& c : a.columns)
        if (b.colIndex(c) < 0)
            removedCols.push(Json(c));
    out.set("added_columns", addedCols);
    out.set("removed_columns", removedCols);

    int    cellChanges = 0;
    size_t commonRows  = std::min(a.rows.size(), b.rows.size());
    for (size_t r = 0; r < commonRows; r++) {
        for (auto& c : a.columns) {
            int ib = b.colIndex(c);
            if (ib < 0)
                continue;
            int ia = a.colIndex(c);
            if (a.cell(r, (size_t)ia) != b.cell(r, (size_t)ib))
                cellChanges++;
        }
    }
    out.set("cell_changes", (double)cellChanges);
    out.set("row_delta", (double)((int)b.rows.size() - (int)a.rows.size()));
    return out;
}

}  // namespace gt
