module;

#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "imgui_wndproc.h"

export module overlay:input;

import :ui;

export namespace overlay::input
{

void HookWndProc(HWND hwnd);
void UnhookWndProc();

}

namespace overlay::input
{
namespace
{

WNDPROC original_wnd_proc_ = nullptr;
HWND hwnd_ = nullptr;

bool IsMouseMessage(UINT msg)
{
    return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
}

bool IsKeyboardMessage(UINT msg)
{
    return msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYUP && wParam == VK_F7)
    {
        overlay::ui::ToggleDisplay();
        return 0;
    }

    if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE)
    {
        overlay::ui::OnWindowResize();
    }

    const bool display = overlay::ui::IsDisplay();
    if (display && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        return 0;
    }

    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = display && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (display)
    {
        if ((IsMouseMessage(msg) && io.WantCaptureMouse) || (IsKeyboardMessage(msg) && io.WantCaptureKeyboard))
        {
            return 0;
        }
    }

    return CallWindowProc(original_wnd_proc_, hWnd, msg, wParam, lParam);
}

}

void HookWndProc(HWND hwnd)
{
    if (original_wnd_proc_ != nullptr || hwnd == nullptr)
    {
        return;
    }
    hwnd_ = hwnd;
    original_wnd_proc_ =
        reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
}

void UnhookWndProc()
{
    if (original_wnd_proc_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc_));
    original_wnd_proc_ = nullptr;
    hwnd_ = nullptr;
}

}
