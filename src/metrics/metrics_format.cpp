#include "metrics.h"

#include <algorithm>
#include <cmath>
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

std::wstring FormatTemp(double c, bool ok) {
    if (!ok) return L"--";
    int deg = static_cast<int>(std::lround(c));
    return std::to_wstring(deg) + L"\u00B0";
}

std::wstring FormatGb(double gb, bool ok) {
    if (!ok) return L"--";
    long long t = std::llround(std::max(0.0, gb) * 10.0);
    if (t > 1000) return std::to_wstring(t / 10) + L" GB";
    return std::to_wstring(t / 10) + L"." + std::to_wstring(t % 10) + L" GB";
}

int DiskSeverity(double freePercent) {
    if (freePercent >= 20.0) return 0;
    if (freePercent >= 10.0) return 1;
    return 2;
}

int SeverityFor(int metric, double c) {
    double warn = 0.0;
    double crit = 0.0;
    switch (static_cast<Id>(metric)) {
        case Id::CpuTemp:
        case Id::GpuTemp:
            warn = 80.0;
            crit = 90.0;
            break;
        case Id::SsdTemp:
            warn = 65.0;
            crit = 75.0;
            break;
        default:
            return 0;
    }
    if (c >= crit) return 2;
    if (c >= warn) return 1;
    return 0;
}

bool ParseConfig(const uint8_t* data, size_t bytes, Config& out) {
    if (!data || bytes < 7) {
        out = kDefaultConfig;
        return false;
    }
    Config parsed = kDefaultConfig;
    const int n = static_cast<int>(std::min(bytes, static_cast<size_t>(kCount)));
    for (int i = 0; i < n; i++) {
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
    constexpr int disk = static_cast<int>(Id::DiskSpace);
    if (a.ok[disk]) {
        if (std::llround(a.diskFreeGb * 10.0) != std::llround(b.diskFreeGb * 10.0)) return false;
        if (std::llround(a.diskTotalGb * 10.0) != std::llround(b.diskTotalGb * 10.0)) return false;
    }
    return true;
}

}  // namespace metrics
