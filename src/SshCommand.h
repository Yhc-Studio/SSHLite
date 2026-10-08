#pragma once

#include "AppTypes.h"

#include <string>
#include <vector>

std::wstring QuoteArg(const std::wstring& arg);
std::vector<std::wstring> TokenizeCommand(const std::wstring& cmd);
std::wstring StripLeadingSsh(const std::wstring& command);
bool TryExtractFirstLocalPort(const std::wstring& command, int& port);
std::vector<std::wstring> SplitForwardSpec(const std::wstring& raw);
std::wstring StripIpv6Brackets(std::wstring value);
bool ParseLocalForwardSpec(const std::wstring& raw, LocalForwardInfo& out);
std::vector<LocalForwardInfo> ExtractLocalForwards(const std::wstring& command);
std::vector<int> ExtractLocalPorts(const std::wstring& command);
bool OptionConsumesNextToken(const std::wstring& option);
std::wstring ExtractDestination(const std::wstring& command);
int ExtractSshPort(const std::wstring& command);
bool HasNoCommandFlag(const std::wstring& command);
std::wstring EnsurePersistentGuiSession(const std::wstring& command);
std::wstring NormalizePortOptionAfterDestination(const std::wstring& command);
std::wstring FindSshExe();
bool TcpPortOpen(int port);
