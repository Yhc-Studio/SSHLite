#pragma once

#include <string>
#include <vector>

inline constexpr int STATE_DISCONNECTED = 0;
inline constexpr int STATE_CONNECTED    = 1;
inline constexpr int STATE_CONNECTING   = 2;

enum class AuthMode {
    Password = 0,
    PrivateKey = 1,
};

struct SavedEntry {
    std::wstring name;
    std::wstring command;
    AuthMode authMode = AuthMode::Password;
    std::wstring keyPath;
    // Tunnel Favorites may intentionally persist this value as plaintext.
    // Server History never writes it.
    std::wstring passwordPlain;
    bool autoBrowser = true;
};

struct LocalForwardInfo {
    std::wstring bindHost = L"127.0.0.1";
    int localPort = 0;
    std::wstring remoteHost;
    int remotePort = 0;

    [[nodiscard]] std::wstring BrowserHost() const {
        if (bindHost.empty() || bindHost == L"*" ||
            bindHost == L"0.0.0.0" || bindHost == L"::") {
            return L"127.0.0.1";
        }
        if (bindHost == L"localhost") {
            return L"127.0.0.1";
        }
        return bindHost;
    }

    [[nodiscard]] std::wstring BrowserUrl() const {
        std::wstring host = BrowserHost();
        if (host.find(L':') != std::wstring::npos &&
            !(host.size() >= 2 && host.front() == L'[' && host.back() == L']')) {
            host = L"[" + host + L"]";
        }
        return L"http://" + host + L":" + std::to_wstring(localPort);
    }

    [[nodiscard]] std::wstring DisplayText() const {
        const std::wstring local = bindHost.empty() ? L"127.0.0.1" : bindHost;
        return local + L":" + std::to_wstring(localPort) +
               L"  \u2192  " + remoteHost + L":" + std::to_wstring(remotePort);
    }
};

struct SessionEvent {
    int id = 0;
    int state = STATE_DISCONNECTED;
    std::wstring text;
};

struct SessionView {
    int id = 0;
    int state = STATE_CONNECTING;
    std::wstring command;
    std::wstring destination;
    int sshPort = 22;
    std::vector<int> localPorts;
};
