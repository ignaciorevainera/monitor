#pragma once

#include <string>
#include "ai_usage.h"

namespace metrics {

bool FetchOpenCodeUsage(const std::wstring& token, AiUsage& out);

}  // namespace metrics
