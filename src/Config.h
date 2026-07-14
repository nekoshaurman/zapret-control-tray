#pragma once

#include <windows.h>

#include <string>

struct AppConfig {
    std::wstring exePath;
    std::wstring arguments;
    std::wstring processName;
    std::wstring serviceMenuChoice = L"1";
    std::wstring strategyIndex = L"1";
    DWORD timeoutMs = 5000;
    bool autoStart = false;
    bool utilityAutoStart = false;
    bool debugMode = false;
};

class Config {
public:
    static AppConfig Load();
    static bool Save(const AppConfig& config);
    static std::wstring Path();
};


