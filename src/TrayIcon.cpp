#include "TrayIcon.h"

#include "AppMessages.h"
#include "resource.h"

#include <shellapi.h>

#include <algorithm>
#include <filesystem>

TrayIcon::TrayIcon(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {
}

TrayIcon::~TrayIcon() {
    Remove();
}

void TrayIcon::FillNotifyData(NOTIFYICONDATAW& nid) const {
    nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = IDI_APP;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    const int iconId = running_ ? IDI_APP_RUNNING : IDI_APP_STOPPED;
    nid.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(iconId));
    if (!nid.hIcon) {
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(nid.szTip, running_ ? L"Zapret Control: running" : L"Zapret Control: stopped");
}

bool TrayIcon::Add() {
    if (added_) {
        return true;
    }

    NOTIFYICONDATAW nid{};
    FillNotifyData(nid);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;

    if (added_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }

    return added_;
}

void TrayIcon::Remove() {
    if (!added_) {
        return;
    }

    NOTIFYICONDATAW nid{};
    FillNotifyData(nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

void TrayIcon::SetRunning(bool running) {
    running_ = running;
    if (!added_) {
        return;
    }

    NOTIFYICONDATAW nid{};
    FillNotifyData(nid);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::ShowMenu(const AppConfig& config, const std::vector<std::filesystem::path>& strategies) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    const std::filesystem::path selected(config.exePath);

    if (strategies.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No general strategies found");
    } else {
        const size_t maxItems = std::min<size_t>(strategies.size(), IDM_TRAY_STRATEGY_LAST - IDM_TRAY_STRATEGY_FIRST + 1);
        for (size_t index = 0; index < maxItems; ++index) {
            UINT flags = MF_STRING;
            if (_wcsicmp(strategies[index].wstring().c_str(), selected.wstring().c_str()) == 0) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(menu, flags, IDM_TRAY_STRATEGY_FIRST + static_cast<UINT>(index), strategies[index].filename().c_str());
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_START, L"Start script");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_STOP, L"Stop winws.exe");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_RESTART, L"Restart winws.exe");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    SetForegroundWindow(owner_);

    POINT pt{};
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, owner_, nullptr);
    DestroyMenu(menu);
}
