#pragma once

#include <windows.h>

#include <string>

struct AppConfig {
    std::wstring exePath;
    std::wstring arguments;
    DWORD timeoutMs = 5000;
    DWORD autoStartDelaySec = 10;
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
