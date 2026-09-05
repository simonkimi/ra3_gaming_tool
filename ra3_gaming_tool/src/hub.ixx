module;

#include <Windows.h>
#include <wincodec.h>
#include <winhttp.h>
#include <webp/decode.h>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <wrl/client.h>

export module hub;

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

LookupResult LookupMap(std::string_view map_path);

}

namespace
{

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\ra3-battlezone-hub";
constexpr DWORD kPipeWaitMs = 1500;
constexpr DWORD kPipeReadMs = 2000;
constexpr int kHttpTimeoutMs = 15000;
constexpr std::uint64_t kMaxImageBytes = 10ull * 1024ull * 1024ull;

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

bool DiscoverPort(unsigned short &port)
{
    WaitNamedPipeW(kPipeName, kPipeWaitMs);

    HANDLE pipe = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr)
    {
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
        return false;
    }
    if (parsed.value("app", "") != "ra3-battlezone-hub")
    {
        return false;
    }
    const int value = parsed.value("port", 0);
    if (value <= 0 || value > 65535)
    {
        return false;
    }
    port = static_cast<unsigned short>(value);
    return true;
}

bool HttpPostLookup(unsigned short port, std::string_view body, DWORD &status_code, std::string &response)
{
    status_code = 0;
    response.clear();

    WinHttpHandle session(WinHttpOpen(L"ra3-gaming-tool", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.handle == nullptr)
    {
        return false;
    }
    WinHttpSetTimeouts(session.handle, 2000, 2000, kHttpTimeoutMs, kHttpTimeoutMs);

    WinHttpHandle connect(WinHttpConnect(session.handle, L"127.0.0.1", port, 0));
    if (connect.handle == nullptr)
    {
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(connect.handle, L"POST", L"/maps/lookup", nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (request.handle == nullptr)
    {
        return false;
    }

    const wchar_t headers[] = L"Content-Type: application/json; charset=utf-8\r\n";
    if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1),
                            reinterpret_cast<LPVOID>(const_cast<char *>(body.data())), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0))
    {
        return false;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr))
    {
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

    const std::wstring wide = Utf8ToWide(image_path);
    if (wide.empty())
    {
        return false;
    }

    if (EndsWithIgnoreCase(image_path, ".png"))
    {
        return DecodePngWic(wide, out);
    }
    if (EndsWithIgnoreCase(image_path, ".webp"))
    {
        std::vector<std::uint8_t> bytes;
        return ReadFileBytes(wide, bytes) && DecodeWebp(bytes, out);
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(wide, bytes) || bytes.size() < 12)
    {
        return false;
    }
    if (bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
    {
        return DecodePngWic(wide, out);
    }
    if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' &&
        bytes[10] == 'B' && bytes[11] == 'P')
    {
        return DecodeWebp(bytes, out);
    }
    return false;
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
        result.status = LookupStatus::not_found;
        return result;
    }

    unsigned short port = 0;
    if (!DiscoverPort(port))
    {
        result.status = LookupStatus::client_offline;
        return result;
    }

    nlohmann::json request = {{"path", map_path}};
    const std::string body = request.dump();

    DWORD status_code = 0;
    std::string response;
    if (!HttpPostLookup(port, body, status_code, response))
    {
        result.status = LookupStatus::client_offline;
        return result;
    }
    result.http_status = static_cast<int>(status_code);
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
    return result;
}
