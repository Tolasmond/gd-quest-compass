#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace gameprocess {
inline std::filesystem::path executable(HANDLE process) {
    wchar_t path[32768]{};
    DWORD length = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &length))
        throw std::runtime_error("Cannot read the target process executable path");
    std::filesystem::path exe(path);
    if (_wcsicmp(exe.filename().c_str(), L"Grim Dawn.exe") ||
        _wcsicmp(exe.parent_path().filename().c_str(), L"x64"))
        throw std::runtime_error("Target is not x64/Grim Dawn.exe");
    if (!std::filesystem::is_regular_file(exe))
        throw std::runtime_error("Target executable is not available on disk");
    BOOL wow = TRUE;
    if (!IsWow64Process(process, &wow) || wow)
        throw std::runtime_error("Target must be a 64-bit process");
    return exe;
}
}
