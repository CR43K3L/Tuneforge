#include "platform/winreg.hpp"

#include <windows.h>

namespace tf::reg {
namespace {

HKEY hkey(Root r) {
    switch (r) {
        case Root::HKLM: return HKEY_LOCAL_MACHINE;
        case Root::HKCU: return HKEY_CURRENT_USER;
        case Root::HKCR: return HKEY_CLASSES_ROOT;
        case Root::HKU:  return HKEY_USERS;
    }
    return HKEY_LOCAL_MACHINE;
}

constexpr REGSAM kView = KEY_WOW64_64KEY;

} // namespace

std::wstring root_name(Root r) {
    switch (r) {
        case Root::HKLM: return L"HKLM";
        case Root::HKCU: return L"HKCU";
        case Root::HKCR: return L"HKCR";
        case Root::HKU:  return L"HKU";
    }
    return L"?";
}

std::optional<Root> root_from_name(std::wstring_view s) {
    if (s == L"HKLM" || s == L"HKEY_LOCAL_MACHINE") return Root::HKLM;
    if (s == L"HKCU" || s == L"HKEY_CURRENT_USER")  return Root::HKCU;
    if (s == L"HKCR" || s == L"HKEY_CLASSES_ROOT")  return Root::HKCR;
    if (s == L"HKU"  || s == L"HKEY_USERS")         return Root::HKU;
    return std::nullopt;
}

bool key_exists(Root root, const std::wstring& key) {
    HKEY h = nullptr;
    if (::RegOpenKeyExW(hkey(root), key.c_str(), 0, KEY_READ | kView, &h) != ERROR_SUCCESS) {
        return false;
    }
    ::RegCloseKey(h);
    return true;
}

Result ensure_key(Root root, const std::wstring& key) {
    HKEY  h = nullptr;
    DWORD disp = 0;
    LSTATUS st = ::RegCreateKeyExW(hkey(root), key.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE | kView,
                                   nullptr, &h, &disp);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("creation de {}\\{} impossible : {}",
                                        narrow(root_name(root)), narrow(key),
                                        win32_error(static_cast<unsigned long>(st))));
    }
    ::RegCloseKey(h);
    return Result::success();
}

std::optional<uint32_t> read_dword(Root root, const std::wstring& key, const std::wstring& name) {
    DWORD  value = 0;
    DWORD  size = sizeof(value);
    DWORD  type = 0;
    LSTATUS st = ::RegGetValueW(hkey(root), key.c_str(), name.c_str(),
                                RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY,
                                &type, &value, &size);
    if (st != ERROR_SUCCESS) return std::nullopt;
    return value;
}

std::optional<std::wstring> read_string(Root root, const std::wstring& key,
                                        const std::wstring& name) {
    DWORD size = 0;
    LSTATUS st = ::RegGetValueW(hkey(root), key.c_str(), name.c_str(),
                                RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_SUBKEY_WOW6464KEY,
                                nullptr, nullptr, &size);
    if (st != ERROR_SUCCESS || size == 0) return std::nullopt;

    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    st = ::RegGetValueW(hkey(root), key.c_str(), name.c_str(),
                        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_SUBKEY_WOW6464KEY,
                        nullptr, buf.data(), &size);
    if (st != ERROR_SUCCESS) return std::nullopt;
    buf.resize(wcslen(buf.c_str()));
    return buf;
}

namespace {

Result write_raw(Root root, const std::wstring& key, const std::wstring& name, DWORD type,
                 const void* data, DWORD bytes) {
    HKEY  h = nullptr;
    DWORD disp = 0;
    LSTATUS st = ::RegCreateKeyExW(hkey(root), key.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | kView,
                                   nullptr, &h, &disp);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("ouverture en ecriture de {}\\{} refusee : {}",
                                        narrow(root_name(root)), narrow(key),
                                        win32_error(static_cast<unsigned long>(st))));
    }
    st = ::RegSetValueExW(h, name.c_str(), 0, type,
                          static_cast<const BYTE*>(data), bytes);
    ::RegCloseKey(h);
    if (st != ERROR_SUCCESS) {
        return Result::fail(std::format("ecriture de {} refusee : {}", narrow(name),
                                        win32_error(static_cast<unsigned long>(st))));
    }
    return Result::success();
}

} // namespace

Result write_dword(Root root, const std::wstring& key, const std::wstring& name, uint32_t value) {
    DWORD v = value;
    return write_raw(root, key, name, REG_DWORD, &v, sizeof(v));
}

Result write_string(Root root, const std::wstring& key, const std::wstring& name,
                    const std::wstring& value) {
    return write_raw(root, key, name, REG_SZ, value.c_str(),
                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

Result delete_value(Root root, const std::wstring& key, const std::wstring& name) {
    HKEY h = nullptr;
    LSTATUS st = ::RegOpenKeyExW(hkey(root), key.c_str(), 0, KEY_SET_VALUE | kView, &h);
    if (st == ERROR_FILE_NOT_FOUND) return Result::success();  // deja absent
    if (st != ERROR_SUCCESS) {
        return Result::fail(win32_error(static_cast<unsigned long>(st)));
    }
    st = ::RegDeleteValueW(h, name.c_str());
    ::RegCloseKey(h);
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) {
        return Result::fail(win32_error(static_cast<unsigned long>(st)));
    }
    return Result::success();
}

std::vector<std::wstring> enum_subkeys(Root root, const std::wstring& key) {
    std::vector<std::wstring> out;
    HKEY h = nullptr;
    if (::RegOpenKeyExW(hkey(root), key.c_str(), 0, KEY_READ | kView, &h) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t name[512];
    DWORD   index = 0;
    while (true) {
        DWORD len = static_cast<DWORD>(std::size(name));
        LSTATUS st = ::RegEnumKeyExW(h, index++, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (st != ERROR_SUCCESS) break;
        out.emplace_back(name, len);
    }
    ::RegCloseKey(h);
    return out;
}

} // namespace tf::reg
