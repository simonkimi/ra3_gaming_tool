module;

#include <imgui.h>

export module overlay:ui;

export namespace overlay::ui
{

void DrawFrame();
void OnWindowResize();
bool IsDisplay();
void ToggleDisplay();

}

namespace
{

bool is_display_ = true;
ImVec2 window_size_ = {0, 0};
ImVec2 window_pos_ = {0, 0};
bool need_set_pos_ = false;

}

void overlay::ui::DrawFrame()
{
    if (!is_display_)
    {
        return;
    }

    ImGui::Begin("Hello, world!", &is_display_);
    ImGui::TextUnformatted("F7: show/hide  F8: hook/unhook");
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

void overlay::ui::OnWindowResize()
{
    need_set_pos_ = true;
}

bool overlay::ui::IsDisplay()
{
    return is_display_;
}

void overlay::ui::ToggleDisplay()
{
    is_display_ = !is_display_;
}
