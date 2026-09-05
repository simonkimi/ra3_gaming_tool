module;

#include <Windows.h>
#include <comdef.h>
#include <cstdio>
#include <cstdlib>

export module overlay:utils;

namespace
{
const wchar_t *SafeWStr(const wchar_t *s)
{
    return s ? s : L"";
}
}

export void DxTrace(HRESULT hresult, bool use_msgbox = false)
{
    if (!FAILED(hresult))
    {
        return;
    }

    _com_error error(hresult);
    wchar_t buf[1024];
    swprintf_s(buf, L"D3D error,Message: \n%s\nDescription: \n%s\nSource: \n%s\n", SafeWStr(error.ErrorMessage()),
               SafeWStr(error.Description()), SafeWStr(error.Source()));

    OutputDebugStringW(buf);
    if (!use_msgbox)
    {
        return;
    }
    int id = MessageBoxW(nullptr, buf, L"D3D error", MB_ICONERROR | MB_ABORTRETRYIGNORE);
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
