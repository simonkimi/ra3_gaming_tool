module;

#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "imgui_wndproc.h"

#include <atomic>

export module overlay:input;

import :ui;

export namespace overlay::input
{

void HookWndProc(HWND hwnd);
void UnhookWndProc();
void SetToggleKey(unsigned vk);
unsigned ToggleKey();

}

namespace overlay::input
{
namespace
{

WNDPROC original_wnd_proc_ = nullptr;
HWND hwnd_ = nullptr;
std::atomic<unsigned> toggle_vk_{static_cast<unsigned>('I')};

bool IsMouseMessage(UINT msg)
{
    return msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
}

bool IsKeyboardMessage(UINT msg)
{
    return msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
}

bool IsSystemHotkeyMessage(UINT msg, WPARAM wParam)
{
    if (msg == WM_SYSCOMMAND)
    {
        return true;
    }
    if (msg != WM_SYSKEYDOWN && msg != WM_SYSKEYUP)
    {
        return false;
    }
    return wParam == VK_TAB || wParam == VK_ESCAPE || wParam == VK_SPACE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYUP && wParam == toggle_vk_.load(std::memory_order_acquire))
    {
        overlay::ui::ToggleDisplay();
        return 0;
    }

    if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE)
    {
        overlay::ui::OnWindowResize();
    }

    // Alt+Tab / Alt+Esc / Alt+Space must reach the game, or exclusive fullscreen cannot yield focus.
    if (IsSystemHotkeyMessage(msg, wParam))
    {
        return CallWindowProc(original_wnd_proc_, hWnd, msg, wParam, lParam);
    }

    const bool display = overlay::ui::IsDisplay();
    if (display)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }

    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = display && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    if (display)
    {
        if ((IsMouseMessage(msg) && io.WantCaptureMouse) ||
            (IsKeyboardMessage(msg) && io.WantTextInput))
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

void SetToggleKey(unsigned vk)
{
    if (vk == 0 || vk > 0xFE)
    {
        return;
    }
    toggle_vk_.store(vk, std::memory_order_release);
}

unsigned ToggleKey()
{
    return toggle_vk_.load(std::memory_order_acquire);
}

}
