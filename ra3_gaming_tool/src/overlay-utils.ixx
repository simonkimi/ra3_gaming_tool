module;

#include <Windows.h>
#include <comdef.h>
#include <format>
#include <string>
#include <cstdlib>

export module overlay:utils;

namespace
{
const wchar_t *SafeWStr(const wchar_t *s)
{
    return s ? s : L"";
}
}

export std::wstring GetHRResult(HRESULT hresult)
{
    _com_error error(hresult);
    return std::format(L"D3D error,Message: \n{}\nDescription: \n{}\nSource: \n{}\n", SafeWStr(error.ErrorMessage()),
                       SafeWStr(error.Description()), SafeWStr(error.Source()));
}

export void DxTrace(HRESULT hresult, bool use_msgbox = false)
{
    if (!FAILED(hresult))
    {
        return;
    }

    auto err_msg = GetHRResult(hresult);

    OutputDebugStringW(err_msg.c_str());
    if (!use_msgbox)
    {
        return;
    }
    int id = MessageBoxW(nullptr, err_msg.c_str(), L"D3D error", MB_ICONERROR | MB_ABORTRETRYIGNORE);
    switch (id)
    {
    case IDRETRY:
        __debugbreak();
        break;
    case IDIGNORE:
        break;
    case IDABORT:
    default:
        exit(1);
    }
}
