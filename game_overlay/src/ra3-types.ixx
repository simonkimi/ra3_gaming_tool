module;

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

export module ra3:types;

import :address;

export namespace ra3
{

template <typename T>
struct BasicRa3String
{
    T *data = nullptr;

    std::size_t length() const noexcept;
    std::basic_string_view<T> view() const noexcept;
};
using Ra3String = BasicRa3String<char>;
using Ra3UnicodeString = BasicRa3String<wchar_t>;

enum class GameSlotType : std::uint32_t
{
    open,
    closed,
    easy,
    medium,
    hard,
    brutal,
    human
};

struct GameSlot
{
    void *vtable;
    GameSlotType type;
    std::byte unknown[0x28];
    Ra3UnicodeString name;
    Ra3String script_name;

    bool is_observer();
};
static_assert(offsetof(GameSlot, type) == 0x4);
static_assert(offsetof(GameSlot, name) == 0x30);
static_assert(offsetof(GameSlot, script_name) == 0x34);

enum class GameType : std::uint32_t
{
    campaign,
    skirmish,
    lan,
    online,
    automatch,
};

struct GameInfo
{
    void *vtable;
    GameSlot *slots[6];
    std::byte unknown_1[0x18];
    Ra3String map_path;
    std::byte unknown_2[0x24];
    GameType game_type;
    std::byte unknown_3[0x38];
    void *unknown_is_replay_related;

    static GameInfo *current();
};
static_assert(offsetof(GameInfo, map_path.data) == 0x34);
static_assert(offsetof(GameInfo, game_type) == 0x5C);
static_assert(offsetof(GameInfo, unknown_is_replay_related) == 0x98);

const char *ToString(GameType type) noexcept;
const char *ToString(GameSlotType type) noexcept;

}

template <typename T>
std::size_t ra3::BasicRa3String<T>::length() const noexcept
{
    if (data == nullptr)
    {
        return {};
    }
    std::uint16_t length = 0;
    std::memcpy(&length, reinterpret_cast<std::byte const *>(data) - 4, sizeof(length));
    return length;
}

template <typename T>
std::basic_string_view<T> ra3::BasicRa3String<T>::view() const noexcept
{
    if (data == nullptr)
    {
        return {};
    }
    return {data, length()};
}

bool ra3::GameSlot::is_observer()
{
    using IsObserver = BOOL __fastcall(void *ecx);
    auto fn = GetAddress<IsObserver *>(0x62C8D0u, 0x66B7C0u);
    if (fn == nullptr)
    {
        return false;
    }
    return fn(this) != FALSE;
}

ra3::GameInfo *ra3::GameInfo::current()
{
    auto pointer = GetAddress<GameInfo * volatile *>(0xCE3A74u, 0xCE8C04u);
    if (pointer == nullptr)
    {
        return nullptr;
    }
    return *pointer;
}

const char *ra3::ToString(GameType type) noexcept
{
    switch (type)
    {
    case GameType::campaign:
        return "campaign";
    case GameType::skirmish:
        return "skirmish";
    case GameType::lan:
        return "lan";
    case GameType::online:
        return "online";
    case GameType::automatch:
        return "automatch";
    default:
        return "unknown";
    }
}

const char *ra3::ToString(GameSlotType type) noexcept
{
    switch (type)
    {
    case GameSlotType::open:
        return "open";
    case GameSlotType::closed:
        return "closed";
    case GameSlotType::easy:
        return "easy";
    case GameSlotType::medium:
        return "medium";
    case GameSlotType::hard:
        return "hard";
    case GameSlotType::brutal:
        return "brutal";
    case GameSlotType::human:
        return "human";
    default:
        return "unknown";
    }
}
