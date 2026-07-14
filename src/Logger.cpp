#include "Logger.h"

#include <shlobj.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr DWORD kMaxLogBytes = 1024 * 1024;
std::atomic_bool g_debugEnabled{ false };

std::wstring ModuleDirectory() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

    std::wstring path(modulePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slash);
}

std::wstring TimestampForLine() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buffer;
}

std::wstring TimestampForFile() {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring ErrorText(DWORD error) {
    wchar_t* text = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&text),
        0,
        nullptr);

    std::wstring result = text ? text : L"Unknown error";
    if (text) {
        LocalFree(text);
    }

    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L'.')) {
        result.pop_back();
    }
    return result;
}

void RotateIfNeeded(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return;
    }

    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    if (size.QuadPart < kMaxLogBytes) {
        return;
    }

    const std::wstring rotated = path + L".1";
    DeleteFileW(rotated.c_str());
    MoveFileW(path.c_str(), rotated.c_str());
}

void AppendLine(const std::wstring& line) {
    const std::string utf8 = ToUtf8(line + L"\r\n");
    if (utf8.empty()) {
        return;
    }

    const std::wstring path = Logger::LogPath();
    RotateIfNeeded(path);

    HANDLE file = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

std::wstring DesktopDirectory() {
    PWSTR desktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) {
        std::wstring result(desktop);
        CoTaskMemFree(desktop);
        return result;
    }

    wchar_t profile[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::wstring(profile) + L"\\Desktop";
    }

    return ModuleDirectory();
}

} // namespace

void Logger::SetDebugEnabled(bool enabled) {
    g_debugEnabled.store(enabled);
}

bool Logger::DebugEnabled() {
    return g_debugEnabled.load();
}

std::wstring Logger::LogPath() {
    return ModuleDirectory() + L"\\ZapretControl.log";
}

void Logger::Info(const std::wstring& message) {
    if (!g_debugEnabled.load()) {
        return;
    }
    AppendLine(L"[" + TimestampForLine() + L"] [info] " + message);
}

void Logger::LastError(const std::wstring& message, DWORD error) {
    AppendLine(L"[" + TimestampForLine() + L"] [error] " + message +
        L"; code=" + std::to_wstring(error) + L"; text=" + ErrorText(error));
}

bool Logger::ExportToDesktop(std::wstring* exportedPath) {
    const std::wstring source = LogPath();
    const std::wstring destination = DesktopDirectory() + L"\\ZapretControl-" + TimestampForFile() + L".log";

    if (!CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
        LastError(L"Export log failed");
        return false;
    }

    if (exportedPath) {
        *exportedPath = destination;
    }
    Info(L"Exported debug log to " + destination);
    return true;
}
