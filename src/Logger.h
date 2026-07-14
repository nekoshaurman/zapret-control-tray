#pragma once

#include <windows.h>

#include <string>

class Logger {
public:
    static void SetDebugEnabled(bool enabled);
    static bool DebugEnabled();
    static void Info(const std::wstring& message);
    static void LastError(const std::wstring& message, DWORD error = GetLastError());
    static std::wstring LogPath();
    static bool ExportToDesktop(std::wstring* exportedPath);
};
