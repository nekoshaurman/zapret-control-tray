#include "AppMessages.h"
#include "Config.h"
#include "Logger.h"
#include "ProcessManager.h"
#include "SettingsWindow.h"
#include "StrategyScanner.h"
#include "TrayIcon.h"
#include "resource.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kMainClassName[] = L"ZapretControlMessageWindow";
constexpr wchar_t kMutexName[] = L"Local\\ZapretControlSingleInstance";
constexpr UINT_PTR kTrayRetryTimerId = 1;
constexpr UINT kTrayRetryIntervalMs = 1000;
UINT g_taskbarCreatedMessage = 0;

struct AppState {
    HINSTANCE instance = nullptr;
    AppConfig config;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<ProcessManager> processManager;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::vector<std::filesystem::path> strategies;
    bool running = false;
    bool trayRetryLogged = false;
};

void EnsureTrayIcon(HWND hwnd, AppState* state) {
    if (!state || !state->tray) {
        return;
    }

    if (state->tray->Add()) {
        KillTimer(hwnd, kTrayRetryTimerId);
        state->tray->SetRunning(state->running);
        state->trayRetryLogged = false;
        Logger::Info(L"Tray icon is present");
        return;
    }

    if (!state->trayRetryLogged) {
        Logger::LastError(L"Tray icon add failed, retry scheduled");
        state->trayRetryLogged = true;
    }
    SetTimer(hwnd, kTrayRetryTimerId, kTrayRetryIntervalMs, nullptr);
}

void ShowLastError(HWND owner, const wchar_t* title) {
    DWORD error = GetLastError();
    Logger::LastError(title, error);

    wchar_t message[512]{};
    FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        message,
        static_cast<DWORD>(std::size(message)),
        nullptr);

    MessageBoxW(owner, message[0] ? message : L"Unknown WinAPI error.", title, MB_ICONERROR | MB_OK);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        Logger::Info(L"Main window WM_NCCREATE");
    }

    if (message == g_taskbarCreatedMessage && state) {
        Logger::Info(L"TaskbarCreated received, restoring tray icon");
        if (state->tray) {
            state->tray->Remove();
            EnsureTrayIcon(hwnd, state);
        }
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        Logger::Info(L"Main window WM_CREATE");
        state->processManager = std::make_unique<ProcessManager>(hwnd, state->config);
        state->tray = std::make_unique<TrayIcon>(state->instance, hwnd);
        state->settingsWindow = std::make_unique<SettingsWindow>(
            state->instance, hwnd, &state->config, state->processManager.get(), &state->strategies);

        Logger::Info(L"Adding tray icon");
        EnsureTrayIcon(hwnd, state);

        if (state->config.autoStart) {
            Logger::Info(L"AutoStart enabled: starting controlled app");
            state->running = state->processManager->Start();
            state->tray->SetRunning(state->running);
        }
        return 0;

    case WM_APP_TRAYICON:
        Logger::Info(L"Tray callback lParam=" + std::to_wstring(static_cast<unsigned long long>(lParam)));
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            state->tray->SetRunning(state->running);
            Logger::Info(L"Showing tray menu");
            state->tray->ShowMenu(state->config, state->strategies);
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            Logger::Info(L"Tray double click: opening settings");
            state->settingsWindow->Show();
        }
        return 0;

    case WM_COMMAND:
    {
        const WORD command = LOWORD(wParam);
        if (command >= IDM_TRAY_STRATEGY_FIRST && command <= IDM_TRAY_STRATEGY_LAST) {
            const size_t index = static_cast<size_t>(command - IDM_TRAY_STRATEGY_FIRST);
            if (index < state->strategies.size()) {
                state->config.exePath = state->strategies[index].wstring();
                Config::Save(state->config);
                state->processManager->UpdateConfig(state->config);
                Logger::Info(L"Strategy selected from tray: " + state->config.exePath);
            }
            return 0;
        }

        switch (command) {
        case IDM_TRAY_START:
            Logger::Info(L"Menu command: Start script");
            if (state->processManager->Start()) {
                state->running = true;
            } else {
                ShowLastError(hwnd, L"Failed to start script");
            }
            state->tray->SetRunning(state->running);
            return 0;

        case IDM_TRAY_STOP:
            Logger::Info(L"Menu command: Stop winws.exe");
            state->processManager->StopAsync(false);
            state->tray->SetRunning(state->running);
            return 0;

        case IDM_TRAY_RESTART:
            Logger::Info(L"Menu command: Restart winws.exe");
            if (!state->processManager->RestartAsync()) {
                Logger::Info(L"Restart fallback: starting controlled app directly");
                state->running = state->processManager->Start();
                state->tray->SetRunning(state->running);
            }
            return 0;

        case IDM_TRAY_SETTINGS:
            Logger::Info(L"Menu command: Settings clicked");
            state->settingsWindow->Show();
            return 0;

        case IDM_TRAY_EXPORT_LOG:
        {
            Logger::Info(L"Menu command: Export debug log clicked");
            std::wstring exportedPath;
            if (Logger::ExportToDesktop(&exportedPath)) {
                MessageBoxW(hwnd, exportedPath.c_str(), L"Debug log exported", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, Logger::LogPath().c_str(), L"Failed to export debug log", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        case IDM_TRAY_EXIT:
            Logger::Info(L"Menu command: Exit");
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_APP_PROCESS_EXITED:
        Logger::Info(L"Controlled process exited pid=" + std::to_wstring(static_cast<DWORD>(wParam)));
        state->processManager->OnProcessExited(static_cast<DWORD>(wParam));
        state->running = false;
        state->tray->SetRunning(false);
        return 0;

    case WM_APP_PROCESS_STOPPED:
        Logger::Info(L"Controlled process stopped restart=" + std::to_wstring(wParam != 0));
        state->running = state->processManager->OnProcessStopped(wParam != 0);
        state->tray->SetRunning(state->running);
        return 0;

    case WM_TIMER:
        if (wParam == kTrayRetryTimerId) {
            EnsureTrayIcon(hwnd, state);
            return 0;
        }
        break;

    case WM_DESTROY:
        Logger::Info(L"Main window WM_DESTROY");
        KillTimer(hwnd, kTrayRetryTimerId);
        if (state) {
            if (state->tray) {
                state->tray->Remove();
            }
            state->settingsWindow.reset();
            state->processManager.reset();
            state->tray.reset();
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    Logger::Info(L"Zapret Control starting");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) {
        Logger::LastError(L"CreateMutexW failed");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Logger::Info(L"Second instance blocked by mutex");
        CloseHandle(mutex);
        return 0;
    }

    AppState state;
    state.instance = instance;
    state.config = Config::Load();
    state.strategies = FindGeneralStrategies(StrategyFolderFromPath(state.config.exePath));
    Logger::SetDebugEnabled(state.config.debugMode);
    Logger::Info(L"Debug logging enabled");

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kMainClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassW(&wc)) {
        Logger::LastError(L"Main RegisterClassW failed");
        CloseHandle(mutex);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        kMainClassName,
        L"Zapret Control",
        0,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        &state);

    if (!hwnd) {
        Logger::LastError(L"Main CreateWindowExW failed");
        CloseHandle(mutex);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Logger::Info(L"Zapret Control exiting");
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}


