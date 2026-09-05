#include <Windows.h>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>

import win32;

namespace
{

void PrintUsage()
{
    fwprintf(stderr, L"usage: game_injector.exe <inject|unload> <pid>\n");
}

std::wstring DllPathNextToExe()
{
    wchar_t exe_path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0)
    {
        throw std::runtime_error("GetModuleFileNameW failed");
    }

    std::wstring path(exe_path);
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        throw std::runtime_error("invalid exe path");
    }
    return path.substr(0, slash + 1) + L"game_overlay.dll";
}

DWORD ParsePid(wchar_t *arg)
{
    wchar_t *end = nullptr;
    const unsigned long value = std::wcstoul(arg, &end, 10);
    if (end == arg || *end != L'\0' || value == 0)
    {
        return 0;
    }
    return static_cast<DWORD>(value);
}

}

int wmain(int argc, wchar_t *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    try
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }

        const std::wstring command(argv[1]);
        const DWORD pid = ParsePid(argv[2]);
        if (pid == 0)
        {
            PrintUsage();
            fwprintf(stderr, L"invalid pid\n");
            return 1;
        }

        const std::wstring dll_path = DllPathNextToExe();
        if (GetFileAttributesW(dll_path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            fwprintf(stderr, L"dll not found: %s\n", dll_path.c_str());
            return 1;
        }

        if (command == L"inject")
        {
            win32::CrtInjectDll(pid, dll_path.c_str());
            fwprintf(stdout, L"injected into pid %lu\n", pid);
            return 0;
        }

        if (command == L"unload")
        {
            win32::CallRemoteExport(pid, dll_path.c_str(), "OverlayUnload");
            win32::CrtFreeDll(pid, dll_path.c_str());
            fwprintf(stdout, L"unhooked and unloaded dll from pid %lu\n", pid);
            return 0;
        }

        PrintUsage();
        return 1;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
