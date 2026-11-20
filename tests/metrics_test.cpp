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
    for (int i = 0; i < 7; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

    assert(!ParseConfig(nullptr, 7, cfg));
    for (int i = 0; i < 7; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

    const uint8_t garbage[7] = { 0, 1, 2, 3, 0, 1, 2 };
    assert(!ParseConfig(garbage, sizeof(garbage), cfg));
    for (int i = 0; i < 7; i++) assert(cfg.where[i] == kDefaultConfig.where[i]);

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

    static_assert(kTrayUidBase > 0x1000, "kTrayUidBase must stay above StableId range");
    assert(kTrayUidBase > 0x1000);
    for (uint32_t id = 0; id < 7; id++) {
        uint32_t uid = kTrayUidBase + id;
        assert(uid != 1 && uid != 2);
        assert(uid >= 0x2000);
    }

    std::puts("metrics_test: all passed");
    return 0;
}
