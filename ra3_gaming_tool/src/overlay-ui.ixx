module;

#include <Windows.h>
#include <d3d9.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef MAP_LOOKUP_DELAY_MS
#define MAP_LOOKUP_DELAY_MS 0
#endif

export module overlay:ui;

import ra3;
import hub;

export namespace overlay::ui
{

void DrawFrame(IDirect3DDevice9 *device);
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
constexpr float kThumbnailMaxSide = 360.0f;

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
        return;
    }

    D3DLOCKED_RECT rect = {};
    if (FAILED(thumbnail_->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD)))
    {
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
    std::thread([id, path = std::move(path)]() mutable {
        hub::LookupResult result;
        try
        {
            result = hub::LookupMap(path);
        }
        catch (...)
        {
            result.status = hub::LookupStatus::http_error;
        }
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
        path = ToUtf8(game_info->map_path.view(), CP_ACP);
    }

    const ULONGLONG now = GetTickCount64();
    if (path != watched_path_)
    {
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
    ImGui::Image(ImTextureRef(reinterpret_cast<void *>(thumbnail_)), size);
}

void DrawLocations()
{
    if (shown_result_.locations.empty())
    {
        return;
    }

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("map_locations", 4, flags))
    {
        return;
    }
    ImGui::TableSetupColumn("位置", ImGuiTableColumnFlags_WidthFixed, 72.0f);
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

void DrawMapLookup()
{
    bool loading = false;
    {
        std::lock_guard lock(lookup_mutex_);
        loading = in_flight_;
    }

    if (watched_path_.empty())
    {
        ImGui::TextUnformatted("无地图路径");
        return;
    }

    if (loading || queried_path_ != watched_path_)
    {
        ImGui::TextUnformatted(loading ? "查询中..." : "等待查询...");
        return;
    }

    switch (shown_result_.status)
    {
    case hub::LookupStatus::idle:
        ImGui::TextUnformatted("等待查询...");
        return;
    case hub::LookupStatus::client_offline:
        ImGui::TextUnformatted("客户端未启动");
        return;
    case hub::LookupStatus::http_error:
        ImGui::Text("查询失败 (%d)", shown_result_.http_status);
        return;
    case hub::LookupStatus::not_found:
        ImGui::TextUnformatted("未找到该地图");
        return;
    case hub::LookupStatus::found:
        break;
    }

    DrawThumbnail();
    if (thumbnail_ != nullptr)
    {
        ImGui::SameLine();
    }

    ImGui::BeginGroup();
    ImGui::TextWrapped("%s", shown_result_.display_name.empty() ? "(无名称)" : shown_result_.display_name.c_str());
    ImGui::Text("上传者: %s", shown_result_.nick_name.empty() ? "-" : shown_result_.nick_name.c_str());
    ImGui::Text("玩家数量: %d", shown_result_.player_count);
    if (!shown_result_.tags.empty())
    {
        ImGui::TextUnformatted("标签:");
        ImGui::SameLine();
        std::string tags;
        for (std::size_t i = 0; i < shown_result_.tags.size(); ++i)
        {
            if (i != 0)
            {
                tags += ", ";
            }
            tags += shown_result_.tags[i];
        }
        ImGui::TextUnformatted(tags.c_str());
    }
    ImGui::EndGroup();

    if (!shown_result_.description.empty())
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("描述");
        ImGui::TextWrapped("%s", shown_result_.description.c_str());
    }

    ImGui::Spacing();
    DrawLocations();
}

}

void overlay::ui::DrawFrame(IDirect3DDevice9 *device)
{
    if (!is_display_)
    {
        return;
    }

    UpdateLookup();
    SyncShownResult(device);

    ImGui::Begin("地图信息", &is_display_);
    ImGui::SetWindowSize({760, 560}, ImGuiCond_Once);
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
    shutting_down_.store(true, std::memory_order_release);
    request_id_.fetch_add(1, std::memory_order_acq_rel);
    for (int i = 0; i < 2000 && workers_.load(std::memory_order_acquire) != 0; ++i)
    {
        Sleep(10);
    }
    ReleaseThumbnail();
    shown_result_ = {};
    pending_result_ = {};
    shutting_down_.store(false, std::memory_order_release);
}

bool overlay::ui::IsDisplay()
{
    return is_display_;
}

void overlay::ui::ToggleDisplay()
{
    is_display_ = !is_display_;
}
