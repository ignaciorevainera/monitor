#pragma once

#include <windows.h>
#include <string>
#include <vector>

bool ProtectToken(const std::wstring& token, std::vector<BYTE>& out);
bool UnprotectToken(const std::vector<BYTE>& in, std::wstring& token);
bool SaveOpenCodeToken(const std::wstring& token);
bool LoadOpenCodeToken(std::wstring& token);
bool HasOpenCodeToken();
void ClearOpenCodeToken();
