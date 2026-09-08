#include "common/util.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace tf {
namespace {

std::mutex   g_log_mutex;
FILE*        g_log_file = nullptr;
LogLevel     g_console_level = LogLevel::Info;
bool         g_vt_enabled = false;

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

const char* level_color(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "\x1b[90m";
        case LogLevel::Info:  return "\x1b[36m";
        case LogLevel::Warn:  return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
    }
    return "";
}

} // namespace

// ---------------------------------------------------------------------------
// Chaines
// ---------------------------------------------------------------------------
std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int need = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                          out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && is_ws(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Erreurs Win32
// ---------------------------------------------------------------------------
std::string win32_error(unsigned long code) {
    LPWSTR buf = nullptr;
    DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::string msg;
    if (n && buf) {
        msg = trim(narrow(std::wstring_view(buf, n)));
        ::LocalFree(buf);
    }
    if (msg.empty()) msg = "erreur inconnue";
    return std::format("{} (0x{:08X})", msg, code);
}

std::string last_error() { return win32_error(::GetLastError()); }

// ---------------------------------------------------------------------------
// Journalisation
// ---------------------------------------------------------------------------
void log_init(const std::wstring& file_path) {
    std::lock_guard lk(g_log_mutex);
    if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
    // Mode texte ANSI volontairement : on ecrit nous-memes des octets UTF-8.
    // Ouvrir avec « ccs=UTF-8 » mettrait le flux en mode Unicode, ou tout
    // fprintf narrow declenche l'invalid parameter handler du CRT — qui tue
    // le processus par __fastfail, sans exception rattrapable.
    g_log_file = ::_wfopen(file_path.c_str(), L"a");
}

void log_set_console_level(LogLevel min_level) { g_console_level = min_level; }

void log_write(LogLevel lvl, std::string_view msg) {
    std::lock_guard lk(g_log_mutex);
    const std::string ts = timestamp_iso();
    if (g_log_file) {
        std::fprintf(g_log_file, "%s [%s] %.*s\n", ts.c_str(), level_name(lvl),
                     static_cast<int>(msg.size()), msg.data());
        std::fflush(g_log_file);
    }
    if (static_cast<int>(lvl) >= static_cast<int>(g_console_level)) {
        if (g_vt_enabled) {
            std::fprintf(stderr, "%s[%s]\x1b[0m %.*s\n", level_color(lvl), level_name(lvl),
                         static_cast<int>(msg.size()), msg.data());
        } else {
            std::fprintf(stderr, "[%s] %.*s\n", level_name(lvl),
                         static_cast<int>(msg.size()), msg.data());
        }
    }
}

void log_close() {
    std::lock_guard lk(g_log_mutex);
    if (g_log_file) { std::fclose(g_log_file); g_log_file = nullptr; }
}

// ---------------------------------------------------------------------------
// Chemins
// ---------------------------------------------------------------------------
std::wstring data_dir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;

    PWSTR local = nullptr;
    std::wstring base;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        base = local;
        ::CoTaskMemFree(local);
    } else {
        wchar_t buf[MAX_PATH]{};
        if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) base = buf;
    }
    if (base.empty()) base = exe_dir();

    cached = base + L"\\Tuneforge";
    ensure_dir(cached);
    return cached;
}

std::wstring exe_path() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::wstring exe_dir() {
    std::wstring p = exe_path();
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

bool file_exists(const std::wstring& p) {
    DWORD a = ::GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::wstring& p) {
    DWORD a = ::GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensure_dir(const std::wstring& p) {
    if (dir_exists(p)) return true;
    // Cree recursivement.
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 2) ensure_dir(p.substr(0, pos));
    return ::CreateDirectoryW(p.c_str(), nullptr) != 0 ||
           ::GetLastError() == ERROR_ALREADY_EXISTS;
}

std::optional<std::string> read_text_file(const std::wstring& p) {
    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart > (64 << 20)) {
        ::CloseHandle(h);
        return std::nullopt;
    }
    std::string out(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) return std::nullopt;
    out.resize(read);
    // Retire un eventuel BOM UTF-8.
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

bool write_text_file(const std::wstring& p, std::string_view content) {
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) ensure_dir(p.substr(0, pos));

    // Ecriture atomique : fichier temporaire puis remplacement.
    std::wstring tmp = p + L".tmp";
    HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = content.empty() ? TRUE
                              : ::WriteFile(h, content.data(),
                                            static_cast<DWORD>(content.size()), &written, nullptr);
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
    if (!ok) { ::DeleteFileW(tmp.c_str()); return false; }

    if (!::MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool delete_file(const std::wstring& p) { return ::DeleteFileW(p.c_str()) != 0; }

// ---------------------------------------------------------------------------
// Divers
// ---------------------------------------------------------------------------
static void local_now(SYSTEMTIME& st) { ::GetLocalTime(&st); }

std::string timestamp_iso() {
    SYSTEMTIME st{};
    local_now(st);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
}

std::string timestamp_compact() {
    SYSTEMTIME st{};
    local_now(st);
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", st.wYear, st.wMonth, st.wDay,
                       st.wHour, st.wMinute, st.wSecond);
}

uint64_t unix_time() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

bool enable_vt_console() {
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (!::GetConsoleMode(h, &mode)) return false;
    if (!::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return false;
    ::SetConsoleOutputCP(CP_UTF8);
    g_vt_enabled = true;
    return true;
}

} // namespace tf
