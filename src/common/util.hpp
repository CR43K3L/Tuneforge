#pragma once
//
// Utilitaires transverses : conversions, resultat, journalisation, chemins.
//
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tf {

// --- Chaines ---------------------------------------------------------------
std::string  narrow(std::wstring_view w);
std::wstring widen(std::string_view s);
std::string  trim(std::string_view s);
std::string  lower(std::string_view s);
bool         iequals(std::string_view a, std::string_view b);
bool         starts_with(std::string_view s, std::string_view prefix);
std::vector<std::string> split(std::string_view s, char sep);

// --- Resultat --------------------------------------------------------------
struct Result {
    bool        ok = true;
    std::string message;

    static Result success(std::string m = {}) { return Result{true, std::move(m)}; }
    static Result fail(std::string m)         { return Result{false, std::move(m)}; }
    explicit operator bool() const { return ok; }
};

// Formate le dernier code d'erreur Win32 en texte lisible.
std::string win32_error(unsigned long code);
std::string last_error();

// --- Journalisation --------------------------------------------------------
enum class LogLevel { Debug, Info, Warn, Error };

void log_init(const std::wstring& file_path);
void log_set_console_level(LogLevel min_level);
void log_write(LogLevel lvl, std::string_view msg);
void log_close();

template <typename... A>
void log_debug(std::format_string<A...> f, A&&... a) {
    log_write(LogLevel::Debug, std::format(f, std::forward<A>(a)...));
}
template <typename... A>
void log_info(std::format_string<A...> f, A&&... a) {
    log_write(LogLevel::Info, std::format(f, std::forward<A>(a)...));
}
template <typename... A>
void log_warn(std::format_string<A...> f, A&&... a) {
    log_write(LogLevel::Warn, std::format(f, std::forward<A>(a)...));
}
template <typename... A>
void log_error(std::format_string<A...> f, A&&... a) {
    log_write(LogLevel::Error, std::format(f, std::forward<A>(a)...));
}

// --- Chemins ---------------------------------------------------------------
// %LOCALAPPDATA%\Tuneforge — cree le dossier si necessaire.
std::wstring data_dir();
std::wstring exe_dir();
std::wstring exe_path();
bool         file_exists(const std::wstring& p);
bool         dir_exists(const std::wstring& p);
bool         ensure_dir(const std::wstring& p);
std::optional<std::string> read_text_file(const std::wstring& p);
bool         write_text_file(const std::wstring& p, std::string_view content);
bool         delete_file(const std::wstring& p);

// --- Divers ----------------------------------------------------------------
std::string timestamp_iso();     // 2026-09-06T14:22:31
std::string timestamp_compact(); // 20260906-142231
uint64_t    unix_time();

// Console : active le rendu ANSI/VT100 si possible. Retourne true si actif.
bool enable_vt_console();

} // namespace tf
