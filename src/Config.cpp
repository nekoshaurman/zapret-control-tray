#include "Config.h"

#include <windows.h>

#include <filesystem>

namespace {

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* fallback, const std::wstring& path) {
    wchar_t buffer[4096]{};
    GetPrivateProfileStringW(section, key, fallback, buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

bool WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value, const std::wstring& path) {
    return WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()) != FALSE;
}

} // namespace

std::wstring Config::Path() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

    std::filesystem::path path(modulePath);
    path.replace_filename(L"config.ini");
    return path.wstring();
}

AppConfig Config::Load() {
    const std::wstring path = Path();

    AppConfig config;
    config.exePath = ReadString(L"app", L"exePath", L"", path);
    config.arguments = ReadString(L"app", L"arguments", L"", path);    config.timeoutMs = GetPrivateProfileIntW(L"app", L"timeoutMs", 5000, path.c_str());
    config.autoStartDelaySec = GetPrivateProfileIntW(L"app", L"autoStartDelaySec", 10, path.c_str());
    config.autoStart = GetPrivateProfileIntW(L"app", L"autoStart", 0, path.c_str()) != 0;
    config.utilityAutoStart = GetPrivateProfileIntW(L"app", L"utilityAutoStart", 0, path.c_str()) != 0;
    config.debugMode = GetPrivateProfileIntW(L"app", L"debugMode", 0, path.c_str()) != 0;
    if (config.timeoutMs < 100) {
        config.timeoutMs = 100;
    }

    return config;
}

bool Config::Save(const AppConfig& config) {
    const std::wstring path = Path();
    bool ok = true;

    ok = WriteString(L"app", L"exePath", config.exePath, path) && ok;
    ok = WriteString(L"app", L"arguments", config.arguments, path) && ok;    ok = WriteString(L"app", L"timeoutMs", std::to_wstring(config.timeoutMs), path) && ok;
    ok = WriteString(L"app", L"autoStartDelaySec", std::to_wstring(config.autoStartDelaySec), path) && ok;
    ok = WriteString(L"app", L"autoStart", config.autoStart ? L"1" : L"0", path) && ok;
    ok = WriteString(L"app", L"utilityAutoStart", config.utilityAutoStart ? L"1" : L"0", path) && ok;
    ok = WriteString(L"app", L"debugMode", config.debugMode ? L"1" : L"0", path) && ok;

    return ok;
}