#include "SshBackend.h"

#include "AppIds.h"
#include "AppServices.h"
#include "SshCommand.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class SshProcess {
public:
    explicit SshProcess(int sessionId) : sessionId_(sessionId) {}

    ~SshProcess() {
        Stop();
    }

    void Start(const std::wstring& rawCommand,
               AuthMode authMode,
               const std::wstring& password,
               const std::wstring& keyPath,
               const std::wstring& keyPassphrase) {
        Stop();

        finished_ = false;
        stop_ = false;
        password_ = password;
        keyPassphrase_ = keyPassphrase;
        authMode_ = authMode;

        int localPort = 0;
        TryExtractFirstLocalPort(rawCommand, localPort);
        localPort_ = localPort;

        worker_ = std::thread(
            [this, rawCommand, authMode, keyPath] {
                Run(rawCommand, authMode, keyPath);
            }
        );
    }

    bool IsFinished() const {
        return finished_.load();
    }

    void Stop() {
        std::lock_guard<std::mutex> stopGuard(stopMutex_);
        stop_ = true;

        HANDLE process = nullptr;
        {
            std::lock_guard<std::mutex> lk(processMutex_);
            process = processHandle_;
            if (process) {
                TerminateProcess(process, 0);
            }
        }

        WakeAskPass();

        if (worker_.joinable()) worker_.join();
        if (askPassThread_.joinable()) askPassThread_.join();

        CloseProcessHandles();

        if (!password_.empty()) {
            SecureZeroMemory(password_.data(), password_.size() * sizeof(wchar_t));
            password_.clear();
        }
        if (!keyPassphrase_.empty()) {
            SecureZeroMemory(keyPassphrase_.data(), keyPassphrase_.size() * sizeof(wchar_t));
            keyPassphrase_.clear();
        }
    }

private:
    int sessionId_ = 0;
    std::atomic<bool> stop_{ false };
    std::atomic<bool> finished_{ true };
    std::thread worker_;
    std::thread askPassThread_;
    std::thread readerThread_;

    std::mutex processMutex_;
    std::mutex stopMutex_;
    HANDLE processHandle_ = nullptr;
    HANDLE processThread_ = nullptr;
    HANDLE stdoutRead_ = nullptr;
    HANDLE stdoutWrite_ = nullptr;

    std::wstring pipeName_;
    std::wstring password_;
    std::wstring keyPassphrase_;
    AuthMode authMode_ = AuthMode::Password;
    int localPort_ = 0;

    void CloseProcessHandles() {
        std::lock_guard<std::mutex> lk(processMutex_);

        if (stdoutRead_) {
            CloseHandle(stdoutRead_);
            stdoutRead_ = nullptr;
        }
        if (stdoutWrite_) {
            CloseHandle(stdoutWrite_);
            stdoutWrite_ = nullptr;
        }
        if (processThread_) {
            CloseHandle(processThread_);
            processThread_ = nullptr;
        }
        if (processHandle_) {
            CloseHandle(processHandle_);
            processHandle_ = nullptr;
        }
    }

    void WakeAskPass() {
        if (pipeName_.empty()) return;

        HANDLE h = CreateFileW(
            pipeName_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr
        );
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    void AskPassLoop() {
        while (!stop_) {
            HANDLE pipe = CreateNamedPipeW(
                pipeName_.c_str(),
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                8192, 8192, 0, nullptr
            );

            if (pipe == INVALID_HANDLE_VALUE) {
                BackendPostSessionLog(sessionId_, BackendTr(L"[错误] 无法创建认证通信管道。\r\n",
                                  L"[Error] Cannot create authentication IPC pipe.\r\n"));
                return;
            }

            BOOL connected = ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : (GetLastError() == ERROR_PIPE_CONNECTED);

            if (!connected) {
                CloseHandle(pipe);
                if (stop_) return;
                continue;
            }

            if (stop_) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                return;
            }

            DWORD promptLen = 0;
            DWORD read = 0;
            if (!ReadFile(pipe, &promptLen, sizeof(promptLen), &read, nullptr) ||
                read != sizeof(promptLen) || promptLen > 65536) {
                DisconnectNamedPipe(pipe);
                CloseHandle(pipe);
                continue;
            }

            std::wstring prompt;
            if (promptLen > 0) {
                prompt.resize(promptLen / sizeof(wchar_t));
                DWORD bytesRead = 0;
                if (!ReadFile(pipe, prompt.data(), promptLen, &bytesRead, nullptr)) {
                    prompt.clear();
                }
            }

            if (!prompt.empty()) {
                BackendPostSessionLog(sessionId_, std::wstring(BackendTr(L"[认证提示] ", L"[Auth prompt] ")) + prompt + L"\r\n");
            }

            std::wstring lower = prompt;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });

            std::wstring secret;
            if (lower.find(L"passphrase") != std::wstring::npos) {
                secret = keyPassphrase_;
            } else {
                secret = password_;
            }

            const DWORD secretLen =
                static_cast<DWORD>(secret.size() * sizeof(wchar_t));

            DWORD written = 0;
            WriteFile(pipe, &secretLen, sizeof(secretLen), &written, nullptr);
            if (secretLen > 0) {
                WriteFile(pipe, secret.data(), secretLen, &written, nullptr);
            }

            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }

    void ReaderLoop() {
        char buf[4096];

        while (!stop_) {
            DWORD got = 0;
            BOOL ok = ReadFile(stdoutRead_, buf, sizeof(buf) - 1, &got, nullptr);
            if (!ok || got == 0) break;

            buf[got] = '\0';

            int chars = MultiByteToWideChar(CP_UTF8, 0, buf, static_cast<int>(got),
                                            nullptr, 0);
            if (chars <= 0) {
                chars = MultiByteToWideChar(CP_ACP, 0, buf, static_cast<int>(got),
                                            nullptr, 0);
                if (chars > 0) {
                    std::wstring w(static_cast<size_t>(chars), L'\0');
                    MultiByteToWideChar(CP_ACP, 0, buf, static_cast<int>(got),
                                        w.data(), chars);
                    BackendPostSessionLog(sessionId_, w);
                }
            } else {
                std::wstring w(static_cast<size_t>(chars), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, buf, static_cast<int>(got),
                                    w.data(), chars);
                BackendPostSessionLog(sessionId_, w);
            }
        }
    }

    void Run(const std::wstring& rawCommand,
             AuthMode authMode,
             const std::wstring& keyPath) {
        struct FinishGuard {
            std::atomic<bool>& flag;
            ~FinishGuard() { flag.store(true); }
        } finishGuard{ finished_ };

        const std::wstring sshExe = FindSshExe();
        if (sshExe.empty()) {
            BackendPostSessionLog(sessionId_, BackendTr(
                L"[错误] 未找到 Windows OpenSSH Client (ssh.exe)。\r\n请在 Windows“可选功能”中安装 OpenSSH Client 后重试。\r\n",
                L"[Error] Windows OpenSSH Client (ssh.exe) was not found.\r\nInstall OpenSSH Client from Windows Optional Features and try again.\r\n"
            ));
            BackendPostSessionState(sessionId_, STATE_DISCONNECTED);
            return;
        }

        const DWORD pid = GetCurrentProcessId();
        const ULONGLONG tick = GetTickCount64();
        pipeName_ = L"\\\\.\\pipe\\SSHLiteAskPass_" +
                    std::to_wstring(pid) + L"_" + std::to_wstring(tick);

        askPassThread_ = std::thread([this] { AskPassLoop(); });

        std::wstring helper = BackendAppDirectory() + L"\\SSHLiteAskPass.exe";
        if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
            BackendPostSessionLog(sessionId_, BackendTr(
                L"[错误] SSHLiteAskPass.exe 不存在。\r\n请确保主程序和 SSHLiteAskPass.exe 位于同一目录。\r\n",
                L"[Error] SSHLiteAskPass.exe was not found.\r\nKeep SSHLite.exe and SSHLiteAskPass.exe in the same directory.\r\n"
            ));
            stop_ = true;
            WakeAskPass();
            if (askPassThread_.joinable()) askPassThread_.join();
            BackendPostSessionState(sessionId_, STATE_DISCONNECTED);
            return;
        }

        std::wstring persistent = EnsurePersistentGuiSession(rawCommand);
        std::wstring normalized = NormalizePortOptionAfterDestination(persistent);
        std::wstring userArgs = StripLeadingSsh(normalized);

        std::wstring prefix =
            L"-o StrictHostKeyChecking=accept-new "
            L"-o ExitOnForwardFailure=yes "
            L"-o ServerAliveInterval=30 "
            L"-o ServerAliveCountMax=3 ";

        if (authMode == AuthMode::Password) {
            prefix +=
                L"-o PreferredAuthentications=password,keyboard-interactive "
                L"-o PubkeyAuthentication=no ";
        } else {
            prefix += L"-o IdentitiesOnly=yes -i " + QuoteArg(keyPath) + L" ";
        }

        std::wstring commandLine =
            QuoteArg(sshExe) + L" " + prefix + userArgs;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
            BackendPostSessionLog(sessionId_, BackendTr(L"[错误] 无法创建 ssh 输出管道。\r\n",
                                  L"[Error] Cannot create the ssh output pipe.\r\n"));
            stop_ = true;
            WakeAskPass();
            if (askPassThread_.joinable()) askPassThread_.join();
            BackendPostSessionState(sessionId_, STATE_DISCONNECTED);
            return;
        }

        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        HANDLE nul = CreateFileW(
            L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
        );

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = writePipe;
        si.hStdError = writePipe;

        PROCESS_INFORMATION pi{};

        BOOL created = FALSE;
        {
            std::lock_guard<std::mutex> launchGuard(BackendLaunchEnvironmentMutex());

        const std::wstring oldAskPass = [] {
            wchar_t buf[32768]{};
            DWORD n = GetEnvironmentVariableW(L"SSH_ASKPASS", buf, 32768);
            return n ? std::wstring(buf, n) : std::wstring();
        }();
        const std::wstring oldRequire = [] {
            wchar_t buf[32768]{};
            DWORD n = GetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", buf, 32768);
            return n ? std::wstring(buf, n) : std::wstring();
        }();
        const std::wstring oldDisplay = [] {
            wchar_t buf[32768]{};
            DWORD n = GetEnvironmentVariableW(L"DISPLAY", buf, 32768);
            return n ? std::wstring(buf, n) : std::wstring();
        }();
        const std::wstring oldPipe = [] {
            wchar_t buf[32768]{};
            DWORD n = GetEnvironmentVariableW(L"SSHLITE_ASKPASS_PIPE", buf, 32768);
            return n ? std::wstring(buf, n) : std::wstring();
        }();

        SetEnvironmentVariableW(L"SSH_ASKPASS", helper.c_str());
        SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", L"force");
        SetEnvironmentVariableW(L"DISPLAY", L"SSHLite");
        SetEnvironmentVariableW(L"SSHLITE_ASKPASS_PIPE", pipeName_.c_str());

        std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
        mutableCmd.push_back(L'\0');

        BackendPostSessionLog(sessionId_, L"[OpenSSH] " + commandLine + L"\r\n");
        BackendPostSessionLog(sessionId_, BackendTr(L"[状态] 正在建立 SSH 连接...\r\n",
                                  L"[Status] Establishing SSH connection...\r\n"));
        BackendPostSessionState(sessionId_, STATE_CONNECTING);

        created = CreateProcessW(
            sshExe.c_str(),
            mutableCmd.data(),
            nullptr, nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            BackendAppDirectory().c_str(),
            &si, &pi
        );

        if (oldAskPass.empty()) SetEnvironmentVariableW(L"SSH_ASKPASS", nullptr);
        else SetEnvironmentVariableW(L"SSH_ASKPASS", oldAskPass.c_str());

        if (oldRequire.empty()) SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", nullptr);
        else SetEnvironmentVariableW(L"SSH_ASKPASS_REQUIRE", oldRequire.c_str());

        if (oldDisplay.empty()) SetEnvironmentVariableW(L"DISPLAY", nullptr);
        else SetEnvironmentVariableW(L"DISPLAY", oldDisplay.c_str());

        if (oldPipe.empty()) SetEnvironmentVariableW(L"SSHLITE_ASKPASS_PIPE", nullptr);
        else SetEnvironmentVariableW(L"SSHLITE_ASKPASS_PIPE", oldPipe.c_str());
        }

        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        CloseHandle(writePipe);

        if (!created) {
            const DWORD err = GetLastError();
            CloseHandle(readPipe);
            BackendPostSessionLog(sessionId_, std::wstring(BackendTr(
                    L"[错误] 无法启动 ssh.exe，Windows 错误码：",
                    L"[Error] Cannot start ssh.exe. Windows error code: ")) +
                    std::to_wstring(err) + L"\r\n");
            stop_ = true;
            WakeAskPass();
            if (askPassThread_.joinable()) askPassThread_.join();
            BackendPostSessionState(sessionId_, STATE_DISCONNECTED);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(processMutex_);
            processHandle_ = pi.hProcess;
            processThread_ = pi.hThread;
            stdoutRead_ = readPipe;
            stdoutWrite_ = nullptr;
        }

        readerThread_ = std::thread([this] { ReaderLoop(); });

        bool announced = false;
        if (localPort_ > 0) {
            for (int i = 0; i < 160 && !stop_; ++i) {
                if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;

                if (TcpPortOpen(localPort_)) {
                    BackendPostSessionLog(sessionId_, std::wstring(BackendTr(L"[状态] 本地端口 ",
                                                      L"[Status] Local port ")) +
                            std::to_wstring(localPort_) +
                            BackendTr(L" 已监听，SSH 隧道可用。\r\n",
                               L" is listening; the SSH tunnel is ready.\r\n"));
                    BackendPostSessionState(sessionId_, STATE_CONNECTED);
                    announced = true;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } else {
            for (int i = 0; i < 10 && !stop_; ++i) {
                if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
                BackendPostSessionState(sessionId_, STATE_CONNECTED);
                announced = true;
            }
        }

        if (!announced && !stop_ &&
            WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) {
            BackendPostSessionLog(sessionId_, BackendTr(
                L"[提示] ssh.exe 仍在运行，但没有检测到本地转发端口。\r\n如果命令使用了 -L，请检查端口是否正确。纯 SSH 会话可正常保持连接。\r\n",
                L"[Hint] ssh.exe is still running but no local forward port was detected.\r\nIf -L is used, check the port. SSH-only keepalive sessions are also supported.\r\n"
            ));
            BackendPostSessionState(sessionId_, STATE_CONNECTED);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        stop_ = true;
        WakeAskPass();

        if (readerThread_.joinable()) readerThread_.join();
        if (askPassThread_.joinable()) askPassThread_.join();

        if (exitCode != 0) {
            const std::wstring exitMessage =
                std::wstring(BackendTr(
                    L"[OpenSSH] ssh.exe 已退出，代码：",
                    L"[OpenSSH] ssh.exe exited with code: ")) +
                std::to_wstring(exitCode) + L"\r\n";
            BackendPostSessionLog(sessionId_, exitMessage);
        } else {
            BackendPostSessionLog(sessionId_, BackendTr(L"[状态] SSH 已断开。\r\n",
                                  L"[Status] SSH disconnected.\r\n"));
        }

        BackendPostSessionState(sessionId_, STATE_DISCONNECTED);
        CloseProcessHandles();
    }
};


SessionManager::~SessionManager() {
    StopAll();
}

int SessionManager::Start(const std::wstring& command,
                          AuthMode authMode,
                          const std::wstring& password,
                          const std::wstring& keyPath,
                          const std::wstring& keyPassphrase) {
    PruneFinished();
    const int id = nextId_++;
    auto process = std::make_shared<SshProcess>(id);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        sessions_[id] = process;
    }
    process->Start(command, authMode, password, keyPath, keyPassphrase);
    return id;
}

void SessionManager::Stop(int id) {
    std::shared_ptr<SshProcess> process;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto it = sessions_.find(id);
        if (it != sessions_.end()) process = it->second;
    }
    if (process) {
        process->Stop();
        PruneFinished();
    }
}

void SessionManager::StopAll() {
    std::vector<std::shared_ptr<SshProcess>> copy;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        copy.reserve(sessions_.size());
        for (auto& kv : sessions_) copy.push_back(kv.second);
    }
    for (auto& process : copy) {
        if (process) process->Stop();
    }
    PruneFinished();
}

void SessionManager::PruneFinished() {
    std::vector<std::shared_ptr<SshProcess>> retired;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second && it->second->IsFinished()) {
                retired.push_back(it->second);
                it = sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }
    retired.clear();
}
