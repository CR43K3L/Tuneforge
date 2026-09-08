#include "platform/elevation.hpp"

#include <windows.h>
#include <shellapi.h>

namespace tf {

bool is_elevated() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD           size = sizeof(elevation);
    BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool relaunch_elevated(const std::vector<std::string>& args, bool wait_for_exit) {
    std::wstring params;
    for (const auto& a : args) {
        if (!params.empty()) params += L' ';
        std::wstring w = widen(a);
        if (w.find(L' ') != std::wstring::npos) {
            params += L'"';
            params += w;
            params += L'"';
        } else {
            params += w;
        }
    }

    std::wstring path = exe_path();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb       = L"runas";
    sei.lpFile       = path.c_str();
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow        = SW_SHOWNORMAL;

    if (!::ShellExecuteExW(&sei)) {
        DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED) {
            log_error("elevation refusee par l'utilisateur (UAC annule)");
        } else {
            log_error("relance elevee impossible : {}", win32_error(err));
        }
        return false;
    }

    if (sei.hProcess) {
        if (wait_for_exit) ::WaitForSingleObject(sei.hProcess, INFINITE);
        ::CloseHandle(sei.hProcess);
    }
    return true;
}

SingleInstance::SingleInstance(const std::wstring& name) {
    handle_ = ::CreateMutexW(nullptr, TRUE, name.c_str());
    acquired_ = handle_ != nullptr && ::GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance() {
    if (handle_) {
        if (acquired_) ::ReleaseMutex(handle_);
        ::CloseHandle(handle_);
    }
}

} // namespace tf
