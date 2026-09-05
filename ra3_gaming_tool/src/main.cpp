#include <Windows.h>

import overlay;
import hub;

namespace
{

HANDLE exit_event_ = nullptr;
HANDLE overlay_thread_ = nullptr;

DWORD WINAPI OverlayThread(LPVOID)
{
    overlay::dx9::StartHook();

    hub::ClientInfo info;
    if (hub::DiscoverClient(info) && info.toggle_vk != 0)
    {
        overlay::input::SetToggleKey(info.toggle_vk);
    }

    if (exit_event_ != nullptr)
    {
        WaitForSingleObject(exit_event_, INFINITE);
    }
    overlay::dx9::EndHook();
    return 0;
}

}

extern "C" __declspec(dllexport) void StartOverlay()
{
    overlay::dx9::StartHook();
}

extern "C" __declspec(dllexport) void StopOverlay()
{
    overlay::dx9::EndHook();
}

extern "C" __declspec(dllexport) DWORD WINAPI OverlayUnload(LPVOID)
{
    if (exit_event_ != nullptr)
    {
        SetEvent(exit_event_);
    }
    if (overlay_thread_ != nullptr)
    {
        WaitForSingleObject(overlay_thread_, 5000);
        CloseHandle(overlay_thread_);
        overlay_thread_ = nullptr;
    }
    overlay::dx9::EndHook();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h_instance, DWORD fdw_reason, LPVOID lpv_reserved)
{
    switch (fdw_reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(h_instance);
        exit_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        overlay_thread_ = CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        if (lpv_reserved != nullptr)
        {
            break;
        }
        if (exit_event_ != nullptr)
        {
            SetEvent(exit_event_);
        }
        overlay::dx9::EndHook();
        break;
    default:
        break;
    }
    return TRUE;
}
