#pragma once

#include "pch.h"
#include <TlHelp32.h>
#include <memory>
#include <list>

namespace win32 {

void CrtInjectDll(DWORD pid, LPCTSTR dll_path);

void CrtFreeDll(DWORD pid, LPCTSTR dll_path);

HWND GetProcessWindow();

std::pair<long, long> GetWindowSize(HWND hwnd);

HMODULE FindRemoteModuleHandle(HANDLE handle, LPCTSTR modulePath);
}


