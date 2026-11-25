#pragma once

#include <cstdint>
#include <string>

namespace metrics {

enum class AiError : uint8_t { None = 0, NoToken, Unauthorized, Forbidden, Network, Parse };

struct AiMeter {
    int percent = -1;
    bool limited = false;
    std::string resetsAt;
};

struct AiUsage {
    AiMeter meter[3];
    bool ok = false;
    AiError error = AiError::NoToken;
};

bool ParseAiUsage(const std::string& json, AiUsage& out);
std::wstring FormatAiUsage(const AiUsage& ai);
std::wstring FormatAiReset(const std::string& iso);
int AiSeverity(const AiUsage& ai);

}  // namespace metrics
