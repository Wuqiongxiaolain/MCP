// csv_util.hpp - 通用 CSV 行解析/转义工具（图与表共用）
#pragma once
#include "model.hpp"

namespace gcsv {

using gm::trim;

// splitCsvLine: 解析单行 CSV，支持双引号字段及转义双引号
inline std::vector<std::string> splitCsvLine(const std::string& line)
{
    std::vector<std::string> out;
    std::string              cur;
    bool                     inQ = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQ) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    i++;
                }
                else {
                    inQ = false;
                }
            }
            else {
                cur += c;
            }
        }
        else {
            if (c == '"') {
                inQ = true;
            }
            else if (c == ',') {
                out.push_back(trim(cur));
                cur.clear();
            }
            else {
                cur += c;
            }
        }
    }
    out.push_back(trim(cur));
    return out;
}

// escapeCsvField: 序列化单个字段（含逗号/引号/换行时加引号）
inline std::string escapeCsvField(const std::string& s)
{
    bool need = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            need = true;
            break;
        }
    }
    if (!need)
        return s;

    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

}  // namespace gcsv
