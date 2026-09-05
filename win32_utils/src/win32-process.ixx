module;

#include <Windows.h>
#include <tchar.h>
#include <Psapi.h>
#include <cstdio>
#include <cstdint>
#include <stdexcept>

export module win32:process;

import :raii;
import :mutex;

export namespace win32
{

void CrtInjectDll(DWORD pid, LPCTSTR dll_path);

void CrtFreeDll(DWORD pid, LPCTSTR dll_path);

HMODULE FindRemoteModuleHandle(HANDLE handle, LPCTSTR modulePath);

void CallRemoteExport(DWORD pid, LPCTSTR dll_path, const char *export_name);

}

namespace
{

[[noreturn]] void ThrowLastError(const char *api)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed, error code: %lu", api, ::GetLastError());
    throw std::runtime_error(buf);
}

}

void win32::CrtInjectDll(DWORD pid, LPCTSTR dll_path)
{
    if (InjectionMutexPresent(pid))
    {
        throw std::runtime_error("当前进程已经注入过");
    }

    HandleRaii hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));

    if (hProcess.Get() == nullptr)
    {
        ThrowLastError("OpenProcess");
    }

    if (FindRemoteModuleHandle(hProcess.Get(), dll_path) != nullptr)
    {
        throw std::runtime_error("当前模块已经被加载");
    }

    SIZE_T dll_size = (lstrlen(dll_path) + 1) * sizeof(TCHAR);

    VirtualAllocRaii dll_addr(hProcess.Get(), dll_size);
    if (dll_addr.IsInvalid())
    {
        ThrowLastError("VirtualAllocEx");
    }

    if (!::WriteProcessMemory(hProcess.Get(), dll_addr.Get(), dll_path, dll_size, nullptr))
    {
        ThrowLastError("WriteProcessMemory");
    }

    FARPROC load_lib_addr = ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    if (load_lib_addr == nullptr)
    {
        ThrowLastError("GetProcAddress");
    }

    HandleRaii thread_handle(::CreateRemoteThread(hProcess.Get(), nullptr, 0, (LPTHREAD_START_ROUTINE)load_lib_addr,
                                                  dll_addr.Get(), 0, nullptr));
    if (thread_handle.IsInvalid())
    {
        ThrowLastError("CreateRemoteThread");
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);

    DWORD exit_code = 0;
    if (!GetExitCodeThread(thread_handle.Get(), &exit_code))
    {
        ThrowLastError("GetExitCodeThread");
    }
    if (exit_code == 0)
    {
        throw std::runtime_error("远程进程加载模块失败");
    }
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
        ThrowLastError("OpenProcess");
    }

    HMODULE dll_handle = FindRemoteModuleHandle(hProcess.Get(), dll_path);
    if (dll_handle == nullptr)
    {
        ThrowLastError("FindRemoteModuleHandle");
    }

    FARPROC free_lib_addr = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary");
    HandleRaii thread_handle(
        CreateRemoteThread(hProcess.Get(), nullptr, 0, (LPTHREAD_START_ROUTINE)free_lib_addr, dll_handle, 0, nullptr));

    if (thread_handle.IsInvalid())
    {
        ThrowLastError("CreateRemoteThread");
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}

void win32::CallRemoteExport(DWORD pid, LPCTSTR dll_path, const char *export_name)
{
    HMODULE local_module = LoadLibraryEx(dll_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (local_module == nullptr)
    {
        ThrowLastError("LoadLibraryEx");
    }

    FARPROC local_fn = GetProcAddress(local_module, export_name);
    if (local_fn == nullptr)
    {
        const DWORD err = ::GetLastError();
        FreeLibrary(local_module);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "GetProcAddress(%s) failed, error code: %lu", export_name, err);
        throw std::runtime_error(buf);
    }

    HandleRaii hProcess(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if (hProcess.Get() == nullptr)
    {
        FreeLibrary(local_module);
        ThrowLastError("OpenProcess");
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
        ThrowLastError("CreateRemoteThread");
    }

    WaitForSingleObject(thread_handle.Get(), INFINITE);
}
