#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace metrics {

enum class Id : int { CpuUsage = 0, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp };
inline constexpr int kCount = 7;

enum class Where : uint8_t { Bar = 0, Tray = 1, Hidden = 2 };

struct Snapshot {
    double val[kCount]{};
    bool ok[kCount]{};
};

struct Config {
    Where where[kCount];
};

inline constexpr Config kDefaultConfig = { {
    Where::Bar, Where::Hidden, Where::Bar, Where::Bar, Where::Hidden, Where::Bar, Where::Hidden
} };

inline constexpr uint32_t kTrayUidBase = 0x2000;

std::wstring FormatPercent(double v, bool ok);
bool ParseConfig(const uint8_t* data, size_t bytes, Config& out);
bool SameValues(const Snapshot& a, const Snapshot& b);

}  // namespace metrics
