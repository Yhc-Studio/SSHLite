
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    GetEnvironmentVariableW(name, out.data(), n);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring pipeName = GetEnv(L"SSHLITE_ASKPASS_PIPE");
    if (pipeName.empty()) return 2;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring prompt;
    if (argv && argc >= 2) prompt = argv[1];
    if (argv) LocalFree(argv);

    if (!WaitNamedPipeW(pipeName.c_str(), 10000)) return 3;

    HANDLE pipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr
    );
    if (pipe == INVALID_HANDLE_VALUE) return 4;

    DWORD written = 0;
    const DWORD promptLen =
        static_cast<DWORD>(prompt.size() * sizeof(wchar_t));

    if (!WriteFile(pipe, &promptLen, sizeof(promptLen), &written, nullptr)) {
        CloseHandle(pipe);
        return 5;
    }

    if (promptLen > 0) {
        if (!WriteFile(pipe, prompt.data(), promptLen, &written, nullptr)) {
            CloseHandle(pipe);
            return 6;
        }
    }

    DWORD secretLen = 0;
    DWORD read = 0;
    if (!ReadFile(pipe, &secretLen, sizeof(secretLen), &read, nullptr) ||
        read != sizeof(secretLen) ||
        secretLen > 1024 * 1024) {
        CloseHandle(pipe);
        return 7;
    }

    std::wstring secret;
    if (secretLen > 0) {
        secret.resize(secretLen / sizeof(wchar_t));
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, secret.data(), secretLen, &bytesRead, nullptr)) {
            CloseHandle(pipe);
            return 8;
        }
    }

    CloseHandle(pipe);

    int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0,
        secret.c_str(), static_cast<int>(secret.size()),
        nullptr, 0, nullptr, nullptr
    );

    std::string utf8;
    if (utf8Len > 0) {
        utf8.resize(static_cast<size_t>(utf8Len));
        WideCharToMultiByte(
            CP_UTF8, 0,
            secret.c_str(), static_cast<int>(secret.size()),
            utf8.data(), utf8Len, nullptr, nullptr
        );
    }
    utf8.push_back('\n');

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != INVALID_HANDLE_VALUE && out != nullptr) {
        DWORD outWritten = 0;
        WriteFile(out, utf8.data(),
                  static_cast<DWORD>(utf8.size()),
                  &outWritten, nullptr);
    }

    SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));
    if (!utf8.empty()) SecureZeroMemory(utf8.data(), utf8.size());

    return 0;
}
