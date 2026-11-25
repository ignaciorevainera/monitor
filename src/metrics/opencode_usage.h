#pragma once

#include <string>
#include "ai_usage.h"

namespace metrics {

bool FetchOpenCodeUsage(const std::wstring& token, AiUsage& out);
// Printable ASCII only: keys with control chars or non-ASCII bytes would corrupt request headers.
bool IsValidOpenCodeToken(const std::wstring& token);

}  // namespace metrics
