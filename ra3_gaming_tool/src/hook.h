#pragma once

#include "pch.h"
#include "d3d9.h"
#include <imgui.h>
#include "gui.h"

BOOL GetDx9VTable(HWND hwnd, void **v_table, int size);

using FuncEndScene = HRESULT(APIENTRY *)(LPDIRECT3DDEVICE9 pDevice);
using FuncReset = HRESULT(APIENTRY *)(D3DPRESENT_PARAMETERS *pPresentationParameters);

class ImguiD39Impl : public ImguiCore
{
private:
    static IDirect3DDevice9 *d3d_device_;
    static FuncEndScene vfun_end_scene_;
    static FuncReset vfun_reset_;

public:
    static void InitImgui();
    static void StartHook();
    static void EndHook();

private:
    static HRESULT HookEndScene(LPDIRECT3DDEVICE9 pDevice);

    static HRESULT APIENTRY HookReset(D3DPRESENT_PARAMETERS *pPresentationParameters);
};