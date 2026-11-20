#include "metrics.h"

#include <cstdint>
#include <string>

namespace metrics {

std::wstring FormatPercent(double v, bool ok) {
    if (!ok) return L"--";
    if (v < 0.0) v = 0.0;
    if (v > 100.0) v = 100.0;
    int pct = static_cast<int>(v + 0.5);
    return std::to_wstring(pct) + L"%";
}

bool ParseConfig(const uint8_t* data, size_t bytes, Config& out) {
    if (!data || bytes < static_cast<size_t>(kCount)) {
        out = kDefaultConfig;
        return false;
    }
    Config parsed{};
    for (int i = 0; i < kCount; i++) {
        if (data[i] > static_cast<uint8_t>(Where::Hidden)) {
            out = kDefaultConfig;
            return false;
        }
        parsed.where[i] = static_cast<Where>(data[i]);
    }
    out = parsed;
    return true;
}

bool SameValues(const Snapshot& a, const Snapshot& b) {
    for (int i = 0; i < kCount; i++) {
        if (a.ok[i] != b.ok[i]) return false;
        if (a.ok[i] && a.val[i] != b.val[i]) return false;
    }
    return true;
}

}  // namespace metrics
