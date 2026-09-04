#include "pch.h"
#include "imgui_ui.h"
#include <imgui.h>

namespace overlay::ui
{
namespace
{

bool is_display_ = true;
ImVec2 window_size_ = {0, 0};
ImVec2 window_pos_ = {0, 0};
bool need_set_pos_ = false;

}

void DrawFrame()
{
    if (!is_display_)
    {
        return;
    }

    ImGui::Begin("Hello, world!", &is_display_);
    ImGui::SetWindowSize({500, 300}, ImGuiCond_Once);
    if (need_set_pos_ && window_pos_.x != 0.0f && window_pos_.y != 0.0f && window_size_.x != 0.0f &&
        window_size_.y != 0.0f)
    {
        ImGui::SetWindowPos(window_pos_, ImGuiCond_Always);
        ImGui::SetWindowSize(window_size_, ImGuiCond_Always);
        need_set_pos_ = false;
    }
    else
    {
        window_pos_ = ImGui::GetWindowPos();
        window_size_ = ImGui::GetWindowSize();
    }
    ImGui::End();
}

void OnWindowResize()
{
    need_set_pos_ = true;
}

bool IsDisplay()
{
    return is_display_;
}

void ToggleDisplay()
{
    is_display_ = !is_display_;
}

}
