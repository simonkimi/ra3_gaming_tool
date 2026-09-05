module;

#include <Windows.h>
#include <cstdio>
#include <stdexcept>
#include <string>

export module win32:mutex;

export namespace win32
{

std::wstring InjectionMutexName(DWORD pid);

bool InjectionMutexPresent(DWORD pid);

}

std::wstring win32::InjectionMutexName(DWORD pid)
{
    return L"Ra3BattlezoneHubMutex" + std::to_wstring(pid);
}

bool win32::InjectionMutexPresent(DWORD pid)
{
    const std::wstring name = InjectionMutexName(pid);
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (mutex != nullptr)
    {
        CloseHandle(mutex);
        return true;
    }

    const DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND)
    {
        return false;
    }
    if (err == ERROR_ACCESS_DENIED)
    {
        return true;
    }

    char buf[256];
    std::snprintf(buf, sizeof(buf), "OpenMutexW failed, error code: %lu", err);
    throw std::runtime_error(buf);
}
