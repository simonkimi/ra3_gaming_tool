#include <stdexcept>
#include <format>
#include "process_helper.h"
#include "raii.h"
#include "Psapi.h"

void win32::CrtInjectDll(DWORD pid, LPCTSTR dll_path)
{
    // 打开注入进程
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

    // 在注入进程中申请内存
    VirtualAllocRaii dll_addr(hProcess.Get(), dll_size);
    if (dll_addr.IsInvalid())
    {
        throw std::runtime_error(std::format("VirtualAllocEx failed, error code: {}", ::GetLastError()));
    }

    // 注入dll文件名称
    if (!::WriteProcessMemory(hProcess.Get(), dll_addr.Get(), dll_path, dll_size, nullptr))
    {
        throw std::runtime_error(std::format("WriteProcessMemory failed, error code: {}", ::GetLastError()));
    }

    // 获取LoadLibraryA函数地址
    FARPROC load_lib_addr = ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    if (load_lib_addr == nullptr)
    {
        throw std::runtime_error(std::format("GetProcAddress failed, error code: {}", ::GetLastError()));
    }

    // 在注入进程中创建远程线程
    HandleRaii thread_handle(::CreateRemoteThread(
        hProcess.Get(),
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)load_lib_addr,
        dll_addr.Get(),
        0,
        nullptr));
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
    HandleRaii thread_handle(CreateRemoteThread(
        hProcess.Get(),
        nullptr,
        0,
        (LPTHREAD_START_ROUTINE)free_lib_addr,
        dll_handle,
        0,
        nullptr));

    if (thread_handle.IsInvalid())
    {
        throw std::runtime_error(std::format("CreateRemoteThread failed, error code: {}", ::GetLastError()));
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}

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
