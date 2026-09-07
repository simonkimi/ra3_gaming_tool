module;

#include <Windows.h>
#include <d3d9.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef MAP_LOOKUP_DELAY_MS
#define MAP_LOOKUP_DELAY_MS 1000
#endif

export module overlay:ui;

import ra3;
import hub;
import win32;

export namespace overlay::ui
{

void DrawFrame(IDirect3DDevice9 *device);
void ApplyStyle();
void OnWindowResize();
void OnDeviceLost();
void OnDeviceReset(IDirect3DDevice9 *device);
void Shutdown();
bool IsDisplay();
void ToggleDisplay();

}

namespace
{

constexpr ULONGLONG kRetryOfflineMs = 10000;
constexpr float kThumbnailMaxSide = 260.0f;
constexpr ImVec4 kTextMuted = {0.55f, 0.58f, 0.62f, 1.0f};
constexpr ImVec4 kTextTitle = {0.96f, 0.78f, 0.28f, 1.0f};
constexpr ImU32 kChipBg = IM_COL32(232, 163, 23, 36);
constexpr ImU32 kChipBorder = IM_COL32(232, 163, 23, 90);
constexpr ImU32 kChipText = IM_COL32(255, 224, 150, 255);

void ApplyModernStyle()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);

    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(10.0f, 8.0f);
    style.ScrollbarSize = 14.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    style.WindowMinSize = ImVec2(400.0f, 280.0f);

    ImVec4 *c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
    c[ImGuiCol_TextDisabled] = kTextMuted;
    c[ImGuiCol_WindowBg] = ImVec4(0.086f, 0.094f, 0.110f, 0.96f);
    c[ImGuiCol_ChildBg] = ImVec4(0.118f, 0.129f, 0.149f, 0.45f);
    c[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.094f, 0.110f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.157f, 0.180f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.060f, 0.072f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.055f, 0.060f, 0.072f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.055f, 0.060f, 0.072f, 0.85f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.086f, 0.094f, 0.110f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.086f, 0.094f, 0.110f, 0.35f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.42f, 0.46f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.91f, 0.64f, 0.09f, 0.80f);
    c[ImGuiCol_CheckMark] = kTextTitle;
    c[ImGuiCol_SliderGrab] = kTextTitle;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.82f, 0.32f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.91f, 0.64f, 0.09f, 0.35f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.91f, 0.64f, 0.09f, 0.55f);
    c[ImGuiCol_Header] = ImVec4(0.91f, 0.64f, 0.09f, 0.18f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.91f, 0.64f, 0.09f, 0.32f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.91f, 0.64f, 0.09f, 0.45f);
    c[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.91f, 0.64f, 0.09f, 0.45f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.91f, 0.64f, 0.09f, 0.70f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.64f, 0.09f, 0.22f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.91f, 0.64f, 0.09f, 0.50f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.91f, 0.64f, 0.09f, 0.80f);
    c[ImGuiCol_Tab] = ImVec4(0.145f, 0.157f, 0.180f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.91f, 0.64f, 0.09f, 0.35f);
    c[ImGuiCol_TabSelected] = ImVec4(0.91f, 0.64f, 0.09f, 0.28f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.145f, 0.157f, 0.180f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(1.00f, 1.00f, 1.00f, 0.08f);
    c[ImGuiCol_TableBorderLight] = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.028f);
}

void DrawMuted(const char *text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void DrawStatus(const char *text)
{
    ImGui::Dummy(ImVec2(0.0f, 28.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextMuted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void DrawChip(const char *text)
{
    const ImVec2 pad{12.0f, 5.0f};
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 size{text_size.x + pad.x * 2.0f, text_size.y + pad.y * 2.0f};
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kChipBg, 12.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), kChipBorder, 12.0f);
    draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), kChipText, text);
    ImGui::Dummy(size);
}

void DrawChips(const std::vector<std::string> &tags)
{
    if (tags.empty())
    {
        return;
    }

    const float wrap = ImGui::GetContentRegionAvail().x;
    float line_x = 0.0f;
    for (std::size_t i = 0; i < tags.size(); ++i)
    {
        const ImVec2 text_size = ImGui::CalcTextSize(tags[i].c_str());
        const float chip_w = text_size.x + 24.0f;
        if (line_x > 0.0f && line_x + chip_w > wrap)
        {
            line_x = 0.0f;
        }
        else if (line_x > 0.0f)
        {
            ImGui::SameLine();
        }
        DrawChip(tags[i].c_str());
        line_x += chip_w + ImGui::GetStyle().ItemSpacing.x;
    }
}

void DrawMetaLine(const char *label, const char *value)
{
    DrawMuted(label);
    ImGui::SameLine(108.0f);
    ImGui::TextUnformatted(value);
}

bool is_display_ = true;
ImVec2 window_size_ = {0, 0};
ImVec2 window_pos_ = {0, 0};
bool need_set_pos_ = false;

std::mutex lookup_mutex_;
std::atomic<std::uint64_t> request_id_{0};
std::atomic<int> workers_{0};
std::atomic<bool> shutting_down_{false};

std::string watched_path_;
std::string queried_path_;
ULONGLONG last_change_tick_ = 0;
ULONGLONG last_fail_tick_ = 0;
bool in_flight_ = false;
hub::LookupResult pending_result_;
std::uint64_t pending_generation_ = 0;

hub::LookupResult shown_result_;
std::uint64_t shown_generation_ = 0;
IDirect3DTexture9 *thumbnail_ = nullptr;
bool texture_dirty_ = false;

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

bool TryNarrowToUtf8(std::string_view text, UINT code_page, std::string &out)
{
    out.clear();
    if (text.empty())
    {
        return true;
    }
    int wide_size = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text.data(),
                                        static_cast<int>(text.size()), nullptr, 0);
    if (wide_size <= 0)
    {
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
    if (MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                            wide.data(), wide_size) != wide_size)
    {
        return false;
    }
    out = ToUtf8(wide);
    return true;
}

std::string MapPathToUtf8(std::string_view text)
{
    std::string out;
    if (TryNarrowToUtf8(text, CP_UTF8, out))
    {
        return out;
    }
    if (TryNarrowToUtf8(text, CP_ACP, out))
    {
        return out;
    }
    constexpr UINT kGb18030 = 54936;
    if (TryNarrowToUtf8(text, kGb18030, out))
    {
        return out;
    }
    return std::string{text};
}

const char *PlayerTypeLabel(std::string_view type)
{
    if (type == "player")
    {
        return "人类玩家";
    }
    if (type == "npc")
    {
        return "电脑玩家";
    }
    return "不限角色";
}

const char *PlotTypeLabel(std::string_view type)
{
    if (type == "sovietUnion")
    {
        return "苏联";
    }
    if (type == "allies")
    {
        return "盟军";
    }
    if (type == "empireOfTheRisingSun")
    {
        return "帝国";
    }
    if (type == "celestial")
    {
        return "神州";
    }
    if (type == "other")
    {
        return "其他";
    }
    return "不限阵营";
}

const char *TeamLabel(std::string_view type)
{
    if (type == "team1")
    {
        return "队伍 1";
    }
    if (type == "team2")
    {
        return "队伍 2";
    }
    if (type == "team3")
    {
        return "队伍 3";
    }
    if (type == "team4")
    {
        return "队伍 4";
    }
    return "不限队伍";
}

const char *DifficultyLabel(std::string_view type)
{
    if (type == "wheelchair")
    {
        return "EZ";
    }
    if (type == "easy")
    {
        return "简单";
    }
    if (type == "medium")
    {
        return "适中";
    }
    if (type == "hard")
    {
        return "困难";
    }
    if (type == "hell")
    {
        return "地狱";
    }
    if (type == "notHuman")
    {
        return "能过？";
    }
    return nullptr;
}

bool IsPveMap(const std::vector<std::string> &tags)
{
    static constexpr std::string_view kPve[] = {"PVE", "进攻", "防守", "流线", "闯关", "战役"};
    static constexpr std::string_view kPvp[] = {"PVP", "三国杀", "小块地"};
    for (const auto &tag : tags)
    {
        for (const auto pve : kPve)
        {
            if (tag == pve)
            {
                return true;
            }
        }
    }
    for (const auto &tag : tags)
    {
        for (const auto pvp : kPvp)
        {
            if (tag == pvp)
            {
                return false;
            }
        }
    }
    return true;
}

void FormatStarRating(float api_rating, char *out, std::size_t out_size)
{
    const float stars = std::round(api_rating / 2.0f * 10.0f) / 10.0f;
    const int tenths = static_cast<int>(std::lround(stars * 10.0f));
    if (tenths % 10 == 0)
    {
        std::snprintf(out, out_size, "%d 星", tenths / 10);
    }
    else
    {
        std::snprintf(out, out_size, "%d.%d 星", tenths / 10, std::abs(tenths % 10));
    }
}

void ReleaseThumbnail()
{
    if (thumbnail_ != nullptr)
    {
        thumbnail_->Release();
        thumbnail_ = nullptr;
    }
}

void UploadThumbnail(IDirect3DDevice9 *device)
{
    ReleaseThumbnail();
    texture_dirty_ = false;
    if (device == nullptr || shown_result_.image.bgra.empty() || shown_result_.image.width <= 0 ||
        shown_result_.image.height <= 0)
    {
        return;
    }

    const UINT width = static_cast<UINT>(shown_result_.image.width);
    const UINT height = static_cast<UINT>(shown_result_.image.height);
    if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &thumbnail_,
                                     nullptr)))
    {
        win32::DebugLog(L"ui: CreateTexture failed %ux%u", width, height);
        return;
    }

    D3DLOCKED_RECT rect = {};
    if (FAILED(thumbnail_->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD)))
    {
        win32::DebugLog(L"ui: LockRect failed %ux%u", width, height);
        ReleaseThumbnail();
        return;
    }

    const std::uint8_t *src = shown_result_.image.bgra.data();
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
    for (UINT y = 0; y < height; ++y)
    {
        std::memcpy(static_cast<std::uint8_t *>(rect.pBits) + static_cast<std::size_t>(y) * rect.Pitch,
                    src + static_cast<std::size_t>(y) * row_bytes, row_bytes);
    }
    thumbnail_->UnlockRect(0);
}

void StartLookup(std::uint64_t id, std::string path)
{
    workers_.fetch_add(1, std::memory_order_acq_rel);
    win32::DebugLogUtf8("ui: lookup start id=%llu path=%s", static_cast<unsigned long long>(id), path.c_str());
    std::thread([id, path = std::move(path)]() mutable {
        hub::LookupResult result;
        try
        {
            result = hub::LookupMap(path);
        }
        catch (...)
        {
            win32::DebugLog(L"ui: lookup threw, id=%llu", static_cast<unsigned long long>(id));
            result.status = hub::LookupStatus::http_error;
        }
        win32::DebugLogUtf8("ui: lookup done id=%llu status=%d http=%d name=%s", static_cast<unsigned long long>(id),
                            static_cast<int>(result.status), result.http_status, result.display_name.c_str());
        {
            std::lock_guard lock(lookup_mutex_);
            if (!shutting_down_.load(std::memory_order_acquire) &&
                id == request_id_.load(std::memory_order_acquire))
            {
                pending_result_ = std::move(result);
                ++pending_generation_;
                in_flight_ = false;
                if (pending_result_.status == hub::LookupStatus::client_offline ||
                    pending_result_.status == hub::LookupStatus::http_error)
                {
                    last_fail_tick_ = GetTickCount64();
                }
            }
        }
        workers_.fetch_sub(1, std::memory_order_acq_rel);
    }).detach();
}

void UpdateLookup()
{
    std::string path;
    ra3::GameInfo *game_info = ra3::GameInfo::current();
    if (game_info != nullptr)
    {
        path = MapPathToUtf8(game_info->map_path.view());
    }

    const ULONGLONG now = GetTickCount64();
    if (path != watched_path_)
    {
        win32::DebugLogUtf8("ui: map path changed: %s", path.empty() ? "(empty)" : path.c_str());
        watched_path_ = path;
        last_change_tick_ = now;
        queried_path_.clear();
        std::lock_guard lock(lookup_mutex_);
        request_id_.fetch_add(1, std::memory_order_acq_rel);
        in_flight_ = false;
        pending_result_ = {};
        ++pending_generation_;
    }

    if (path.empty())
    {
        return;
    }

    bool should_query = false;
    std::uint64_t id = 0;
    {
        std::lock_guard lock(lookup_mutex_);
        if (in_flight_)
        {
            return;
        }
        if (queried_path_ != path)
        {
            if (now - last_change_tick_ >= MAP_LOOKUP_DELAY_MS)
            {
                should_query = true;
            }
        }
        else if (pending_result_.status == hub::LookupStatus::client_offline ||
                 pending_result_.status == hub::LookupStatus::http_error)
        {
            if (now - last_fail_tick_ >= kRetryOfflineMs)
            {
                should_query = true;
            }
        }
        if (should_query)
        {
            queried_path_ = path;
            in_flight_ = true;
            id = request_id_.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
    }

    if (should_query)
    {
        StartLookup(id, path);
    }
}

void SyncShownResult(IDirect3DDevice9 *device)
{
    bool updated = false;
    {
        std::lock_guard lock(lookup_mutex_);
        if (shown_generation_ != pending_generation_)
        {
            shown_result_ = pending_result_;
            shown_generation_ = pending_generation_;
            texture_dirty_ = true;
            updated = true;
        }
    }
    if (updated || (texture_dirty_ && thumbnail_ == nullptr))
    {
        UploadThumbnail(device);
    }
}

void DrawThumbnail()
{
    if (thumbnail_ == nullptr || shown_result_.image.width <= 0 || shown_result_.image.height <= 0)
    {
        return;
    }

    const float width = static_cast<float>(shown_result_.image.width);
    const float height = static_cast<float>(shown_result_.image.height);
    const float scale = (std::max)(width, height) > kThumbnailMaxSide ? kThumbnailMaxSide / (std::max)(width, height)
                                                                       : 1.0f;
    const ImVec2 size{width * scale, height * scale};
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImTextureRef tex(reinterpret_cast<void *>(thumbnail_));
    draw->AddImageRounded(tex, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                          IM_COL32_WHITE, 10.0f);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 28), 10.0f);
    ImGui::Dummy(size);
}

void DrawDescription()
{
    if (shown_result_.description.empty())
    {
        DrawMuted("暂无简介");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.80f, 0.83f, 1.0f));
    ImGui::TextWrapped("%s", shown_result_.description.c_str());
    ImGui::PopStyleColor();
}

void DrawLocations()
{
    if (shown_result_.locations.empty())
    {
        DrawMuted("暂无出生点信息");
        return;
    }

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("map_locations", 4, flags))
    {
        return;
    }
    ImGui::TableSetupColumn("位置", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("玩家");
    ImGui::TableSetupColumn("阵营");
    ImGui::TableSetupColumn("队伍");
    ImGui::TableHeadersRow();
    for (const hub::MapLocation &loc : shown_result_.locations)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%d", loc.location);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(PlayerTypeLabel(loc.player_type));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(PlotTypeLabel(loc.plot_type));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(TeamLabel(loc.team));
    }
    ImGui::EndTable();
}

std::string FormatCommentTime(std::string_view iso)
{
    if (iso.size() < 16)
    {
        return std::string(iso);
    }
    std::string out(iso.substr(0, 16));
    if (out[10] == 'T')
    {
        out[10] = ' ';
    }
    return out;
}

void DrawCommentNode(const std::vector<hub::MapComment> &comments,
                     const std::vector<std::vector<std::size_t>> &children, std::size_t index, int depth)
{
    const hub::MapComment &comment = comments[index];
    ImGui::PushID(comment.id);
    const float indent = static_cast<float>(depth) * 18.0f;
    if (indent > 0.0f)
    {
        ImGui::Indent(indent);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, kTextTitle);
    ImGui::TextUnformatted(comment.nick_name.empty() ? "匿名" : comment.nick_name.c_str());
    ImGui::PopStyleColor();
    if (!comment.created_at.empty())
    {
        ImGui::SameLine(0.0f, 10.0f);
        DrawMuted(FormatCommentTime(comment.created_at).c_str());
    }

    if (!comment.content.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, comment.is_collapsed ? kTextMuted : ImVec4(0.78f, 0.80f, 0.83f, 1.0f));
        ImGui::TextWrapped("%s", comment.content.c_str());
        ImGui::PopStyleColor();
    }

    if (depth < 8)
    {
        for (std::size_t child : children[index])
        {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            DrawCommentNode(comments, children, child, depth + 1);
        }
    }

    if (indent > 0.0f)
    {
        ImGui::Unindent(indent);
    }
    ImGui::PopID();
}

void DrawComments()
{
    switch (shown_result_.comments_status)
    {
    case hub::CommentsStatus::idle:
        DrawMuted("等待加载评论…");
        return;
    case hub::CommentsStatus::failed:
        DrawMuted("评论加载失败");
        return;
    case hub::CommentsStatus::loaded:
        break;
    }

    if (shown_result_.comments.empty())
    {
        DrawMuted("暂无评论");
        return;
    }

    const std::vector<hub::MapComment> &comments = shown_result_.comments;
    std::unordered_map<int, std::size_t> id_index;
    id_index.reserve(comments.size());
    for (std::size_t i = 0; i < comments.size(); ++i)
    {
        if (comments[i].id != 0)
        {
            id_index.emplace(comments[i].id, i);
        }
    }

    std::vector<std::vector<std::size_t>> children(comments.size());
    std::vector<std::size_t> roots;
    roots.reserve(comments.size());
    for (std::size_t i = 0; i < comments.size(); ++i)
    {
        const auto parent = id_index.find(comments[i].reply_to_id);
        if (comments[i].reply_to_id != 0 && parent != id_index.end() && parent->second != i)
        {
            children[parent->second].push_back(i);
        }
        else
        {
            roots.push_back(i);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    for (std::size_t i = 0; i < roots.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        DrawCommentNode(comments, children, roots[i], 0);
    }
    ImGui::PopStyleVar();
}

void DrawDetailTabs()
{
    ImGui::Spacing();
    if (!ImGui::BeginTabBar("map_detail_tabs"))
    {
        return;
    }
    if (ImGui::BeginTabItem("简介"))
    {
        ImGui::BeginChild("tab_desc");
        DrawDescription();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("出生点"))
    {
        ImGui::BeginChild("tab_locs");
        DrawLocations();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (shown_result_.allow_comments)
    {
        char comments_tab[48];
        if (shown_result_.comments_status == hub::CommentsStatus::loaded)
        {
            std::snprintf(comments_tab, sizeof(comments_tab), "评论 (%zu)###comments",
                          shown_result_.comments.size());
        }
        else
        {
            std::snprintf(comments_tab, sizeof(comments_tab), "评论###comments");
        }
        if (ImGui::BeginTabItem(comments_tab))
        {
            ImGui::BeginChild("tab_comments");
            DrawComments();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

void DrawMapLookup()
{
    bool loading = false;
    {
        std::lock_guard lock(lookup_mutex_);
        loading = in_flight_;
    }

    if (watched_path_.empty())
    {
        DrawStatus("请进入一个在线游戏房间...");
        return;
    }

    if (loading || queried_path_ != watched_path_)
    {
        DrawStatus(loading ? "正在查询地图…" : "等待查询…");
        return;
    }

    switch (shown_result_.status)
    {
    case hub::LookupStatus::idle:
        DrawStatus("等待查询…");
        return;
    case hub::LookupStatus::client_offline:
        DrawStatus("无法连接至Ra3战区中枢, 请保证Ra3战区中枢正在运行");
        return;
    case hub::LookupStatus::http_error:
        DrawStatus("查询失败");
        return;
    case hub::LookupStatus::not_found:
        DrawStatus("此地图未上传至Ra3战区中枢");
        return;
    case hub::LookupStatus::found:
        break;
    }

    DrawThumbnail();
    if (thumbnail_ != nullptr)
    {
        ImGui::SameLine(0.0f, 18.0f);
    }

    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTitle);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(shown_result_.display_name.empty() ? "未命名地图" : shown_result_.display_name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Spacing();
    DrawMetaLine("上传者", shown_result_.nick_name.empty() ? "-" : shown_result_.nick_name.c_str());
    char players[32];
    std::snprintf(players, sizeof(players), "%d 人", shown_result_.player_count);
    DrawMetaLine("人数", players);
    if (shown_result_.allow_rating)
    {
        if (shown_result_.has_rating)
        {
            char rating[32];
            FormatStarRating(shown_result_.rating, rating, sizeof(rating));
            DrawMetaLine("评分", rating);
        }
        else
        {
            DrawMetaLine("评分", "评分不足");
        }
    }
    if (shown_result_.allow_difficulty_vote && IsPveMap(shown_result_.tags))
    {
        const char *difficulty = DifficultyLabel(shown_result_.difficulty);
        DrawMetaLine("难度", difficulty != nullptr ? difficulty : "票数不足");
    }
    ImGui::Spacing();
    DrawChips(shown_result_.tags);
    if (IsPveMap(shown_result_.tags))
    {
        if (const char *author_diff = DifficultyLabel(shown_result_.author_difficulty); author_diff != nullptr)
        {
            char badge[48];
            std::snprintf(badge, sizeof(badge), "难度 · %s", author_diff);
            if (!shown_result_.tags.empty())
            {
                ImGui::SameLine();
            }
            DrawChip(badge);
        }
    }
    ImGui::EndGroup();

    DrawDetailTabs();
}

}

void overlay::ui::ApplyStyle()
{
    ApplyModernStyle();
}

void overlay::ui::DrawFrame(IDirect3DDevice9 *device)
{
    if (!is_display_)
    {
        return;
    }

    UpdateLookup();
    SyncShownResult(device);

    unsigned vk = hub::AdvertisedToggleVk();
    if (vk == 0)
    {
        vk = static_cast<unsigned>('I');
    }
    const std::string key = hub::VirtualKeyName(vk);
    char title[96];
    std::snprintf(title, sizeof(title), "Ra3战区中枢 (按%s显示/隐藏 控件)###map_info", key.c_str());
    ImGui::Begin(title, &is_display_, ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowSize({760, 640}, ImGuiCond_Once);
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

    DrawMapLookup();
    ImGui::End();
}

void overlay::ui::OnWindowResize()
{
    need_set_pos_ = true;
}

void overlay::ui::OnDeviceLost()
{
    ReleaseThumbnail();
    texture_dirty_ = true;
}

void overlay::ui::OnDeviceReset(IDirect3DDevice9 *device)
{
    UploadThumbnail(device);
}

void overlay::ui::Shutdown()
{
    win32::DebugLog(L"ui: shutdown begin, workers=%d", workers_.load(std::memory_order_acquire));
    shutting_down_.store(true, std::memory_order_release);
    request_id_.fetch_add(1, std::memory_order_acq_rel);
    for (int i = 0; i < 2000 && workers_.load(std::memory_order_acquire) != 0; ++i)
    {
        Sleep(10);
    }
    const int leftover = workers_.load(std::memory_order_acquire);
    if (leftover != 0)
    {
        win32::DebugLog(L"ui: shutdown still waiting, workers=%d", leftover);
    }
    ReleaseThumbnail();
    shown_result_ = {};
    pending_result_ = {};
    shutting_down_.store(false, std::memory_order_release);
    win32::DebugLog(L"ui: shutdown done");
}

bool overlay::ui::IsDisplay()
{
    return is_display_;
}

void overlay::ui::ToggleDisplay()
{
    is_display_ = !is_display_;
    win32::DebugLog(L"ui: display %s", is_display_ ? L"on" : L"off");
}
