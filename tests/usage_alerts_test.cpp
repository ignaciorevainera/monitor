#include "../src/metrics/usage_alerts.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {

bool EvalUsageSample(double value, double threshold, bool ok, bool& hasSeenOk, bool& latched) {
    if (!ok || value < 0.0) return false;
    if (!hasSeenOk) {
        hasSeenOk = true;
        if (metrics::UsageFirstSampleHot(value, threshold)) latched = true;
        return false;
    }
    if (metrics::UsageResetLatch(value, threshold)) {
        latched = false;
        return false;
    }
    if (metrics::UsageShouldNotify(value, threshold, latched)) {
        latched = true;
        return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace metrics;
    constexpr double kThreshold = 90.0;

    // Pure policy primitives
    assert(!UsageAbove(89.9, kThreshold));
    assert(UsageAbove(90.0, kThreshold));
    assert(UsageAbove(92.0, kThreshold));
    assert(!UsageAbove(-1.0, kThreshold));
    assert(!UsageAbove(std::numeric_limits<double>::quiet_NaN(), kThreshold));

    assert(!UsageShouldNotify(88.0, kThreshold, false));
    assert(!UsageShouldNotify(88.0, kThreshold, true));
    assert(UsageShouldNotify(91.0, kThreshold, false));
    assert(!UsageShouldNotify(91.0, kThreshold, true));
    assert(!UsageShouldNotify(-5.0, kThreshold, false));
    assert(!UsageShouldNotify(std::numeric_limits<double>::quiet_NaN(), kThreshold, false));

    // Reset hysteresis: threshold - 5 = 85.0
    assert(UsageResetLatch(84.0, kThreshold));
    assert(!UsageResetLatch(85.0, kThreshold));
    assert(!UsageResetLatch(88.0, kThreshold));
    assert(!UsageResetLatch(-1.0, kThreshold));
    assert(!UsageResetLatch(std::numeric_limits<double>::quiet_NaN(), kThreshold));

    assert(!UsageFirstSampleHot(89.0, kThreshold));
    assert(UsageFirstSampleHot(90.0, kThreshold));
    assert(UsageFirstSampleHot(92.0, kThreshold));
    assert(!UsageFirstSampleHot(-1.0, kThreshold));

    // 1. Constant 92 with latch initialized (first hot sample initializes latch silently -> 0 balloons)
    {
        bool seen = false;
        bool latched = false;
        int balloons = 0;
        for (int i = 0; i < 5; ++i) {
            if (EvalUsageSample(92.0, kThreshold, true, seen, latched)) ++balloons;
        }
        assert(seen);
        assert(latched);
        assert(balloons == 0);
    }

    // Constant 92 with latch already initialized -> 0 balloons
    {
        bool seen = true;
        bool latched = true;
        int balloons = 0;
        for (int i = 0; i < 5; ++i) {
            if (EvalUsageSample(92.0, kThreshold, true, seen, latched)) ++balloons;
        }
        assert(latched);
        assert(balloons == 0);
    }

    // 2. 88 -> 91 yields 1 balloon, 92 stays latched, drop to 84 resets (85 does not), rising to 91 yields new balloon
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalUsageSample(88.0, kThreshold, true, seen, latched));
        assert(seen);
        assert(!latched);

        int balloons = 0;
        if (EvalUsageSample(91.0, kThreshold, true, seen, latched)) ++balloons;
        assert(balloons == 1);
        assert(latched);

        // 92 stays latched
        assert(!EvalUsageSample(92.0, kThreshold, true, seen, latched));
        assert(latched);

        // 85 is not below threshold - 5 (85), so it stays latched
        assert(!EvalUsageSample(85.0, kThreshold, true, seen, latched));
        assert(latched);

        // Dropping to 84 resets (84 < 85)
        assert(!EvalUsageSample(84.0, kThreshold, true, seen, latched));
        assert(!latched);

        // Rising to 91 again yields a new balloon
        assert(EvalUsageSample(91.0, kThreshold, true, seen, latched));
        assert(latched);
    }

    // 3. Invalid (ok=false, NaN) or negative values never notify and never touch latch/seen state
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalUsageSample(95.0, kThreshold, false, seen, latched));
        assert(!seen);
        assert(!latched);
        assert(!EvalUsageSample(-10.0, kThreshold, true, seen, latched));
        assert(!seen);
        assert(!latched);

        // Initialize with valid normal sample
        assert(!EvalUsageSample(50.0, kThreshold, true, seen, latched));
        assert(seen);
        assert(!latched);

        // Invalid/negative hot sample never notifies
        assert(!EvalUsageSample(95.0, kThreshold, false, seen, latched));
        assert(!latched);
        assert(!EvalUsageSample(std::numeric_limits<double>::quiet_NaN(), kThreshold, true, seen, latched));
        assert(!latched);

        // Valid hot sample latches and notifies
        assert(EvalUsageSample(91.0, kThreshold, true, seen, latched));
        assert(latched);

        // Invalid/negative low sample must not reset latch
        assert(!EvalUsageSample(10.0, kThreshold, false, seen, latched));
        assert(latched);
        assert(!EvalUsageSample(-1.0, kThreshold, true, seen, latched));
        assert(latched);
    }

    std::puts("Usage alert policy tests passed");
}
