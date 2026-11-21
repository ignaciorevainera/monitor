#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

struct TempFileData {
    uint32_t magic;
    uint64_t tick;
    double tempC;
};

constexpr uint32_t kTempFileMagic = 0x4D4F4E54;

inline bool ParseTempFile(const void* buf, size_t len, TempFileData& out) {
    if (!buf || len < sizeof(TempFileData)) return false;
    TempFileData tmp{};
    std::memcpy(&tmp, buf, sizeof(tmp));
    if (tmp.magic != kTempFileMagic) return false;
    out = tmp;
    return true;
}

inline bool FreshTemp(const TempFileData& d, uint64_t now, uint64_t maxAgeMs = 10000) {
    if (d.magic != kTempFileMagic || d.tick == 0 || now < d.tick) return false;
    return (now - d.tick) < maxAgeMs;
}
