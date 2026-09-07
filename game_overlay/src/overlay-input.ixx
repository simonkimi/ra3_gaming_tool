module;

#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include "imgui_wndproc.h"

#include <atomic>

export module overlay:input;

import :ui;
import win32;

export namespace overlay::input
{

void HookWndProc(HWND hwnd);
void UnhookWndProc();
void SetToggleKey(unsigned vk, unsigned mods = 0);
unsigned ToggleKey();

}

namespace overlay::input
{
namespace
{

WNDPROC original_wnd_proc_ = nullptr;
HWND hwnd_ = nullptr;
std::atomic<unsigned> toggle_vk_{static_cast<unsigned>('I')};
std::atomic<unsigned> toggle_mods_{0};

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

bool KeyIsDown(int vk)
{
    return (GetKeyState(vk) & 0x8000) != 0;
}

bool ToggleHotkeyMatch(WPARAM wParam)
{
    if (wParam != toggle_vk_.load(std::memory_order_acquire))
    {
        return false;
    }
    const unsigned mods = toggle_mods_.load(std::memory_order_acquire);
    const bool need_ctrl = (mods & MOD_CONTROL) != 0;
    const bool need_shift = (mods & MOD_SHIFT) != 0;
    const bool need_alt = (mods & MOD_ALT) != 0;
    return KeyIsDown(VK_CONTROL) == need_ctrl && KeyIsDown(VK_SHIFT) == need_shift &&
           KeyIsDown(VK_MENU) == need_alt;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE)
    {
        overlay::ui::OnWindowResize();
    }

    // Alt+Tab / Alt+Esc / Alt+Space must reach the game, or exclusive fullscreen cannot yield focus.
    if (IsSystemHotkeyMessage(msg, wParam))
    {
        return CallWindowProc(original_wnd_proc_, hWnd, msg, wParam, lParam);
    }

    if ((msg == WM_KEYUP || msg == WM_SYSKEYUP) && ToggleHotkeyMatch(wParam))
    {
        overlay::ui::ToggleDisplay();
        return 0;
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
    if (hwnd == nullptr)
    {
        win32::DebugLog(L"input: HookWndProc hwnd is null");
        return;
    }
    if (original_wnd_proc_ != nullptr)
    {
        win32::DebugLog(L"input: HookWndProc skipped, already hooked hwnd=%p", hwnd_);
        return;
    }
    hwnd_ = hwnd;
    original_wnd_proc_ =
        reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
    if (original_wnd_proc_ == nullptr)
    {
        win32::DebugLog(L"input: SetWindowLongPtr failed, error=%lu hwnd=%p", GetLastError(), hwnd_);
        hwnd_ = nullptr;
        return;
    }
    win32::DebugLog(L"input: wndproc hooked hwnd=%p original=%p", hwnd_, original_wnd_proc_);
}

void UnhookWndProc()
{
    if (original_wnd_proc_ == nullptr || hwnd_ == nullptr)
    {
        return;
    }
    win32::DebugLog(L"input: restoring wndproc hwnd=%p", hwnd_);
    SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wnd_proc_));
    original_wnd_proc_ = nullptr;
    hwnd_ = nullptr;
}

void SetToggleKey(unsigned vk, unsigned mods)
{
    if (vk == 0 || vk > 0xFE)
    {
        win32::DebugLog(L"input: SetToggleKey ignored vk=0x%02X mods=0x%02X", vk, mods);
        return;
    }
    toggle_vk_.store(vk, std::memory_order_release);
    toggle_mods_.store(mods & (MOD_ALT | MOD_CONTROL | MOD_SHIFT), std::memory_order_release);
    win32::DebugLog(L"input: toggle key set to vk=0x%02X mods=0x%02X", vk, mods);
}

unsigned ToggleKey()
{
    return toggle_vk_.load(std::memory_order_acquire);
}

}
