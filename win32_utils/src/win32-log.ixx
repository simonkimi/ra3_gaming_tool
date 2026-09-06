module;

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

export module win32:log;

export namespace win32
{

void DebugLog(const wchar_t *fmt, ...);
void DebugLogUtf8(const char *fmt, ...);

}

void win32::DebugLog(const wchar_t *fmt, ...)
{
    if (fmt == nullptr)
    {
        return;
    }

    wchar_t body[1800];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);

    wchar_t line[1900];
    swprintf_s(line, L"[Ra3BH] %s\n", body);
    OutputDebugStringW(line);
}

void win32::DebugLogUtf8(const char *fmt, ...)
{
    if (fmt == nullptr)
    {
        return;
    }

    char body[1800];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);

    wchar_t wide[1800];
    if (MultiByteToWideChar(CP_UTF8, 0, body, -1, wide, static_cast<int>(_countof(wide))) <= 0)
    {
        OutputDebugStringW(L"[Ra3BH] (utf8 log conversion failed)\n");
        return;
    }

    wchar_t line[1900];
    swprintf_s(line, L"[Ra3BH] %s\n", wide);
    OutputDebugStringW(line);
}
