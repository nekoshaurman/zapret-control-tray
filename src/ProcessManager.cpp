#include "ProcessManager.h"

#include "AppMessages.h"
#include "Logger.h"

#include <tlhelp32.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

struct ProcessInfo {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring name;
};

std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            result += L'\\';
        }
        result += ch;
    }
    result += L"\"";
    return result;
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

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) {
        return;
    }
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::wstring ToWideBytes(const std::string& bytes) {
    std::wstring result;
    result.reserve(bytes.size());
    for (unsigned char ch : bytes) {
        result.push_back(static_cast<wchar_t>(ch));
    }
    return result;
}

std::wstring ReadFileBytesAsWide(const std::filesystem::path& path, bool logFailure = true) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (logFailure) {
            Logger::LastError(L"Failed to open file " + path.wstring());
        }
        return L"";
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return ToWideBytes(stream.str());
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L'\n') {
            if (!current.empty() && current.back() == L'\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::wstring ReadGameFilterMode(const std::filesystem::path& root, const wchar_t* kind) {
    std::filesystem::path flag = root / L"utils" / L"game_filter.enabled";
    std::wstring mode = Trim(ReadFileBytesAsWide(flag, false));
    if (mode.empty()) {
        return L"12";
    }
    if (_wcsicmp(mode.c_str(), L"all") == 0) {
        return L"1024-65535";
    }
    if (_wcsicmp(mode.c_str(), L"tcp") == 0) {
        return _wcsicmp(kind, L"tcp") == 0 ? L"1024-65535" : L"12";
    }
    return _wcsicmp(kind, L"udp") == 0 ? L"1024-65535" : L"12";
}

std::wstring ExtractWinwsArguments(const std::filesystem::path& strategyPath, const std::filesystem::path& root) {
    std::wstring text = ReadFileBytesAsWide(strategyPath);
    if (text.empty()) {
        return L"";
    }

    bool capture = false;
    std::wstring args;
    for (std::wstring line : SplitLines(text)) {
        std::wstring trimmed = Trim(line);
        if (trimmed.empty()) {
            continue;
        }

        if (!capture) {
            std::wstring lower = trimmed;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
            size_t winws = lower.find(L"winws.exe");
            if (winws == std::wstring::npos) {
                continue;
            }
            capture = true;
            size_t after = winws + wcslen(L"winws.exe");
            if (after < trimmed.size() && trimmed[after] == L'"') {
                ++after;
            }
            trimmed = Trim(trimmed.substr(after));
        }

        bool continued = false;
        if (!trimmed.empty() && trimmed.back() == L'^') {
            continued = true;
            trimmed.pop_back();
            trimmed = Trim(trimmed);
        }

        if (!trimmed.empty()) {
            if (!args.empty()) {
                args += L" ";
            }
            args += trimmed;
        }

        if (!continued) {
            break;
        }
    }

    std::wstring bin = (root / L"bin").wstring() + L"\\";
    std::wstring lists = (root / L"lists").wstring() + L"\\";
    ReplaceAll(args, L"%BIN%", bin);
    ReplaceAll(args, L"%LISTS%", lists);
    ReplaceAll(args, L"%GameFilterTCP%", ReadGameFilterMode(root, L"tcp"));
    ReplaceAll(args, L"%GameFilterUDP%", ReadGameFilterMode(root, L"udp"));
    ReplaceAll(args, L"%GameFilter%", ReadGameFilterMode(root, L"all"));
    return args;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    }
    return result;
}

bool CreateUtf8FileIfMissing(const std::filesystem::path& path, const std::wstring& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::string bytes = WideToUtf8(text);
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_EXISTS;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok) {
        Logger::LastError(L"Write user list file failed " + path.wstring());
        return false;
    }
    return true;
}

void EnsureUserLists(const std::filesystem::path& root) {
    const std::filesystem::path lists = root / L"lists";
    CreateUtf8FileIfMissing(lists / L"ipset-exclude-user.txt", L"203.0.113.113/32\r\n");
    CreateUtf8FileIfMissing(lists / L"list-general-user.txt", L"# Never leave this file empty\r\ndomain.example.abc\r\n");
    CreateUtf8FileIfMissing(lists / L"list-exclude-user.txt", L"domain.example.abc\r\n");
}

std::wstring ScriptLogPath() {
    std::wstring path = Logger::LogPath();
    const size_t dot = path.find_last_of(L".");
    if (dot == std::wstring::npos) {
        return path + L"-script.log";
    }
    return path.substr(0, dot) + L"-script" + path.substr(dot);
}

std::vector<ProcessInfo> SnapshotProcesses() {
    std::vector<ProcessInfo> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Logger::LastError(L"CreateToolhelp32Snapshot failed");
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes.push_back(ProcessInfo{ entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile });
        } while (Process32NextW(snapshot, &entry));
    } else {
        Logger::LastError(L"Process32FirstW failed");
    }

    CloseHandle(snapshot);
    return processes;
}

bool IsPidRunning(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }

    DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}


std::wstring LowerPath(std::wstring value) {
    std::replace(value.begin(), value.end(), L'/', L'\\');
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring ProcessImagePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return L"";
    }

    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        result.assign(path, size);
    }
    CloseHandle(process);
    return result;
}

DWORD FindPidByImagePath(const std::filesystem::path& imagePath) {
    const std::wstring expected = LowerPath(imagePath.wstring());
    if (expected.empty()) {
        return 0;
    }

    for (const ProcessInfo& process : SnapshotProcesses()) {
        if (_wcsicmp(process.name.c_str(), imagePath.filename().c_str()) != 0) {
            continue;
        }
        if (LowerPath(ProcessImagePath(process.pid)) == expected && IsPidRunning(process.pid)) {
            return process.pid;
        }
    }
    return 0;
}
std::vector<DWORD> FindProcessTreeByRootPid(DWORD rootPid) {
    std::vector<DWORD> result;
    if (rootPid == 0) {
        return result;
    }

    std::unordered_set<DWORD> selected{ rootPid };
    std::vector<ProcessInfo> processes = SnapshotProcesses();

    bool changed = true;
    while (changed) {
        changed = false;
        for (const ProcessInfo& process : processes) {
            if (selected.count(process.parentPid) != 0 && selected.insert(process.pid).second) {
                changed = true;
            }
        }
    }

    result.assign(selected.begin(), selected.end());
    result.erase(std::remove_if(result.begin(), result.end(), [](DWORD pid) { return !IsPidRunning(pid); }), result.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool AnyPidRunning(const std::vector<DWORD>& pids) {
    for (DWORD pid : pids) {
        if (IsPidRunning(pid)) {
            return true;
        }
    }
    return false;
}

struct CloseWindowContext {
    std::unordered_set<DWORD>* pids = nullptr;
};

BOOL CALLBACK CloseWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* context = reinterpret_cast<CloseWindowContext*>(lParam);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!context->pids || context->pids->count(pid) == 0) {
        return TRUE;
    }

    if (GetWindow(hwnd, GW_OWNER) == nullptr) {
        Logger::Info(L"Posting WM_CLOSE to hwnd=" + std::to_wstring(reinterpret_cast<UINT_PTR>(hwnd)) +
            L" pid=" + std::to_wstring(pid));
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    return TRUE;
}

void PostCloseToProcessWindows(const std::vector<DWORD>& pids) {
    std::unordered_set<DWORD> pidSet(pids.begin(), pids.end());
    CloseWindowContext context{ &pidSet };
    EnumWindows(CloseWindowsProc, reinterpret_cast<LPARAM>(&context));
}

void TerminateRemaining(const std::vector<DWORD>& pids) {
    for (auto it = pids.rbegin(); it != pids.rend(); ++it) {
        DWORD pid = *it;
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) {
            continue;
        }

        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            Logger::Info(L"Terminating pid=" + std::to_wstring(pid));
            if (!TerminateProcess(process, 1)) {
                Logger::LastError(L"TerminateProcess failed pid=" + std::to_wstring(pid));
            }
        }

        CloseHandle(process);
    }
}

} // namespace

struct ProcessManager::StopContext {
    HWND notifyWindow = nullptr;
    std::vector<DWORD> targetPids;
    DWORD rootPid = 0;
    DWORD timeoutMs = 5000;
    bool restartAfterStop = false;
};

struct ProcessManager::WaitContext {
    HWND notifyWindow = nullptr;
    HANDLE processHandle = nullptr;
    DWORD pid = 0;
};

ProcessManager::ProcessManager(HWND notifyWindow, const AppConfig& config)
    : notifyWindow_(notifyWindow), config_(config) {
    InitializeCriticalSection(&lock_);
}

ProcessManager::~ProcessManager() {
    EnterCriticalSection(&lock_);
    ClearProcessLocked();
    LeaveCriticalSection(&lock_);
    DeleteCriticalSection(&lock_);
}

void ProcessManager::UpdateConfig(const AppConfig& config) {
    EnterCriticalSection(&lock_);
    config_ = config;
    LeaveCriticalSection(&lock_);
}

bool ProcessManager::Start() {
    EnterCriticalSection(&lock_);
    ReapExitedLocked();
    RefreshPidsLocked();
    AppConfig config = config_;
    const bool alreadyRunning = !pids_.empty();
    LeaveCriticalSection(&lock_);

    if (config.exePath.empty()) {
        Logger::Info(L"Start skipped: batch path is empty");
        return false;
    }

    if (alreadyRunning) {
        Logger::Info(L"Start skipped: controlled process is already running");
        return true;
    }

    std::filesystem::path scriptPath(config.exePath);
    std::filesystem::path root = scriptPath.parent_path();
    std::filesystem::path winwsPath = root / L"bin" / L"winws.exe";
    EnsureUserLists(root);
    Logger::Info(L"Selected strategy is parsed directly; service.bat check_updates is not executed");

    std::wstring args = ExtractWinwsArguments(scriptPath, root);
    if (args.empty()) {
        Logger::Info(L"Start failed: could not extract winws.exe arguments from selected batch");
        return false;
    }

    std::wstring processImage = winwsPath.wstring();
    std::wstring commandLine = Quote(processImage) + L" " + args;
    std::wstring workingDirectory = winwsPath.parent_path().wstring();
    Logger::Info(L"Starting winws.exe directly commandLine=" + commandLine);

    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    SECURITY_ATTRIBUTES handleSecurity{};
    handleSecurity.nLength = sizeof(handleSecurity);
    handleSecurity.bInheritHandle = TRUE;

    HANDLE scriptOutput = INVALID_HANDLE_VALUE;
    if (Logger::DebugEnabled()) {
        std::wstring scriptLogPath = ScriptLogPath();
        scriptOutput = CreateFileW(
            scriptLogPath.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            &handleSecurity,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (scriptOutput == INVALID_HANDLE_VALUE) {
            Logger::LastError(L"CreateFileW script output log failed, falling back to NUL");
        } else {
            Logger::Info(L"Process output will be appended to " + scriptLogPath);
        }
    }

    if (scriptOutput == INVALID_HANDLE_VALUE) {
        scriptOutput = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &handleSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (scriptOutput == INVALID_HANDLE_VALUE) {
            Logger::LastError(L"CreateFileW NUL stdout failed");
            return false;
        }
    }

    HANDLE nulInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &handleSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulInput == INVALID_HANDLE_VALUE) {
        Logger::LastError(L"CreateFileW NUL stdin failed");
        CloseHandle(scriptOutput);
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nulInput;
    si.hStdOutput = scriptOutput;
    si.hStdError = scriptOutput;
    PROCESS_INFORMATION pi{};

    BOOL created = CreateProcessW(
        processImage.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &si,
        &pi);

    CloseHandle(scriptOutput);
    CloseHandle(nulInput);

    if (!created) {
        Logger::LastError(L"CreateProcessW failed");
        return false;
    }

    CloseHandle(pi.hThread);

    EnterCriticalSection(&lock_);
    ClearProcessLocked();
    processHandle_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    pids_.clear();
    pids_.push_back(pi.dwProcessId);

    HANDLE waitHandle = nullptr;
    DuplicateProcessHandleLocked(&waitHandle);
    LeaveCriticalSection(&lock_);

    if (waitHandle) {
        auto* context = new WaitContext{ notifyWindow_, waitHandle, pi.dwProcessId };
        HANDLE thread = CreateThread(nullptr, 0, WaitThreadProc, context, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        } else {
            Logger::LastError(L"CreateThread WaitThreadProc failed");
            CloseHandle(waitHandle);
            delete context;
        }
    }

    return true;
}

bool ProcessManager::StopAsync(bool restartAfterStop) {
    EnterCriticalSection(&lock_);
    ReapExitedLocked();
    RefreshPidsLocked();

    if (stopping_) {
        LeaveCriticalSection(&lock_);
        Logger::Info(L"Stop skipped: stop is already in progress");
        return false;
    }

    if (pids_.empty()) {
        LeaveCriticalSection(&lock_);
        Logger::Info(L"Stop requested but controlled process is not running");
        if (restartAfterStop) {
            return Start();
        }
        return false;
    }

    stopping_ = true;
    auto* context = new StopContext{ notifyWindow_, pids_, pid_, config_.timeoutMs, restartAfterStop };
    LeaveCriticalSection(&lock_);

    HANDLE thread = CreateThread(nullptr, 0, StopThreadProc, context, 0, nullptr);
    if (!thread) {
        Logger::LastError(L"CreateThread StopThreadProc failed");
        delete context;

        EnterCriticalSection(&lock_);
        stopping_ = false;
        LeaveCriticalSection(&lock_);
        return false;
    }

    CloseHandle(thread);
    return true;
}

bool ProcessManager::RestartAsync() {
    return StopAsync(true);
}

bool ProcessManager::IsRunning() {
    EnterCriticalSection(&lock_);
    ReapExitedLocked();
    RefreshPidsLocked();
    const bool running = !pids_.empty();
    LeaveCriticalSection(&lock_);
    return running;
}

DWORD ProcessManager::Pid() const {
    EnterCriticalSection(&lock_);
    const DWORD pid = pid_;
    LeaveCriticalSection(&lock_);
    return pid;
}

std::vector<DWORD> ProcessManager::Pids() const {
    EnterCriticalSection(&lock_);
    std::vector<DWORD> pids = pids_;
    LeaveCriticalSection(&lock_);
    return pids;
}

void ProcessManager::OnProcessExited(DWORD pid) {
    EnterCriticalSection(&lock_);
    if (pid == pid_) {
        Logger::Info(L"Controlled process exited pid=" + std::to_wstring(pid));
        ClearProcessLocked();
    }
    LeaveCriticalSection(&lock_);
}

bool ProcessManager::OnProcessStopped(bool restartAfterStop) {
    EnterCriticalSection(&lock_);
    ReapExitedLocked();
    stopping_ = false;
    LeaveCriticalSection(&lock_);

    if (restartAfterStop) {
        return Start();
    }
    return false;
}

DWORD WINAPI ProcessManager::StopThreadProc(LPVOID parameter) {
    std::unique_ptr<StopContext> context(static_cast<StopContext*>(parameter));

    Logger::Info(L"Stopping controlled process tree root pid=" + std::to_wstring(context->rootPid));
    std::vector<DWORD> pids = context->rootPid ? FindProcessTreeByRootPid(context->rootPid) : context->targetPids;
    if (pids.empty()) {
        Logger::Info(L"StopThread: no controlled pids found");
        PostMessageW(context->notifyWindow, WM_APP_PROCESS_STOPPED, context->restartAfterStop ? 1 : 0, 0);
        return 0;
    }

    std::wstring pidList;
    for (DWORD pid : pids) {
        if (!pidList.empty()) {
            pidList += L", ";
        }
        pidList += std::to_wstring(pid);
    }
    Logger::Info(L"StopThread target pids: " + pidList);

    PostCloseToProcessWindows(pids);

    DWORD start = GetTickCount();
    while (AnyPidRunning(pids) && GetTickCount() - start < context->timeoutMs) {
        Sleep(100);
    }

    if (AnyPidRunning(pids)) {
        Logger::Info(L"StopThread timeout reached, terminating remaining controlled processes");
        std::vector<DWORD> refreshed = context->rootPid ? FindProcessTreeByRootPid(context->rootPid) : pids;
        TerminateRemaining(refreshed.empty() ? pids : refreshed);
    } else {
        Logger::Info(L"StopThread target processes exited gracefully");
    }

    PostMessageW(context->notifyWindow, WM_APP_PROCESS_STOPPED, context->restartAfterStop ? 1 : 0, 0);
    return 0;
}

DWORD WINAPI ProcessManager::WaitThreadProc(LPVOID parameter) {
    std::unique_ptr<WaitContext> context(static_cast<WaitContext*>(parameter));
    WaitForSingleObject(context->processHandle, INFINITE);
    CloseHandle(context->processHandle);
    PostMessageW(context->notifyWindow, WM_APP_PROCESS_EXITED, context->pid, 0);
    return 0;
}

void ProcessManager::RefreshPidsLocked() {
    if (pid_ == 0) {
        pids_.clear();
        return;
    }

    pids_ = FindProcessTreeByRootPid(pid_);
    if (pids_.empty()) {
        pid_ = 0;
    }
}

void ProcessManager::ReapExitedLocked() {
    if (!processHandle_) {
        pid_ = 0;
        pids_.clear();
        return;
    }

    DWORD waitResult = WaitForSingleObject(processHandle_, 0);
    if (waitResult == WAIT_OBJECT_0) {
        ClearProcessLocked();
    }
}

bool ProcessManager::DuplicateProcessHandleLocked(HANDLE* duplicate) const {
    *duplicate = nullptr;
    if (!processHandle_) {
        return false;
    }

    return DuplicateHandle(
        GetCurrentProcess(),
        processHandle_,
        GetCurrentProcess(),
        duplicate,
        SYNCHRONIZE,
        FALSE,
        0) != FALSE;
}

void ProcessManager::ClearProcessLocked() {
    if (processHandle_) {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
    pid_ = 0;
    pids_.clear();
}