#pragma once
//
// Acces registre : lecture / ecriture / suppression, avec vue 64 bits forcee.
//
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/util.hpp"

namespace tf::reg {

enum class Root { HKLM, HKCU, HKCR, HKU };

std::wstring root_name(Root r);
std::optional<Root> root_from_name(std::wstring_view s);

bool key_exists(Root root, const std::wstring& key);
Result ensure_key(Root root, const std::wstring& key);

std::optional<uint32_t>     read_dword(Root root, const std::wstring& key, const std::wstring& name);
std::optional<std::wstring> read_string(Root root, const std::wstring& key, const std::wstring& name);

Result write_dword(Root root, const std::wstring& key, const std::wstring& name, uint32_t value);
Result write_string(Root root, const std::wstring& key, const std::wstring& name,
                    const std::wstring& value);
Result delete_value(Root root, const std::wstring& key, const std::wstring& name);

std::vector<std::wstring> enum_subkeys(Root root, const std::wstring& key);

} // namespace tf::reg
