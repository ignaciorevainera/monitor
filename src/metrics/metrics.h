#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ai_usage.h"

namespace metrics {

enum class Id : int { CpuUsage = 0, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp, DiskSpace, OpenCodeUsage };
inline constexpr int kCount = 9;

enum class Where : uint8_t { Bar = 0, Tray = 1, Hidden = 2 };
enum class DiskDisplay : uint8_t { FreeGb = 0, FreePercent = 1, UsedPercent = 2 };

struct Snapshot {
    double val[kCount]{};
    bool ok[kCount]{};
    double diskFreeGb{};
    double diskTotalGb{};
    AiUsage ai;
};

struct Config {
    Where where[kCount];
};

inline constexpr Config kDefaultConfig = { {
    Where::Bar, Where::Bar, Where::Bar, Where::Bar, Where::Bar, Where::Bar, Where::Hidden, Where::Bar, Where::Hidden
} };

inline constexpr uint32_t kTrayUidBase = 0x2000;

std::wstring FormatPercent(double v, bool ok);
std::wstring FormatTemp(double c, bool ok);
std::wstring FormatGb(double gb, bool ok);
int SeverityFor(int metric, double c);
int DiskSeverity(double freePercent);
bool ParseConfig(const uint8_t* data, size_t bytes, Config& out);
bool SameValues(const Snapshot& a, const Snapshot& b);

bool TakeSnapshot(Snapshot& out);
void SetSamplerTarget(void* hwnd, unsigned int msg);
void SetSamplerPaused(bool paused);
void SetSamplerInterval(unsigned long ms);
void StopSampler();
unsigned long __stdcall MetricsWorker(void* unused);

bool AiSnapshot(AiUsage& out);
void StartAiSampler();
void StopAiSampler();
void RequestAiRefresh();
void ClearAiUsage();

}  // namespace metrics
