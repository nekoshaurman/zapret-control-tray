#pragma once

#include "Config.h"

#include <windows.h>

#include <vector>

class ProcessManager {
public:
    ProcessManager(HWND notifyWindow, const AppConfig& config);
    ~ProcessManager();

    void UpdateConfig(const AppConfig& config);
    bool Start();
    bool StopAsync(bool restartAfterStop);
    bool RestartAsync();
    bool IsRunning();
    DWORD Pid() const;
    std::vector<DWORD> Pids() const;

    void OnProcessExited(DWORD pid);
    bool OnProcessStopped(bool restartAfterStop);

private:
    struct StopContext;
    struct WaitContext;

    static DWORD WINAPI StopThreadProc(LPVOID parameter);
    static DWORD WINAPI WaitThreadProc(LPVOID parameter);

    void RefreshPidsLocked();
    void ReapExitedLocked();
    bool DuplicateProcessHandleLocked(HANDLE* duplicate) const;
    void ClearProcessLocked();

    HWND notifyWindow_ = nullptr;
    AppConfig config_{};
    HANDLE processHandle_ = nullptr;
    DWORD pid_ = 0;
    std::vector<DWORD> pids_;
    bool stopping_ = false;
    mutable CRITICAL_SECTION lock_{};
};






