#include "SettingsWindow.h"

#include "Config.h"
#include "Logger.h"
#include "StrategyScanner.h"
#include "resource.h"

#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kSettingsClassName[] = L"ZapretControlSettingsWindow";

HWND AddLabel(HWND parent, int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, nullptr, nullptr);
}

HWND AddEdit(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr,
        nullptr);
}

HWND AddCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr,
        nullptr);
}

HWND AddButton(HWND parent, int id, int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        nullptr,
        nullptr);
}

std::wstring CurrentExePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}


std::wstring Trim(const std::wstring& value) {
    const wchar_t* whitespace = L" \t\r\n";
    size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::wstring::npos) {
        return L"";
    }
    size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}
bool ApplyUtilityAutoStart(bool enabled) {
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"ZapretControl";

    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        Logger::LastError(L"RegCreateKeyExW Run key failed", static_cast<DWORD>(result));
        return false;
    }

    bool ok = true;
    if (enabled) {
        std::wstring value = L"\"" + CurrentExePath() + L"\"";
        result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        if (result != ERROR_SUCCESS) {
            Logger::LastError(L"RegSetValueExW utility autostart failed", static_cast<DWORD>(result));
            ok = false;
        }
    } else {
        result = RegDeleteValueW(key, kRunValue);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            Logger::LastError(L"RegDeleteValueW utility autostart failed", static_cast<DWORD>(result));
            ok = false;
        }
    }

    RegCloseKey(key);
    return ok;
}


std::wstring NormalizePathForCompare(std::wstring value) {
    value = Trim(value);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool IsUtilityAutoStartEnabled() {
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"ZapretControl";

    HKEY key = nullptr;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    wchar_t value[MAX_PATH * 2]{};
    DWORD type = 0;
    DWORD size = sizeof(value);
    result = RegQueryValueExW(key, kRunValue, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }

    return _wcsicmp(NormalizePathForCompare(value).c_str(), CurrentExePath().c_str()) == 0;
}
void LogControl(HWND hwnd, const wchar_t* name) {
    if (hwnd) {
        Logger::Info(std::wstring(L"Created control: ") + name);
    } else {
        Logger::LastError(std::wstring(L"Failed to create control: ") + name);
    }
}

} // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HWND owner, AppConfig* config, ProcessManager* processManager, std::vector<std::filesystem::path>* availableStrategies)
    : instance_(instance), owner_(owner), config_(config), processManager_(processManager), availableStrategies_(availableStrategies) {
}

SettingsWindow::~SettingsWindow() {
    if (hwnd_) {
        Logger::Info(L"SettingsWindow destructor destroying window");
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void SettingsWindow::Show() {
    Logger::Info(L"SettingsWindow::Show requested");

    if (hwnd_) {
        Logger::Info(L"SettingsWindow already exists, showing existing window");
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kSettingsClassName;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    ATOM atom = RegisterClassW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Logger::LastError(L"SettingsWindow RegisterClassW failed");
        return;
    }
    Logger::Info(L"SettingsWindow class ready");

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        kSettingsClassName,
        L"Zapret Control Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        430,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_) {
        Logger::Info(L"SettingsWindow CreateWindowExW succeeded hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd_)));
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        UpdateWindow(hwnd_);
    } else {
        Logger::LastError(L"SettingsWindow CreateWindowExW failed");
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = nullptr;

    if (message == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            Logger::Info(L"SettingsWindow WM_NCCREATE hwnd assigned");
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        Logger::Info(L"SettingsWindow WM_CREATE");
        CreateControls();
        LoadValues();
        Logger::Info(L"SettingsWindow WM_CREATE completed");
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE:
            Logger::Info(L"SettingsWindow Browse folder clicked");
            BrowseExe();
            return 0;
        case IDC_SAVE:
            Logger::Info(L"SettingsWindow Save clicked");
            if (SaveValues()) {
                ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        case IDC_CANCEL:
            Logger::Info(L"SettingsWindow Cancel clicked");
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        break;

    case WM_CLOSE:
        Logger::Info(L"SettingsWindow WM_CLOSE hiding window");
        ShowWindow(hwnd_, SW_HIDE);
        return 0;

    case WM_NCDESTROY:
        Logger::Info(L"SettingsWindow WM_NCDESTROY");
        hwnd_ = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void SettingsWindow::CreateControls() {
    Logger::Info(L"SettingsWindow CreateControls hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd_)));

    LogControl(AddLabel(hwnd_, 16, 20, 130, 22, L"Zapret folder:"), L"Zapret folder label");
    LogControl(AddEdit(hwnd_, IDC_EXE_PATH, 150, 18, 300, 24), L"Zapret folder edit");
    LogControl(AddButton(hwnd_, IDC_BROWSE, 462, 17, 70, 26, L"Browse"), L"Browse button");

    LogControl(AddLabel(hwnd_, 16, 58, 130, 22, L"Strategy:"), L"Strategy label");
    LogControl(AddCombo(hwnd_, IDC_STRATEGY_INDEX, 150, 56, 284, 180), L"Strategy combo");
    LogControl(AddButton(hwnd_, IDC_REFRESH_STRATEGIES, 444, 55, 88, 26, L"Refresh"), L"Refresh strategies button");

    LogControl(AddLabel(hwnd_, 16, 96, 130, 22, L"Extra args:"), L"Extra args label");
    LogControl(AddEdit(hwnd_, IDC_ARGUMENTS, 150, 94, 382, 24), L"Extra args edit");

    LogControl(AddLabel(hwnd_, 16, 134, 130, 22, L"Timeout, ms:"), L"Timeout label");
    LogControl(AddEdit(hwnd_, IDC_TIMEOUT, 150, 132, 120, 24), L"Timeout edit");

    LogControl(AddLabel(hwnd_, 290, 134, 130, 22, L"Start delay, sec:"), L"Start delay label");
    LogControl(AddEdit(hwnd_, IDC_START_DELAY, 412, 132, 120, 24), L"Start delay edit");

    LogControl(CreateWindowExW(0, L"BUTTON", L"Start controlled application with this utility",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        150, 176, 382, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUTOSTART)), instance_, nullptr),
        L"Controlled app autostart checkbox");

    LogControl(CreateWindowExW(0, L"BUTTON", L"Run Zapret Control when Windows starts",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        150, 208, 382, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_UTILITY_AUTOSTART)), instance_, nullptr),
        L"Utility autostart checkbox");

    LogControl(CreateWindowExW(0, L"BUTTON", L"Debug logging",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        150, 240, 382, 24, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DEBUG_MODE)), instance_, nullptr),
        L"Debug logging checkbox");

    LogControl(AddButton(hwnd_, IDC_SAVE, 346, 340, 88, 28, L"Save"), L"Save button");
    LogControl(AddButton(hwnd_, IDC_CANCEL, 444, 340, 88, 28, L"Cancel"), L"Cancel button");
}

void SettingsWindow::LoadValues() {
    Logger::Info(L"SettingsWindow LoadValues");

    if (!config_) {
        Logger::Info(L"SettingsWindow LoadValues skipped: config is null");
        return;
    }

    const std::wstring folder = StrategyFolderFromPath(config_->exePath).wstring();
    SetText(IDC_EXE_PATH, folder);
    LoadStrategies(folder, config_->exePath);
    SetText(IDC_ARGUMENTS, config_->arguments);    SetText(IDC_TIMEOUT, std::to_wstring(config_->timeoutMs));
    SetText(IDC_START_DELAY, std::to_wstring(config_->autoStartDelaySec));
    config_->utilityAutoStart = IsUtilityAutoStartEnabled();
    SendDlgItemMessageW(hwnd_, IDC_AUTOSTART, BM_SETCHECK, config_->autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd_, IDC_UTILITY_AUTOSTART, BM_SETCHECK, config_->utilityAutoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(hwnd_, IDC_DEBUG_MODE, BM_SETCHECK, config_->debugMode ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool SettingsWindow::SaveValues() {
    Logger::Info(L"SettingsWindow SaveValues");

    if (!config_) {
        Logger::Info(L"SettingsWindow SaveValues skipped: config is null");
        return false;
    }

    const LRESULT selected = SendDlgItemMessageW(hwnd_, IDC_STRATEGY_INDEX, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR || selected < 0 || static_cast<size_t>(selected) >= strategyPaths_.size()) {
        MessageBoxW(hwnd_, L"Select a zapret folder with at least one general strategy bat.", L"Zapret Control", MB_OK | MB_ICONWARNING);
        return false;
    }

    config_->exePath = strategyPaths_[static_cast<size_t>(selected)].wstring();
    config_->arguments = GetText(IDC_ARGUMENTS);
    try {
        config_->timeoutMs = std::max<DWORD>(100, std::stoul(GetText(IDC_TIMEOUT)));
    } catch (...) {
        Logger::Info(L"SettingsWindow timeout parse failed, using default 5000 ms");
        config_->timeoutMs = 5000;
    }

    config_->autoStart = SendDlgItemMessageW(hwnd_, IDC_AUTOSTART, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config_->utilityAutoStart = SendDlgItemMessageW(hwnd_, IDC_UTILITY_AUTOSTART, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config_->debugMode = SendDlgItemMessageW(hwnd_, IDC_DEBUG_MODE, BM_GETCHECK, 0, 0) == BST_CHECKED;
    Logger::SetDebugEnabled(config_->debugMode);
    ApplyUtilityAutoStart(config_->utilityAutoStart);

    if (!Config::Save(*config_)) {
        Logger::LastError(L"Config save failed from SettingsWindow");
    }
    if (processManager_) {
        processManager_->UpdateConfig(*config_);
    }
    return true;
}

void SettingsWindow::BrowseExe() {
    Logger::Info(L"SettingsWindow Browse folder");

    BROWSEINFOW bi{};
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"Select zapret folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE idList = SHBrowseForFolderW(&bi);
    if (!idList) {
        Logger::Info(L"SettingsWindow folder browse canceled");
        return;
    }

    wchar_t folder[MAX_PATH]{};
    if (SHGetPathFromIDListW(idList, folder)) {
        SetText(IDC_EXE_PATH, folder);
        LoadStrategies(folder, L"");
    } else {
        Logger::LastError(L"SHGetPathFromIDListW failed");
    }
    CoTaskMemFree(idList);
}

void SettingsWindow::LoadStrategies(const std::wstring& folder, const std::wstring& selectedPath) {
    HWND combo = GetDlgItem(hwnd_, IDC_STRATEGY_INDEX);
    if (!combo) {
        Logger::LastError(L"SettingsWindow strategy combo missing");
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    strategyPaths_ = FindGeneralStrategies(folder);
    if (availableStrategies_) {
        *availableStrategies_ = strategyPaths_;
    }

    int selectedIndex = -1;
    for (size_t index = 0; index < strategyPaths_.size(); ++index) {
        const std::wstring name = strategyPaths_[index].filename().wstring();
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (!selectedPath.empty() && _wcsicmp(strategyPaths_[index].wstring().c_str(), selectedPath.c_str()) == 0) {
            selectedIndex = static_cast<int>(index);
        }
    }

    if (strategyPaths_.empty()) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No general strategies found"));
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        return;
    }

    SendMessageW(combo, CB_SETCURSEL, selectedIndex >= 0 ? selectedIndex : 0, 0);
}

std::wstring SettingsWindow::GetText(int controlId) const {
    HWND control = GetDlgItem(hwnd_, controlId);
    if (!control) {
        Logger::LastError(L"SettingsWindow GetText missing control id=" + std::to_wstring(controlId));
        return {};
    }

    int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);

    return buffer.data();
}

void SettingsWindow::SetText(int controlId, const std::wstring& value) {
    if (!SetDlgItemTextW(hwnd_, controlId, value.c_str())) {
        Logger::LastError(L"SettingsWindow SetText failed id=" + std::to_wstring(controlId));
    }
}