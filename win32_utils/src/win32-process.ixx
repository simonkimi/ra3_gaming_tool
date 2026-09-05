module;

#include <Windows.h>
#include <tchar.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <stdexcept>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <cstdint>

export module win32:process;

import :raii;

export namespace win32
{

void CrtInjectDll(DWORD pid, LPCTSTR dll_path);

void CrtFreeDll(DWORD pid, LPCTSTR dll_path);

HWND GetProcessWindow();

std::pair<long, long> GetWindowSize(HWND hwnd);

HMODULE FindRemoteModuleHandle(HANDLE handle, LPCTSTR modulePath);

DWORD FindProcessId(std::wstring_view exe_name);

void CallRemoteExport(DWORD pid, LPCTSTR dll_path, const char *export_name);

}

void win32::CrtInjectDll(DWORD pid, LPCTSTR dll_path)
{
    HandleRaii hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));

    if (hProcess.Get() == nullptr)
    {
        throw std::runtime_error(std::format("OpenProcess failed, error code: {}", ::GetLastError()));
    }

    if (FindRemoteModuleHandle(hProcess.Get(), dll_path) != nullptr)
    {
        throw std::runtime_error("当前模块已经被加载");
    }

    SIZE_T dll_size = (lstrlen(dll_path) + 1) * sizeof(TCHAR);

    VirtualAllocRaii dll_addr(hProcess.Get(), dll_size);
    if (dll_addr.IsInvalid())
    {
        throw std::runtime_error(std::format("VirtualAllocEx failed, error code: {}", ::GetLastError()));
    }

    if (!::WriteProcessMemory(hProcess.Get(), dll_addr.Get(), dll_path, dll_size, nullptr))
    {
        throw std::runtime_error(std::format("WriteProcessMemory failed, error code: {}", ::GetLastError()));
    }

    FARPROC load_lib_addr = ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    if (load_lib_addr == nullptr)
    {
        throw std::runtime_error(std::format("GetProcAddress failed, error code: {}", ::GetLastError()));
    }

    HandleRaii thread_handle(::CreateRemoteThread(hProcess.Get(), nullptr, 0, (LPTHREAD_START_ROUTINE)load_lib_addr,
                                                  dll_addr.Get(), 0, nullptr));
    if (thread_handle.IsInvalid())
    {
        throw std::runtime_error(std::format("CreateRemoteThread failed, error code: {}", ::GetLastError()));
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}

HMODULE win32::FindRemoteModuleHandle(HANDLE handle, LPCTSTR modulePath)
{
    HMODULE hMods[1024];
    DWORD cbNeeded;
    if (EnumProcessModules(handle, hMods, sizeof(hMods), &cbNeeded))
    {
        for (int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            TCHAR szModName[MAX_PATH];
            if (GetModuleFileNameExW(handle, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                if (lstrcmpi(szModName, modulePath) == 0)
                {
                    return hMods[i];
                }
            }
        }
    }
    return nullptr;
}

void win32::CrtFreeDll(DWORD pid, LPCTSTR dll_path)
{
    HandleRaii hProcess(::OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if (hProcess.Get() == nullptr)
    {
        throw std::runtime_error(std::format("OpenProcess failed, error code: {}", ::GetLastError()));
    }

    HMODULE dll_handle = FindRemoteModuleHandle(hProcess.Get(), dll_path);
    if (dll_handle == nullptr)
    {
        throw std::runtime_error(std::format("FindRemoteModuleHandle failed, error code: {}", ::GetLastError()));
    }

    FARPROC free_lib_addr = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary");
    HandleRaii thread_handle(
        CreateRemoteThread(hProcess.Get(), nullptr, 0, (LPTHREAD_START_ROUTINE)free_lib_addr, dll_handle, 0, nullptr));

    if (thread_handle.IsInvalid())
    {
        throw std::runtime_error(std::format("CreateRemoteThread failed, error code: {}", ::GetLastError()));
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}

namespace
{

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    DWORD lpdwProcessId;
    GetWindowThreadProcessId(hwnd, &lpdwProcessId);
    if (lpdwProcessId == GetCurrentProcessId())
    {
        HWND *pWnd = reinterpret_cast<HWND *>(lParam);
        if (pWnd)
        {
            *pWnd = hwnd;
        }
        return FALSE;
    }
    return TRUE;
}

}

HWND win32::GetProcessWindow()
{
    HWND h_wnd_ = nullptr;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&h_wnd_));
    return h_wnd_;
}

std::pair<long, long> win32::GetWindowSize(HWND hwnd)
{
    RECT rect;
    ::GetWindowRect(hwnd, &rect);
    return std::make_pair(rect.right - rect.left, rect.bottom - rect.top);
}

DWORD win32::FindProcessId(std::wstring_view exe_name)
{
    HandleRaii snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.IsInvalid())
    {
        throw std::runtime_error(std::format("CreateToolhelp32Snapshot failed, error code: {}", ::GetLastError()));
    }

    PROCESSENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    if (!Process32First(snapshot.Get(), &entry))
    {
        return 0;
    }

    do
    {
        if (_wcsicmp(entry.szExeFile, std::wstring(exe_name).c_str()) == 0)
        {
            return entry.th32ProcessID;
        }
    } while (Process32Next(snapshot.Get(), &entry));

    return 0;
}

void win32::CallRemoteExport(DWORD pid, LPCTSTR dll_path, const char *export_name)
{
    HMODULE local_module = LoadLibraryEx(dll_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (local_module == nullptr)
    {
        throw std::runtime_error(std::format("LoadLibraryEx failed, error code: {}", ::GetLastError()));
    }

    FARPROC local_fn = GetProcAddress(local_module, export_name);
    if (local_fn == nullptr)
    {
        const DWORD err = ::GetLastError();
        FreeLibrary(local_module);
        throw std::runtime_error(std::format("GetProcAddress({}) failed, error code: {}", export_name, err));
    }

    HandleRaii hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if (hProcess.Get() == nullptr)
    {
        FreeLibrary(local_module);
        throw std::runtime_error(std::format("OpenProcess failed, error code: {}", ::GetLastError()));
    }

    HMODULE remote_module = FindRemoteModuleHandle(hProcess.Get(), dll_path);
    if (remote_module == nullptr)
    {
        FreeLibrary(local_module);
        throw std::runtime_error("remote module not found");
    }

    const auto remote_fn = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<uintptr_t>(remote_module) +
        (reinterpret_cast<uintptr_t>(local_fn) - reinterpret_cast<uintptr_t>(local_module)));
    FreeLibrary(local_module);

    HandleRaii thread_handle(CreateRemoteThread(hProcess.Get(), nullptr, 0, remote_fn, nullptr, 0, nullptr));
    if (thread_handle.IsInvalid())
    {
        throw std::runtime_error(std::format("CreateRemoteThread failed, error code: {}", ::GetLastError()));
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}
