#include "platform/power.hpp"

#include <powrprof.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace tf::power {
namespace {

// Reglages masques dans l'UI Windows : Attributes bit 0 = masque.
constexpr wchar_t kPowerSettingsKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings";

} // namespace

// ---------------------------------------------------------------------------
// GUID
// ---------------------------------------------------------------------------
std::string guid_to_string(const GUID& g) {
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%08lx-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
                  g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                  g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

std::optional<GUID> guid_from_string(const std::string& s) {
    GUID g{};
    unsigned long d1 = 0;
    unsigned int  d2 = 0, d3 = 0;
    unsigned int  d4[8]{};
    int n = std::sscanf(s.c_str(), "%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
                        &d1, &d2, &d3, &d4[0], &d4[1], &d4[2], &d4[3],
                        &d4[4], &d4[5], &d4[6], &d4[7]);
    if (n != 11) return std::nullopt;
    g.Data1 = d1;
    g.Data2 = static_cast<unsigned short>(d2);
    g.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) g.Data4[i] = static_cast<unsigned char>(d4[i]);
    return g;
}

bool guid_equal(const GUID& a, const GUID& b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

// ---------------------------------------------------------------------------
// Plans
// ---------------------------------------------------------------------------
std::optional<GUID> active_scheme() {
    GUID* p = nullptr;
    if (::PowerGetActiveScheme(nullptr, &p) != ERROR_SUCCESS || !p) return std::nullopt;
    GUID g = *p;
    ::LocalFree(p);
    return g;
}

Result set_active_scheme(const GUID& scheme) {
    DWORD st = ::PowerSetActiveScheme(nullptr, &scheme);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("activation du plan {} impossible : {}",
                                        guid_to_string(scheme), win32_error(st)));
    }
    return Result::success();
}

std::string scheme_name(const GUID& scheme) {
    DWORD bytes = 0;
    if (::PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &bytes) !=
            ERROR_SUCCESS ||
        bytes == 0) {
        return guid_to_string(scheme);
    }
    std::vector<UCHAR> buf(bytes + sizeof(wchar_t), 0);
    if (::PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, buf.data(), &bytes) !=
        ERROR_SUCCESS) {
        return guid_to_string(scheme);
    }
    return narrow(reinterpret_cast<const wchar_t*>(buf.data()));
}

std::vector<GUID> list_schemes() {
    std::vector<GUID> out;
    for (ULONG index = 0;; ++index) {
        GUID  g{};
        DWORD size = sizeof(g);
        DWORD st = ::PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                                    reinterpret_cast<UCHAR*>(&g), &size);
        if (st != ERROR_SUCCESS) break;
        out.push_back(g);
        if (index > 64) break;  // garde-fou
    }
    return out;
}

bool scheme_exists(const GUID& scheme) {
    for (const GUID& g : list_schemes()) {
        if (guid_equal(g, scheme)) return true;
    }
    return false;
}

std::optional<GUID> ensure_ultimate_scheme() {
    if (scheme_exists(kSchemeUltimate)) return kSchemeUltimate;

    // Le plan « Performances ultimes » existe comme modele mais n'est pas
    // expose : on le duplique pour le rendre selectionnable.
    GUID* dst = nullptr;
    DWORD st = ::PowerDuplicateScheme(nullptr, &kSchemeUltimate, &dst);
    if (st != ERROR_SUCCESS || !dst) {
        log_warn("duplication du plan Performances ultimes impossible : {}", win32_error(st));
        return std::nullopt;
    }
    GUID g = *dst;
    ::LocalFree(dst);
    log_info("plan Performances ultimes cree : {}", guid_to_string(g));
    return g;
}

// ---------------------------------------------------------------------------
// Reglages
// ---------------------------------------------------------------------------
namespace {

std::optional<GUID> resolve(const GUID* scheme) {
    if (scheme) return *scheme;
    return active_scheme();
}

} // namespace

std::optional<uint32_t> read_ac(const GUID& sub, const GUID& setting, const GUID* scheme) {
    auto s = resolve(scheme);
    if (!s) return std::nullopt;
    DWORD value = 0;
    if (::PowerReadACValueIndex(nullptr, &*s, &sub, &setting, &value) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

std::optional<uint32_t> read_dc(const GUID& sub, const GUID& setting, const GUID* scheme) {
    auto s = resolve(scheme);
    if (!s) return std::nullopt;
    DWORD value = 0;
    if (::PowerReadDCValueIndex(nullptr, &*s, &sub, &setting, &value) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return value;
}

Result write_ac(const GUID& sub, const GUID& setting, uint32_t value, const GUID* scheme) {
    auto s = resolve(scheme);
    if (!s) return Result::fail("plan d'alimentation actif introuvable");
    DWORD st = ::PowerWriteACValueIndex(nullptr, &*s, &sub, &setting, value);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("ecriture AC {} = {} refusee : {}",
                                        guid_to_string(setting), value, win32_error(st)));
    }
    return Result::success();
}

Result write_dc(const GUID& sub, const GUID& setting, uint32_t value, const GUID* scheme) {
    auto s = resolve(scheme);
    if (!s) return Result::fail("plan d'alimentation actif introuvable");
    DWORD st = ::PowerWriteDCValueIndex(nullptr, &*s, &sub, &setting, value);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("ecriture DC {} = {} refusee : {}",
                                        guid_to_string(setting), value, win32_error(st)));
    }
    return Result::success();
}

Result commit() {
    auto s = active_scheme();
    if (!s) return Result::fail("plan d'alimentation actif introuvable");
    return set_active_scheme(*s);
}

void unhide_setting(const GUID& sub, const GUID& setting) {
    // HKLM\...\PowerSettings\<sous-groupe>\<reglage>\Attributes = 2
    std::wstring key = std::wstring(kPowerSettingsKey) + L"\\" +
                       widen(guid_to_string(sub)) + L"\\" + widen(guid_to_string(setting));
    HKEY h = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) {
        return;
    }
    DWORD v = 2;
    ::RegSetValueExW(h, L"Attributes", 0, REG_DWORD,
                     reinterpret_cast<const BYTE*>(&v), sizeof(v));
    ::RegCloseKey(h);
}

} // namespace tf::power
