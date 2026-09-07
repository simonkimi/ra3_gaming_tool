#include <Windows.h>
#include <string>

import overlay;
import hub;
import win32;

namespace
{

HANDLE instance_mutex_ = nullptr;
HANDLE exit_event_ = nullptr;
HANDLE overlay_thread_ = nullptr;

DWORD WINAPI OverlayThread(LPVOID)
{
    win32::DebugLog(L"overlay: thread start, pid=%lu tid=%lu", GetCurrentProcessId(), GetCurrentThreadId());
    overlay::dx9::StartHook();

    hub::ClientInfo info;
    if (hub::DiscoverClient(info))
    {
        win32::DebugLog(L"overlay: hub client port=%u toggle_vk=0x%02X toggle_mods=0x%02X", info.port,
                        info.toggle_vk, info.toggle_mods);
        if (info.toggle_vk != 0)
        {
            overlay::input::SetToggleKey(info.toggle_vk, info.toggle_mods);
        }
    }
    else
    {
        win32::DebugLog(L"overlay: hub client not found");
    }

    if (exit_event_ != nullptr)
    {
        win32::DebugLog(L"overlay: waiting for exit event");
        WaitForSingleObject(exit_event_, INFINITE);
    }
    overlay::dx9::EndHook();
    win32::DebugLog(L"overlay: thread exit");
    return 0;
}

}

extern "C" __declspec(dllexport) void StartOverlay()
{
    win32::DebugLog(L"overlay: StartOverlay");
    overlay::dx9::StartHook();
}

extern "C" __declspec(dllexport) void StopOverlay()
{
    win32::DebugLog(L"overlay: StopOverlay");
    overlay::dx9::EndHook();
}

extern "C" __declspec(dllexport) DWORD WINAPI OverlayUnload(LPVOID)
{
    win32::DebugLog(L"overlay: OverlayUnload");
    if (exit_event_ != nullptr)
    {
        SetEvent(exit_event_);
    }
    if (overlay_thread_ != nullptr)
    {
        const DWORD wait = WaitForSingleObject(overlay_thread_, 5000);
        if (wait != WAIT_OBJECT_0)
        {
            win32::DebugLog(L"overlay: OverlayUnload wait result=%lu", wait);
        }
        CloseHandle(overlay_thread_);
        overlay_thread_ = nullptr;
    }
    overlay::dx9::EndHook();
    win32::DebugLog(L"overlay: OverlayUnload done");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h_instance, DWORD fdw_reason, LPVOID lpv_reserved)
{
    switch (fdw_reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(h_instance);
        win32::DebugLog(L"overlay: DLL_PROCESS_ATTACH pid=%lu instance=%p", GetCurrentProcessId(), h_instance);
        const std::wstring mutex_name = win32::InjectionMutexName(GetCurrentProcessId());
        SetLastError(ERROR_SUCCESS);
        instance_mutex_ = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
        if (instance_mutex_ == nullptr)
        {
            win32::DebugLog(L"overlay: CreateMutexW failed, error=%lu", GetLastError());
            return FALSE;
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            win32::DebugLog(L"overlay: already injected, refusing attach");
            CloseHandle(instance_mutex_);
            instance_mutex_ = nullptr;
            return FALSE;
        }
        exit_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (exit_event_ == nullptr)
        {
            win32::DebugLog(L"overlay: CreateEventW failed, error=%lu", GetLastError());
        }
        overlay_thread_ = CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr);
        if (overlay_thread_ == nullptr)
        {
            win32::DebugLog(L"overlay: CreateThread failed, error=%lu", GetLastError());
            return FALSE;
        }
        win32::DebugLog(L"overlay: overlay thread created");
        break;
    }
    case DLL_PROCESS_DETACH:
        win32::DebugLog(L"overlay: DLL_PROCESS_DETACH reserved=%p", lpv_reserved);
        if (lpv_reserved != nullptr)
        {
            break;
        }
        if (exit_event_ != nullptr)
        {
            SetEvent(exit_event_);
        }
        overlay::dx9::EndHook();
        if (instance_mutex_ != nullptr)
        {
            CloseHandle(instance_mutex_);
            instance_mutex_ = nullptr;
        }
        win32::DebugLog(L"overlay: DLL_PROCESS_DETACH done");
        break;
    default:
        break;
    }
    return TRUE;
}
