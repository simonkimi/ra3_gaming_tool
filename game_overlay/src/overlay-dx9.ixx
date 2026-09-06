module;

#include <Windows.h>
#include <atomic>
#include <d3d9.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx9.h>
#include <detours/detours.h>

export module overlay:dx9;

import :input;
import :ui;
import :utils;
import hub;
import win32;

export namespace overlay::dx9
{

void StartHook();
void EndHook();
bool IsHooked();

}

namespace overlay::dx9
{
namespace
{

using FuncEndScene = HRESULT(APIENTRY *)(IDirect3DDevice9 *device);
using FuncReset = HRESULT(APIENTRY *)(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp);

CRITICAL_SECTION cs_;
bool cs_ready_ = false;
bool hooked_ = false;
bool imgui_ready_ = false;
std::atomic<bool> unhook_requested_{false};
std::atomic<long> in_hook_{0};
FuncEndScene original_end_scene_ = nullptr;
FuncReset original_reset_ = nullptr;
FuncEndScene vfun_end_scene_ = nullptr;
FuncReset vfun_reset_ = nullptr;

void EnsureLock()
{
    if (cs_ready_)
    {
        return;
    }
    InitializeCriticalSection(&cs_);
    cs_ready_ = true;
}

struct AutoLock
{
    AutoLock()
    {
        EnsureLock();
        EnterCriticalSection(&cs_);
    }
    ~AutoLock()
    {
        LeaveCriticalSection(&cs_);
    }
};

struct InHookGuard
{
    InHookGuard()
    {
        in_hook_.fetch_add(1, std::memory_order_acq_rel);
    }
    ~InHookGuard()
    {
        in_hook_.fetch_sub(1, std::memory_order_acq_rel);
    }
};

bool GetDx9VTable(void **v_table, size_t size)
{
    HWND hwnd = GetDesktopWindow();
    Microsoft::WRL::ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    if (!d3d)
    {
        win32::DebugLog(L"dx9: Direct3DCreate9 failed");
        return false;
    }

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hwnd;
    d3dpp.Windowed = TRUE;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
    HRESULT hresult = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hwnd,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, device.GetAddressOf());
    if (FAILED(hresult))
    {
        DxTrace(hresult, true);
        return false;
    }

    memcpy(v_table, *reinterpret_cast<void ***>(device.Get()), size);
    return true;
}

bool CaptureOriginals()
{
    if (original_end_scene_ != nullptr && original_reset_ != nullptr)
    {
        return true;
    }

    void *v_table[119];
    if (!GetDx9VTable(v_table, sizeof(v_table)))
    {
        win32::DebugLog(L"dx9: GetDx9VTable failed");
        return false;
    }

    original_end_scene_ = reinterpret_cast<FuncEndScene>(v_table[42]);
    original_reset_ = reinterpret_cast<FuncReset>(v_table[16]);
    if (original_end_scene_ == nullptr || original_reset_ == nullptr)
    {
        win32::DebugLog(L"dx9: vtable missing EndScene=%p Reset=%p", original_end_scene_, original_reset_);
        return false;
    }
    win32::DebugLog(L"dx9: captured EndScene=%p Reset=%p", original_end_scene_, original_reset_);
    return true;
}

void InitImgui(IDirect3DDevice9 *device)
{
    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (FAILED(device->GetCreationParameters(&params)))
    {
        win32::DebugLog(L"dx9: GetCreationParameters failed");
        return;
    }

    HWND hwnd = params.hFocusWindow;
    if (hwnd == nullptr)
    {
        Microsoft::WRL::ComPtr<IDirect3DSwapChain9> swap_chain;
        D3DPRESENT_PARAMETERS pp = {};
        if (SUCCEEDED(device->GetSwapChain(0, swap_chain.GetAddressOf())) &&
            SUCCEEDED(swap_chain->GetPresentParameters(&pp)))
        {
            hwnd = pp.hDeviceWindow;
        }
    }
    if (hwnd == nullptr)
    {
        win32::DebugLog(L"dx9: InitImgui hwnd is null");
        return;
    }

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (io.Fonts->AddFontFromFileTTF(R"(c:\Windows\Fonts\msyh.ttc)", 24.0f, nullptr,
                                     io.Fonts->GetGlyphRangesChineseFull()) == nullptr)
    {
        win32::DebugLog(L"dx9: msyh.ttc not loaded, using default font");
        io.Fonts->AddFontDefault();
    }
    overlay::ui::ApplyStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(device);
    overlay::input::HookWndProc(hwnd);
    imgui_ready_ = true;
    win32::DebugLog(L"dx9: imgui ready, hwnd=%p device=%p", hwnd, device);
}

void ShutdownImgui()
{
    if (!imgui_ready_)
    {
        return;
    }
    win32::DebugLog(L"dx9: shutting down imgui");
    overlay::input::UnhookWndProc();
    overlay::ui::Shutdown();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    imgui_ready_ = false;
}

void WaitForHooksIdle()
{
    for (int i = 0; i < 100 && in_hook_.load(std::memory_order_acquire) != 0; ++i)
    {
        Sleep(10);
    }
    const long remaining = in_hook_.load(std::memory_order_acquire);
    if (remaining != 0)
    {
        win32::DebugLog(L"dx9: still in hook after wait, in_hook=%ld", remaining);
    }
}

HRESULT APIENTRY HookEndScene(IDirect3DDevice9 *device)
{
    InHookGuard in_hook;
    if (!unhook_requested_.load(std::memory_order_acquire))
    {
        if (!imgui_ready_)
        {
            InitImgui(device);
        }
        if (imgui_ready_)
        {
            const unsigned vk = hub::AdvertisedToggleVk();
            static unsigned applied_vk = 0;
            if (vk != 0 && vk != applied_vk)
            {
                win32::DebugLog(L"dx9: applying advertised toggle vk=0x%02X", vk);
                overlay::input::SetToggleKey(vk);
                applied_vk = vk;
            }
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            overlay::ui::DrawFrame(device);
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
    }
    return vfun_end_scene_(device);
}

HRESULT APIENTRY HookReset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp)
{
    InHookGuard in_hook;
    if (!unhook_requested_.load(std::memory_order_acquire) && imgui_ready_)
    {
        overlay::ui::OnDeviceLost();
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    const HRESULT hr = vfun_reset_(device, pp);
    win32::DebugLog(L"dx9: Reset hr=0x%08X", static_cast<unsigned>(hr));
    if (!unhook_requested_.load(std::memory_order_acquire) && imgui_ready_ && SUCCEEDED(hr))
    {
        ImGui_ImplDX9_CreateDeviceObjects();
        overlay::ui::OnDeviceReset(device);
        win32::DebugLog(L"dx9: device objects recreated");
    }
    return hr;
}

}

void StartHook()
{
    AutoLock lock;
    if (hooked_)
    {
        win32::DebugLog(L"dx9: StartHook skipped, already hooked");
        return;
    }
    win32::DebugLog(L"dx9: StartHook begin");
    if (!CaptureOriginals())
    {
        return;
    }

    vfun_end_scene_ = original_end_scene_;
    vfun_reset_ = original_reset_;
    unhook_requested_.store(false, std::memory_order_release);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)vfun_end_scene_, HookEndScene);
    DetourAttach(&(PVOID &)vfun_reset_, HookReset);
    const LONG err = DetourTransactionCommit();
    if (err == NO_ERROR)
    {
        hooked_ = true;
        win32::DebugLog(L"dx9: hook attached");
    }
    else
    {
        win32::DebugLog(L"dx9: DetourTransactionCommit attach failed, error=%ld", err);
    }
}

void EndHook()
{
    AutoLock lock;
    if (!hooked_)
    {
        win32::DebugLog(L"dx9: EndHook skipped, not hooked");
        return;
    }

    win32::DebugLog(L"dx9: EndHook begin");
    unhook_requested_.store(true, std::memory_order_release);
    WaitForHooksIdle();
    ShutdownImgui();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)vfun_end_scene_, HookEndScene);
    DetourDetach(&(PVOID &)vfun_reset_, HookReset);
    const LONG err = DetourTransactionCommit();
    if (err == NO_ERROR)
    {
        hooked_ = false;
        vfun_end_scene_ = original_end_scene_;
        vfun_reset_ = original_reset_;
        win32::DebugLog(L"dx9: hook detached");
    }
    else
    {
        unhook_requested_.store(false, std::memory_order_release);
        win32::DebugLog(L"dx9: DetourTransactionCommit detach failed, error=%ld", err);
    }
}

bool IsHooked()
{
    AutoLock lock;
    return hooked_;
}

}
