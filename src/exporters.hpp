// exporters.hpp - 统一图模型导出：drawio XML、Mermaid、Excalidraw JSON、
// SVG、浏览器 URL；PNG/PDF 在可用时通过外部转换器生成。
#pragma once
#include "layout.hpp"
#include "model.hpp"
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

#ifdef _WIN32
#    include <direct.h>
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

#    include <shellapi.h>
#elif defined(__APPLE__)
#    include <cerrno>
#    include <dirent.h>
#    include <fcntl.h>
#    include <limits.h>
#    include <sys/file.h>
#    include <unistd.h>

#    include <mach-o/dyld.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <signal.h>
#    ifndef PATH_MAX
#        define PATH_MAX 4096
#    endif
#else
#    include <cerrno>
#    include <dirent.h>
#    include <fcntl.h>
#    include <limits.h>
#    include <sys/file.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <signal.h>
#    include <unistd.h>
#    ifndef PATH_MAX
#        define PATH_MAX 4096
#    endif
#endif

namespace ge {

using gj::Json;
using gm::Edge;
using gm::Graph;
using gm::Node;

// FreedrawStroke: 从 Excalidraw freedraw 提取出的矢量笔迹
struct FreedrawStroke
{
    std::string                            id;
    std::string                            strokeColor = "#1e1e1e";
    std::string                            strokeStyle = "solid";
    double                                 strokeWidth = 2.0;
    double                                 opacity     = 1.0;
    std::vector<std::pair<double, double>> points;  // 绝对坐标点列
};

// ------------------------------------------------------------------ 工具函数
// --

// xmlEscape: 通用 XML 转义（文本与属性均安全；含单引号）
inline std::string xmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

// xmlTextEscape: XML 文本节点转义；保留 CSS 单/双引号语义
inline std::string xmlTextEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            default: out += c;
        }
    }
    return out;
}

// xmlAttrEscape: 双引号包裹的 XML 属性值转义（不转义单引号）
inline std::string xmlAttrEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// base64Encode: URL/嵌入场景下的二进制安全编码
inline std::string base64Encode(const std::string& in)
{
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t      i = 0;
    while (i + 2 < in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) |
                     ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    }
    else if (i + 2 == in.size()) {
        unsigned v =
            ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// getEnvVar: 跨平台读取环境变量；Windows 下避免直接使用废弃 getenv
inline std::string getEnvVar(const char* name)
{
#if defined(_WIN32) && !defined(__MINGW32__)
    char*  value = nullptr;
    size_t len   = 0;
    if (_dupenv_s(&value, &len, name) != 0 || !value)
        return "";
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

// envFlagEnabled: 仅 "1" / "true"（大小写不敏感）视为启用
inline bool envFlagEnabled(const char* name)
{
    std::string v = getEnvVar(name);
    std::string lower;
    lower.reserve(v.size());
    for (unsigned char c : v)
        lower.push_back((char)std::tolower(c));
    return lower == "1" || lower == "true";
}

// parseTruthy: 解析布尔字面量；仅 "1"/"true"（大小写不敏感）为真
inline bool parseTruthy(const std::string& raw)
{
    std::string lower;
    lower.reserve(raw.size());
    for (unsigned char c : raw)
        lower.push_back((char)std::tolower(c));
    return lower == "1" || lower == "true";
}

// 创建文件路径的所有父目录（尽力而为）
// ensureParentDirs: 按路径逐级创建父目录（best-effort）
inline void ensureParentDirs(const std::string& path)
{
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] == '/' || path[i] == '\\') {
            std::string dir = path.substr(0, i);
            if (dir.size() == 2 && dir[1] == ':')
                continue;  // Windows 盘符
#ifdef _WIN32
            _mkdir(dir.c_str());
#else
            mkdir(dir.c_str(), 0755);
#endif
        }
    }
}

// writeFile/readFile: 统一文件 IO 封装，供导出与存储模块复用
inline bool writeFile(const std::string& path, const std::string& content)
{
    ensureParentDirs(path);
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(content.data(), (std::streamsize)content.size());
    return f.good();
}

// writeFileAtomic: 先写唯一 tmp 再原子替换到 path；失败时清理残留
inline bool writeFileAtomic(const std::string& path, const std::string& content)
{
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    unsigned long pid        = GetCurrentProcessId();
    auto          toWidePath = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    };
#else
    long pid = (long)getpid();
#endif
    std::string tmp = path + ".tmp." + std::to_string(pid) + "." +
                      std::to_string((long long)stamp);

    if (!writeFile(tmp, content)) {
#ifdef _WIN32
        DeleteFileW(toWidePath(tmp).c_str());
#else
        std::remove(tmp.c_str());
#endif
        return false;
    }
#ifdef _WIN32
    std::wstring wtmp  = toWidePath(tmp);
    std::wstring wpath = toWidePath(path);
    // Windows 读者未开启 FILE_SHARE_DELETE 时，原子替换会短暂失败；
    // 对共享冲突进行有界重试，避免 latest/HEAD 已写而 index 未提交。
    for (int attempt = 0; attempt < 100; ++attempt) {
        // REPLACE_EXISTING 保证同卷原子替换；不使用 WRITE_THROUGH，
        // 避免每个 latest/snapshot/meta/index/HEAD 都同步刷盘。
        if (MoveFileExW(wtmp.c_str(), wpath.c_str(),
                        MOVEFILE_REPLACE_EXISTING))
            return true;
        DWORD code = GetLastError();
        // 共享冲突常表现为 SHARING_VIOLATION；部分环境下也会变成 ACCESS_DENIED。
        // 真实权限问题会在有界重试后仍失败并返回 false。
        if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED)
            break;
        Sleep(20);
    }
    DeleteFileW(wtmp.c_str());
    return false;
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
#endif
}

// StoreLock: 跨进程存储互斥（保护 index.json 合并写与图 save 原子性）
// Windows 用 LockFileEx；POSIX 用 flock。
class StoreLock {
  public:
    // 获取 <root>/.store.lock 独占锁；timeoutMs 为最长等待（0=非阻塞）
    explicit StoreLock(const std::string& root, int timeoutMs = 30000)
        : locked_(false)
    {
        lockPath_ = root + "/.store.lock";
        ensureParentDirs(lockPath_);
#ifdef _WIN32
        auto toWide = [](const std::string& s) {
            int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
            return w;
        };
        handle_ = CreateFileW(toWide(lockPath_).c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            return;
        OVERLAPPED ov;
        ZeroMemory(&ov, sizeof(ov));
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs
                                                                : 0);
        for (;;) {
            DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
            if (LockFileEx(handle_, flags, 0, 1, 0, &ov)) {
                locked_ = true;
                return;
            }
            if (timeoutMs == 0 ||
                std::chrono::steady_clock::now() >= deadline) {
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
                return;
            }
            Sleep(20);
        }
#else
        fd_ = ::open(lockPath_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0)
            return;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs
                                                                : 0);
        for (;;) {
            int flags = LOCK_EX | (timeoutMs == 0 ? LOCK_NB : 0);
            if (timeoutMs > 0) {
                // 非阻塞重试以支持超时
                flags = LOCK_EX | LOCK_NB;
            }
            if (flock(fd_, flags) == 0) {
                locked_ = true;
                return;
            }
            if (timeoutMs == 0 ||
                std::chrono::steady_clock::now() >= deadline) {
                ::close(fd_);
                fd_ = -1;
                return;
            }
            usleep(20000);
        }
#endif
    }

    ~StoreLock()
    { unlock(); }

    StoreLock(const StoreLock&)            = delete;
    StoreLock& operator=(const StoreLock&) = delete;

    bool locked() const
    { return locked_; }

    void unlock()
    {
        if (!locked_)
            return;
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov;
            ZeroMemory(&ov, sizeof(ov));
            UnlockFileEx(handle_, 0, 1, 0, &ov);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            ::close(fd_);
            fd_ = -1;
        }
#endif
        locked_ = false;
    }

  private:
    std::string lockPath_;
    bool        locked_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

// exportTimeoutMs: 外部转换硬超时（毫秒），环境变量 GRAPHMCP_EXPORT_TIMEOUT_MS
inline int exportTimeoutMs()
{
    std::string e = getEnvVar("GRAPHMCP_EXPORT_TIMEOUT_MS");
    if (e.empty())
        return 60000;
    int v = std::atoi(e.c_str());
    return v > 0 ? v : 60000;
}

// inlineExportMaxBytes: 内联导出护栏默认 1MB，可被 GRAPHMCP_INLINE_MAX_BYTES 覆盖
inline size_t inlineExportMaxBytes()
{
    std::string e = getEnvVar("GRAPHMCP_INLINE_MAX_BYTES");
    if (e.empty())
        return 1024 * 1024;
    long long v = std::atoll(e.c_str());
    return v > 0 ? (size_t)v : (size_t)(1024 * 1024);
}

inline std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return "";
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

// removeDirectory: 递归删除目录及其内容（不使用 shell，避免注入风险）
inline bool removeDirectory(const std::string& dirPath)
{
#ifdef _WIN32
    auto toWide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    };
    std::wstring     wpath      = toWide(dirPath);
    std::wstring     searchPath = wpath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE           hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (DeleteFileW(wpath.c_str()))
            return true;
        return RemoveDirectoryW(wpath.c_str()) != 0;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        std::wstring child = wpath + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            int clen = WideCharToMultiByte(CP_UTF8, 0, child.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
            std::string childUtf8(clen > 0 ? (size_t)(clen - 1) : 0, '\0');
            if (clen > 0)
                WideCharToMultiByte(CP_UTF8, 0, child.c_str(), -1,
                                    &childUtf8[0], clen, nullptr, nullptr);
            removeDirectory(childUtf8);
        }
        else {
            DeleteFileW(child.c_str());
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return RemoveDirectoryW(wpath.c_str()) != 0;
#else
    DIR* dir = opendir(dirPath.c_str());
    if (!dir)
        return remove(dirPath.c_str()) == 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        std::string child = dirPath + "/" + entry->d_name;
        struct stat st;
        if (stat(child.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode))
                removeDirectory(child);
            else
                remove(child.c_str());
        }
    }
    closedir(dir);
    return rmdir(dirPath.c_str()) == 0;
#endif
}

// fileReadable: 探测路径是否可打开读取
inline bool fileReadable(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

// joinPath: 用 / 拼接路径片段（兼容 Windows API 接受正斜杠）
inline std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    char last = a.back();
    if (last == '/' || last == '\\')
        return a + b;
    return a + "/" + b;
}

// executableDir: 当前进程可执行文件所在目录（失败返回空）
inline std::string executableDir()
{
#ifdef _WIN32
    char  buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return "";
    std::string path(buf, n);
#else
    char    buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return "";
    buf[n] = '\0';
    std::string path(buf);
#endif
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
        return "";
    return path.substr(0, slash);
}

// bundledAssetPath: 解析 vendored Excalidraw 资源文件
// 顺序：GRAPHMCP_ASSETS → 可执行文件旁 third_party → CWD 相对路径
inline std::string bundledAssetPath(const std::string& name)
{
    std::vector<std::string> candidates;
    std::string              env = getEnvVar("GRAPHMCP_ASSETS");
    if (!env.empty()) {
        candidates.push_back(joinPath(env, name));
        candidates.push_back(
            joinPath(joinPath(env, "excalidraw-assets"), name));
        candidates.push_back(
            joinPath(joinPath(env, "third_party/excalidraw-assets"), name));
    }
    std::string exeDir = executableDir();
    if (!exeDir.empty()) {
        candidates.push_back(
            joinPath(exeDir, joinPath("third_party/excalidraw-assets", name)));
        candidates.push_back(joinPath(
            exeDir, joinPath("../third_party/excalidraw-assets", name)));
        candidates.push_back(joinPath(
            exeDir, joinPath("../../third_party/excalidraw-assets", name)));
    }
    candidates.push_back(joinPath("third_party/excalidraw-assets", name));

    for (const auto& c : candidates) {
        if (fileReadable(c))
            return c;
    }
    return "";
}

struct ExcalidrawFontMetrics
{
    int    unitsPerEm = 1000;
    int    ascender   = 886;
    int    descender  = -374;
    double lineHeight = 1.25;
    // 家族名用单引号：写入 font-family="..." 时由 xmlAttrEscape 保留 '
    std::string cssFamily =
        "'Excalifont',sans-serif,'Segoe UI Emoji','Apple Color Emoji'";
};

inline ExcalidrawFontMetrics excalidrawFontMetricsByFamily(int fontFamily)
{
    switch (fontFamily) {
        case 1:
            return {1000, 886, -374, 1.25,
                    "'Virgil',sans-serif,'Segoe UI Emoji','Apple Color Emoji'"};
        case 2:
            return {2048, 1577, -471, 1.15,
                    "'Helvetica Neue',Arial,sans-serif,'Segoe UI "
                    "Emoji','Apple Color Emoji'"};
        case 3:
            return {2048, 1900, -480, 1.2,
                    "'Cascadia',Consolas,monospace,'Segoe UI Emoji','Apple "
                    "Color Emoji'"};
        case 5:
            return {1000, 886, -374, 1.25,
                    "'Excalifont',sans-serif,'Segoe UI Emoji','Apple Color "
                    "Emoji'"};
        default: return {};
    }
}

inline int excalidrawFontFamily(const Json& el)
{ return (int)std::llround(el.num("fontFamily", 5)); }

inline double excalidrawLineHeight(const Json& el)
{
    if (const Json* lh = el.find("lineHeight"))
        return lh->isNum() ? lh->n : 1.25;
    return excalidrawFontMetricsByFamily(excalidrawFontFamily(el)).lineHeight;
}

inline std::string excalidrawTextFontFamilyCss(const Json& el)
{ return excalidrawFontMetricsByFamily(excalidrawFontFamily(el)).cssFamily; }

inline std::string excalidrawEmbeddedFontCss()
{
    // 非空才缓存：首轮若资源未就绪（错误 CWD），后续可重试加载
    static std::string css;
    if (!css.empty())
        return css;

    auto fontFace = [](const std::string& family, const std::string& file,
                       const std::string& unicodeRange = "") {
        std::string data = readFile(bundledAssetPath(file));
        if (data.empty())
            return std::string();
        std::ostringstream os;
        os << "@font-face{font-family:'" << family
           << "';src:url(data:font/woff2;base64," << base64Encode(data)
           << ") format('woff2');font-weight:400;font-style:normal;";
        if (!unicodeRange.empty())
            os << "unicode-range:" << unicodeRange << ";";
        os << "}";
        return os.str();
    };

    std::ostringstream os;
    os << fontFace("Virgil", "Virgil.woff2");
    os << fontFace("Cascadia", "Cascadia.woff2");
    os << fontFace(
        "Excalifont",
        "Excalifont-Regular-a88b72a24fb54c9f94e3b5fdaa7481c9.woff2",
        "U+20-7e,U+a0-a3,U+a5-a6,U+a8-ab,U+ad-b1,U+b4,U+b6-b8,U+ba-ff,"
        "U+131,U+152-153,U+2bc,U+2c6,U+2da,U+2dc,U+304,U+308,U+2013-2014,"
        "U+2018-201a,U+201c-201e,U+2020,U+2022,U+2024-2026,U+2030,"
        "U+2039-203a,U+20ac,U+2122,U+2212");
    os << fontFace("Excalifont",
                   "Excalifont-Regular-be310b9bcd4f1a43f571c46df7809174.woff2",
                   "U+100-130,U+132-137,U+139-149,U+14c-151,U+154-17e,U+192,"
                   "U+1fc-1ff,U+218-21b,U+237,U+1e80-1e85,U+1ef2-1ef3,U+2113");
    os << fontFace("Excalifont",
                   "Excalifont-Regular-b9dcf9d2e50a1eaf42fc664b50a3fd0d.woff2",
                   "U+400-45f,U+490-491,U+2116");
    os << fontFace(
        "Excalifont",
        "Excalifont-Regular-41b173a47b57366892116a575a43e2b6.woff2",
        "U+37e,U+384-38a,U+38c,U+38e-393,U+395-3a1,U+3a3-3a8,U+3aa-3cf,"
        "U+3d7");
    os << fontFace(
        "Excalifont",
        "Excalifont-Regular-3f2c5db56cc93c5a6873b1361d730c16.woff2",
        "U+2c7,U+2d8-2d9,U+2db,U+2dd,U+302,U+306-307,U+30a-30c,U+326-328,"
        "U+212e,U+2211,U+fb01-fb02");
    os << fontFace(
        "Excalifont",
        "Excalifont-Regular-349fac6ca4700ffec595a7150a0d1e1d.woff2",
        "U+462-463,U+472-475,U+4d8-4d9,U+4e2-4e3,U+4e6-4e9,U+4ee-4ef");
    os << fontFace("Excalifont",
                   "Excalifont-Regular-623ccf21b21ef6b3a0d87738f77eb071.woff2",
                   "U+300-301,U+303");
    css = os.str();
    return css;
}

// collectFreedrawStrokes: 从 g.elements 提取 freedraw 并转换为绝对坐标点列
inline std::vector<FreedrawStroke> collectFreedrawStrokes(const Graph& g)
{
    std::vector<FreedrawStroke> out;
    for (const auto& el : g.elements) {
        if (el.str("type") != "freedraw")
            continue;
        const Json* pts = el.find("points");
        if (!pts || !pts->isArr() || pts->size() < 2)
            continue;
        FreedrawStroke s;
        s.id          = el.str("id");
        s.strokeColor = el.str("strokeColor", "#1e1e1e");
        s.strokeStyle = el.str("strokeStyle", "solid");
        s.strokeWidth = el.num("strokeWidth", 2);
        s.opacity =
            std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
        double bx = el.num("x");
        double by = el.num("y");
        for (const auto& p : *pts->a) {
            if (!p.isArr() || p.size() < 2)
                continue;
            const Json& px = p.a->at(0);
            const Json& py = p.a->at(1);
            if (!px.isNum() || !py.isNum())
                continue;
            s.points.push_back({bx + px.n, by + py.n});
        }
        if (s.points.size() >= 2)
            out.push_back(std::move(s));
    }
    return out;
}

// isWhiteboardElements: 是否应按 Excalidraw 原始 elements 渲染
inline bool isWhiteboardElements(const Graph& g)
{ return g.type == "whiteboard" && !g.elements.empty(); }

// elementAbsolutePoints: 将元素局部 points 转为画布绝对坐标
inline std::vector<std::pair<double, double>>
elementAbsolutePoints(const Json& el)
{
    std::vector<std::pair<double, double>> out;
    const Json*                            pts = el.find("points");
    if (!pts || !pts->isArr())
        return out;
    double bx = el.num("x");
    double by = el.num("y");
    for (const auto& p : *pts->a) {
        if (!p.isArr() || p.size() < 2)
            continue;
        const Json& px = p.a->at(0);
        const Json& py = p.a->at(1);
        if (!px.isNum() || !py.isNum())
            continue;
        out.push_back({bx + px.n, by + py.n});
    }
    return out;
}

// extendBounds: 用矩形扩展画布边界
inline void extendBounds(double& minX,
                         double& minY,
                         double& maxX,
                         double& maxY,
                         double  x,
                         double  y,
                         double  w,
                         double  h)
{
    minX = std::min(minX, x);
    minY = std::min(minY, y);
    maxX = std::max(maxX, x + w);
    maxY = std::max(maxY, y + h);
}

// svgDashAttr: Excalidraw 线型映射为 SVG dash 属性片段
inline std::string svgDashAttr(const std::string& strokeStyle)
{
    if (strokeStyle == "dashed")
        return " stroke-dasharray=\"6,4\"";
    if (strokeStyle == "dotted")
        return " stroke-dasharray=\"1,4\"";
    return "";
}

// svgFill: 将 Excalidraw 背景色映射为 SVG fill（transparent -> none）
inline std::string svgFill(const Json& el)
{
    std::string bg = el.str("backgroundColor", "transparent");
    if (bg.empty() || bg == "transparent")
        return "none";
    return bg;
}

// svgTextAnchor: Excalidraw 文本对齐映射
inline std::string svgTextAnchor(const std::string& align)
{
    if (align == "center")
        return "middle";
    if (align == "right")
        return "end";
    return "start";
}

// excalidrawTextSvgXAt: 给定 bbox 左上角计算 SVG 文本锚点 x
inline double excalidrawTextSvgXAt(double bx, const Json& el)
{
    double      w  = el.num("width");
    std::string ta = el.str("textAlign", "left");
    if (ta == "center")
        return bx + w / 2;
    if (ta == "right")
        return bx + w;
    return bx;
}

// excalidrawTextSvgYAt: 给定 bbox 左上角计算 alphabetic baseline y
inline double
excalidrawTextSvgYAt(double by, const Json& el, size_t lineIndex = 0)
{
    double                fs = el.num("fontSize", 16);
    ExcalidrawFontMetrics metrics =
        excalidrawFontMetricsByFamily(excalidrawFontFamily(el));
    double lh             = excalidrawLineHeight(el);
    double lineHeightPx   = fs * lh;
    double fontSizeEm     = fs / metrics.unitsPerEm;
    double verticalOffset = fontSizeEm * metrics.ascender +
                            (lineHeightPx - fontSizeEm * metrics.ascender +
                             fontSizeEm * metrics.descender) /
                                2.0;
    return by + lineIndex * lineHeightPx + verticalOffset;
}

inline std::pair<double, double>
polylineMidpoint(const std::vector<std::pair<double, double>>& pts);

// arrowBoundTextBBoxOrigin: 折线箭头嵌字 bbox 左上角（按路径长度中点）
inline std::pair<double, double> arrowBoundTextBBoxOrigin(const Json& arrow,
                                                          const Json& text)
{
    auto pts = elementAbsolutePoints(arrow);
    if (pts.size() < 2)
        return {text.num("x"), text.num("y")};
    auto [mx, my] = polylineMidpoint(pts);
    return {mx - text.num("width") / 2, my - text.num("height") / 2};
}

// polylineMidpoint: 按折线路径长度取几何中点，避免按点序号取中偏向拐点
inline std::pair<double, double>
polylineMidpoint(const std::vector<std::pair<double, double>>& pts)
{
    if (pts.empty())
        return {0, 0};
    if (pts.size() == 1)
        return pts.front();
    double total = 0;
    for (size_t i = 1; i < pts.size(); i++) {
        double dx = pts[i].first - pts[i - 1].first;
        double dy = pts[i].second - pts[i - 1].second;
        total += std::sqrt(dx * dx + dy * dy);
    }
    if (total <= 0)
        return pts.front();
    double half = total / 2.0;
    double acc  = 0;
    for (size_t i = 1; i < pts.size(); i++) {
        double x0  = pts[i - 1].first;
        double y0  = pts[i - 1].second;
        double dx  = pts[i].first - x0;
        double dy  = pts[i].second - y0;
        double seg = std::sqrt(dx * dx + dy * dy);
        if (acc + seg >= half && seg > 0) {
            double t = (half - acc) / seg;
            return {x0 + dx * t, y0 + dy * t};
        }
        acc += seg;
    }
    return pts.back();
}

inline std::pair<double, double>
excalidrawTextBBoxOrigin(const Json&                        el,
                         const std::map<std::string, Json>& arrows)
{
    std::string cid = el.str("containerId");
    if (!cid.empty()) {
        auto it = arrows.find(cid);
        if (it != arrows.end())
            return arrowBoundTextBBoxOrigin(it->second, el);
    }
    return {el.num("x"), el.num("y")};
}

// splitTextLinesNonEmpty: 为 SVG/HTML 文本渲染保留至少一行
inline std::vector<std::string> splitTextLinesNonEmpty(const std::string& text)
{
    std::vector<std::string> lines = gm::splitLines(text);
    if (lines.empty())
        lines.push_back(text);
    return lines;
}

// excalidrawFileDataUrl: 按 fileId 从 Excalidraw files 取 dataURL
inline std::string excalidrawFileDataUrl(const Graph&       g,
                                         const std::string& fileId)
{
    if (fileId.empty() || !g.files.isObj())
        return "";
    const Json* f = g.files.find(fileId);
    if (!f || !f->isObj())
        return "";
    return f->str("dataURL");
}

// excalidrawImagePlacement: image 元素在 SVG 中的最终放置参数
struct ExcalidrawImagePlacement
{
    double x = 0, y = 0, w = 0, h = 0;
    bool   hasClip = false;
    double clipX = 0, clipY = 0, clipW = 0, clipH = 0;
};

// resolveExcalidrawImagePlacement: 计算 image/crop 对应的渲染参数
inline ExcalidrawImagePlacement resolveExcalidrawImagePlacement(const Json& el)
{
    ExcalidrawImagePlacement p;
    p.x              = el.num("x");
    p.y              = el.num("y");
    p.w              = el.num("width");
    p.h              = el.num("height");
    const Json* crop = el.find("crop");
    if (!crop || !crop->isObj())
        return p;
    double cx = crop->num("x");
    double cy = crop->num("y");
    double cw = crop->num("width");
    double ch = crop->num("height");
    double nw = crop->num("naturalWidth");
    double nh = crop->num("naturalHeight");
    if (cw <= 0 || ch <= 0 || nw <= 0 || nh <= 0)
        return p;

    double sx = p.w / cw;
    double sy = p.h / ch;
    p.x       = p.x - cx * sx;
    p.y       = p.y - cy * sy;
    p.w       = nw * sx;
    p.h       = nh * sy;
    p.hasClip = true;
    p.clipX   = el.num("x");
    p.clipY   = el.num("y");
    p.clipW   = el.num("width");
    p.clipH   = el.num("height");
    return p;
}

// ExcalidrawAffine: SVG matrix(a b c d e f) 形式的二维仿射矩阵
struct ExcalidrawAffine
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    bool   identity = true;
};

// clamp01: 将数值限制到 [0,1]
inline double clamp01(double v)
{
    if (v < 0.0)
        return 0.0;
    if (v > 1.0)
        return 1.0;
    return v;
}

// excalidrawElementScale: 读取 Excalidraw element.scale（默认 1,1）
inline void excalidrawElementScale(const Json& el, double& sx, double& sy)
{
    sx             = 1.0;
    sy             = 1.0;
    const Json* sc = el.find("scale");
    if (!sc || !sc->isArr() || sc->size() < 2)
        return;
    const Json& vx = sc->a->at(0);
    const Json& vy = sc->a->at(1);
    if (vx.isNum() && std::fabs(vx.n) > 1e-9)
        sx = vx.n;
    if (vy.isNum() && std::fabs(vy.n) > 1e-9)
        sy = vy.n;
}

// excalidrawElementAffineByBox: 指定包围盒下的中心旋转+镜像变换
inline ExcalidrawAffine excalidrawElementAffineByBox(const Json& el,
                                                     double      x,
                                                     double      y,
                                                     double      w,
                                                     double      h)
{
    ExcalidrawAffine t;
    double           sx, sy;
    excalidrawElementScale(el, sx, sy);
    double angle = el.num("angle", 0);
    if (std::fabs(angle) <= 1e-9 && std::fabs(sx - 1.0) <= 1e-9 &&
        std::fabs(sy - 1.0) <= 1e-9)
        return t;

    double cx  = x + w / 2.0;
    double cy  = y + h / 2.0;
    double cs  = std::cos(angle);
    double sn  = std::sin(angle);
    t.a        = cs * sx;
    t.b        = sn * sx;
    t.c        = -sn * sy;
    t.d        = cs * sy;
    t.e        = cx - t.a * cx - t.c * cy;
    t.f        = cy - t.b * cx - t.d * cy;
    t.identity = false;
    return t;
}

// excalidrawElementAffine: 对齐 Excalidraw 的中心点旋转+镜像变换
inline ExcalidrawAffine excalidrawElementAffine(const Json& el)
{
    return excalidrawElementAffineByBox(el, el.num("x"), el.num("y"),
                                        el.num("width"), el.num("height"));
}

// applyAffinePoint: 用仿射矩阵变换二维点
inline void applyAffinePoint(const ExcalidrawAffine& t,
                             double                  x,
                             double                  y,
                             double&                 ox,
                             double&                 oy)
{
    ox = t.a * x + t.c * y + t.e;
    oy = t.b * x + t.d * y + t.f;
}

// extendBoundsAffineRect: 用仿射变换后的矩形四角扩展画布边界
inline void extendBoundsAffineRect(double&                 minX,
                                   double&                 minY,
                                   double&                 maxX,
                                   double&                 maxY,
                                   double                  x,
                                   double                  y,
                                   double                  w,
                                   double                  h,
                                   const ExcalidrawAffine& t)
{
    if (w <= 0 || h <= 0)
        return;
    if (t.identity) {
        extendBounds(minX, minY, maxX, maxY, x, y, w, h);
        return;
    }
    double tx, ty;
    applyAffinePoint(t, x, y, tx, ty);
    minX = std::min(minX, tx);
    minY = std::min(minY, ty);
    maxX = std::max(maxX, tx);
    maxY = std::max(maxY, ty);
    applyAffinePoint(t, x + w, y, tx, ty);
    minX = std::min(minX, tx);
    minY = std::min(minY, ty);
    maxX = std::max(maxX, tx);
    maxY = std::max(maxY, ty);
    applyAffinePoint(t, x + w, y + h, tx, ty);
    minX = std::min(minX, tx);
    minY = std::min(minY, ty);
    maxX = std::max(maxX, tx);
    maxY = std::max(maxY, ty);
    applyAffinePoint(t, x, y + h, tx, ty);
    minX = std::min(minX, tx);
    minY = std::min(minY, ty);
    maxX = std::max(maxX, tx);
    maxY = std::max(maxY, ty);
}

// FreedrawOutline: pressure 参与计算后的自由画描边轮廓
struct FreedrawOutline
{
    std::vector<std::pair<double, double>> polygon;
    std::string                            fillColor = "#1e1e1e";
    double                                 opacity   = 1.0;
};

// makeFreedrawOutline: 参考 perfect-freehand 思路生成压力可变描边轮廓
inline FreedrawOutline makeFreedrawOutline(const Json& el)
{
    FreedrawOutline out;
    out.fillColor = el.str("strokeColor", "#1e1e1e");
    out.opacity = std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
    auto pts    = elementAbsolutePoints(el);
    if (pts.size() < 2)
        return out;

    std::vector<double> pressures(pts.size(), 0.5);
    if (const Json* ps = el.find("pressures")) {
        if (ps->isArr()) {
            size_t n = std::min(ps->size(), pts.size());
            for (size_t i = 0; i < n; i++) {
                const Json& p = ps->a->at(i);
                if (p.isNum())
                    pressures[i] = clamp01(p.n);
            }
        }
    }
    bool simulatePressure = el.boolean("simulatePressure", true);
    if (simulatePressure && pts.size() > 2) {
        for (size_t i = 1; i < pts.size(); i++) {
            double dx    = pts[i].first - pts[i - 1].first;
            double dy    = pts[i].second - pts[i - 1].second;
            double v     = std::sqrt(dx * dx + dy * dy);
            double vp    = clamp01(1.0 - std::min(1.0, v / 32.0));
            pressures[i] = std::max(pressures[i], vp);
        }
    }

    double thinning = 0.6;
    if (const Json* so = el.find("strokeOptions")) {
        if (so->isObj()) {
            if (const Json* t = so->find("thinning"))
                if (t->isNum())
                    thinning = clamp01((t->n + 1.0) * 0.5);
        }
    }
    double baseRadius = std::max(0.5, el.num("strokeWidth", 2.0) * 0.5);

    std::vector<std::pair<double, double>> left, right;
    left.reserve(pts.size());
    right.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        size_t pi = i > 0 ? i - 1 : i;
        size_t ni = i + 1 < pts.size() ? i + 1 : i;
        double tx = pts[ni].first - pts[pi].first;
        double ty = pts[ni].second - pts[pi].second;
        double tl = std::sqrt(tx * tx + ty * ty);
        if (tl <= 1e-9) {
            tx = 1.0;
            ty = 0.0;
            tl = 1.0;
        }
        tx /= tl;
        ty /= tl;
        double nx = -ty;
        double ny = tx;
        double pr = clamp01(pressures[i]);
        double rw = baseRadius * (1.0 - thinning + thinning * pr);
        rw        = std::max(0.35, rw);
        left.push_back({pts[i].first + nx * rw, pts[i].second + ny * rw});
        right.push_back({pts[i].first - nx * rw, pts[i].second - ny * rw});
    }
    for (const auto& p : left)
        out.polygon.push_back(p);
    for (size_t i = right.size(); i > 0; --i)
        out.polygon.push_back(right[i - 1]);
    return out;
}

// svgPathFromPolygon: 轮廓点列转闭合 path 字符串
inline std::string
svgPathFromPolygon(const std::vector<std::pair<double, double>>& polygon)
{
    if (polygon.size() < 3)
        return "";
    std::ostringstream os;
    os << "M " << polygon[0].first << " " << polygon[0].second;
    for (size_t i = 1; i < polygon.size(); i++)
        os << " L " << polygon[i].first << " " << polygon[i].second;
    os << " Z";
    return os.str();
}

// excalidrawCanvasBounds: 根据 elements 计算画布边界
inline void excalidrawCanvasBounds(const Graph& g,
                                   double&      minX,
                                   double&      minY,
                                   double&      maxX,
                                   double&      maxY)
{
    minX = 1e18;
    minY = 1e18;
    maxX = -1e18;
    maxY = -1e18;
    for (const auto& el : g.elements) {
        std::string ty = el.str("type");
        if (ty == "arrow" || ty == "line" || ty == "freedraw") {
            auto   pts = elementAbsolutePoints(el);
            double pad = 0.0;
            // freedraw 轮廓半径约 strokeWidth/2，边界需计入以免 viewBox 裁切
            if (ty == "freedraw")
                pad = std::max(0.5, el.num("strokeWidth", 2.0) * 0.5) + 1.0;
            for (const auto& p : pts) {
                minX = std::min(minX, p.first - pad);
                minY = std::min(minY, p.second - pad);
                maxX = std::max(maxX, p.first + pad);
                maxY = std::max(maxY, p.second + pad);
            }
            continue;
        }
        ExcalidrawAffine tf = excalidrawElementAffine(el);
        extendBoundsAffineRect(minX, minY, maxX, maxY, el.num("x"), el.num("y"),
                               el.num("width"), el.num("height"), tf);
    }
    if (minX > maxX) {
        minX = 0;
        minY = 0;
        maxX = 200;
        maxY = 150;
    }
}

// collectArrowBoundTexts: 收集带嵌字的箭头及其文本元素
inline void collectArrowBoundTexts(const Graph&                 g,
                                   std::map<std::string, Json>& arrows,
                                   std::map<std::string, Json>& arrowLabels)
{
    arrows.clear();
    arrowLabels.clear();
    for (const auto& el : g.elements) {
        if (el.str("type") == "arrow")
            arrows[el.str("id")] = el;
    }
    for (const auto& el : g.elements) {
        if (el.str("type") != "text")
            continue;
        std::string cid = el.str("containerId");
        if (!cid.empty() && !el.str("text").empty() && arrows.count(cid))
            arrowLabels[cid] = el;
    }
}

// arrowLabelMaskHole: 箭头嵌字遮罩挖空区域（含少量边距）
inline void arrowLabelMaskHole(const Json& arrow,
                               const Json& text,
                               double      pad,
                               double&     hx,
                               double&     hy,
                               double&     hw,
                               double&     hh)
{
    auto origin = arrowBoundTextBBoxOrigin(arrow, text);
    hx          = origin.first - pad;
    hy          = origin.second - pad;
    hw          = text.num("width") + pad * 2;
    hh          = text.num("height") + pad * 2;
}

// toSVGExcalidraw: 按 Excalidraw elements 几何导出精确 SVG
inline std::string toSVGExcalidraw(const Graph& g)
{
    double minX = 0, minY = 0, maxX = 200, maxY = 150;
    excalidrawCanvasBounds(g, minX, minY, maxX, maxY);
    double                      pad = 40;
    double                      w   = maxX - minX + pad * 2;
    double                      h   = maxY - minY + pad * 2;
    double                      vx  = minX - pad;
    double                      vy  = minY - pad;
    std::map<std::string, Json> arrows, arrowLabels;
    collectArrowBoundTexts(g, arrows, arrowLabels);
    constexpr double   kLabelMaskPad = 4.0;
    std::ostringstream os;
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << (int)w
       << "\" height=\"" << (int)h << "\" viewBox=\"" << vx << " " << vy << " "
       << (int)w << " " << (int)h << "\">\n";
    os << "  <defs>\n";
    os << "    <marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" "
          "refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" "
          "orient=\"auto-start-reverse\"><path d=\"M0,0 L10,5 L0,10 z\" "
          "fill=\"context-stroke\"/></marker>\n";
    for (const auto& kv : arrowLabels) {
        auto it = arrows.find(kv.first);
        if (it == arrows.end())
            continue;
        double hx, hy, hw, hh;
        arrowLabelMaskHole(it->second, kv.second, kLabelMaskPad, hx, hy, hw,
                           hh);
        os << "    <mask id=\"mask-" << xmlEscape(kv.first)
           << "\" maskUnits=\"userSpaceOnUse\">\n";
        os << "      <rect x=\"" << vx << "\" y=\"" << vy << "\" width=\"" << w
           << "\" height=\"" << h << "\" fill=\"#ffffff\"/>\n";
        os << "      <rect x=\"" << hx << "\" y=\"" << hy << "\" width=\"" << hw
           << "\" height=\"" << hh << "\" fill=\"#000000\"/>\n";
        os << "    </mask>\n";
    }
    os << "    <style>" << xmlTextEscape(excalidrawEmbeddedFontCss())
       << "</style>\n";
    os << "  </defs>\n";
    os << "  <rect x=\"" << vx << "\" y=\"" << vy << "\" width=\"" << w
       << "\" height=\"" << h << "\" fill=\"#ffffff\"/>\n";
    os << "  <style>.ex-text{white-space:pre;}</style>\n";

    auto emitPolyline = [&](const std::vector<std::pair<double, double>>& pts,
                            const Json& el, bool arrowEnd, bool arrowStart) {
        if (pts.size() < 2)
            return;
        std::string stroke = el.str("strokeColor", "#1e1e1e");
        double      sw     = std::max(0.5, el.num("strokeWidth", 2));
        double      op =
            std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
        os << "  <polyline fill=\"none\" stroke=\"" << xmlEscape(stroke)
           << "\" stroke-width=\"" << sw << "\" opacity=\"" << op
           << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" points=\"";
        for (size_t i = 0; i < pts.size(); i++) {
            if (i)
                os << " ";
            os << pts[i].first << "," << pts[i].second;
        }
        os << "\"" << svgDashAttr(el.str("strokeStyle", "solid"));
        if (arrowEnd)
            os << " marker-end=\"url(#arrow)\"";
        if (arrowStart)
            os << " marker-start=\"url(#arrow)\"";
        os << "/>\n";
    };

    for (const auto& el : g.elements) {
        std::string ty = el.str("type");
        if (ty == "rectangle") {
            ExcalidrawAffine tf = excalidrawElementAffine(el);
            if (!tf.identity)
                os << "  <g transform=\"matrix(" << tf.a << " " << tf.b << " "
                   << tf.c << " " << tf.d << " " << tf.e << " " << tf.f
                   << ")\">\n";
            std::string stroke = el.str("strokeColor", "#1e1e1e");
            std::string fill   = svgFill(el);
            double      sw     = std::max(0.5, el.num("strokeWidth", 2));
            double      op =
                std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
            double rx = 0;
            if (const Json* rnd = el.find("roundness"))
                if (rnd->isObj() && rnd->num("type") >= 2)
                    rx = 8;
            os << "  <rect x=\"" << el.num("x") << "\" y=\"" << el.num("y")
               << "\" width=\"" << el.num("width") << "\" height=\""
               << el.num("height") << "\" rx=\"" << rx << "\" fill=\"" << fill
               << "\" stroke=\"" << xmlEscape(stroke) << "\" stroke-width=\""
               << sw << "\" opacity=\"" << op << "\""
               << svgDashAttr(el.str("strokeStyle", "solid")) << "/>\n";
            if (!tf.identity)
                os << "  </g>\n";
        }
        else if (ty == "ellipse") {
            ExcalidrawAffine tf = excalidrawElementAffine(el);
            if (!tf.identity)
                os << "  <g transform=\"matrix(" << tf.a << " " << tf.b << " "
                   << tf.c << " " << tf.d << " " << tf.e << " " << tf.f
                   << ")\">\n";
            double cx = el.num("x") + el.num("width") / 2;
            double cy = el.num("y") + el.num("height") / 2;
            double op =
                std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
            os << "  <ellipse cx=\"" << cx << "\" cy=\"" << cy << "\" rx=\""
               << el.num("width") / 2 << "\" ry=\"" << el.num("height") / 2
               << "\" fill=\"" << svgFill(el) << "\" stroke=\""
               << xmlEscape(el.str("strokeColor", "#1e1e1e"))
               << "\" stroke-width=\""
               << std::max(0.5, el.num("strokeWidth", 2)) << "\" opacity=\""
               << op << "\"" << svgDashAttr(el.str("strokeStyle", "solid"))
               << "/>\n";
            if (!tf.identity)
                os << "  </g>\n";
        }
        else if (ty == "diamond") {
            ExcalidrawAffine tf = excalidrawElementAffine(el);
            if (!tf.identity)
                os << "  <g transform=\"matrix(" << tf.a << " " << tf.b << " "
                   << tf.c << " " << tf.d << " " << tf.e << " " << tf.f
                   << ")\">\n";
            double x = el.num("x"), y = el.num("y");
            double ew = el.num("width"), eh = el.num("height");
            double cx = x + ew / 2, cy = y + eh / 2;
            double op =
                std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
            os << "  <polygon points=\"" << cx << "," << y << " " << x + ew
               << "," << cy << " " << cx << "," << y + eh << " " << x << ","
               << cy << "\" fill=\"" << svgFill(el) << "\" stroke=\""
               << xmlEscape(el.str("strokeColor", "#1e1e1e"))
               << "\" stroke-width=\""
               << std::max(0.5, el.num("strokeWidth", 2)) << "\" opacity=\""
               << op << "\"" << svgDashAttr(el.str("strokeStyle", "solid"))
               << "/>\n";
            if (!tf.identity)
                os << "  </g>\n";
        }
        else if (ty == "arrow" || ty == "line") {
            auto pts      = elementAbsolutePoints(el);
            bool endArr   = ty == "arrow" && !el.str("endArrowhead").empty() &&
                            el.str("endArrowhead") != "null";
            bool startArr = ty == "arrow" &&
                            !el.str("startArrowhead").empty() &&
                            el.str("startArrowhead") != "null";
            std::string aid    = el.str("id");
            bool        masked = ty == "arrow" && arrowLabels.count(aid);
            if (masked)
                os << "  <g mask=\"url(#mask-" << xmlEscape(aid) << ")\">\n";
            emitPolyline(pts, el, endArr, startArr);
            if (masked)
                os << "  </g>\n";
        }
        else if (ty == "freedraw") {
            FreedrawOutline outline = makeFreedrawOutline(el);
            std::string     path    = svgPathFromPolygon(outline.polygon);
            if (!path.empty()) {
                os << "  <path d=\"" << path << "\" fill=\""
                   << xmlEscape(outline.fillColor) << "\" opacity=\""
                   << outline.opacity << "\"/>\n";
            }
        }
        else if (ty == "image") {
            std::string fileId  = el.str("fileId");
            std::string dataUrl = excalidrawFileDataUrl(g, fileId);
            if (dataUrl.empty())
                continue;
            double op =
                std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
            ExcalidrawImagePlacement p  = resolveExcalidrawImagePlacement(el);
            ExcalidrawAffine         tf = excalidrawElementAffine(el);
            if (!tf.identity) {
                os << "  <g transform=\"matrix(" << tf.a << " " << tf.b << " "
                   << tf.c << " " << tf.d << " " << tf.e << " " << tf.f
                   << ")\">\n";
            }
            if (p.hasClip) {
                os << "  <svg x=\"" << p.clipX << "\" y=\"" << p.clipY
                   << "\" width=\"" << p.clipW << "\" height=\"" << p.clipH
                   << "\" overflow=\"hidden\" preserveAspectRatio=\"none\">\n";
                os << "    <image x=\"" << (p.x - p.clipX) << "\" y=\""
                   << (p.y - p.clipY) << "\" width=\"" << p.w << "\" height=\""
                   << p.h << "\" href=\"" << xmlEscape(dataUrl)
                   << "\" opacity=\"" << op << "\" preserveAspectRatio=\"none\""
                   << "/>\n";
                os << "  </svg>\n";
            }
            else {
                os << "  <image x=\"" << p.x << "\" y=\"" << p.y
                   << "\" width=\"" << p.w << "\" height=\"" << p.h
                   << "\" href=\"" << xmlEscape(dataUrl) << "\" opacity=\""
                   << op << "\" preserveAspectRatio=\"none\"/>\n";
            }
            if (!tf.identity)
                os << "  </g>\n";
        }
    }
    // 文本层（形状标签 + 箭头嵌入文字 + 独立标注）
    for (const auto& el : g.elements) {
        if (el.str("type") != "text")
            continue;
        std::string txt = el.str("text");
        if (txt.empty())
            continue;
        auto        origin = excalidrawTextBBoxOrigin(el, arrows);
        double      fs     = el.num("fontSize", 16);
        double      x      = excalidrawTextSvgXAt(origin.first, el);
        double      y      = excalidrawTextSvgYAt(origin.second, el);
        std::string align  = svgTextAnchor(el.str("textAlign", "left"));
        std::string fill   = el.str("strokeColor", "#1e1e1e");
        double      op =
            std::max(0.0, std::min(1.0, el.num("opacity", 100) / 100.0));
        ExcalidrawAffine tf = excalidrawElementAffineByBox(
            el, origin.first, origin.second, el.num("width"), el.num("height"));
        if (!tf.identity)
            os << "  <g transform=\"matrix(" << tf.a << " " << tf.b << " "
               << tf.c << " " << tf.d << " " << tf.e << " " << tf.f << ")\">\n";
        auto lines = splitTextLinesNonEmpty(txt);
        os << "  <text class=\"ex-text\" x=\"" << x << "\" y=\"" << y
           << "\" font-size=\"" << fs << "\" text-anchor=\"" << align
           << "\" font-family=\""
           << xmlAttrEscape(excalidrawTextFontFamilyCss(el)) << "\" fill=\""
           << xmlEscape(fill) << "\" opacity=\"" << op << "\">";
        for (size_t i = 0; i < lines.size(); i++) {
            double lineY = excalidrawTextSvgYAt(origin.second, el, i);
            os << "<tspan x=\"" << x << "\" y=\"" << lineY << "\">"
               << xmlEscape(lines[i]) << "</tspan>";
        }
        os << "</text>\n";
        if (!tf.identity)
            os << "  </g>\n";
    }
    os << "</svg>\n";
    return os.str();
}

// sanitizeMermaidId: Mermaid 标识符清洗，规避非法字符导致的渲染失败
inline std::string sanitizeMermaidId(const std::string& id)
{
    std::string out;
    for (char c : id)
        out += (isalnum((unsigned char)c) || c == '_') ? c : '_';
    if (out.empty())
        out = "n";
    return out;
}

// edgeHasEndArrow / edgeHasStartArrow: 从 headStart/headEnd 推导箭头可见性
// 替代直接读 e.arrow，确保与扩展箭头字段一致
inline bool edgeHasEndArrow(const Edge& e)
{ return e.headEnd != "none"; }
inline bool edgeHasStartArrow(const Edge& e)
{ return e.headStart != "none"; }

// toMermaid: 统一模型导出为 Mermaid 文本
// 关键步骤：按图类型分支（mindmap/er/flowchart）-> 输出节点 -> 输出边
inline std::string toMermaid(const Graph& g)
{
    // properties 类型：从结构化数据重建 Mermaid
    if (g.properties.isObj() && g.properties.o) {
        // gantt 导出
        if (const Json* gantt = g.properties.find("gantt")) {
            std::ostringstream os;
            os << "gantt\n";
            std::string df = gantt->str("dateFormat");
            if (!df.empty())
                os << "    dateFormat " << df << "\n";
            std::string t = gantt->str("title");
            if (!t.empty())
                os << "    title " << t << "\n";
            if (const Json* secs = gantt->find("sections")) {
                if (secs->isArr())
                    for (auto& sec : *secs->a) {
                        std::string sn = sec.str("name");
                        if (!sn.empty())
                            os << "    section " << sn << "\n";
                        if (const Json* tasks = sec.find("tasks")) {
                            if (tasks->isArr())
                                for (auto& tk : *tasks->a) {
                                    os << "    " << tk.str("label") << " :";
                                    std::string st = tk.str("status");
                                    if (!st.empty())
                                        os << " " << st << ",";
                                    std::string tid = tk.str("id");
                                    if (!tid.empty())
                                        os << " " << tid << ",";
                                    std::string start = tk.str("start");
                                    if (!start.empty())
                                        os << " " << start << ",";
                                    std::string end = tk.str("end");
                                    if (!end.empty())
                                        os << " " << end;
                                    std::string after = tk.str("after");
                                    if (!after.empty())
                                        os << " after " << after;
                                    os << "\n";
                                }
                        }
                    }
            }
            return os.str();
        }
        // pie 导出
        if (const Json* pie = g.properties.find("pie")) {
            std::ostringstream os;
            os << "pie";
            std::string title = pie->str("title");
            if (!title.empty())
                os << " title " << title;
            os << "\n";
            if (pie->boolean("showData", false))
                os << "    showData\n";
            if (const Json* entries = pie->find("entries")) {
                if (entries->isArr())
                    for (auto& e : *entries->a) {
                        std::string lbl = e.str("label");
                        os << "    \"" << lbl << "\" : " << e.num("value", 0)
                           << "\n";
                    }
            }
            return os.str();
        }
        // sequenceDiagram 导出
        if (const Json* seq = g.properties.find("sequence")) {
            std::ostringstream os;
            os << "sequenceDiagram\n";
            if (seq->boolean("autonumber", false))
                os << "    autonumber\n";
            // participants
            if (const Json* parts = seq->find("participants")) {
                if (parts->isArr())
                    for (auto& p : *parts->a) {
                        std::string type = p.str("type");
                        if (type.empty())
                            type = "participant";
                        os << "    " << type << " " << p.str("label");
                        std::string pid = p.str("id");
                        if (!pid.empty() && pid != p.str("label"))
                            os << " as " << pid;
                        os << "\n";
                    }
            }
            // 辅助：输出消息
            auto emitMsg = [&](const Json& m, int indent) {
                std::string prefix(indent * 4, ' ');
                std::string mType   = m.str("type");
                std::string headEnd = m.str("headEnd");
                // 构建箭头：dash(es) + head symbol
                bool        isReturn = (mType == "return");
                std::string dash     = isReturn ? "--" : "-";
                std::string head;
                if (headEnd == "cross")
                    head = "x";
                else if (headEnd == "open")
                    head = ")";
                else
                    head = ">>";  // arrow (default)
                std::string arrow = dash + head;
                os << prefix << m.str("from") << arrow << m.str("to");
                if (!m.str("label").empty())
                    os << ": " << m.str("label");
                os << "\n";
            };
            // 输出顶层消息和片段
            // 递归处理消息和嵌套 fragment
            std::function<void(const Json&, int)> emitItem;
            emitItem = [&](const Json& item, int depth) {
                // 判断是嵌套 fragment（有 type + messages）还是消息（有
                // from/to）
                if (item.isObj() && item.find("messages")) {
                    // 嵌套 fragment
                    std::string prefix(depth * 4, ' ');
                    os << prefix << item.str("type");
                    if (!item.str("label").empty())
                        os << " " << item.str("label");
                    os << "\n";
                    if (const Json* inner = item.find("messages")) {
                        if (inner->isArr())
                            for (auto& m : *inner->a)
                                emitItem(m, depth + 1);
                    }
                    os << prefix << "end\n";
                }
                else {
                    emitMsg(item, depth);
                }
            };
            auto emitFrag = [&](const Json& frag, int depth) {
                emitItem(frag, depth);
            };
            // 先输出顶层消息，遇到 fragment 就嵌套
            const Json* frags = seq->find("fragments");
            if (const Json* ms = seq->find("messages")) {
                if (ms->isArr())
                    for (auto& m : *ms->a)
                        emitMsg(m, 1);
            }
            if (frags && frags->isArr())
                for (auto& f : *frags->a)
                    emitFrag(f, 1);
            // notes
            if (const Json* nts = seq->find("notes")) {
                if (nts->isArr())
                    for (auto& nt : *nts->a) {
                        os << "    Note " << nt.str("placement") << " of ";
                        if (const Json* tgt = nt.find("targets")) {
                            if (tgt->isArr()) {
                                bool firstT = true;
                                for (auto& t : *tgt->a) {
                                    if (!firstT)
                                        os << ",";
                                    os << t.s;
                                    firstT = false;
                                }
                            }
                        }
                        os << ": " << nt.str("text") << "\n";
                    }
            }
            return os.str();
        }
        if (const Json* rd = g.properties.find("requirementDiagram")) {
            // requirementDiagram 导出
            std::ostringstream os;
            os << "requirementDiagram\n";
            if (const Json* els = rd->find("elements")) {
                if (els->isArr())
                    for (auto& el : *els->a) {
                        std::string type = el.str("type");
                        std::string id   = el.str("id");
                        if (type.empty())
                            type = "requirement";
                        os << "    " << type << " " << id << " {\n";
                        // 输出 el 的所有自定义属性（除了 id 和 type）
                        if (el.isObj() && el.o)
                            for (auto& kv : *el.o) {
                                if (kv.first == "id" || kv.first == "type")
                                    continue;
                                if (kv.second.isStr())
                                    os << "        " << kv.first << ": \""
                                       << kv.second.s << "\"\n";
                                else
                                    os << "        " << kv.first << ": "
                                       << kv.second.dump() << "\n";
                            }
                        os << "    }\n";
                    }
            }
            if (const Json* rels = rd->find("relations")) {
                if (rels->isArr())
                    for (auto& rel : *rels->a) {
                        os << "    " << rel.str("from") << " - "
                           << rel.str("type") << " -> " << rel.str("to")
                           << "\n";
                    }
            }
            return os.str();
        }
        // sankey-beta 导出
        if (const Json* sk = g.properties.find("sankey")) {
            std::ostringstream os;
            os << "sankey-beta\n";
            if (sk->boolean("showValues", false))
                os << "    showValues\n";
            if (const Json* flows = sk->find("flows")) {
                if (flows->isArr())
                    for (auto& f : *flows->a) {
                        os << "    " << f.str("from") << "," << f.str("to")
                           << "," << f.num("value", 0) << "\n";
                    }
            }
            return os.str();
        }
        // architecture-beta 导出
        if (const Json* arch = g.properties.find("architecture")) {
            std::ostringstream os;
            os << "architecture-beta\n";
            // 输出 groups
            if (const Json* grps = arch->find("groups")) {
                if (grps->isArr())
                    for (auto& grp : *grps->a) {
                        os << "    group " << grp.str("id");
                        if (!grp.str("icon").empty())
                            os << "(" << grp.str("icon") << ")";
                        if (!grp.str("label").empty())
                            os << "[" << grp.str("label") << "]";
                        if (!grp.str("groupId").empty())
                            os << " in " << grp.str("groupId");
                        os << "\n";
                    }
            }
            // 输出 services
            if (const Json* svcs = arch->find("services")) {
                if (svcs->isArr())
                    for (auto& svc : *svcs->a) {
                        os << "    service " << svc.str("id");
                        if (!svc.str("icon").empty())
                            os << "(" << svc.str("icon") << ")";
                        if (!svc.str("label").empty())
                            os << "[" << svc.str("label") << "]";
                        if (!svc.str("groupId").empty())
                            os << " in " << svc.str("groupId");
                        os << "\n";
                    }
            }
            // 输出 junctions
            if (const Json* jns = arch->find("junctions")) {
                if (jns->isArr())
                    for (auto& jn : *jns->a) {
                        os << "    junction " << jn.str("id");
                        if (!jn.str("groupId").empty())
                            os << " in " << jn.str("groupId");
                        os << "\n";
                    }
            }
            // 输出 edges
            if (const Json* aEdges = arch->find("edges")) {
                if (aEdges->isArr())
                    for (auto& ae : *aEdges->a) {
                        bool        bidi  = ae.boolean("bidi", false);
                        std::string arrow = bidi ? "<-->" :
                                            ae.boolean("directed", false) ?
                                                   "-->" :
                                                   "--";
                        os << "    " << ae.str("from");
                        if (!ae.str("fromPort").empty())
                            os << ":" << ae.str("fromPort");
                        os << " " << arrow << " ";
                        if (!ae.str("toPort").empty())
                            os << ae.str("toPort") << ":";
                        os << ae.str("to") << "\n";
                    }
            }
            return os.str();
        }
        // kanban 导出
        if (const Json* kb = g.properties.find("kanban")) {
            std::ostringstream os;
            os << "kanban\n";
            if (const Json* cols = kb->find("columns")) {
                if (cols->isArr())
                    for (auto& col : *cols->a) {
                        os << "    " << col.str("id") << "[" << col.str("title")
                           << "]\n";
                        if (const Json* cards = col.find("cards")) {
                            if (cards->isArr())
                                for (auto& card : *cards->a) {
                                    os << "        " << card.str("id") << "["
                                       << card.str("title") << "]";
                                    // 元数据
                                    bool hasMeta = false;
                                    if (card.isObj() && card.o) {
                                        for (auto& kv : *card.o)
                                            if (kv.first != "id" &&
                                                kv.first != "title") {
                                                hasMeta = true;
                                                break;
                                            }
                                    }
                                    if (hasMeta) {
                                        os << "@{ ";
                                        bool firstKv = true;
                                        for (auto& kv : *card.o) {
                                            if (kv.first == "id" ||
                                                kv.first == "title")
                                                continue;
                                            if (!firstKv)
                                                os << ", ";
                                            os << kv.first << ": '";
                                            if (kv.second.isStr())
                                                os << kv.second.s;
                                            else if (kv.second.isNum())
                                                os << kv.second.as_num();
                                            else if (kv.second.isBool())
                                                os << (kv.second.b ? "true" :
                                                                     "false");
                                            else
                                                os << kv.second.dump();
                                            os << "'";
                                            firstKv = false;
                                        }
                                        os << " }";
                                    }
                                    os << "\n";
                                }
                        }
                    }
            }
            return os.str();
        }
        // gitGraph 导出
        if (const Json* gg = g.properties.find("gitGraph")) {
            std::ostringstream os;
            os << "gitGraph\n";
            // 按 order 排序 commits
            std::map<int, const Json*> sorted;
            if (const Json* commits = gg->find("commits")) {
                if (commits->isArr())
                    for (auto& cm : *commits->a)
                        sorted[(int)cm.num("order", 0)] = &cm;
            }
            std::string curBranch;
            for (auto& kv : sorted) {
                auto&       cm = *kv.second;
                std::string b  = cm.str("branch");
                if (b != curBranch && !b.empty()) {
                    os << "    checkout " << b << "\n";
                    curBranch = b;
                }
                std::string ctype = cm.str("type");
                if (ctype == "MERGE")
                    os << "    merge " << cm.str("label") << "\n";
                else if (ctype == "CHERRY_PICK")
                    os << "    cherry-pick " << cm.str("label") << "\n";
                else {
                    os << "    commit";
                    std::string cid = cm.str("id");
                    if (!cid.empty())
                        os << " id:\"" << cid << "\"";
                    std::string tag = cm.str("tag");
                    if (!tag.empty())
                        os << " tag:\"" << tag << "\"";
                    os << "\n";
                }
            }
            return os.str();
        }
        // journey 导出
        if (const Json* jn = g.properties.find("journey")) {
            std::ostringstream os;
            os << "journey\n";
            std::string t = jn->str("title");
            if (!t.empty())
                os << "    title " << t << "\n";
            if (const Json* secs = jn->find("sections")) {
                if (secs->isArr())
                    for (auto& sec : *secs->a) {
                        std::string sn = sec.str("name");
                        if (!sn.empty())
                            os << "    section " << sn << "\n";
                        if (const Json* tasks = sec.find("tasks")) {
                            if (tasks->isArr())
                                for (auto& tk : *tasks->a) {
                                    os << "        " << tk.str("label") << ": "
                                       << (int)tk.num("score", 0) << ": ";
                                    if (const Json* actors =
                                            tk.find("actors")) {
                                        if (actors->isArr()) {
                                            bool firstA = true;
                                            for (auto& a : *actors->a) {
                                                if (!firstA)
                                                    os << ", ";
                                                os << a.s;
                                                firstA = false;
                                            }
                                        }
                                    }
                                    os << "\n";
                                }
                        }
                    }
            }
            return os.str();
        }
        // timeline 导出
        if (const Json* tl = g.properties.find("timeline")) {
            std::ostringstream os;
            os << "timeline\n";
            std::string t = tl->str("title");
            if (!t.empty())
                os << "    title " << t << "\n";
            if (const Json* secs = tl->find("sections")) {
                if (secs->isArr())
                    for (auto& sec : *secs->a) {
                        std::string sn = sec.str("name");
                        if (!sn.empty())
                            os << "    section " << sn << "\n";
                        if (const Json* events = sec.find("events")) {
                            if (events->isArr())
                                for (auto& evt : *events->a) {
                                    os << "        " << evt.str("period")
                                       << " :";
                                    if (const Json* elist =
                                            evt.find("events")) {
                                        if (elist->isArr() &&
                                            elist->a->size() > 0) {
                                            for (size_t ei = 0;
                                                 ei < elist->a->size(); ei++) {
                                                os << " " << (*elist->a)[ei].s;
                                                if (ei + 1 < elist->a->size())
                                                    os << " :";
                                            }
                                        }
                                    }
                                    os << "\n";
                                }
                        }
                    }
            }
            return os.str();
        }
        // quadrantChart 导出
        if (const Json* qc = g.properties.find("quadrantChart")) {
            std::ostringstream os;
            os << "quadrantChart\n";
            std::string t = qc->str("title");
            if (!t.empty())
                os << "    title " << t << "\n";
            std::string xl = qc->str("xLeft"), xr = qc->str("xRight");
            if (!xl.empty() || !xr.empty())
                os << "    x-axis " << xl << " --> " << xr << "\n";
            std::string yb = qc->str("yBottom"), yt = qc->str("yTop");
            if (!yb.empty() || !yt.empty())
                os << "    y-axis " << yb << " --> " << yt << "\n";
            for (int qi = 1; qi <= 4; qi++) {
                std::string qkey = "q" + std::to_string(qi);
                std::string qv   = qc->str(qkey);
                if (!qv.empty())
                    os << "    quadrant-" << qi << ": " << qv << "\n";
            }
            if (const Json* pts = qc->find("points")) {
                if (pts->isArr())
                    for (auto& pt : *pts->a)
                        os << "    " << pt.str("label") << ": ["
                           << pt.num("x", 0) << ", " << pt.num("y", 0) << "]\n";
            }
            return os.str();
        }
        // xychart 导出
        if (const Json* xy = g.properties.find("xychart")) {
            std::ostringstream os;
            os << "xychart-beta\n";
            std::string t = xy->str("title");
            if (!t.empty())
                os << "    title \"" << t << "\"\n";
            if (xy->boolean("horizontal", false))
                os << "    horizontal\n";
            if (const Json* xa = xy->find("xAxis")) {
                os << "    x-axis ";
                if (xa->str("type") == "categorical") {
                    os << "[";
                    if (const Json* cats = xa->find("categories")) {
                        bool firstC = true;
                        if (cats->isArr())
                            for (auto& c : *cats->a) {
                                if (!firstC)
                                    os << ", ";
                                os << "\"" << c.s << "\"";
                                firstC = false;
                            }
                    }
                    os << "]";
                }
                else {
                    os << xa->str("label");
                }
                os << "\n";
            }
            if (const Json* ya = xy->find("yAxis"))
                os << "    y-axis " << ya->str("label") << "\n";
            if (const Json* ser = xy->find("series")) {
                if (ser->isArr())
                    for (auto& s : *ser->a) {
                        os << "    " << s.str("type");
                        std::string sn = s.str("label");
                        if (!sn.empty())
                            os << " \"" << sn << "\"";
                        os << " [";
                        if (const Json* data = s.find("data")) {
                            if (data->isArr()) {
                                bool firstV = true;
                                for (auto& v : *data->a) {
                                    if (!firstV)
                                        os << ", ";
                                    os << v.as_num();
                                    firstV = false;
                                }
                            }
                        }
                        os << "]\n";
                    }
            }
            return os.str();
        }
        // block-beta 导出
        if (const Json* bl = g.properties.find("block")) {
            std::ostringstream os;
            os << "block-beta\n";
            int cols = (int)bl->num("columns", 0);
            if (cols > 0)
                os << "    columns " << cols << "\n";
            if (const Json* blks = bl->find("blocks")) {
                if (blks->isArr())
                    for (auto& bk : *blks->a) {
                        os << "    " << bk.str("id");
                        std::string shape = bk.str("shape");
                        if (shape == "round")
                            os << "(" << bk.str("label") << ")";
                        else if (shape == "diamond")
                            os << "{" << bk.str("label") << "}";
                        else
                            os << "[" << bk.str("label") << "]";
                        // 不额外输出 label（已在括号语法中）
                        int cs = (int)bk.num("colSpan", 1);
                        if (cs > 1)
                            os << ":" << cs;
                        os << "\n";
                    }
            }
            if (const Json* bedges = bl->find("edges")) {
                if (bedges->isArr())
                    for (auto& e : *bedges->a) {
                        std::string arrow =
                            e.boolean("directed", false) ? "-->" : "---";
                        os << "    " << e.str("from") << " " << arrow << " "
                           << e.str("to") << "\n";
                    }
            }
            return os.str();
        }
        // packet-beta 导出
        if (const Json* pk = g.properties.find("packet")) {
            std::ostringstream os;
            os << "packet-beta\n";
            std::string t = pk->str("title");
            if (!t.empty())
                os << "    title " << t << "\n";
            if (const Json* flds = pk->find("fields")) {
                int offset = 0;
                if (flds->isArr())
                    for (auto& f : *flds->a) {
                        int bits  = (int)f.num("bits", 0);
                        int start = (int)f.num("start", -1);
                        int end   = (int)f.num("end", -1);
                        if (start >= 0 && end >= 0)
                            os << "    " << start << "-" << end;
                        else if (bits > 0) {
                            os << "    " << offset << "-"
                               << (offset + bits - 1);
                            offset += bits;
                        }
                        else
                            os << "    0-7";
                        os << ": \"" << f.str("label") << "\"\n";
                    }
            }
            return os.str();
        }
    }

    // 透传模式：如果存有原始 Mermaid 文本（无法深度解析的类型），直接返回
    if (!g.rawMermaid.empty())
        return g.rawMermaid;

    std::ostringstream os;
    if (g.type == "mindmap") {
        os << "mindmap\n";
        std::map<std::string, std::vector<const Node*>> children;
        std::vector<const Node*>                        roots;
        for (auto& n : g.nodes) {
            if (n.parent.empty())
                roots.push_back(&n);
            else
                children[n.parent].push_back(&n);
        }
        std::function<void(const Node*, int)> emit = [&](const Node* n,
                                                         int         depth) {
            os << std::string((size_t)(depth + 1) * 2, ' ');
            if (depth == 0)
                os << "root((" << n->label << "))";
            else
                os << n->label;
            os << "\n";
            for (auto* c : children[n->id])
                emit(c, depth + 1);
        };
        for (auto* r : roots)
            emit(r, 0);
        return os.str();
    }
    if (g.type == "er") {
        os << "erDiagram\n";
        for (auto& e : g.edges) {
            os << "    " << sanitizeMermaidId(e.from) << " ||--o{ "
               << sanitizeMermaidId(e.to) << " : \""
               << (e.label.empty() ? "relates" : e.label) << "\"\n";
        }
        for (auto& n : g.nodes) {
            if (n.attrs.empty())
                continue;
            os << "    " << sanitizeMermaidId(n.id) << " {\n";
            for (auto& a : n.attrs)
                os << "        " << a << "\n";
            os << "    }\n";
        }
        return os.str();
    }
    if (g.type == "classDiagram") {
        os << "classDiagram\n";
        // 输出关系边
        for (auto& e : g.edges) {
            os << "    " << sanitizeMermaidId(e.from);
            // 根据 label 推断关系箭头
            std::string rel = e.label;
            if (rel == "inheritance")
                os << " <|-- ";
            else if (rel == "composition")
                os << " *-- ";
            else if (rel == "aggregation")
                os << " o-- ";
            else if (rel == "bidirectional")
                os << " <--> ";
            else if (rel == "realization")
                os << " ..|> ";
            else if (rel == "dependency")
                os << " ..> ";
            else if (rel == "dotted")
                os << " .. ";
            else if (rel == "link")
                os << " -- ";
            else
                os << " --> ";
            os << sanitizeMermaidId(e.to) << " : \"" << rel << "\"\n";
        }
        // 输出类定义
        for (auto& n : g.nodes) {
            os << "    class " << sanitizeMermaidId(n.id) << " {\n";
            for (auto& a : n.attrs)
                os << "        " << a << "\n";
            os << "    }\n";
        }
        return os.str();
    }
    if (g.type == "stateDiagram") {
        os << "stateDiagram-v2\n";
        // 输出转移
        for (auto& e : g.edges) {
            os << "    " << sanitizeMermaidId(e.from) << " --> "
               << sanitizeMermaidId(e.to);
            if (!e.label.empty())
                os << " : " << e.label;
            os << "\n";
        }
        return os.str();
    }
    // flowchart / architecture / orgchart / whiteboard 统一导出为 flowchart TD
    // 顺序：图声明 → 节点/边 → classDef/class/linkStyle（保证再导入可被解析）
    os << "flowchart TD\n";
    std::map<std::string, std::vector<const Node*>> byGroup;
    for (auto& n : g.nodes) {
        if (n.shape == "group")
            continue;
        byGroup[n.parent].push_back(&n);
    }
    auto emitNode = [&](const Node* n, int indent) {
        std::string id    = sanitizeMermaidId(n->id);
        std::string label = n->label.empty() ? n->id : n->label;
        // 处理会影响 mermaid 解析的标签字符
        std::string safe;
        for (char c : label) {
            if (c == '"')
                safe += "#quot;";
            else
                safe += c;
        }
        os << std::string((size_t)indent, ' ') << id;
        if (n->shape == "diamond")
            os << "{\"" << safe << "\"}";
        else if (n->shape == "round")
            os << "(\"" << safe << "\")";
        else if (n->shape == "circle")
            os << "((\"" << safe << "\"))";
        else if (n->shape == "stadium")
            os << "([\"" << safe << "\"])";
        else if (n->shape == "ellipse")
            os << "([\"" << safe << "\"])";
        else
            os << "[\"" << safe << "\"]";
        os << "\n";
    };
    // group 节点导出为 subgraph（单层）
    for (auto& n : g.nodes) {
        if (n.shape != "group")
            continue;
        os << "    subgraph " << sanitizeMermaidId(n.id) << " [\""
           << (n.label.empty() ? n.id : n.label) << "\"]\n";
        for (auto* c : byGroup[n.id])
            emitNode(c, 8);
        os << "    end\n";
    }
    // 未分组节点（以及 parent 不是 group 的节点）
    for (auto& kv : byGroup) {
        const Node* p = kv.first.empty() ? nullptr : g.findNode(kv.first);
        if (p && p->shape == "group")
            continue;
        for (auto* n : kv.second)
            emitNode(n, 4);
    }
    for (auto& e : g.edges) {
        os << "    " << sanitizeMermaidId(e.from);
        if (e.style == "dashed")
            os << " -.->";
        else if (e.style == "thick")
            os << " ==>";
        else if (!edgeHasEndArrow(e) && !edgeHasStartArrow(e))
            os << " ---";
        else if (edgeHasStartArrow(e) && edgeHasEndArrow(e))
            os << " <-->";
        else if (edgeHasStartArrow(e))
            os << " <--";
        else
            os << " -->";
        if (!e.label.empty())
            os << "|" << e.label << "|";
        os << " " << sanitizeMermaidId(e.to) << "\n";
    }
    // 输出节点颜色 classDef / class（必须在 flowchart 声明之后）
    {
        std::map<std::string, int> colorClass;  // "fill:stroke" → classIdx
        int                        classIdx = 0;
        for (auto& n : g.nodes) {
            if (n.fillColor.empty() && n.strokeColor.empty())
                continue;
            std::string key = n.fillColor + ":" + n.strokeColor;
            if (!colorClass.count(key)) {
                colorClass[key] = ++classIdx;
                os << "classDef c" << classIdx << " ";
                if (!n.fillColor.empty())
                    os << "fill:" << n.fillColor;
                if (!n.strokeColor.empty()) {
                    if (!n.fillColor.empty())
                        os << ",";
                    os << "stroke:" << n.strokeColor;
                }
                os << "\n";
            }
        }
        for (auto& n : g.nodes) {
            if (n.fillColor.empty() && n.strokeColor.empty())
                continue;
            std::string key = n.fillColor + ":" + n.strokeColor;
            os << "class " << sanitizeMermaidId(n.id) << " c" << colorClass[key]
               << "\n";
        }
    }
    // 输出边的颜色（Mermaid linkStyle 按 0 起编号）
    for (size_t ei = 0; ei < g.edges.size(); ++ei) {
        if (!g.edges[ei].strokeColor.empty())
            os << "linkStyle " << ei << " stroke:" << g.edges[ei].strokeColor
               << "\n";
    }
    return os.str();
}

// ----------------------------------------------------------------- draw.io --

// drawioStyle: 将统一 shape 映射为 draw.io 样式字符串
inline std::string drawioStyle(const Node& n)
{
    // 构建颜色扩展（fillColor / strokeColor）
    std::string extra;
    if (!n.fillColor.empty())
        extra += "fillColor=" + n.fillColor + ";";
    if (!n.strokeColor.empty())
        extra += "strokeColor=" + n.strokeColor + ";";

    // 特殊逻辑：group 和 ER entity 不走通用映射表
    if (n.shape == "group") {
        // group 固定透明填充 + 虚线；自定义描边通过 extra 拼接
        std::string gs =
            "rounded=0;whiteSpace=wrap;html=1;verticalAlign=top;fillColor="
            "none;dashed=1;";
        if (!n.strokeColor.empty())
            gs += "strokeColor=" + n.strokeColor + ";";
        return gs;
    }
    if (!n.attrs.empty())
        return "shape=table;startSize=30;container=1;collapsible=0;"
               "whiteSpace=wrap;html=1;" + extra;

    // round/stadium 需要动态 arcSize，单独处理
    if (n.shape == "round")
        return "rounded=1;arcSize=10;whiteSpace=wrap;html=1;" + extra;
    if (n.shape == "stadium")
        return "rounded=1;arcSize=50;whiteSpace=wrap;html=1;" + extra;

    // 通用形状映射表：统一形状名 → draw.io 样式（不含颜色扩展）
    static const std::map<std::string, std::string> kShapeStyleMap = {
        {"diamond",       "rhombus;whiteSpace=wrap;html=1;"},
        {"ellipse",       "ellipse;whiteSpace=wrap;html=1;"},
        {"circle",        "ellipse;whiteSpace=wrap;html=1;"},
        {"hexagon",       "shape=hexagon;whiteSpace=wrap;html=1;"},
        {"triangle",      "shape=triangle;perimeter=trianglePerimeter;whiteSpace=wrap;html=1;"},
        {"parallelogram", "shape=parallelogram;perimeter=parallelogramPerimeter;whiteSpace=wrap;html=1;"},
        {"trapezoid",     "shape=trapezoid;perimeter=trapezoidPerimeter;whiteSpace=wrap;html=1;"},
        {"step",          "shape=step;perimeter=stepPerimeter;whiteSpace=wrap;html=1;"},
        {"process",       "shape=process;whiteSpace=wrap;html=1;"},
        {"document",      "shape=document;whiteSpace=wrap;html=1;"},
        {"cylinder",      "shape=cylinder3;whiteSpace=wrap;html=1;boundedLbl=1;container=0;size=15;"},
        {"delay",         "shape=delay;whiteSpace=wrap;html=1;"},
        {"manualInput",   "shape=manualInput;perimeter=manualInputPerimeter;whiteSpace=wrap;html=1;"},
        {"display",       "shape=display;whiteSpace=wrap;html=1;"},
        {"cloud",         "shape=cloud;whiteSpace=wrap;html=1;"},
        {"umlActor",      "shape=umlActor;whiteSpace=wrap;html=1;"},
        {"note",          "shape=note;whiteSpace=wrap;html=1;size=14;"},
        {"cube",          "shape=cube;whiteSpace=wrap;html=1;"},
        {"message",       "shape=message;whiteSpace=wrap;html=1;"},
    };
    auto it = kShapeStyleMap.find(n.shape);
    if (it != kShapeStyleMap.end())
        return it->second + extra;

    // 默认：普通矩形
    return "rounded=0;whiteSpace=wrap;html=1;" + extra;
}

// toDrawio: 导出 draw.io XML（支持多页）
// 关键步骤：先 layout 保证有坐标 -> 先输出 group 再输出普通节点 -> 最后输出边
inline std::string toDrawio(Graph g)
{
    std::ostringstream os;
    os << "<mxfile host=\"graphmcp\" agent=\"graphmcp/1.0\" type=\"device\">\n";

    // writePage: 输出单页 diagram 的节点、边、图层、free 笔画
    auto writePage = [&](Graph& page, const std::string& diagId) {
    gl::layout(page);
    std::vector<FreedrawStroke> strokes = collectFreedrawStrokes(page);
    os << "  <diagram name=\""
       << xmlEscape(page.name.empty() ? "Page-1" : page.name)
       << "\" id=\"" << xmlEscape(diagId) << "\">\n";
    os << "    <mxGraphModel dx=\"800\" dy=\"600\" grid=\"1\" gridSize=\"10\" "
          "guides=\"1\" tooltips=\"1\" connect=\"1\" arrows=\"1\" fold=\"1\" "
          "page=\"1\" pageScale=\"1\" pageWidth=\"1169\" pageHeight=\"826\" "
          "math=\"0\" shadow=\"0\">\n";
    os << "      <root>\n";
    os << "        <mxCell id=\"0\"/>\n";
    os << "        <mxCell id=\"1\" parent=\"0\"/>\n";
    // 输出自定义图层（默认图层 id=1 已经存在）
    for (auto& l : page.layers) {
        os << "        <mxCell id=\"" << xmlEscape(l.id) << "\" parent=\"0\""
           << " value=\"" << xmlEscape(l.name) << "\"";
        if (l.locked)
            os << " style=\"locked=1\"";
        os << "/>\n";
    }
    // 构建 layer 名称 → id 的反向映射，供节点 parent 引用
    std::map<std::string, std::string> layerIdForName;
    for (auto& l : page.layers)
        layerIdForName[l.name] = l.id;
    // 先输出 group，确保子节点可引用其作为 parent
    std::vector<const Node*> ordered;
    for (auto& n : page.nodes)
        if (n.shape == "group")
            ordered.push_back(&n);
    for (auto& n : page.nodes)
        if (n.shape != "group")
            ordered.push_back(&n);
    for (auto* n : ordered) {
        std::string label = n->label;
        if (!n->attrs.empty()) {  // ER 实体：标题 + 属性行（HTML）
            label = "<b>" + n->label + "</b>";
            for (auto& a : n->attrs)
                label += "<br/>" + a;
        }
        const Node* p = n->parent.empty() ? nullptr : page.findNode(n->parent);
        bool        insideGroup = p && p->shape == "group";
        double      x = n->x, y = n->y;
        if (insideGroup) {
            x -= p->x;
            y -= p->y;
        }  // draw.io 子节点使用相对坐标
        // 确定 parent 引用：group 优先 → 图层 → 默认图层 "1"
        std::string parentRef = "1";
        if (insideGroup)
            parentRef = n->parent;
        else if (!n->layer.empty() && layerIdForName.count(n->layer))
            parentRef = layerIdForName[n->layer];
        os << "        <mxCell id=\"" << xmlEscape(n->id) << "\" value=\""
           << xmlEscape(label) << "\" style=\"" << drawioStyle(*n)
           << "\" vertex=\"1\" parent=\"" << xmlEscape(parentRef) << "\">\n";
        os << "          <mxGeometry x=\"" << x << "\" y=\"" << y
           << "\" width=\"" << n->w << "\" height=\"" << n->h
           << "\" as=\"geometry\"/>\n";
        os << "        </mxCell>\n";
    }
    int ei = 0;
    for (auto& e : page.edges) {
        std::string style = "edgeStyle=orthogonalEdgeStyle;rounded=1;html=1;";
        if (e.style == "dashed")
            style += "dashed=1;";
        if (e.style == "thick")
            style += "strokeWidth=3;";
        if (!edgeHasEndArrow(e))
            style += "endArrow=none;";
        if (edgeHasStartArrow(e))
            style += "startArrow=classic;";
        if (!e.strokeColor.empty())
            style += "strokeColor=" + e.strokeColor + ";";
        os << "        <mxCell id=\"edge" << ++ei << "\" value=\""
           << xmlEscape(e.label) << "\" style=\"" << style
           << "\" edge=\"1\" parent=\"1\" source=\"" << xmlEscape(e.from)
           << "\" target=\"" << xmlEscape(e.to) << "\">\n";
        os << "          <mxGeometry relative=\"1\" as=\"geometry\">\n";
        // 边标签位置：layout 设置了 labelX/labelY（画布绝对坐标），
        // draw.io 需要的是相对边中心的偏移量
        if (!e.label.empty() && (e.labelX != 0 || e.labelY != 0)) {
            const Node* src = g.findNode(e.from);
            const Node* dst = g.findNode(e.to);
            if (src && dst) {
                double cx = (src->x + src->w / 2.0 + dst->x + dst->w / 2.0) / 2.0;
                double cy = (src->y + src->h / 2.0 + dst->y + dst->h / 2.0) / 2.0;
                double ox = e.labelX - cx;
                double oy = e.labelY - cy;
                os << "            <mxPoint x=\"" << ox << "\" y=\"" << oy
                   << "\" as=\"offset\"/>\n";
            }
        }
        os << "          </mxGeometry>\n";
        os << "        </mxCell>\n";
    }
    // Excalidraw freedraw -> draw.io 矢量线段（按相邻采样点拆分）
    int fi = 0;
    for (const auto& s : strokes) {
        std::string style =
            "endArrow=none;startArrow=none;html=1;rounded=0;curved=0;";
        style += "strokeColor=" + s.strokeColor + ";";
        style +=
            "strokeWidth=" + std::to_string(std::max(0.5, s.strokeWidth)) + ";";
        if (s.strokeStyle == "dashed")
            style += "dashed=1;";
        else if (s.strokeStyle == "dotted")
            style += "dashed=1;dashPattern=1 4;";
        for (size_t i = 1; i < s.points.size(); i++) {
            const auto& p0 = s.points[i - 1];
            const auto& p1 = s.points[i];
            os << "        <mxCell id=\"freedraw" << (++fi)
               << "\" value=\"\" style=\"" << style
               << "\" edge=\"1\" parent=\"1\">\n";
            os << "          <mxGeometry relative=\"1\" as=\"geometry\">\n";
            os << "            <mxPoint x=\"" << p0.first << "\" y=\""
               << p0.second << "\" as=\"sourcePoint\"/>\n";
            os << "            <mxPoint x=\"" << p1.first << "\" y=\""
               << p1.second << "\" as=\"targetPoint\"/>\n";
            os << "          </mxGeometry>\n";
            os << "        </mxCell>\n";
        }
    }
    os << "      </root>\n";
    os << "    </mxGraphModel>\n";
    os << "  </diagram>\n";
    };  // writePage lambda

    // 首页和附加页依次输出
    writePage(g, g.id.empty() ? "d1" : g.id);
    int di = 2;
    for (auto& p : g.pages)
        writePage(p, "d" + std::to_string(di++));

    os << "</mxfile>\n";
    return os.str();
}

// ------------------------------------------------------------- Excalidraw --

// excalidrawBase: 生成 Excalidraw 元素公共字段模板
inline Json excalidrawBase(const std::string& id,
                           const std::string& type,
                           double             x,
                           double             y,
                           double             w,
                           double             h,
                           int                seed)
{
    Json el = Json::obj();
    el.set("id", id);
    el.set("type", type);
    el.set("x", x);
    el.set("y", y);
    el.set("width", w);
    el.set("height", h);
    el.set("angle", 0);
    el.set("strokeColor", "#1e1e1e");
    el.set("backgroundColor", "transparent");
    el.set("fillStyle", "solid");
    el.set("strokeWidth", 2);
    el.set("strokeStyle", "solid");
    el.set("roughness", 1);
    el.set("opacity", 100);
    el.set("groupIds", Json::arr());
    el.set("frameId", Json());
    el.set("seed", seed);
    el.set("version", 1);
    el.set("versionNonce", seed * 7 + 1);
    el.set("isDeleted", false);
    el.set("boundElements", Json());
    el.set("updated", 1);
    el.set("link", Json());
    el.set("locked", false);
    return el;
}

// toExcalidraw: 导出 Excalidraw JSON
// 关键步骤：whiteboard 直接透传；否则按节点/文本/连线分别生成元素
inline std::string toExcalidraw(Graph g)
{
    Json doc = Json::obj();
    doc.set("type", "excalidraw");
    doc.set("version", 2);
    doc.set("source", "graphmcp");
    Json els = Json::arr();
    if (!g.elements.empty()) {
        // 白板场景：无损透传原始 elements
        for (auto& el : g.elements)
            els.push(el);
    }
    else {
        gl::layout(g);
        int seed = 1000;
        for (auto& n : g.nodes) {
            std::string ty = "rectangle";
            if (n.shape == "ellipse" || n.shape == "circle" ||
                n.shape == "round" || n.shape == "stadium")
                ty = "ellipse";
            if (n.shape == "diamond")
                ty = "diamond";
            Json el = excalidrawBase(n.id, ty, n.x, n.y, n.w, n.h, ++seed);
            if (!n.fillColor.empty())
                el.set("backgroundColor", n.fillColor);
            if (!n.strokeColor.empty())
                el.set("strokeColor", n.strokeColor);
            if (n.shape == "group") {
                el.set("backgroundColor", "transparent");
                el.set("strokeStyle", "dashed");
            }
            Json bound = Json::arr();
            Json bt    = Json::obj();
            bt.set("id", n.id + "_txt");
            bt.set("type", "text");
            bound.push(bt);
            el.set("boundElements", bound);
            els.push(el);
            // 绑定文本标签
            std::string label = n.label;
            for (auto& a : n.attrs)
                label += "\n" + a;
            Json txt = excalidrawBase(n.id + "_txt", "text", n.x + 10,
                                      n.y + n.h / 2 - 10, n.w - 20, 20, ++seed);
            txt.set("text", label);
            txt.set("originalText", label);
            txt.set("fontSize", 16);
            txt.set("fontFamily", 1);
            txt.set("textAlign", "center");
            txt.set("verticalAlign", n.attrs.empty() ? "middle" : "top");
            txt.set("containerId", n.id);
            txt.set("lineHeight", 1.25);
            els.push(txt);
        }
        for (auto& e : g.edges) {
            const Node* a = g.findNode(e.from);
            const Node* b = g.findNode(e.to);
            if (!a || !b)
                continue;
            double x1 = a->x + a->w / 2, y1 = a->y + a->h;
            double x2 = b->x + b->w / 2, y2 = b->y;
            if (y2 < y1) {
                y1 = a->y + a->h / 2;
                y2 = b->y + b->h / 2;
                x1 = x2 > a->x + a->w / 2 ? a->x + a->w : a->x;
            }
            Json el  = excalidrawBase(e.id, "arrow", x1, y1, x2 - x1, y2 - y1,
                                      5000 + (int)els.size());
            Json pts = Json::arr();
            Json p0  = Json::arr();
            p0.push(0.0);
            p0.push(0.0);
            Json p1 = Json::arr();
            p1.push(x2 - x1);
            p1.push(y2 - y1);
            pts.push(p0);
            pts.push(p1);
            el.set("points", pts);
            el.set("lastCommittedPoint", Json());
            Json sb = Json::obj();
            sb.set("elementId", e.from);
            sb.set("focus", 0);
            sb.set("gap", 1);
            Json eb = Json::obj();
            eb.set("elementId", e.to);
            eb.set("focus", 0);
            eb.set("gap", 1);
            el.set("startBinding", sb);
            el.set("endBinding", eb);
            el.set("startArrowhead",
                   edgeHasStartArrow(e) ? Json("arrow") : Json());
            el.set("endArrowhead",
                   edgeHasEndArrow(e) ? Json("arrow") : Json());
            if (e.style == "dashed")
                el.set("strokeStyle", "dashed");
            if (!e.strokeColor.empty())
                el.set("strokeColor", e.strokeColor);
            els.push(el);
        }
    }
    doc.set("elements", els);
    Json app = Json::obj();
    app.set("gridSize", Json());
    app.set("viewBackgroundColor", "#ffffff");
    doc.set("appState", app);
    if (g.files.isObj())
        doc.set("files", g.files);
    else
        doc.set("files", Json::obj());
    return doc.dump();
}

// -------------------------------------------------------------------- SVG --

// toSVG: 导出可视化 SVG
// 关键步骤：白板走 elements 原样渲染；其它图走 nodes/edges 布局渲染
inline std::string toSVG(Graph g)
{
    // rawMermaid 类型：生成嵌入式 SVG（提示使用 mermaid.live 或 PNG 导出查看）
    if (!g.rawMermaid.empty()) {
        std::ostringstream os;
        os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"800\" "
              "height=\"200\""
              " viewBox=\"0 0 800 200\">\n";
        os << "  <rect width=\"800\" height=\"200\" fill=\"#fafafa\" "
              "rx=\"8\"/>\n";
        os << "  <text x=\"400\" y=\"60\" text-anchor=\"middle\" "
              "font-size=\"18\""
              " fill=\"#333\" font-family=\"sans-serif\">";
        os << "Mermaid Diagram (" << xmlEscape(g.type) << ")</text>\n";
        os << "  <text x=\"400\" y=\"100\" text-anchor=\"middle\" "
              "font-size=\"13\""
              " fill=\"#666\" font-family=\"sans-serif\">";
        os << "Use 'graph_export to=png' for rendered output, or 'to=url' for "
              "mermaid.live</text>\n";
        os << "  <text x=\"400\" y=\"140\" text-anchor=\"middle\" "
              "font-size=\"11\""
              " fill=\"#999\" font-family=\"monospace\">";
        os << xmlEscape(g.type) << " (" << g.rawMermaid.size()
           << " bytes)</text>\n";
        os << "</svg>\n";
        return os.str();
    }
    if (isWhiteboardElements(g))
        return toSVGExcalidraw(g);

    gl::layout(g);
    std::vector<FreedrawStroke> strokes = collectFreedrawStrokes(g);
    double                      maxX = 200, maxY = 150;
    for (auto& n : g.nodes) {
        maxX = std::max(maxX, n.x + n.w);
        maxY = std::max(maxY, n.y + n.h);
    }
    for (const auto& s : strokes) {
        for (const auto& p : s.points) {
            maxX = std::max(maxX, p.first);
            maxY = std::max(maxY, p.second);
        }
    }
    std::ostringstream os;
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
       << (int)(maxX + 40) << "\" height=\"" << (int)(maxY + 40)
       << "\" viewBox=\"0 0 " << (int)(maxX + 40) << " " << (int)(maxY + 40)
       << "\">\n";
    os << "  <defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" "
          "refY=\"5\" "
          "markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\">"
          "<path d=\"M0,0 L10,5 L0,10 z\" fill=\"#333\"/></marker></defs>\n";
    os << "  <style>text{font-family:'Segoe "
          "UI',Arial,sans-serif;font-size:13px;}"
          ".lbl{fill:#222;}.elabel{fill:#555;font-size:11px;}</style>\n";
    // ---- 正交折线边路由 + 端口分配 + 槽位消重叠 + 反馈边样式 ----
    // 推断每层节点所属图层
    std::map<std::string, int> nodeLayer;
    {
        std::vector<double> layerY;
        for (auto& n : g.nodes) {
            bool dup = false;
            for (auto ly : layerY) { if (std::abs(ly - n.y) < 5) { dup = true; break; } }
            if (!dup) layerY.push_back(n.y);
        }
        std::sort(layerY.begin(), layerY.end());
        for (auto& n : g.nodes) {
            int best = 0; double bestD = 1e18;
            for (size_t i = 0; i < layerY.size(); i++) {
                double d = std::abs(layerY[i] - n.y);
                if (d < bestD) { bestD = d; best = (int)i; }
            }
            nodeLayer[n.id] = best;
        }
    }
    // 第一遍：统计层对边数 + 为每条边算初始 rX（槽位初值）
    std::map<std::pair<int,int>, int> pairTotal;
    struct EdgeRoute { std::string edgeId; double rX; int slot; };
    std::map<std::pair<int,int>, std::vector<EdgeRoute>> pairRoutes;
    for (auto& e : g.edges) {
        auto ai = nodeLayer.find(e.from), bi = nodeLayer.find(e.to);
        if (ai == nodeLayer.end() || bi == nodeLayer.end()) continue;
        int la = ai->second, lb = bi->second;
        if (la == lb) continue;
        auto key = std::make_pair(std::min(la, lb), std::max(la, lb));
        const Node* a = g.findNode(e.from);
        const Node* b = g.findNode(e.to);
        if (!a || !b) continue;
        double rX = (a->x + a->w/2 + b->x + b->w/2) / 2;
        pairRoutes[key].push_back({e.id, rX, 0});
        pairTotal[key]++;
    }
    // 第二遍：每对层内按 rX 排序后左右扫描，推开重叠的边
    std::map<std::string, double> edgeRouteX;  // 每条边最终路由 X
    for (auto& kv : pairRoutes) {
        auto& routes = kv.second;
        std::sort(routes.begin(), routes.end(),
            [](const EdgeRoute& a, const EdgeRoute& b) { return a.rX < b.rX; });
        for (size_t i = 0; i < routes.size(); i++) routes[i].slot = (int)i;
        // 左右扫描，保证相邻边至少间隔 16px
        for (int sweep = 0; sweep < 2; sweep++) {
            for (size_t i = 1; i < routes.size(); i++) {
                double need = routes[i-1].rX + 16;
                if (routes[i].rX < need) routes[i].rX = need;
            }
            for (size_t i = routes.size() - 1; i > 0; i--) {
                double need = routes[i].rX - 16;
                if (routes[i-1].rX > need) routes[i-1].rX = need;
            }
        }
        for (auto& rt : routes) edgeRouteX[rt.edgeId] = rt.rX;
    }

    // 第三遍：绘制边
    for (auto& e : g.edges) {
        const Node* a = g.findNode(e.from);
        const Node* b = g.findNode(e.to);
        if (!a || !b) continue;

        auto ai = nodeLayer.find(e.from), bi = nodeLayer.find(e.to);
        int  la = (ai != nodeLayer.end()) ? ai->second : -1;
        int  lb = (bi != nodeLayer.end()) ? bi->second : -1;

        double a_cx = a->x + a->w / 2, a_cy = a->y + a->h / 2;
        double b_cx = b->x + b->w / 2, b_cy = b->y + b->h / 2;
        // 反馈边判定：源在目标下方 3 层以上才视为"反向流"，避免过度标记
        bool   feedback = (la >= 0 && lb >= 0 && la - lb >= 3);
        std::vector<std::pair<double,double>> pts;

        if (la >= 0 && lb >= 0 && la != lb) {
            // ---- 端口分配：根据目标方向选择最近出口 ----
            double dx = b_cx - a_cx;
            double aBot = a->y + a->h, aLft = a->x, aRgt = a->x + a->w;
            double bTop = b->y, bLft = b->x, bRgt = b->x + b->w;

            if (!feedback) {
                // 正向边：从 A 的底部/侧边出，从 B 的顶部/侧边入
                double sx, sy;  // source port (on A)
                if (std::abs(dx) > a->w * 0.8) {
                    sx = (dx > 0) ? aRgt : aLft;
                    sy = a_cy;
                } else {
                    sx = a_cx; sy = aBot;
                }
                pts.push_back({sx, sy});

                // 目标端口
                double tx, ty;
                if (std::abs(dx) > b->w * 0.8) {
                    tx = (dx > 0) ? bLft : bRgt;
                    ty = b_cy;
                } else {
                    tx = b_cx; ty = bTop;
                }

                if (!e.waypoints.empty()) {
                    // 使用布局阶段计算好的路径点（虚拟节点在各中间层的坐标）
                    for (auto& wp : e.waypoints) {
                        auto& last = pts.back();
                        if (std::abs(last.first - wp.first) > 4 ||
                            std::abs(last.second - wp.second) > 4)
                            pts.push_back(wp);
                    }
                } else {
                    // 兜底：单中点路由
                    auto rxit = edgeRouteX.find(e.id);
                    double midY = (aBot + bTop) / 2;
                    double rX = (rxit != edgeRouteX.end()) ? rxit->second
                                                           : (a_cx + b_cx) / 2;
                    if (std::abs(sx - rX) > 4 || std::abs(sy - midY) > 4)
                        pts.push_back({rX, midY});
                }

                auto& last = pts.back();
                if (std::abs(last.first - tx) > 4 ||
                    std::abs(last.second - ty) > 4)
                    pts.push_back({tx, ty});
                else if (pts.size() < 2)
                    pts.push_back({tx, ty});
            } else {
                // 反馈边：从右侧绕行
                double rX = std::max(aRgt, bRgt) + 34;
                auto rxit = edgeRouteX.find(e.id);
                if (rxit != edgeRouteX.end()) rX = rxit->second;
                pts.push_back({aRgt, a_cy});
                pts.push_back({rX, a_cy});
                pts.push_back({rX, b_cy});
                pts.push_back({bRgt, b_cy});
            }
        }
        if (pts.size() < 2) {
            // 兜底：直线裁剪
            auto clip = [](double cx, double cy, double w, double h,
                           double tx, double ty, double& ox, double& oy) {
                double dx = tx - cx, dy = ty - cy;
                double sx = dx != 0 ? (w / 2) / std::fabs(dx) : 1e18;
                double sy = dy != 0 ? (h / 2) / std::fabs(dy) : 1e18;
                double s  = std::min(sx, sy); s = std::min(s, 1.0);
                ox = cx + dx * s; oy = cy + dy * s;
            };
            double x1, y1, x2, y2;
            clip(a_cx, a_cy, a->w, a->h, b_cx, b_cy, x1, y1);
            clip(b_cx, b_cy, b->w, b->h, a_cx, a_cy, x2, y2);
            pts.push_back({x1, y1});
            pts.push_back({x2, y2});
        }
        // 输出 polyline（支持 strokeColor 定制，fallback 到默认色）
        std::string edgeColor = e.strokeColor.empty()
            ? (feedback ? "#b0714b" : "#333")
            : e.strokeColor;
        bool edgeDashed = feedback || e.style == "dashed";
        os << "  <polyline fill=\"none\" stroke=\""
           << xmlEscape(edgeColor)
           << "\" stroke-width=\"" << (e.style == "thick" ? 3 : 1.5) << "\""
           << " stroke-linejoin=\"round\" points=\"";
        for (size_t i = 0; i < pts.size(); i++) {
            if (i) os << " ";
            os << pts[i].first << "," << pts[i].second;
        }
        os << "\"";
        if (edgeDashed)
            os << " stroke-dasharray=\"6,4\"";
        if (edgeHasEndArrow(e))
            os << " marker-end=\"url(#arrow)\"";
        if (edgeHasStartArrow(e))
            os << " marker-start=\"url(#arrow)\"";
        os << "/>\n";
        // 边标签
        if (!e.label.empty()) {
            size_t mid = pts.size() / 2;
            size_t nxt = std::min(mid + 1, pts.size() - 1);
            double lx = (pts[mid].first + pts[nxt].first) / 2;
            double ly = (pts[mid].second + pts[nxt].second) / 2;
            os << "  <text class=\"elabel\" x=\"" << lx << "\" y=\""
               << ly - 4 << "\" text-anchor=\"middle\">"
               << xmlEscape(e.label) << "</text>\n";
        }
    }
    // 再绘制节点
    for (auto& n : g.nodes) {
        // fc/sc: 空串回退默认色，写入属性前统一 xmlEscape
        auto fc = [&](const char* def) {
            return xmlEscape(n.fillColor.empty() ? def : n.fillColor);
        };
        auto sc = [&](const char* def) {
            return xmlEscape(n.strokeColor.empty() ? def : n.strokeColor);
        };
        if (n.shape == "group") {
            os << "  <rect x=\"" << n.x << "\" y=\"" << n.y << "\" width=\""
               << n.w << "\" height=\"" << n.h << "\" fill=\"none\" stroke=\""
               << sc("#999")
               << "\" "
                  "stroke-dasharray=\"5,4\" rx=\"6\"/>\n";
            os << "  <text class=\"lbl\" x=\"" << n.x + 8 << "\" y=\""
               << n.y + 18 << "\" fill=\"#777\">" << xmlEscape(n.label)
               << "</text>\n";
            continue;
        }
        double cx = n.x + n.w / 2, cy = n.y + n.h / 2;
        if (n.shape == "diamond") {
            os << "  <polygon points=\"" << cx << "," << n.y << " " << n.x + n.w
               << "," << cy << " " << cx << "," << n.y + n.h << " " << n.x
               << "," << cy << "\" fill=\"" << fc("#fff7e0") << "\" stroke=\""
               << sc("#c9a227") << "\"/>\n";
        }
        else if (n.shape == "ellipse" || n.shape == "circle" ||
                 n.shape == "round" || n.shape == "stadium") {
            os << "  <ellipse cx=\"" << cx << "\" cy=\"" << cy << "\" rx=\""
               << n.w / 2 << "\" ry=\"" << n.h / 2 << "\" fill=\""
               << fc("#e8f7ec") << "\" stroke=\"" << sc("#3d9155") << "\"/>\n";
        }
        else {
            os << "  <rect x=\"" << n.x << "\" y=\"" << n.y << "\" width=\""
               << n.w << "\" height=\"" << n.h << "\" rx=\"4\" fill=\""
               << fc("#eef4ff") << "\" stroke=\"" << sc("#4a72b8") << "\"/>\n";
        }
        if (n.attrs.empty()) {
            os << "  <text class=\"lbl\" x=\"" << cx << "\" y=\"" << cy + 5
               << "\" text-anchor=\"middle\">" << xmlEscape(n.label)
               << "</text>\n";
        }
        else {  // ER 实体（包含属性行）
            os << "  <text class=\"lbl\" x=\"" << cx << "\" y=\"" << n.y + 20
               << "\" text-anchor=\"middle\" font-weight=\"bold\">"
               << xmlEscape(n.label) << "</text>\n";
            os << "  <line x1=\"" << n.x << "\" y1=\"" << n.y + 28 << "\" x2=\""
               << n.x + n.w << "\" y2=\"" << n.y + 28 << "\" stroke=\""
               << sc("#4a72b8") << "\"/>\n";
            double ty = n.y + 46;
            for (auto& a : n.attrs) {
                os << "  <text class=\"lbl\" x=\"" << n.x + 10 << "\" y=\""
                   << ty << "\">" << xmlEscape(a) << "</text>\n";
                ty += 22;
            }
        }
    }
    // 最后绘制 freedraw，作为白板标注层
    for (const auto& s : strokes) {
        os << "  <polyline fill=\"none\" stroke=\"" << xmlEscape(s.strokeColor)
           << "\" stroke-width=\"" << std::max(0.5, s.strokeWidth)
           << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" opacity=\""
           << s.opacity << "\" points=\"";
        for (size_t i = 0; i < s.points.size(); i++) {
            if (i)
                os << " ";
            os << s.points[i].first << "," << s.points[i].second;
        }
        os << "\"";
        if (s.strokeStyle == "dashed")
            os << " stroke-dasharray=\"6,4\"";
        else if (s.strokeStyle == "dotted")
            os << " stroke-dasharray=\"1,4\"";
        os << "/>\n";
    }
    os << "</svg>\n";
    return os.str();
}

// -------------------------------------------------------------------- URL --

// mermaid.live URL：使用普通 base64 载荷（无需压缩）
// toMermaidLiveUrl: 生成可直接打开的 mermaid.live 编辑链接
inline std::string toMermaidLiveUrl(const Graph& g)
{
    std::string code    = g.rawMermaid.empty() ? toMermaid(g) : g.rawMermaid;
    Json        payload = Json::obj();
    payload.set("code", code);
    payload.set("mermaid", "{\"theme\":\"default\"}");
    payload.set("autoSync", true);
    payload.set("updateDiagram", true);
    return "https://mermaid.live/edit#base64:" + base64Encode(payload.dump());
}

// ------------------------------------------------------ PNG/PDF 工具导出 --

// 绝对路径（浏览器 file:// URL 需要）
// absPath: 取绝对路径，避免外部工具在不同工作目录下找不到文件
inline std::string absPath(const std::string& p)
{
#ifdef _WIN32
    char buf[1024];
    if (_fullpath(buf, p.c_str(), sizeof(buf)))
        return buf;
#else
    char buf[4096];
    if (realpath(p.c_str(), buf))
        return buf;
#endif
    return p;
}

// 由文件系统路径构建浏览器可用 file:// URL
// fileUrl: 将本地文件路径转换为浏览器可识别的 file:/// URL
inline std::string fileUrl(const std::string& path)
{
    std::string abs = absPath(path);
    for (char& c : abs)
        if (c == '\\')
            c = '/';
    return "file:///" + abs;
}

// 从 SVG 头部读取整型属性（width="..." / height="..."）
// svgDim: 从 SVG 头部读取宽高属性，为截图/PDF输出提供尺寸参数
inline int svgDim(const std::string& svg, const std::string& attr)
{
    size_t p = svg.find(attr + "=\"");
    if (p == std::string::npos)
        return 0;
    p += attr.size() + 2;
    int v = 0;
    while (p < svg.size() && isdigit((unsigned char)svg[p]))
        v = v * 10 + (svg[p++] - '0');
    return v;
}

// 按 PATH 名称或安装路径定位 Chromium 内核浏览器（Chrome/Edge）
// findBrowser: 按候选路径探测可用 Chromium 内核浏览器
inline std::string findBrowser()
{
    std::vector<std::string> cands;
#ifdef _WIN32
    std::string pf   = getEnvVar("ProgramFiles");
    std::string pf86 = getEnvVar("ProgramFiles(x86)");
    std::string lad  = getEnvVar("LOCALAPPDATA");
    auto        add  = [&](const std::string& base, const char* rel) {
        if (!base.empty())
            cands.push_back(base + rel);
    };
    add(pf, "\\Google\\Chrome\\Application\\chrome.exe");
    add(pf86, "\\Google\\Chrome\\Application\\chrome.exe");
    add(lad, "\\Google\\Chrome\\Application\\chrome.exe");
    add(pf, "\\Microsoft\\Edge\\Application\\msedge.exe");
    add(pf86, "\\Microsoft\\Edge\\Application\\msedge.exe");
    // 硬编码兜底：某些 shell（MSYS/Git Bash）会隐藏带括号的环境变量名，
    // 例如 ProgramFiles(x86)，导致 getenv 取不到。
    cands.push_back(
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe");
    cands.push_back(
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe");
    cands.push_back(
        "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe");
    cands.push_back(
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe");
#else
    cands = {"/usr/bin/google-chrome", "/usr/bin/chromium",
             "/usr/bin/chromium-browser", "/usr/bin/microsoft-edge"};
#endif
    for (auto& c : cands) {
#ifdef _WIN32
        std::ifstream f(c, std::ios::binary);
        if (f.good())
#else
        if (access(c.c_str(), X_OK) == 0)
#endif
            return c;
    }
    return "";
}

#ifdef _WIN32
inline std::wstring widen(const std::string& s)
{
    int          n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#endif

// 静默执行命令。Windows 下通过临时 .bat 规避 cmd.exe 在“带引号可执行路径
// + 重定向”场景下的引号剥离问题（该问题会让 std::system 悄悄失败）。
// runQuiet: 静默执行命令；可选硬超时（毫秒），超时返回 -2 并置 timedOut
inline int runQuiet(const std::string& cmd,
                    const std::string& tmpBase,
                    int                timeoutMs = -1,
                    bool*              timedOut  = nullptr)
{
    if (timedOut)
        *timedOut = false;
    if (timeoutMs < 0)
        timeoutMs = exportTimeoutMs();
#ifdef _WIN32
    std::string bat = tmpBase + ".run.bat";
    writeFile(bat, "@echo off\r\n" + cmd + "\r\n");
    std::string comspec = getEnvVar("COMSPEC");
    if (comspec.empty())
        comspec = "C:\\Windows\\System32\\cmd.exe";
    std::string cmdline =
        "\"" + comspec + "\" /d /s /c call \"" + bat + "\"";
    std::wstring         wexe    = widen(comspec);
    std::wstring         wcmd    = widen(cmdline);
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessW(wexe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        std::remove(bat.c_str());
        return -1;
    }
    // Job Object 确保超时时连同 cmd 拉起的转换器子进程一起结束。
    // Assign 失败（嵌套 Job 等）时废弃 Job，退化为只终止主进程。
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                                sizeof(info));
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    int   rc   = -1;
    if (wait == WAIT_TIMEOUT) {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        if (timedOut)
            *timedOut = true;
        rc = -2;
    }
    else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = (int)code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (job)
        CloseHandle(job);
    std::remove(bat.c_str());
    return rc;
#else
    (void)tmpBase;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    }
    setpgid(pid, pid);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 && errno != EINTR)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            if (timedOut)
                *timedOut = true;
            return -2;
        }
        usleep(20000);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// 直接拉起浏览器（不经 shell）。Windows 使用 CreateProcessW，因此不依赖
// cmd.exe / COMSPEC / PATH。MCP 客户端常在精简环境中启动服务端，这点很关键。
// bInheritHandles 设为 FALSE，避免子进程写入 stdout（JSON-RPC 通道）。
// argstr 表示可执行文件之后的完整参数字符串（需提前处理好引号）。
// launchBrowser: 直接拉起浏览器子进程做渲染，避免依赖 shell 环境
// 返回 -2 表示超时
inline int launchBrowser(const std::string& exe,
                         const std::string& argstr,
                         bool*              timedOut = nullptr)
{
    if (timedOut)
        *timedOut = false;
    int timeoutMs = exportTimeoutMs();
#ifdef _WIN32
    std::string          cmdline = "\"" + exe + "\" " + argstr;
    std::wstring         wexe    = widen(exe);
    std::wstring         wcmd    = widen(cmdline);
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessW(wexe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok)
        return -1;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
        ZeroMemory(&info, sizeof(info));
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info,
                                sizeof(info));
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    int   rc   = -1;
    if (wait == WAIT_TIMEOUT) {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        if (timedOut)
            *timedOut = true;
        rc = -2;
    }
    else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        rc = (int)code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (job)
        CloseHandle(job);
    return rc;
#else
    std::string command =
        "\"" + exe + "\" " + argstr + " >/dev/null 2>&1";
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127);
    }
    setpgid(pid, pid);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0 && errno != EINTR)
            return -1;
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            if (timedOut)
                *timedOut = true;
            return -2;
        }
        usleep(20000);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// 尝试外部转换器；成功返回工具名，失败返回空字符串。
// 顺序：inkscape/rsvg/magick（若在 PATH）-> Chromium 浏览器（Chrome/Edge）。
// rasterize: 将 SVG 栅格化/打印为 png 或 pdf
// 关键步骤：优先本地工具链 -> 失败后回退 headless 浏览器 -> 校验输出文件
inline std::string rasterize(const std::string& svgPathIn,
                             const std::string& outPathIn,
                             const std::string& fmt)
{
    // 统一转绝对路径：浏览器会以自身工作目录解析 --print-to-pdf /
    // --screenshot， 若 outPath 是相对路径，可能静默写到错误位置。
    std::string svgPath = absPath(svgPathIn);
    std::string outPath = absPath(outPathIn);
#ifdef _WIN32
    std::string quiet = " >nul 2>nul";
#else
    std::string quiet = " >/dev/null 2>/dev/null";
#endif
    auto produced = [&]() {
        std::ifstream check(outPath, std::ios::binary);
        return check.good() &&
               check.peek() != std::ifstream::traits_type::eof();
    };

    std::vector<std::pair<std::string, std::string>> cands;  // {工具名, 命令}
    cands.push_back({"inkscape", "inkscape \"" + svgPath +
                                     "\" --export-filename=\"" + outPath +
                                     "\"" + quiet});
    cands.push_back({"rsvg-convert", "rsvg-convert -f " + fmt + " -o \"" +
                                         outPath + "\" \"" + svgPath + "\"" +
                                         quiet});
    cands.push_back(
        {"magick", "magick \"" + svgPath + "\" \"" + outPath + "\"" + quiet});
    for (auto& c : cands) {
        std::remove(outPath.c_str());
        bool timedOut = false;
        int  rc       = runQuiet(c.second, outPath, -1, &timedOut);
        if (timedOut)
            return std::string("__timeout__");
        if (rc == 0 && produced())
            return c.first;
    }

    // Chromium 兜底：将 SVG 包进紧凑尺寸 HTML，避免输出默认为整页 A4。
    std::string browser = findBrowser();
    if (!browser.empty()) {
        std::string svg = readFile(svgPath);
        int         w = svgDim(svg, "width"), h = svgDim(svg, "height");
        if (w <= 0)
            w = 1200;
        if (h <= 0)
            h = 800;
        std::string html =
            "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
            "@page{size:" +
            std::to_string(w) + "px " + std::to_string(h) +
            "px;margin:0}html,body{margin:0;padding:0;background:#fff}"
            "svg{display:block}</style></head><body>" +
            svg + "</body></html>";
        std::string htmlPath = svgPath + ".wrap.html";
        writeFile(htmlPath, html);
        std::string url = fileUrl(htmlPath);
        // 独立 user-data-dir：没有它时，新 headless
        // 进程可能附着到已运行浏览器， 导致任务被静默跳过。若 TEMP 不可用（精简
        // MCP 环境），回退到输出目录旁。
        std::string tmp = getEnvVar("TEMP");
        if (tmp.empty())
            tmp = getEnvVar("TMP");
        std::string profile = (!tmp.empty() ? tmp + "/graphmcp-chrome-profile" :
                                              outPath + ".chromeprofile");
        std::string args    = "--headless=new --disable-gpu --no-sandbox "
                              "--virtual-time-budget=3000 "
                              "--user-data-dir=\"" +
                              profile + "\" ";
        if (fmt == "pdf")
            args += "--no-pdf-header-footer --print-to-pdf=\"" + outPath +
                    "\" \"" + url + "\"";
        else
            args += "--force-device-scale-factor=2 --window-size=" +
                    std::to_string(w) + "," + std::to_string(h) +
                    " --screenshot=\"" + outPath + "\" \"" + url + "\"";
        std::remove(outPath.c_str());
        bool timedOut = false;
        launchBrowser(browser, args, &timedOut);
        std::remove(htmlPath.c_str());
        if (timedOut)
            return std::string("__timeout__");
        if (produced()) {
#ifdef _WIN32
            return browser.find("msedge") != std::string::npos ? "edge" :
                                                                 "chrome";
#else
            return "chromium";
#endif
        }
    }
    return "";
}

// toMermaidBrowserPage: 为 rawMermaid 类型生成内嵌 Mermaid.js 的完整 HTML 页面
// 可由浏览器渲染 SVG，进而截图/打印为 PNG/PDF
inline std::string toMermaidBrowserPage(const Graph& g)
{
    std::string code = g.rawMermaid.empty() ? toMermaid(g) : g.rawMermaid;
    std::string escaped;
    for (char c : code) {
        switch (c) {
            case '<': escaped += "&lt;"; break;
            case '&': escaped += "&amp;"; break;
            default: escaped += c;
        }
    }
    return R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>graphmcp Mermaid</title>
<style>
  body{margin:0;padding:20px;background:#fff;font-family:sans-serif;}
  .mermaid{display:flex;justify-content:center;}
</style>
<script src="https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.min.js"></script>
<script>
  mermaid.initialize({startOnLoad:true,theme:'default',securityLevel:'loose'});
</script>
</head>
<body>
<pre class="mermaid">
)" + escaped +
           R"(
</pre>
</body>
</html>)";
}

// rasterizeMermaid: 将 rawMermaid 图表通过 headless 浏览器渲染为 PNG 或 PDF
// 关键步骤：生成 HTML -> 写临时文件 -> 浏览器截图/打印 -> 校验输出
inline std::string rasterizeMermaid(const Graph&       g,
                                    const std::string& outPath,
                                    const std::string& fmt)
{
    std::string html     = toMermaidBrowserPage(g);
    std::string htmlPath = outPath + ".mermaid.html";
    if (!writeFile(htmlPath, html))
        return "";

    std::string absOut  = absPath(outPath);
    std::string browser = findBrowser();
    if (browser.empty()) {
        std::remove(htmlPath.c_str());
        return "";
    }

    std::string tmp = getEnvVar("TEMP");
    if (tmp.empty())
        tmp = getEnvVar("TMP");
    std::string profile = (!tmp.empty() ? tmp + "/graphmcp-chrome-profile" :
                                          outPath + ".chromeprofile");

    int         w = 1400, h = 1000;
    std::string url  = fileUrl(htmlPath);
    std::string args = "--headless=new --disable-gpu --no-sandbox "
                       "--virtual-time-budget=5000 "
                       "--user-data-dir=\"" +
                       profile + "\" ";
    if (fmt == "pdf") {
        args += "--no-pdf-header-footer --print-to-pdf=\"" + absOut + "\" \"" +
                url + "\"";
    }
    else {
        args +=
            "--force-device-scale-factor=2 --window-size=" + std::to_string(w) +
            "," + std::to_string(h) + " --screenshot=\"" + absOut + "\" \"" +
            url + "\"";
    }

    std::remove(outPath.c_str());
    bool timedOut = false;
    launchBrowser(browser, args, &timedOut);
    std::remove(htmlPath.c_str());
    if (timedOut)
        return std::string("__timeout__");

    std::ifstream check(outPath, std::ios::binary);
    if (check.good() && check.peek() != std::ifstream::traits_type::eof()) {
#ifdef _WIN32
        return browser.find("msedge") != std::string::npos ? "edge" : "chrome";
#else
        return "chromium";
#endif
    }
    return "";
}

// --------------------------------------------------------------- 分发入口 --

// ExportResult: 导出结果对象（ok/message/content/path 四元信息）
struct ExportResult
{
    bool        ok = false;
    bool        timedOut = false;  // 外部转换硬超时
    std::string message;  // 人类可读状态信息
    std::string content;  // 内联内容（文本格式 / URL）
    std::string path;     // 写文件时的输出路径
};

// to 可选：drawio | mermaid | excalidraw | svg | png | pdf | url | model
// exportGraph: 统一导出分发入口
// 关键步骤：按目标格式生成内容 -> 可选写文件 -> 对 png/pdf 提供 SVG 回退
inline ExportResult
exportGraph(Graph g, const std::string& to, const std::string& outPath = "")
{
    ExportResult r;
    std::string  content;
    if (to == "drawio") {
        // rawMermaid 类型：生成含注释节点的 drawio，避免空白图表
        if (!g.rawMermaid.empty() && g.nodes.empty()) {
            Node& note = g.ensureNode("raw-mermaid-note", g.rawMermaid);
            note.shape = "note";
            note.x = 20; note.y = 20; note.w = 400; note.h = 200;
        }
        content = toDrawio(g);
    }
    else if (to == "mermaid")
        content = toMermaid(g);
    else if (to == "excalidraw")
        content = toExcalidraw(g);
    else if (to == "svg")
        content = toSVG(g);
    else if (to == "model" || to == "json") {
        gl::layout(g);
        content = g.toJson().dump();
    }
    else if (to == "url") {
        r.ok      = true;
        r.content = toMermaidLiveUrl(g);
        r.message = "browser URL generated (mermaid.live)";
        return r;
    }
    else if (to == "png" || to == "pdf") {
        std::string base = outPath.empty() ? ("graph_export." + to) : outPath;
        // rawMermaid 类型：通过浏览器渲染 Mermaid.js -> 截图/打印
        if (!g.rawMermaid.empty()) {
            std::string tool = rasterizeMermaid(g, base, to);
            if (tool == "__timeout__") {
                r.timedOut = true;
                r.message =
                    "export timed out after " +
                    std::to_string(exportTimeoutMs()) +
                    "ms (set GRAPHMCP_EXPORT_TIMEOUT_MS to adjust); "
                    "install/check converter or export to svg";
                return r;
            }
            if (!tool.empty()) {
                r.ok      = true;
                r.path    = base;
                r.message = to + " written via " + tool +
                            " (mermaid browser render): " + base;
                return r;
            }
            // 降级：生成 Mermaid 文本
            std::string fallback = base + ".mmd";
            writeFile(fallback, g.rawMermaid);
            r.message =
                "no browser found for mermaid render; wrote raw mermaid to " +
                fallback + " - open in mermaid.live to view";
            r.path = fallback;
            return r;
        }
        // PNG/PDF：统一走精确 SVG 再栅格化（不尝试近似 rough 叠加）。
        std::string svg     = toSVG(g);
        std::string svgPath = base + ".tmp.svg";
        if (!writeFile(svgPath, svg)) {
            r.message = "cannot write temp svg: " + svgPath;
            return r;
        }
        std::string tool = rasterize(svgPath, base, to);
        std::remove(svgPath.c_str());
        if (tool == "__timeout__") {
            std::string fallback = base + ".svg";
            writeFile(fallback, svg);
            r.timedOut = true;
            r.path     = fallback;
            r.message =
                "export timed out after " + std::to_string(exportTimeoutMs()) +
                "ms; wrote SVG fallback to " + fallback +
                " (set GRAPHMCP_EXPORT_TIMEOUT_MS to adjust)";
            return r;
        }
        if (tool.empty()) {
            // 平滑兜底：在目标输出旁保留一份 SVG
            std::string fallback = base + ".svg";
            writeFile(fallback, svg);
            r.message =
                "no external converter found (tried inkscape/rsvg-convert/"
                "magick); wrote SVG fallback to " +
                fallback + " - convert it manually or install a converter";
            r.path = fallback;
            return r;
        }
        r.ok      = true;
        r.path    = base;
        r.message = to + " written via " + tool + ": " + base;
        return r;
    }
    else {
        r.message =
            "unknown export format: " + to +
            " (expected drawio|mermaid|excalidraw|svg|png|pdf|url|model)";
        return r;
    }

    if (!outPath.empty()) {
        if (!writeFile(outPath, content)) {
            r.message = "cannot write file: " + outPath;
            return r;
        }
        r.ok      = true;
        r.path    = outPath;
        r.message = to + " written: " + outPath;
    }
    else {
        r.ok      = true;
        r.content = content;
        r.message =
            to + " generated (" + std::to_string(content.size()) + " bytes)";
    }
    return r;
}

// ----------------------------------------------------- 外部编辑器打开 --

// editorFromEnv: 读取 GRAPHMCP_EDITOR 环境变量，允许用户覆盖默认编辑器
inline std::string editorFromEnv()
{ return getEnvVar("GRAPHMCP_EDITOR"); }

// findExecutable: 从候选路径列表中定位第一个存在的可执行文件
inline std::string findExecutable(const std::vector<std::string>& cands)
{
    for (auto& c : cands) {
        std::ifstream f(c, std::ios::binary);
        if (f.good())
            return c;
    }
    return "";
}

// findDrawioDesktop: 按候选路径探测 draw.io Desktop 安装
inline std::string findDrawioDesktop()
{
    std::vector<std::string> cands;
#ifdef _WIN32
    std::string lad = getEnvVar("LOCALAPPDATA");
    if (!lad.empty())
        cands.push_back(lad + "\\Programs\\draw.io\\draw.io.exe");
    std::string pf = getEnvVar("ProgramFiles");
    if (!pf.empty())
        cands.push_back(pf + "\\draw.io\\draw.io.exe");
#elif __APPLE__
    cands.push_back("/Applications/draw.io.app/Contents/MacOS/draw.io");
#else
    cands.push_back("/usr/bin/draw.io");
    cands.push_back("/usr/local/bin/draw.io");
#endif
    return findExecutable(cands);
}

// findVSCode: 按候选路径探测 VS Code 安装（常用于 SVG 编辑）
inline std::string findVSCode()
{
    std::vector<std::string> cands;
#ifdef _WIN32
    std::string lad = getEnvVar("LOCALAPPDATA");
    if (!lad.empty())
        cands.push_back(lad + "\\Programs\\Microsoft VS Code\\Code.exe");
    std::string pf = getEnvVar("ProgramFiles");
    if (!pf.empty())
        cands.push_back(pf + "\\Microsoft VS Code\\Code.exe");
#elif __APPLE__
    cands.push_back("/usr/local/bin/code");
    cands.push_back(
        "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
#else
    cands.push_back("/usr/bin/code");
    cands.push_back("/usr/local/bin/code");
#endif
    return findExecutable(cands);
}

// resolveEditor: 按优先级解析编辑器路径
// 1. 显式传入 editorPath  2. GRAPHMCP_EDITOR 环境变量
// 3. 按类型自动探测（drawio→findDrawioDesktop, svg→findVSCode）
// 返回空串表示使用系统默认关联打开
inline std::string resolveEditor(const std::string& editorType,
                                 const std::string& editorPath = "")
{
    if (!editorPath.empty())
        return editorPath;
    std::string env = editorFromEnv();
    if (!env.empty())
        return env;
    if (editorType == "drawio") {
        std::string found = findDrawioDesktop();
        if (!found.empty())
            return found;
    }
    if (editorType == "svg") {
        std::string found = findVSCode();
        if (!found.empty())
            return found;
    }
    // 交叉兜底
    std::string alt = findDrawioDesktop();
    if (!alt.empty())
        return alt;
    alt = findVSCode();
    if (!alt.empty())
        return alt;
    return "";
}

// readOpenFile: 从图存储目录探测 graph_open 生成的临时编辑文件
// 按 .drawio → .excalidraw → .svg 顺序查找，返回内容并设置 format
inline std::string readOpenFile(const std::string& storeRoot,
                                const std::string& graphId,
                                std::string&       format)
{
    std::string base = storeRoot + "/" + graphId + "/open";
    struct
    {
        const char* ext;
        const char* fmt;
    } cands[] = {
        {".drawio", "drawio"},
        {".excalidraw", "excalidraw"},
        {".svg", "svg"},
    };
    for (auto& c : cands) {
        std::string text = readFile(base + c.ext);
        if (!text.empty()) {
            if (format == "auto" || format.empty())
                format = c.fmt;
            return text;
        }
    }
    return "";
}

// openExternal: 用系统默认处理器或指定编辑器打开 URL/文件
// 当指定编辑器失败时，自动降级为系统默认关联打开
// 设置环境变量 GRAPHMCP_NO_LAUNCH=1 时跳过实际拉起（供单元测试/CI）
inline bool openExternal(const std::string& target,
                         const std::string& editor = "")
{
    // GRAPHMCP_NO_LAUNCH=1 时跳过 ShellExecute/xdg-open（单元测试与无头 CI）
    const char* no_launch = std::getenv("GRAPHMCP_NO_LAUNCH");
    if (no_launch && no_launch[0] != '\0' && no_launch[0] != '0')
        return false;
#ifdef _WIN32
    std::wstring wtarget = widen(target);
    if (!editor.empty()) {
        std::wstring weditor = widen(editor);
        HINSTANCE    h = ShellExecuteW(nullptr, L"open", weditor.c_str(),
                                       wtarget.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(h) > 32)
            return true;
        // 降级：用系统默认关联重试
        h = ShellExecuteW(nullptr, L"open", wtarget.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(h) > 32;
    }
    HINSTANCE h = ShellExecuteW(nullptr, L"open", wtarget.c_str(), nullptr,
                                nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(h) > 32;
#elif __APPLE__
    if (!editor.empty()) {
        std::string appPath = editor;
        size_t      dotApp  = appPath.find(".app/");
        if (dotApp != std::string::npos)
            appPath = appPath.substr(0, dotApp + 4);
        if (std::system(
                ("open -a \"" + appPath + "\" \"" + target + "\"").c_str()) ==
            0)
            return true;
        if (std::system(
                ("\"" + editor + "\" \"" + target + "\" >/dev/null 2>&1")
                    .c_str()) == 0)
            return true;
        return std::system(("open \"" + target + "\"").c_str()) == 0;
    }
    return std::system(("open \"" + target + "\"").c_str()) == 0;
#else
    std::string quiet = " >/dev/null 2>&1";
    if (!editor.empty()) {
        if (std::system(
                ("\"" + editor + "\" \"" + target + "\"" + quiet).c_str()) == 0)
            return true;
        return std::system(("xdg-open \"" + target + "\"" + quiet).c_str()) ==
               0;
    }
    return std::system(("xdg-open \"" + target + "\"" + quiet).c_str()) == 0;
#endif
}

}  // namespace ge
