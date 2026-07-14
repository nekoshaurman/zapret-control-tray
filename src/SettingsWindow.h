#pragma once

#include "Config.h"
#include "ProcessManager.h"

#include <windows.h>

#include <filesystem>
#include <vector>

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, HWND owner, AppConfig* config, ProcessManager* processManager, std::vector<std::filesystem::path>* availableStrategies);
    ~SettingsWindow();

    void Show();
    HWND Window() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void LoadValues();
    bool SaveValues();
    void BrowseExe();
    void LoadStrategies(const std::wstring& folder, const std::wstring& selectedPath);
    std::wstring GetText(int controlId) const;
    void SetText(int controlId, const std::wstring& value);

    HINSTANCE instance_ = nullptr;
    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig* config_ = nullptr;
    ProcessManager* processManager_ = nullptr;
    std::vector<std::filesystem::path> strategyPaths_;
    std::vector<std::filesystem::path>* availableStrategies_ = nullptr;
};

