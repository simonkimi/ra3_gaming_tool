#include "process_helper.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

DWORD g_pid = 0;
std::wstring g_dll_path;
std::atomic<bool> g_unloaded{false};
std::mutex g_unload_mutex;
HANDLE g_user_exit = nullptr;

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
    return path.substr(0, slash + 1) + L"ra3_gaming_tool.dll";
}

DWORD ResolvePid(int argc, wchar_t *argv[])
{
    if (argc >= 2)
    {
        return static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10));
    }
    return win32::FindProcessId(L"ra3_1.12.game");
}

bool GameStillRunning()
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, g_pid);
    if (process == nullptr)
    {
        return false;
    }
    const DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}

void UnloadOnce()
{
    std::lock_guard<std::mutex> lock(g_unload_mutex);
    if (g_unloaded.exchange(true))
    {
        return;
    }
    if (!GameStillRunning())
    {
        std::wcout << L"game already exited, skip unload\n";
        return;
    }

    try
    {
        win32::CallRemoteExport(g_pid, g_dll_path.c_str(), "OverlayUnload");
        win32::CrtFreeDll(g_pid, g_dll_path.c_str());
        std::wcout << L"unhooked and unloaded dll\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "unload failed: " << e.what() << '\n';
    }
}

BOOL WINAPI ConsoleHandler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
    {
        UnloadOnce();
        if (g_user_exit != nullptr)
        {
            SetEvent(g_user_exit);
        }
        return TRUE;
    }
    return FALSE;
}

}

int wmain(int argc, wchar_t *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    try
    {
        g_pid = ResolvePid(argc, argv);
        if (g_pid == 0)
        {
            std::wcerr << L"usage: hook_helper.exe [pid]\n";
            std::wcerr << L"RA3 process not found\n";
            return 1;
        }

        g_dll_path = DllPathNextToExe();
        if (GetFileAttributesW(g_dll_path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            std::wcerr << L"dll not found: " << g_dll_path << L'\n';
            return 1;
        }

        win32::CrtInjectDll(g_pid, g_dll_path.c_str());
        std::wcout << L"injected into pid " << g_pid << L'\n';
        std::wcout << L"close this window or press Enter to unload\n";

        g_user_exit = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);

        std::thread([] {
            std::wstring line;
            std::getline(std::wcin, line);
            if (g_user_exit != nullptr)
            {
                SetEvent(g_user_exit);
            }
        }).detach();

        HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, g_pid);
        HANDLE waits[2] = {g_user_exit, game};
        const DWORD count = game != nullptr ? 2 : 1;
        WaitForMultipleObjects(count, waits, FALSE, INFINITE);
        if (game != nullptr)
        {
            CloseHandle(game);
        }

        UnloadOnce();
        if (g_user_exit != nullptr)
        {
            CloseHandle(g_user_exit);
            g_user_exit = nullptr;
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
