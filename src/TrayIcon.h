#pragma once

#include "Config.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <string>
#include <vector>

class TrayIcon {
public:
    TrayIcon(HINSTANCE instance, HWND owner);
    ~TrayIcon();

    bool Add();
    void Remove();
    void ShowMenu(const AppConfig& config, const std::vector<std::filesystem::path>& strategies);
    void SetRunning(bool running);
    void SetStatus(bool running, const AppConfig& config, DWORD pid);

private:
    void FillNotifyData(NOTIFYICONDATAW& nid) const;

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    bool added_ = false;
    bool running_ = false;
    std::wstring tooltip_;
};
