module;

#include <Windows.h>
#include <wincodec.h>
#include <winhttp.h>
#include <webp/decode.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

export module hub;

import win32;

export namespace hub
{

enum class LookupStatus
{
    idle,
    not_found,
    client_offline,
    http_error,
    found,
};

struct MapLocation
{
    int location = 0;
    std::string player_type;
    std::string plot_type;
    std::string team;
};

struct ImagePixels
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
};

struct LookupResult
{
    LookupStatus status = LookupStatus::idle;
    int http_status = 0;
    std::string display_name;
    std::string description;
    std::string nick_name;
    int player_count = 0;
    std::vector<std::string> tags;
    std::vector<MapLocation> locations;
    ImagePixels image;
};

struct ClientInfo
{
    unsigned short port = 0;
    unsigned toggle_vk = 0;
};

LookupResult LookupMap(std::string_view map_path);
bool DiscoverClient(ClientInfo &out);
unsigned AdvertisedToggleVk() noexcept;
std::string VirtualKeyName(unsigned vk);

}

namespace
{

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ra3-battlezone-hub";
constexpr DWORD kPipeWaitMs = 1500;
constexpr DWORD kPipeReadMs = 2000;
constexpr int kHttpTimeoutMs = 15000;
constexpr std::uint64_t kMaxImageBytes = 10ull * 1024ull * 1024ull;

std::atomic<unsigned> advertised_toggle_vk_{0};

std::string NormalizedKey(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text)
    {
        if (c == ' ' || c == '\t' || c == '_' || c == '-')
        {
            continue;
        }
        out.push_back(static_cast<char>(std::toupper(c)));
    }
    if (out.size() > 2 && out[0] == 'V' && out[1] == 'K')
    {
        out.erase(0, 2);
    }
    return out;
}

unsigned ParseVirtualKeyImpl(std::string_view text)
{
    const std::string key = NormalizedKey(text);
    if (key.empty())
    {
        return 0;
    }

    if (key.size() > 2 && key[0] == '0' && key[1] == 'X')
    {
        char *end = nullptr;
        const unsigned long value = std::strtoul(key.c_str() + 2, &end, 16);
        if (end != nullptr && end != key.c_str() + 2 && *end == '\0' && value > 0 && value <= 0xFE)
        {
            return static_cast<unsigned>(value);
        }
        return 0;
    }

    bool digits = true;
    for (char c : key)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            digits = false;
            break;
        }
    }
    if (digits)
    {
        const unsigned long value = std::strtoul(key.c_str(), nullptr, 10);
        if (value > 0 && value <= 0xFE)
        {
            return static_cast<unsigned>(value);
        }
        return 0;
    }

    if (key.size() == 1)
    {
        const char c = key[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            return static_cast<unsigned>(c);
        }
        return 0;
    }

    if (key[0] == 'F' && key.size() >= 2 && key.size() <= 3)
    {
        int n = 0;
        for (std::size_t i = 1; i < key.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(key[i])))
            {
                n = 0;
                break;
            }
            n = n * 10 + (key[i] - '0');
        }
        if (n >= 1 && n <= 24)
        {
            return static_cast<unsigned>(VK_F1 + n - 1);
        }
    }

    if (key.size() == 7 && key.compare(0, 6, "NUMPAD") == 0 && key[6] >= '0' && key[6] <= '9')
    {
        return static_cast<unsigned>(VK_NUMPAD0 + (key[6] - '0'));
    }

    struct NamedKey
    {
        const char *name;
        unsigned vk;
    };
    static constexpr NamedKey kNamed[] = {
        {"INSERT", VK_INSERT},     {"INS", VK_INSERT},      {"DELETE", VK_DELETE}, {"DEL", VK_DELETE},
        {"HOME", VK_HOME},         {"END", VK_END},         {"PRIOR", VK_PRIOR},   {"PAGEUP", VK_PRIOR},
        {"PGUP", VK_PRIOR},        {"NEXT", VK_NEXT},       {"PAGEDOWN", VK_NEXT}, {"PGDN", VK_NEXT},
        {"LEFT", VK_LEFT},         {"RIGHT", VK_RIGHT},     {"UP", VK_UP},         {"DOWN", VK_DOWN},
        {"SPACE", VK_SPACE},       {"SPACEBAR", VK_SPACE},  {"TAB", VK_TAB},       {"ESCAPE", VK_ESCAPE},
        {"ESC", VK_ESCAPE},        {"BACKSPACE", VK_BACK},  {"BACK", VK_BACK},     {"PAUSE", VK_PAUSE},
        {"CAPITAL", VK_CAPITAL},   {"CAPS", VK_CAPITAL},    {"CAPSLOCK", VK_CAPITAL}, {"NUMLOCK", VK_NUMLOCK},
        {"SCROLL", VK_SCROLL},     {"PRINT", VK_SNAPSHOT},  {"PRINTSCREEN", VK_SNAPSHOT}, {"SNAPSHOT", VK_SNAPSHOT},
        {"MULTIPLY", VK_MULTIPLY}, {"ADD", VK_ADD},         {"SUBTRACT", VK_SUBTRACT}, {"DECIMAL", VK_DECIMAL},
        {"DIVIDE", VK_DIVIDE},     {"RETURN", VK_RETURN},   {"ENTER", VK_RETURN},
    };
    for (const auto &item : kNamed)
    {
        if (key == item.name)
        {
            return item.vk;
        }
    }
    return 0;
}

unsigned ParseVkJson(const nlohmann::json &value)
{
    if (value.is_number_integer())
    {
        const int n = value.get<int>();
        if (n > 0 && n <= 0xFE)
        {
            return static_cast<unsigned>(n);
        }
        return 0;
    }
    if (value.is_string())
    {
        return ParseVirtualKeyImpl(value.get<std::string>());
    }
    return 0;
}

unsigned ParseToggleVk(const nlohmann::json &obj)
{
    if (obj.contains("toggleVk"))
    {
        const unsigned vk = ParseVkJson(obj["toggleVk"]);
        if (vk != 0)
        {
            return vk;
        }
    }
    if (obj.contains("toggleKey"))
    {
        return ParseVkJson(obj["toggleKey"]);
    }
    return 0;
}

struct WinHttpHandle
{
    HINTERNET handle = nullptr;

    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : handle(h)
    {
    }
    ~WinHttpHandle()
    {
        if (handle != nullptr)
        {
            WinHttpCloseHandle(handle);
        }
    }
    WinHttpHandle(const WinHttpHandle &) = delete;
    WinHttpHandle &operator=(const WinHttpHandle &) = delete;
    WinHttpHandle(WinHttpHandle &&other) noexcept : handle(other.handle)
    {
        other.handle = nullptr;
    }
    WinHttpHandle &operator=(WinHttpHandle &&other) noexcept
    {
        if (this != &other)
        {
            if (handle != nullptr)
            {
                WinHttpCloseHandle(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
};

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(text[text.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }
    return true;
}

std::string StripHtml(std::string_view html)
{
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (std::size_t i = 0; i < html.size(); ++i)
    {
        const char c = html[i];
        if (!in_tag && c == '<' && i + 3 < html.size())
        {
            const char n1 = html[i + 1];
            const char n2 = html[i + 2];
            if ((n1 == 'b' || n1 == 'B') && (n2 == 'r' || n2 == 'R'))
            {
                out.push_back('\n');
            }
        }
        if (c == '<')
        {
            in_tag = true;
            continue;
        }
        if (c == '>')
        {
            in_tag = false;
            continue;
        }
        if (in_tag)
        {
            continue;
        }
        if (c == '&')
        {
            if (html.substr(i, 5) == "&amp;")
            {
                out.push_back('&');
                i += 4;
                continue;
            }
            if (html.substr(i, 4) == "&lt;")
            {
                out.push_back('<');
                i += 3;
                continue;
            }
            if (html.substr(i, 4) == "&gt;")
            {
                out.push_back('>');
                i += 3;
                continue;
            }
            if (html.substr(i, 6) == "&nbsp;")
            {
                out.push_back(' ');
                i += 5;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

bool DiscoverPort(unsigned short &port, unsigned &toggle_vk)
{
    toggle_vk = 0;
    WaitNamedPipeW(kPipeName, kPipeWaitMs);

    HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        win32::DebugLog(L"hub: open pipe failed, error=%lu", GetLastError());
        return false;
    }

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr)
    {
        win32::DebugLog(L"hub: CreateEventW failed, error=%lu", GetLastError());
        CloseHandle(pipe);
        return false;
    }

    char buffer[1024];
    std::string json;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = event;
    for (;;)
    {
        ResetEvent(event);
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &read, &overlapped);
        if (!ok)
        {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                const DWORD wait = WaitForSingleObject(event, kPipeReadMs);
                if (wait != WAIT_OBJECT_0 || !GetOverlappedResult(pipe, &overlapped, &read, FALSE))
                {
                    CancelIo(pipe);
                    break;
                }
            }
            else if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
            {
                break;
            }
            else
            {
                break;
            }
        }
        if (read == 0)
        {
            break;
        }
        json.append(buffer, read);
        if (json.find('\n') != std::string::npos)
        {
            break;
        }
    }
    CloseHandle(event);
    CloseHandle(pipe);

    auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
    {
        win32::DebugLogUtf8("hub: pipe json parse failed, bytes=%zu", json.size());
        return false;
    }
    if (parsed.value("app", "") != "ra3-battlezone-hub")
    {
        win32::DebugLogUtf8("hub: unexpected pipe app=%s", parsed.value("app", "").c_str());
        return false;
    }
    const int value = parsed.value("port", 0);
    if (value <= 0 || value > 65535)
    {
        win32::DebugLog(L"hub: invalid pipe port=%d", value);
        return false;
    }
    port = static_cast<unsigned short>(value);
    toggle_vk = ParseToggleVk(parsed);
    if (toggle_vk != 0)
    {
        advertised_toggle_vk_.store(toggle_vk, std::memory_order_release);
    }
    win32::DebugLog(L"hub: discovered port=%u toggle_vk=0x%02X", port, toggle_vk);
    return true;
}

bool DiscoverPort(unsigned short &port)
{
    unsigned toggle_vk = 0;
    return DiscoverPort(port, toggle_vk);
}

bool HttpPostLookup(unsigned short port, std::string_view body, DWORD &status_code, std::string &response)
{
    status_code = 0;
    response.clear();

    WinHttpHandle session(WinHttpOpen(L"ra3-gaming-tool", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.handle == nullptr)
    {
        win32::DebugLog(L"hub: WinHttpOpen failed, error=%lu", GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session.handle, 2000, 2000, kHttpTimeoutMs, kHttpTimeoutMs);

    WinHttpHandle connect(WinHttpConnect(session.handle, L"127.0.0.1", port, 0));
    if (connect.handle == nullptr)
    {
        win32::DebugLog(L"hub: WinHttpConnect failed, port=%u error=%lu", port, GetLastError());
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(connect.handle, L"POST", L"/maps/lookup", nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (request.handle == nullptr)
    {
        win32::DebugLog(L"hub: WinHttpOpenRequest failed, error=%lu", GetLastError());
        return false;
    }

    const wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\n";
    if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1),
                            reinterpret_cast<LPVOID>(const_cast<char *>(body.data())), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0))
    {
        win32::DebugLog(L"hub: WinHttpSendRequest failed, error=%lu", GetLastError());
        return false;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr))
    {
        win32::DebugLog(L"hub: WinHttpReceiveResponse failed, error=%lu", GetLastError());
        return false;
    }

    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX))
    {
        return false;
    }

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available))
        {
            return false;
        }
        if (available == 0)
        {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, chunk.data(), available, &read))
        {
            return false;
        }
        response.append(chunk.data(), read);
    }
    return true;
}

bool DecodePngWic(const std::wstring &path, hub::ImagePixels &out)
{
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder)))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)))
    {
        return false;
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
    {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0)
    {
        return false;
    }

    const std::size_t stride = static_cast<std::size_t>(width) * 4;
    const std::size_t bytes = stride * static_cast<std::size_t>(height);
    out.bgra.resize(bytes);
    if (FAILED(converter->CopyPixels(nullptr, static_cast<UINT>(stride), static_cast<UINT>(bytes), out.bgra.data())))
    {
        out.bgra.clear();
        return false;
    }
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    return true;
}

bool ReadFileBytes(const std::wstring &path, std::vector<std::uint8_t> &bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || static_cast<std::uint64_t>(size.QuadPart) > kMaxImageBytes)
    {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != bytes.size())
    {
        bytes.clear();
        return false;
    }
    return true;
}

bool DecodeWebp(const std::vector<std::uint8_t> &bytes, hub::ImagePixels &out)
{
    int width = 0;
    int height = 0;
    uint8_t *decoded = WebPDecodeBGRA(bytes.data(), bytes.size(), &width, &height);
    if (decoded == nullptr || width <= 0 || height <= 0)
    {
        if (decoded != nullptr)
        {
            WebPFree(decoded);
        }
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    out.bgra.assign(decoded, decoded + count);
    out.width = width;
    out.height = height;
    WebPFree(decoded);
    return true;
}

bool DecodeImage(std::string_view image_path, hub::ImagePixels &out)
{
    out = {};
    if (image_path.empty())
    {
        return false;
    }

    const std::string path_str(image_path);
    const std::wstring wide = Utf8ToWide(image_path);
    if (wide.empty())
    {
        win32::DebugLogUtf8("hub: image path utf8 conversion failed: %s", path_str.c_str());
        return false;
    }

    bool ok = false;
    if (EndsWithIgnoreCase(image_path, ".png"))
    {
        ok = DecodePngWic(wide, out);
    }
    else if (EndsWithIgnoreCase(image_path, ".webp"))
    {
        std::vector<std::uint8_t> bytes;
        ok = ReadFileBytes(wide, bytes) && DecodeWebp(bytes, out);
    }
    else
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadFileBytes(wide, bytes) || bytes.size() < 12)
        {
            win32::DebugLogUtf8("hub: failed to read image: %s", path_str.c_str());
            return false;
        }
        if (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
        {
            ok = DecodePngWic(wide, out);
        }
        else if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' &&
                 bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
        {
            ok = DecodeWebp(bytes, out);
        }
    }
    if (!ok)
    {
        win32::DebugLogUtf8("hub: decode image failed: %s", path_str.c_str());
        return false;
    }
    win32::DebugLog(L"hub: decoded image %dx%d", out.width, out.height);
    return true;
}

void ParseLocations(const nlohmann::json &value, std::vector<hub::MapLocation> &out)
{
    out.clear();
    nlohmann::json array = nlohmann::json::array();
    if (value.is_array())
    {
        array = value;
    }
    else if (value.is_string())
    {
        auto parsed = nlohmann::json::parse(value.get<std::string>(), nullptr, false);
        if (!parsed.is_discarded() && parsed.is_array())
        {
            array = std::move(parsed);
        }
    }

    int index = 0;
    for (const auto &item : array)
    {
        hub::MapLocation loc;
        loc.location = item.value("location", index + 1);
        loc.player_type = item.value("playerType", "any");
        loc.plot_type = item.value("plotType", "any");
        loc.team = item.value("team", "any");
        out.push_back(std::move(loc));
        ++index;
    }
}

hub::LookupResult ParseLookupBody(const std::string &body)
{
    hub::LookupResult result;
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        win32::DebugLog(L"hub: lookup body is not json object, bytes=%zu", body.size());
        result.status = hub::LookupStatus::http_error;
        return result;
    }
    if (!json.value("isFound", false))
    {
        result.status = hub::LookupStatus::not_found;
        return result;
    }

    const auto &info = json["info"];
    if (!info.is_object())
    {
        result.status = hub::LookupStatus::not_found;
        return result;
    }

    result.status = hub::LookupStatus::found;
    result.display_name = info.value("displayName", "");
    result.description = StripHtml(info.value("description", ""));
    if (info.contains("user") && info["user"].is_object())
    {
        result.nick_name = info["user"].value("nickName", "");
    }
    result.player_count = info.value("playerCount", 0);
    if (info.contains("tags") && info["tags"].is_array())
    {
        for (const auto &tag : info["tags"])
        {
            if (tag.is_string())
            {
                result.tags.push_back(tag.get<std::string>());
            }
        }
    }
    if (info.contains("location"))
    {
        ParseLocations(info["location"], result.locations);
    }

    const std::string image_path = json.value("imagePath", "");
    if (!image_path.empty())
    {
        DecodeImage(image_path, result.image);
    }
    return result;
}

}

hub::LookupResult hub::LookupMap(std::string_view map_path)
{
    LookupResult result;
    if (map_path.empty())
    {
        win32::DebugLog(L"hub: LookupMap empty path");
        result.status = LookupStatus::not_found;
        return result;
    }

    win32::DebugLogUtf8("hub: LookupMap path=%s", std::string(map_path).c_str());
    unsigned short port = 0;
    if (!DiscoverPort(port))
    {
        win32::DebugLog(L"hub: client offline");
        result.status = LookupStatus::client_offline;
        return result;
    }

    nlohmann::json request = {{"path", map_path}};
    const std::string body = request.dump();

    DWORD status_code = 0;
    std::string response;
    if (!HttpPostLookup(port, body, status_code, response))
    {
        win32::DebugLog(L"hub: http request failed, port=%u", port);
        result.status = LookupStatus::client_offline;
        return result;
    }
    result.http_status = static_cast<int>(status_code);
    win32::DebugLog(L"hub: lookup http status=%lu bytes=%zu", status_code, response.size());
    if (status_code != 200)
    {
        result.status = LookupStatus::http_error;
        return result;
    }

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    result = ParseLookupBody(response);
    if (com_hr == S_OK)
    {
        CoUninitialize();
    }
    win32::DebugLogUtf8("hub: lookup status=%d name=%s players=%d", static_cast<int>(result.status),
                        result.display_name.c_str(), result.player_count);
    return result;
}

bool hub::DiscoverClient(ClientInfo &out)
{
    out = {};
    return DiscoverPort(out.port, out.toggle_vk);
}

unsigned hub::AdvertisedToggleVk() noexcept
{
    return advertised_toggle_vk_.load(std::memory_order_acquire);
}

std::string hub::VirtualKeyName(unsigned vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
    {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
    {
        return "Numpad" + std::to_string(vk - VK_NUMPAD0);
    }
    switch (vk)
    {
    case VK_INSERT:
        return "Insert";
    case VK_DELETE:
        return "Delete";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "PageUp";
    case VK_NEXT:
        return "PageDown";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_SPACE:
        return "Space";
    case VK_TAB:
        return "Tab";
    case VK_ESCAPE:
        return "Esc";
    case VK_BACK:
        return "Backspace";
    case VK_PAUSE:
        return "Pause";
    case VK_CAPITAL:
        return "CapsLock";
    case VK_NUMLOCK:
        return "NumLock";
    case VK_SCROLL:
        return "ScrollLock";
    case VK_SNAPSHOT:
        return "PrintScreen";
    case VK_MULTIPLY:
        return "Numpad*";
    case VK_ADD:
        return "Numpad+";
    case VK_SUBTRACT:
        return "Numpad-";
    case VK_DECIMAL:
        return "Numpad.";
    case VK_DIVIDE:
        return "Numpad/";
    case VK_RETURN:
        return "Enter";
    default:
        break;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}
