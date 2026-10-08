#pragma once

#include "AppTypes.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

class SshProcess;

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    int Start(const std::wstring& command,
              AuthMode authMode,
              const std::wstring& password,
              const std::wstring& keyPath,
              const std::wstring& keyPassphrase);

    void Stop(int id);
    void StopAll();
    void PruneFinished();

private:
    std::atomic<int> nextId_{1};
    std::mutex mutex_;
    std::map<int, std::shared_ptr<SshProcess>> sessions_;
};
