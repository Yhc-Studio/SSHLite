#pragma once

#include <mutex>
#include <string>

void BackendPostSessionLog(int sessionId, const std::wstring& text);
void BackendPostSessionState(int sessionId, int state);
const wchar_t* BackendTr(const wchar_t* zh, const wchar_t* en);
std::wstring BackendAppDirectory();
std::mutex& BackendLaunchEnvironmentMutex();
