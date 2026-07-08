// json.hpp - graphmcp 使用的最小自包含 JSON 解析/序列化实现
// 无外部依赖，并保留对象键的插入顺序。
#pragma once
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>


namespace gj {

class Json;
using JArray  = std::vector<Json>;
using JObject = std::vector<std::pair<std::string, Json>>;

// Json: 轻量 JSON
// 值类型（命名约定：t=类型，b/n/s=布尔/数字/字符串，a/o=数组/对象）
class Json {
  public:
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ };
    Type                     t = NUL;
    bool                     b = false;
    double                   n = 0;
    std::string              s;
    std::shared_ptr<JArray>  a;
    std::shared_ptr<JObject> o;

    Json() {}
    Json(bool v) : t(BOOL), b(v) {}
    Json(int v) : t(NUM), n(v) {}
    Json(double v) : t(NUM), n(v) {}
    Json(const char* v) : t(STR), s(v) {}
    Json(const std::string& v) : t(STR), s(v) {}

    // arr/obj: 工厂方法，显式创建数组或对象类型，避免调用方手动设置内部字段
    static Json arr()
    {
        Json j;
        j.t = ARR;
        j.a = std::make_shared<JArray>();
        return j;
    }
    static Json obj()
    {
        Json j;
        j.t = OBJ;
        j.o = std::make_shared<JObject>();
        return j;
    }

    bool isNull() const
    { return t == NUL; }
    bool isBool() const
    { return t == BOOL; }
    bool isNum() const
    { return t == NUM; }
    bool isStr() const
    { return t == STR; }
    bool isArr() const
    { return t == ARR; }
    bool isObj() const
    { return t == OBJ; }

    // ---- 对象操作辅助 ----
    // find: 在对象中按 key 查值；找不到返回 nullptr
    const Json* find(const std::string& k) const
    {
        if (t != OBJ || !o)
            return nullptr;
        for (auto& kv : *o)
            if (kv.first == k)
                return &kv.second;
        return nullptr;
    }
    // set: 设置对象字段；若 key 已存在则覆盖，保证调用简单直观
    Json& set(const std::string& k, Json v)
    {
        if (t != OBJ) {
            t = OBJ;
            o = std::make_shared<JObject>();
        }
        for (auto& kv : *o)
            if (kv.first == k) {
                kv.second = std::move(v);
                return kv.second;
            }
        o->emplace_back(k, std::move(v));
        return o->back().second;
    }
    // operator[]: 访问/创建对象字段，便于链式构造 JSON
    Json& operator[](const std::string& k)
    {
        if (t != OBJ) {
            t = OBJ;
            o = std::make_shared<JObject>();
        }
        for (auto& kv : *o)
            if (kv.first == k)
                return kv.second;
        o->emplace_back(k, Json());
        return o->back().second;
    }
    // str/num/boolean: 类型安全读取字段；类型不匹配时返回默认值
    std::string str(const std::string& k, const std::string& def = "") const
    {
        const Json* j = find(k);
        return (j && j->isStr()) ? j->s : def;
    }
    double num(const std::string& k, double def = 0) const
    {
        const Json* j = find(k);
        return (j && j->isNum()) ? j->n : def;
    }
    bool boolean(const std::string& k, bool def = false) const
    {
        const Json* j = find(k);
        return (j && j->isBool()) ? j->b : def;
    }

    // ---- 数组操作辅助 ----
    // push: 向数组追加元素；当前值不是数组时自动转为数组
    void push(Json v)
    {
        if (t != ARR) {
            t = ARR;
            a = std::make_shared<JArray>();
        }
        a->push_back(std::move(v));
    }
    size_t size() const
    {
        if (t == ARR && a)
            return a->size();
        if (t == OBJ && o)
            return o->size();
        return 0;
    }

    // ---- 序列化 ----
    // escape: 序列化前转义控制字符，保证输出是合法 JSON 字符串
    static void escape(const std::string& in, std::string& out)
    {
        for (unsigned char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += (char)c;
            }
        }
    }

    // dumpTo: 递归序列化核心函数
    // 关键步骤：按类型分派 -> 处理嵌套容器 -> 根据 indent 控制格式化
    void dumpTo(std::string& out, int indent, int depth) const
    {
        auto pad = [&](int d) {
            if (indent >= 0) {
                out += '\n';
                out.append((size_t)(indent * d), ' ');
            }
        };
        switch (t) {
            case NUL: out += "null"; break;
            case BOOL: out += b ? "true" : "false"; break;
            case NUM: {
                if (std::isfinite(n) && n == (long long)n &&
                    std::fabs(n) < 9.0e15) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
                    out += buf;
                }
                else {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.10g", n);
                    out += buf;
                }
                break;
            }
            case STR:
                out += '"';
                escape(s, out);
                out += '"';
                break;
            case ARR: {
                out += '[';
                bool first = true;
                if (a)
                    for (auto& v : *a) {
                        if (!first)
                            out += ',';
                        first = false;
                        pad(depth + 1);
                        v.dumpTo(out, indent, depth + 1);
                    }
                if (!first)
                    pad(depth);
                out += ']';
                break;
            }
            case OBJ: {
                out += '{';
                bool first = true;
                if (o)
                    for (auto& kv : *o) {
                        if (!first)
                            out += ',';
                        first = false;
                        pad(depth + 1);
                        out += '"';
                        escape(kv.first, out);
                        out += "\":";
                        if (indent >= 0)
                            out += ' ';
                        kv.second.dumpTo(out, indent, depth + 1);
                    }
                if (!first)
                    pad(depth);
                out += '}';
                break;
            }
        }
    }
    std::string dump(int indent = -1) const
    {
        std::string out;
        dumpTo(out, indent, 0);
        return out;
    }

    // ---- 解析 ----
    // Parser: 递归下降解析器（命名上 pos=当前游标，err=首个错误）
    struct Parser
    {
        const std::string& src;
        size_t             pos = 0;
        std::string        err;
        explicit Parser(const std::string& s) : src(s) {}

        // skipWs: 跳过空白，确保后续解析从有效 token 开始
        void skipWs()
        {
            while (pos < src.size()) {
                char c = src[pos];
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    pos++;
                else
                    break;
            }
        }
        // fail: 记录首个错误并携带偏移位置，方便定位问题输入
        bool fail(const std::string& m)
        {
            if (err.empty()) {
                std::ostringstream os;
                os << m << " at offset " << pos;
                err = os.str();
            }
            return false;
        }
        // parseValue: JSON 值分发入口（对象/数组/字符串/字面量/数字）
        bool parseValue(Json& out)
        {
            skipWs();
            if (pos >= src.size())
                return fail("unexpected end of input");
            char c = src[pos];
            if (c == '{')
                return parseObject(out);
            if (c == '[')
                return parseArray(out);
            if (c == '"') {
                out.t = STR;
                return parseString(out.s);
            }
            if (c == 't') {
                if (src.compare(pos, 4, "true") == 0) {
                    pos += 4;
                    out = Json(true);
                    return true;
                }
                return fail("bad literal");
            }
            if (c == 'f') {
                if (src.compare(pos, 5, "false") == 0) {
                    pos += 5;
                    out = Json(false);
                    return true;
                }
                return fail("bad literal");
            }
            if (c == 'n') {
                if (src.compare(pos, 4, "null") == 0) {
                    pos += 4;
                    out = Json();
                    return true;
                }
                return fail("bad literal");
            }
            return parseNumber(out);
        }
        // parseNumber: 读取数字 token 并交给 stod 转换
        bool parseNumber(Json& out)
        {
            size_t start = pos;
            if (pos < src.size() && (src[pos] == '-' || src[pos] == '+'))
                pos++;
            while (pos < src.size() &&
                   (isdigit((unsigned char)src[pos]) || src[pos] == '.' ||
                    src[pos] == 'e' || src[pos] == 'E' || src[pos] == '-' ||
                    src[pos] == '+'))
                pos++;
            if (pos == start)
                return fail("invalid number");
            try {
                out = Json(std::stod(src.substr(start, pos - start)));
            }
            catch (...) {
                return fail("invalid number");
            }
            return true;
        }
        // parseString: 解析字符串，支持常见转义与 \uXXXX（含代理对）
        // 关键步骤：逐字符扫描 -> 处理反斜杠转义 -> 最终编码为 UTF-8
        bool parseString(std::string& out)
        {
            if (src[pos] != '"')
                return fail("expected string");
            pos++;
            out.clear();
            while (pos < src.size()) {
                char c = src[pos];
                if (c == '"') {
                    pos++;
                    return true;
                }
                if (c == '\\') {
                    pos++;
                    if (pos >= src.size())
                        return fail("bad escape");
                    char e = src[pos++];
                    switch (e) {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            if (pos + 4 > src.size())
                                return fail("bad \\u escape");
                            unsigned cp = 0;
                            for (int i = 0; i < 4; i++) {
                                char h = src[pos++];
                                cp <<= 4;
                                if (h >= '0' && h <= '9')
                                    cp |= (unsigned)(h - '0');
                                else if (h >= 'a' && h <= 'f')
                                    cp |= (unsigned)(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F')
                                    cp |= (unsigned)(h - 'A' + 10);
                                else
                                    return fail("bad hex digit");
                            }
                            // 代理对
                            if (cp >= 0xD800 && cp <= 0xDBFF &&
                                pos + 6 <= src.size() && src[pos] == '\\' &&
                                src[pos + 1] == 'u') {
                                unsigned lo   = 0;
                                size_t   save = pos;
                                pos += 2;
                                bool ok = true;
                                for (int i = 0; i < 4; i++) {
                                    char h = src[pos++];
                                    lo <<= 4;
                                    if (h >= '0' && h <= '9')
                                        lo |= (unsigned)(h - '0');
                                    else if (h >= 'a' && h <= 'f')
                                        lo |= (unsigned)(h - 'a' + 10);
                                    else if (h >= 'A' && h <= 'F')
                                        lo |= (unsigned)(h - 'A' + 10);
                                    else {
                                        ok = false;
                                        break;
                                    }
                                }
                                if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) +
                                         (lo - 0xDC00);
                                else
                                    pos = save;
                            }
                            // 编码为 UTF-8
                            if (cp < 0x80)
                                out += (char)cp;
                            else if (cp < 0x800) {
                                out += (char)(0xC0 | (cp >> 6));
                                out += (char)(0x80 | (cp & 0x3F));
                            }
                            else if (cp < 0x10000) {
                                out += (char)(0xE0 | (cp >> 12));
                                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                out += (char)(0x80 | (cp & 0x3F));
                            }
                            else {
                                out += (char)(0xF0 | (cp >> 18));
                                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                out += (char)(0x80 | (cp & 0x3F));
                            }
                            break;
                        }
                        default: return fail("unknown escape");
                    }
                }
                else {
                    out += c;
                    pos++;
                }
            }
            return fail("unterminated string");
        }
        // parseArray: 解析数组，循环读取 value 并处理逗号/结束括号
        bool parseArray(Json& out)
        {
            pos++;  // 读取 '['
            out = Json::arr();
            skipWs();
            if (pos < src.size() && src[pos] == ']') {
                pos++;
                return true;
            }
            while (true) {
                Json v;
                if (!parseValue(v))
                    return false;
                out.a->push_back(std::move(v));
                skipWs();
                if (pos >= src.size())
                    return fail("unterminated array");
                if (src[pos] == ',') {
                    pos++;
                    continue;
                }
                if (src[pos] == ']') {
                    pos++;
                    return true;
                }
                return fail("expected , or ]");
            }
        }
        // parseObject: 解析对象，按 "key":value 形式逐项读取
        bool parseObject(Json& out)
        {
            pos++;  // 读取 '{'
            out = Json::obj();
            skipWs();
            if (pos < src.size() && src[pos] == '}') {
                pos++;
                return true;
            }
            while (true) {
                skipWs();
                std::string key;
                if (pos >= src.size() || src[pos] != '"')
                    return fail("expected object key");
                if (!parseString(key))
                    return false;
                skipWs();
                if (pos >= src.size() || src[pos] != ':')
                    return fail("expected :");
                pos++;
                Json v;
                if (!parseValue(v))
                    return false;
                out.o->emplace_back(std::move(key), std::move(v));
                skipWs();
                if (pos >= src.size())
                    return fail("unterminated object");
                if (src[pos] == ',') {
                    pos++;
                    continue;
                }
                if (src[pos] == '}') {
                    pos++;
                    return true;
                }
                return fail("expected , or }");
            }
        }
    };

    // parse: 对外解析入口；返回 Json，同时通过 err 输出错误信息
    static Json parse(const std::string& text, std::string* err = nullptr)
    {
        Parser p(text);
        Json   out;
        if (!p.parseValue(out)) {
            if (err)
                *err = p.err;
            return Json();
        }
        p.skipWs();
        if (p.pos != text.size() && err && err->empty()) {
            // 为了鲁棒性可容忍尾随内容，但会在 err 中反馈
        }
        if (err)
            *err = p.err;
        return out;
    }
};

}  // namespace gj
