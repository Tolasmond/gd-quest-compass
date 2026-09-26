// Loads only this workspace's position prototype into the specified game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include "game_process.h"

static uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W m{sizeof(m)}; uintptr_t base = 0;
    if (Module32FirstW(s, &m)) do {
        if (!_wcsicmp(m.szModule, name)) { base = reinterpret_cast<uintptr_t>(m.modBaseAddr); break; }
    } while (Module32NextW(s, &m));
    CloseHandle(s); return base;
}
int wmain(int argc, wchar_t** argv) {
    bool checkOnly = argc == 3 && !_wcsicmp(argv[1], L"--check-process");
    if (!(argc == 2 || checkOnly)) { puts("Usage: position-loader.exe <game PID> | --check-process <game PID>"); return 1; }
    wchar_t* end; unsigned long value = wcstoul(checkOnly ? argv[2] : argv[1], &end, 10);
    if (!value || *end) { puts("Invalid PID."); return 1; }
    DWORD pid = value;
    DWORD access = checkOnly ? PROCESS_QUERY_LIMITED_INFORMATION :
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE proc = OpenProcess(access, FALSE, pid);
    if (!proc) { printf("OpenProcess failed: %lu\n", GetLastError()); return 1; }
    try {
        auto game = gameprocess::executable(proc);
        wprintf(L"Running game executable: %ls\n", game.c_str());
    } catch (const std::exception& e) {
        printf("Refusing target: %s\n", e.what()); CloseHandle(proc); return 1;
    }
    if (checkOnly) { CloseHandle(proc); return 0; }
    if (!ModuleBase(pid, L"Game.dll") || !ModuleBase(pid, L"Engine.dll")) {
        puts("Refusing: Game.dll and Engine.dll must be loaded before attachment."); CloseHandle(proc); return 1;
    }
    if (ModuleBase(pid, L"position-overlay.dll")) { puts("Prototype already loaded. Restart the game before loading again."); CloseHandle(proc); return 1; }
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) { CloseHandle(proc); return 1; }
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"position-overlay.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) { puts("Missing position-overlay.dll beside loader."); CloseHandle(proc); return 1; }
    auto local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!local || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(local), &owner)) { CloseHandle(proc); return 1; }
    wchar_t ownerPath[MAX_PATH]; GetModuleFileNameW(owner, ownerPath, MAX_PATH);
    const wchar_t* ownerName = wcsrchr(ownerPath, L'\\'); ownerName = ownerName ? ownerName + 1 : ownerPath;
    uintptr_t remoteBase = ModuleBase(pid, ownerName);
    if (!remoteBase) { puts("Cannot resolve remote LoadLibraryW module."); CloseHandle(proc); return 1; }
    auto remote = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + reinterpret_cast<uintptr_t>(local) - reinterpret_cast<uintptr_t>(owner));
    SIZE_T bytes = (wcslen(path) + 1) * sizeof(wchar_t), written = 0;
    void* allocation = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!allocation || !WriteProcessMemory(proc, allocation, path, bytes, &written) || written != bytes) {
        printf("DLL path transfer failed: %lu\n", GetLastError());
        if (allocation) VirtualFreeEx(proc, allocation, 0, MEM_RELEASE); CloseHandle(proc); return 1;
    }
    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, remote, allocation, 0, nullptr);
    if (!thread) { printf("Load thread failed: %lu\n", GetLastError()); VirtualFreeEx(proc, allocation, 0, MEM_RELEASE); CloseHandle(proc); return 1; }
    DWORD wait = WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0) {
        puts("Load did not finish in 10 seconds. Do not retry until the game restarts.");
        // The remote thread may still use this string: retain it until process exit.
        CloseHandle(proc); return 1;
    }
    VirtualFreeEx(proc, allocation, 0, MEM_RELEASE);
    bool loaded = ModuleBase(pid, L"position-overlay.dll") != 0;
    CloseHandle(proc);
    puts(loaded ? "DLL loaded. Check position-overlay.log for hook/window status, then return to the game." : "DLL load failed.");
    return loaded ? 0 : 1;
}
