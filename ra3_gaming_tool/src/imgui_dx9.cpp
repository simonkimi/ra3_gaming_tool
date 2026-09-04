#include "pch.h"
#include "imgui_dx9.h"
#include "imgui_input.h"
#include "imgui_ui.h"
#include "utils.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx9.h>
#include <detours/detours.h>

namespace overlay::dx9
{
namespace
{

using FuncEndScene = HRESULT(APIENTRY *)(IDirect3DDevice9 *device);
using FuncReset = HRESULT(APIENTRY *)(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp);

bool hooked_ = false;
bool imgui_ready_ = false;
FuncEndScene vfun_end_scene_ = nullptr;
FuncReset vfun_reset_ = nullptr;

bool GetDx9VTable(void **v_table, size_t size)
{
    HWND hwnd = GetDesktopWindow();
    Microsoft::WRL::ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    if (!d3d)
    {
        return false;
    }

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hwnd;
    d3dpp.Windowed = TRUE;

    Microsoft::WRL::ComPtr<IDirect3DDevice9> device;
    HRESULT hresult = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                        &d3dpp, device.GetAddressOf());
    if (FAILED(hresult))
    {
        DxTrace(hresult);
        d3dpp.Windowed = FALSE;
        hresult = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                    &d3dpp, device.GetAddressOf());
    }
    if (FAILED(hresult))
    {
        DxTrace(hresult, true);
        return false;
    }

    memcpy(v_table, *reinterpret_cast<void ***>(device.Get()), size);
    return true;
}

void InitImgui(IDirect3DDevice9 *device)
{
    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (FAILED(device->GetCreationParameters(&params)))
    {
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
        return;
    }

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    if (io.Fonts->AddFontFromFileTTF(R"(c:\Windows\Fonts\msyh.ttc)", 18.0f, nullptr,
                                     io.Fonts->GetGlyphRangesChineseFull()) == nullptr)
    {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(device);
    overlay::input::HookWndProc(hwnd);
    imgui_ready_ = true;
}

void ShutdownImgui()
{
    if (!imgui_ready_)
    {
        return;
    }
    overlay::input::UnhookWndProc();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    imgui_ready_ = false;
}

HRESULT APIENTRY HookEndScene(IDirect3DDevice9 *device)
{
    if (!imgui_ready_)
    {
        InitImgui(device);
    }

    if (imgui_ready_)
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        overlay::ui::DrawFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    return vfun_end_scene_(device);
}

HRESULT APIENTRY HookReset(IDirect3DDevice9 *device, D3DPRESENT_PARAMETERS *pp)
{
    if (imgui_ready_)
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }

    const HRESULT hr = vfun_reset_(device, pp);
    if (imgui_ready_ && SUCCEEDED(hr))
    {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

}

void StartHook()
{
    if (hooked_)
    {
        return;
    }

    void *v_table[119];
    if (!GetDx9VTable(v_table, sizeof(v_table)))
    {
        OutputDebugStringW(L"GetDx9VTable failed");
        return;
    }

    vfun_end_scene_ = reinterpret_cast<FuncEndScene>(v_table[42]);
    vfun_reset_ = reinterpret_cast<FuncReset>(v_table[16]);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID &)vfun_end_scene_, HookEndScene);
    DetourAttach(&(PVOID &)vfun_reset_, HookReset);
    if (DetourTransactionCommit() == NO_ERROR)
    {
        hooked_ = true;
    }
}

void EndHook()
{
    ShutdownImgui();
    if (!hooked_)
    {
        return;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID &)vfun_end_scene_, HookEndScene);
    DetourDetach(&(PVOID &)vfun_reset_, HookReset);
    if (DetourTransactionCommit() == NO_ERROR)
    {
        hooked_ = false;
    }
}

}
