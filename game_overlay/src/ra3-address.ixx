module;

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

export module ra3:address;

import win32;

export namespace ra3
{

enum class GameVersion
{
    unknown = 0,
    retail_1_12,
    ea_app_1_12,
};

GameVersion GetVersion() noexcept;
const char *VersionName() noexcept;

template <typename T = std::byte *>
    requires std::is_pointer_v<T>
T GetAddress(std::uintptr_t retail, std::uintptr_t ea_app) noexcept
{
    switch (GetVersion())
    {
    case GameVersion::retail_1_12:
        return reinterpret_cast<T>(retail);
    case GameVersion::ea_app_1_12:
        return reinterpret_cast<T>(ea_app);
    default:
        return nullptr;
    }
}

}

namespace
{

bool TryCompare(std::uintptr_t address, std::string_view expected) noexcept
{
    auto buffer = reinterpret_cast<char const *>(address);
    auto buffer_end = buffer + expected.size();
    auto p = buffer;
    do
    {
        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(p, &information, sizeof(information)) == 0)
        {
            return false;
        }
        constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((information.Protect & readable) == 0)
        {
            return false;
        }
        if ((information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }
        auto page_begin = static_cast<char const *>(information.BaseAddress);
        auto page_end = page_begin + information.RegionSize;
        if (p < page_begin || page_end <= p)
        {
            return false;
        }
        p = page_end;
    } while (p < buffer_end);
    return std::string_view{buffer, expected.size()} == expected;
}

}

ra3::GameVersion ra3::GetVersion() noexcept
{
    static std::atomic<GameVersion> game_version{GameVersion::unknown};
    auto value = game_version.load(std::memory_order_acquire);
    if (value != GameVersion::unknown)
    {
        return value;
    }

    constexpr auto retail_path =
        R"(C:\BF_RA3_QABUILD_EALA-BUILD20\RA3\code\SageEngine\source\GameEngine\GameLogic/Object/Contain/OpenContain.h:144)";
    constexpr auto ea_app_path =
        R"(E:\Projects\Ra3\Production\code\SageEngine\source\GameEngine\GameLogic/Object/Contain/OpenContain.h:144)";

    if (TryCompare(0xC462B8u, retail_path))
    {
        value = GameVersion::retail_1_12;
    }
    else if (TryCompare(0xC4D3A8u, ea_app_path))
    {
        value = GameVersion::ea_app_1_12;
    }

    GameVersion expected = GameVersion::unknown;
    if (game_version.compare_exchange_strong(expected, value, std::memory_order_acq_rel))
    {
        if (value != GameVersion::unknown)
        {
            win32::DebugLogUtf8("ra3: detected game version %s", VersionName());
        }
        return value;
    }
    return expected;
}

const char *ra3::VersionName() noexcept
{
    switch (GetVersion())
    {
    case GameVersion::retail_1_12:
        return "Retail 1.12";
    case GameVersion::ea_app_1_12:
        return "EA App / Steam 1.12";
    default:
        return "Unknown";
    }
}
