#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "SshCommand.h"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

std::wstring QuoteArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;

    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }

        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }

        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(c);
    }

    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::vector<std::wstring> TokenizeCommand(const std::wstring& cmd) {
    std::vector<std::wstring> out;
    std::wstring cur;
    bool quote = false;
    wchar_t quoteChar = 0;

    for (wchar_t c : cmd) {
        if ((c == L'"' || c == L'\'') && (!quote || c == quoteChar)) {
            if (!quote) {
                quote = true;
                quoteChar = c;
            } else {
                quote = false;
                quoteChar = 0;
            }
            continue;
        }

        if (!quote && iswspace(c)) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }

    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::wstring StripLeadingSsh(const std::wstring& command) {
    size_t start = 0;
    while (start < command.size() && iswspace(command[start])) ++start;

    size_t end = start;
    bool quoted = false;
    wchar_t quote = 0;

    if (end < command.size() && (command[end] == L'"' || command[end] == L'\'')) {
        quoted = true;
        quote = command[end++];
        start = end;
        while (end < command.size() && command[end] != quote) ++end;
    } else {
        while (end < command.size() && !iswspace(command[end])) ++end;
    }

    const std::wstring first = command.substr(start, end - start);
    std::wstring lower = first;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });

    const bool isSsh =
        lower == L"ssh" ||
        lower == L"ssh.exe" ||
        (lower.size() >= 7 && lower.substr(lower.size() - 7) == L"ssh.exe");

    if (!isSsh) return command;

    if (quoted && end < command.size() && command[end] == quote) ++end;
    while (end < command.size() && iswspace(command[end])) ++end;
    return command.substr(end);
}

bool TryExtractFirstLocalPort(const std::wstring& command, int& port) {
    port = 0;
    const auto tokens = TokenizeCommand(command);

    auto parseL = [&](const std::wstring& raw) -> bool {
        std::vector<std::wstring> parts;
        std::wstring cur;
        for (wchar_t c : raw) {
            if (c == L':') {
                parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        parts.push_back(cur);

        try {
            if (parts.size() == 3) {
                port = std::stoi(parts[0]);
            } else if (parts.size() == 4) {
                port = std::stoi(parts[1]);
            } else {
                return false;
            }
        } catch (...) {
            return false;
        }
        return port > 0 && port <= 65535;
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::wstring& t = tokens[i];

        if (t == L"-L" && i + 1 < tokens.size()) {
            if (parseL(tokens[i + 1])) return true;
        }

        if (t.size() > 2 && t.rfind(L"-L", 0) == 0) {
            if (parseL(t.substr(2))) return true;
        }
    }

    return false;
}

std::vector<std::wstring> SplitForwardSpec(const std::wstring& raw) {
    // Split on ':' except inside [IPv6] brackets.
    std::vector<std::wstring> parts;
    std::wstring cur;
    int bracketDepth = 0;

    for (wchar_t c : raw) {
        if (c == L'[') ++bracketDepth;
        else if (c == L']' && bracketDepth > 0) --bracketDepth;

        if (c == L':' && bracketDepth == 0) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

std::wstring StripIpv6Brackets(std::wstring value) {
    if (value.size() >= 2 && value.front() == L'[' && value.back() == L']') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool ParseLocalForwardSpec(const std::wstring& raw, LocalForwardInfo& out) {
    const auto parts = SplitForwardSpec(raw);

    try {
        if (parts.size() == 3) {
            // -L localPort:remoteHost:remotePort
            out.bindHost = L"127.0.0.1";
            out.localPort = std::stoi(parts[0]);
            out.remoteHost = StripIpv6Brackets(parts[1]);
            out.remotePort = std::stoi(parts[2]);
        } else if (parts.size() == 4) {
            // -L bindHost:localPort:remoteHost:remotePort
            out.bindHost = StripIpv6Brackets(parts[0]);
            if (out.bindHost.empty()) out.bindHost = L"127.0.0.1";
            out.localPort = std::stoi(parts[1]);
            out.remoteHost = StripIpv6Brackets(parts[2]);
            out.remotePort = std::stoi(parts[3]);
        } else {
            return false;
        }
    } catch (...) {
        return false;
    }

    if (out.localPort < 1 || out.localPort > 65535 ||
        out.remotePort < 1 || out.remotePort > 65535 ||
        out.remoteHost.empty()) {
        return false;
    }

    return true;
}

std::vector<LocalForwardInfo> ExtractLocalForwards(const std::wstring& command) {
    std::vector<LocalForwardInfo> result;
    const auto tokens = TokenizeCommand(command);

    auto append = [&](const std::wstring& raw) {
        LocalForwardInfo info;
        if (!ParseLocalForwardSpec(raw, info)) return;

        const auto duplicate = std::find_if(
            result.begin(), result.end(),
            [&](const LocalForwardInfo& item) {
                return item.bindHost == info.bindHost &&
                       item.localPort == info.localPort &&
                       item.remoteHost == info.remoteHost &&
                       item.remotePort == info.remotePort;
            }
        );

        if (duplicate == result.end()) {
            result.push_back(std::move(info));
        }
    };

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];

        if (token == L"-L" && i + 1 < tokens.size()) {
            append(tokens[++i]);
        } else if (token.size() > 2 && token.rfind(L"-L", 0) == 0) {
            append(token.substr(2));
        }
    }

    return result;
}

std::vector<int> ExtractLocalPorts(const std::wstring& command) {
    std::vector<int> ports;
    for (const auto& forward : ExtractLocalForwards(command)) {
        if (std::find(ports.begin(), ports.end(), forward.localPort) == ports.end()) {
            ports.push_back(forward.localPort);
        }
    }
    return ports;
}


bool OptionConsumesNextToken(const std::wstring& option) {
    return option == L"-L" || option == L"-R" || option == L"-D" ||
           option == L"-p" || option == L"-l" || option == L"-i" ||
           option == L"-J" || option == L"-F" || option == L"-o" ||
           option == L"-b" || option == L"-W" || option == L"-E" ||
           option == L"-S" || option == L"-c" || option == L"-m" ||
           option == L"-Q";
}

std::wstring ExtractDestination(const std::wstring& command) {
    const auto tokens = TokenizeCommand(command);
    if (tokens.empty()) return L"";

    size_t i = 0;
    std::wstring first = tokens[0];
    std::transform(first.begin(), first.end(), first.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (first == L"ssh" || first == L"ssh.exe" ||
        (first.size() >= 7 && first.substr(first.size() - 7) == L"ssh.exe")) {
        i = 1;
    }

    for (; i < tokens.size(); ++i) {
        const std::wstring& t = tokens[i];
        if (t.empty()) continue;

        if (t[0] == L'-') {
            if (OptionConsumesNextToken(t) && i + 1 < tokens.size()) ++i;
            continue;
        }

        return t;
    }
    return L"";
}

int ExtractSshPort(const std::wstring& command) {
    const auto tokens = TokenizeCommand(command);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& t = tokens[i];
        try {
            if (t == L"-p" && i + 1 < tokens.size()) {
                int p = std::stoi(tokens[i + 1]);
                return (p >= 1 && p <= 65535) ? p : 22;
            }
            if (t.size() > 2 && t.rfind(L"-p", 0) == 0) {
                int p = std::stoi(t.substr(2));
                return (p >= 1 && p <= 65535) ? p : 22;
            }
        } catch (...) {
            return 22;
        }
    }
    return 22;
}

bool HasNoCommandFlag(const std::wstring& command) {
    const auto tokens = TokenizeCommand(command);
    for (const auto& t : tokens) {
        if (t == L"-N") return true;
        if (t.size() > 2 && t[0] == L'-' && t.find(L'N', 1) != std::wstring::npos)
            return true;
    }
    return false;
}

std::wstring EnsurePersistentGuiSession(const std::wstring& command) {
    // SSHLite has no embedded terminal. If the command is a plain
    // "ssh user@host -p 22" style connection with no remote command,
    // keep it alive as an SSH-only session by injecting -N.
    if (HasNoCommandFlag(command)) return command;

    auto tokens = TokenizeCommand(command);
    if (tokens.empty()) return command;

    size_t sshOffset = 0;
    std::wstring first = tokens[0];
    std::transform(first.begin(), first.end(), first.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (first == L"ssh" || first == L"ssh.exe" ||
        (first.size() >= 7 && first.substr(first.size() - 7) == L"ssh.exe")) {
        sshOffset = 1;
    }

    size_t destination = static_cast<size_t>(-1);
    for (size_t i = sshOffset; i < tokens.size(); ++i) {
        if (tokens[i].empty()) continue;
        if (tokens[i][0] == L'-') {
            if (OptionConsumesNextToken(tokens[i]) && i + 1 < tokens.size()) ++i;
            continue;
        }
        destination = i;
        break;
    }
    if (destination == static_cast<size_t>(-1)) return command;

    // Anything after the destination (except a cloud-provider "-p port")
    // is treated as an explicit remote command, so don't alter semantics.
    for (size_t i = destination + 1; i < tokens.size(); ++i) {
        if (tokens[i] == L"-p" && i + 1 < tokens.size()) {
            ++i;
            continue;
        }
        return command;
    }

    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(destination), L"-N");

    std::wstring rebuilt;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) rebuilt += L" ";
        rebuilt += QuoteArg(tokens[i]);
    }
    return rebuilt;
}

std::wstring NormalizePortOptionAfterDestination(const std::wstring& command) {
    // A common cloud-provider copy format is:
    // ssh user@host -p 12345
    // Canonical OpenSSH syntax places -p before destination.
    // Move only this one well-defined option, leaving every other argument untouched.
    auto tokens = TokenizeCommand(command);
    if (tokens.size() < 3) return command;

    size_t sshOffset = 0;
    std::wstring first = tokens[0];
    std::transform(first.begin(), first.end(), first.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (first == L"ssh" || first == L"ssh.exe" ||
        (first.size() >= 7 && first.substr(first.size() - 7) == L"ssh.exe")) {
        sshOffset = 1;
    }

    size_t destination = static_cast<size_t>(-1);
    for (size_t i = sshOffset; i < tokens.size(); ++i) {
        if (!tokens[i].empty() && tokens[i][0] != L'-') {
            // Skip parameter values belonging to known options before destination.
            if (i > sshOffset) {
                const std::wstring& prev = tokens[i - 1];
                if (prev == L"-L" || prev == L"-p" || prev == L"-l" ||
                    prev == L"-i" || prev == L"-J" || prev == L"-F" ||
                    prev == L"-o" || prev == L"-b" || prev == L"-D" ||
                    prev == L"-R" || prev == L"-W") {
                    continue;
                }
            }
            destination = i;
            break;
        }
    }

    if (destination == static_cast<size_t>(-1)) return command;

    size_t pIndex = static_cast<size_t>(-1);
    for (size_t i = destination + 1; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == L"-p") {
            pIndex = i;
            break;
        }
    }

    if (pIndex == static_cast<size_t>(-1)) return command;

    const std::wstring pValue = tokens[pIndex + 1];
    tokens.erase(tokens.begin() + static_cast<ptrdiff_t>(pIndex),
                 tokens.begin() + static_cast<ptrdiff_t>(pIndex + 2));

    destination = static_cast<size_t>(-1);
    for (size_t i = sshOffset; i < tokens.size(); ++i) {
        if (!tokens[i].empty() && tokens[i][0] != L'-') {
            if (i > sshOffset) {
                const std::wstring& prev = tokens[i - 1];
                if (prev == L"-L" || prev == L"-p" || prev == L"-l" ||
                    prev == L"-i" || prev == L"-J" || prev == L"-F" ||
                    prev == L"-o" || prev == L"-b" || prev == L"-D" ||
                    prev == L"-R" || prev == L"-W") {
                    continue;
                }
            }
            destination = i;
            break;
        }
    }

    if (destination == static_cast<size_t>(-1)) return command;

    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(destination), pValue);
    tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(destination), L"-p");

    std::wstring rebuilt;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) rebuilt += L" ";
        rebuilt += QuoteArg(tokens[i]);
    }
    return rebuilt;
}

std::wstring FindSshExe() {
    static std::once_flag once;
    static std::wstring cached;

    std::call_once(once, [] {
        wchar_t found[MAX_PATH]{};
        DWORD n = SearchPathW(nullptr, L"ssh.exe", nullptr, MAX_PATH, found, nullptr);
        if (n > 0 && n < MAX_PATH) {
            cached = found;
            return;
        }

        wchar_t winDir[MAX_PATH]{};
        if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
            std::wstring path = std::wstring(winDir) + L"\\System32\\OpenSSH\\ssh.exe";
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                cached = path;
            }
        }
    });

    return cached;
}

bool TcpPortOpen(int port) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);

    int rc = connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(s, &writeSet);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            rc = select(0, nullptr, &writeSet, nullptr, &tv);
        } else {
            rc = 0;
        }
    } else {
        rc = 1;
    }

    closesocket(s);
    WSACleanup();
    return rc > 0;
}

