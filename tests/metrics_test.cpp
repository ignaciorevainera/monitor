#include "../src/metrics/metrics.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    using namespace metrics;

    assert(FormatPercent(12.4, true) == L"12%");
    assert(FormatPercent(99.6, true) == L"100%");
    assert(FormatPercent(0.0, true) == L"0%");
    assert(FormatPercent(-3.0, true) == L"0%");
    assert(FormatPercent(120.0, true) == L"100%");
    assert(FormatPercent(42.0, false) == L"--");

    assert(FormatTemp(54.4, true) == L"54\u00B0");
    assert(FormatTemp(54.6, true) == L"55\u00B0");
    assert(FormatTemp(0.0, true) == L"0\u00B0");
    assert(FormatTemp(42.0, false) == L"--");

    assert(SeverityFor(static_cast<int>(Id::CpuTemp), 79.9) == 0);
    assert(SeverityFor(static_cast<int>(Id::CpuTemp), 80.0) == 1);
    assert(SeverityFor(static_cast<int>(Id::CpuTemp), 89.9) == 1);
    assert(SeverityFor(static_cast<int>(Id::CpuTemp), 90.0) == 2);

    assert(SeverityFor(static_cast<int>(Id::GpuTemp), 79.9) == 0);
    assert(SeverityFor(static_cast<int>(Id::GpuTemp), 80.0) == 1);
    assert(SeverityFor(static_cast<int>(Id::GpuTemp), 89.9) == 1);
    assert(SeverityFor(static_cast<int>(Id::GpuTemp), 90.0) == 2);

    assert(SeverityFor(static_cast<int>(Id::SsdTemp), 64.9) == 0);
    assert(SeverityFor(static_cast<int>(Id::SsdTemp), 65.0) == 1);
    assert(SeverityFor(static_cast<int>(Id::SsdTemp), 74.9) == 1);
    assert(SeverityFor(static_cast<int>(Id::SsdTemp), 75.0) == 2);

    assert(SeverityFor(static_cast<int>(Id::CpuUsage), 100.0) == 0);
    assert(SeverityFor(static_cast<int>(Id::RamUsage), 100.0) == 0);
    assert(SeverityFor(static_cast<int>(Id::GpuUsage), 100.0) == 0);
    assert(SeverityFor(static_cast<int>(Id::Vram), 100.0) == 0);
    assert(SeverityFor(-1, 100.0) == 0);
    assert(SeverityFor(99, 100.0) == 0);

    assert(kDefaultConfig.where[static_cast<int>(Id::CpuUsage)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::CpuTemp)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::RamUsage)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::GpuUsage)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::GpuTemp)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::Vram)] == Where::Bar);
    assert(kDefaultConfig.where[static_cast<int>(Id::SsdTemp)] == Where::Hidden);

    Config cfg{};
    const uint8_t valid[7] = {
        static_cast<uint8_t>(Where::Tray),
        static_cast<uint8_t>(Where::Hidden),
        static_cast<uint8_t>(Where::Bar),
        static_cast<uint8_t>(Where::Tray),
        static_cast<uint8_t>(Where::Bar),
        static_cast<uint8_t>(Where::Hidden),
        static_cast<uint8_t>(Where::Tray),
    };
    assert(ParseConfig(valid, sizeof(valid), cfg));
    assert(cfg.where[0] == Where::Tray);
    assert(cfg.where[1] == Where::Hidden);
    assert(cfg.where[2] == Where::Bar);
    assert(cfg.where[6] == Where::Tray);

    const uint8_t shortBuf[3] = { 0, 1, 2 };
    assert(!ParseConfig(shortBuf, sizeof(shortBuf), cfg));
    for (int i = 0; i < kCount; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

    assert(!ParseConfig(nullptr, 7, cfg));
    for (int i = 0; i < kCount; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

    const uint8_t garbage[7] = { 0, 1, 2, 3, 0, 1, 2 };
    assert(!ParseConfig(garbage, sizeof(garbage), cfg));
    for (int i = 0; i < kCount; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

    Snapshot a{};
    Snapshot b{};
    assert(SameValues(a, b));
    a.ok[0] = true;
    a.val[0] = 25.0;
    assert(!SameValues(a, b));
    b.ok[0] = true;
    b.val[0] = 25.0;
    assert(SameValues(a, b));
    b.val[0] = 26.0;
    assert(!SameValues(a, b));

    assert(FormatGb(62.44, true) == L"62.4 GB");
    assert(FormatGb(0.04, true) == L"0.0 GB");
    assert(FormatGb(99.96, true) == L"100.0 GB");
    assert(FormatGb(128.0, true) == L"128 GB");
    assert(FormatGb(1024.0, true) == L"1024 GB");
    assert(FormatGb(-3.0, true) == L"0.0 GB");
    assert(FormatGb(1.0, false) == L"--");

    assert(DiskSeverity(20.0) == 0);
    assert(DiskSeverity(19.9) == 1);
    assert(DiskSeverity(10.0) == 1);
    assert(DiskSeverity(9.9) == 2);
    assert(DiskSeverity(0.0) == 2);

    assert(kDefaultConfig.where[static_cast<int>(Id::DiskSpace)] == Where::Bar);

    Config legacy{};
    const uint8_t old7[7] = { 1, 1, 1, 1, 1, 1, 2 };
    assert(ParseConfig(old7, sizeof(old7), legacy));
    assert(legacy.where[0] == Where::Tray);
    assert(legacy.where[6] == Where::Hidden);
    assert(legacy.where[static_cast<int>(Id::DiskSpace)] == Where::Bar);

    Snapshot d1{}, d2{};
    d1.ok[static_cast<int>(Id::DiskSpace)] = true;
    d1.val[static_cast<int>(Id::DiskSpace)] = 24.0;
    d1.diskFreeGb = 62.4;
    d1.diskTotalGb = 256.0;
    d2 = d1;
    assert(SameValues(d1, d2));
    d2.diskFreeGb = 62.3;
    assert(!SameValues(d1, d2));
    Snapshot d3 = d1;
    d3.diskFreeGb = d1.diskFreeGb + 0.02;
    d3.val[static_cast<int>(Id::DiskSpace)] = 24.02;  // sub-umbral: ni el % entero ni las décimas cambian
    assert(SameValues(d1, d3));

    Config cfg8{};
    const uint8_t valid8[8] = { 0, 1, 2, 0, 1, 2, 1, 2 };
    assert(ParseConfig(valid8, sizeof(valid8), cfg8));
    assert(cfg8.where[static_cast<int>(Id::DiskSpace)] == Where::Hidden);
    const uint8_t bad8[8] = { 0, 1, 2, 0, 1, 2, 1, 3 };
    assert(!ParseConfig(bad8, sizeof(bad8), cfg8));
    for (int i = 0; i < kCount; i++) assert(cfg8.where[i] == kDefaultConfig.where[i]);

    for (int i = 0; i < kCount; i++) {
        uint32_t uid = kTrayUidBase + static_cast<uint32_t>(i);
        assert(uid != 1 && uid != 2 && uid >= 0x2000);
        static_assert((kTrayUidBase & 0x100) == 0, "kTrayUidBase..+kCount must keep bit 8 clear");
    }

    assert(kDefaultConfig.where[static_cast<int>(Id::OpenCodeUsage)] == Where::Hidden);

    Config old8{};
    const uint8_t cfg8_legacy[8] = { 1, 1, 1, 1, 1, 1, 2, 0 };
    assert(ParseConfig(cfg8_legacy, sizeof(cfg8_legacy), old8));
    assert(old8.where[7] == Where::Bar);
    assert(old8.where[static_cast<int>(Id::OpenCodeUsage)] == Where::Hidden);

    Snapshot a1{}, b1{};
    a1.ai.ok = true;
    a1.ai.meter[0] = { 60, false, "2026-11-21T15:00:00.000Z" };
    b1 = a1;
    assert(SameValues(a1, b1));
    b1.ai.meter[0].percent = 61;
    assert(!SameValues(a1, b1));

    std::puts("metrics_test: all passed");
    return 0;
}
