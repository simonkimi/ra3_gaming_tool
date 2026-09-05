module;

#include <Windows.h>
#include <imgui.h>
#include <string>
#include <string_view>

export module overlay:ui;

import ra3;

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

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string ToUtf8(std::string_view text, UINT code_page)
{
    if (text.empty())
    {
        return {};
    }
    int wide_size = MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (wide_size <= 0)
    {
        return std::string{text};
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()), wide.data(), wide_size);
    return ToUtf8(wide);
}

const char *DisplayOrEmpty(const std::string &text)
{
    return text.empty() ? "(empty)" : text.c_str();
}

void DrawSlots(ra3::GameInfo *game_info)
{
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("slots", 5, flags))
    {
        return;
    }

    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Script");
    ImGui::TableSetupColumn("Observer");
    ImGui::TableHeadersRow();

    for (int i = 0; i < 6; ++i)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", i);

        ra3::GameSlot *slot = game_info->slots[i];
        if (slot == nullptr)
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("(null)");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("-");
            continue;
        }

        ImGui::TableNextColumn();
        ImGui::Text("%s (%u)", ra3::ToString(slot->type), static_cast<unsigned>(slot->type));

        const std::string name = ToUtf8(slot->name.view());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(DisplayOrEmpty(name));

        const std::string script = ToUtf8(slot->script_name.view(), CP_ACP);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(DisplayOrEmpty(script));

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(slot->is_observer() ? "yes" : "no");
    }

    ImGui::EndTable();
}

void DrawGameInfo()
{
    ImGui::Text("Version: %s", ra3::VersionName());

    ra3::GameInfo *game_info = ra3::GameInfo::current();
    if (game_info == nullptr)
    {
        ImGui::TextUnformatted("GameInfo: null");
        return;
    }

    ImGui::Text("Pointer: %p", static_cast<void *>(game_info));
    ImGui::Text("Game Type: %s (%u)", ra3::ToString(game_info->game_type),
                static_cast<unsigned>(game_info->game_type));
    ImGui::Text("Replay related: %p%s", game_info->unknown_is_replay_related,
                game_info->unknown_is_replay_related == nullptr ? " (likely replay)" : "");

    const std::string map_path = ToUtf8(game_info->map_path.view(), CP_ACP);
    ImGui::TextWrapped("Map: %s", DisplayOrEmpty(map_path));

    ImGui::SeparatorText("Slots");
    DrawSlots(game_info);
}

}

void overlay::ui::DrawFrame()
{
    if (!is_display_)
    {
        return;
    }

    ImGui::Begin("GameInfo", &is_display_);
    ImGui::TextUnformatted("F7: show/hide  F8: hook/unhook");
    ImGui::SetWindowSize({560, 420}, ImGuiCond_Once);
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

    ImGui::Separator();
    DrawGameInfo();
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
