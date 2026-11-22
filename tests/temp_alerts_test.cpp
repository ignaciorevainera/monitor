#include "../src/metrics/metrics.h"
#include "../src/metrics/temp_alerts.h"

#include <cassert>
#include <cstdio>

namespace {

bool EvalTempSample(int metricId, double tempC, bool ok, bool& hasSeenOk, bool& latched) {
    if (!ok) return false;
    const int sev = metrics::SeverityFor(metricId, tempC);
    if (!hasSeenOk) {
        hasSeenOk = true;
        if (metrics::FirstHotSample(sev)) latched = true;
        return false;
    }
    if (metrics::ResetLatch(sev)) {
        latched = false;
        return false;
    }
    if (metrics::ShouldNotifyTemp(sev, latched)) {
        latched = true;
        return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace metrics;
    constexpr int kSsd = static_cast<int>(Id::SsdTemp);
    constexpr int kCpu = static_cast<int>(Id::CpuTemp);

    // Pure policy primitives
    assert(!ShouldNotifyTemp(0, false));
    assert(!ShouldNotifyTemp(0, true));
    assert(ShouldNotifyTemp(1, false));
    assert(!ShouldNotifyTemp(1, true));
    assert(ShouldNotifyTemp(2, false));
    assert(!ShouldNotifyTemp(2, true));

    assert(ResetLatch(0));
    assert(!ResetLatch(1));
    assert(!ResetLatch(2));

    assert(!FirstHotSample(0));
    assert(FirstHotSample(1));
    assert(FirstHotSample(2));

    // 1. SSD constant at 79 with no prior state yields zero balloons (first hot sample initializes latch silently)
    {
        bool seen = false;
        bool latched = false;
        int balloons = 0;
        for (int i = 0; i < 5; ++i) {
            if (EvalTempSample(kSsd, 79.0, true, seen, latched)) ++balloons;
        }
        assert(seen);
        assert(latched);
        assert(balloons == 0);
    }

    // 2. First hot sample at warn (sev 1) also initializes latch silently
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalTempSample(kCpu, 85.0, true, seen, latched));
        assert(seen);
        assert(latched);
        assert(!EvalTempSample(kCpu, 92.0, true, seen, latched));
    }

    // 3. Rising 70 -> 76 yields one balloon, latch prevents a second at 77,
    //    dropping to 60 resets, rising again yields a new balloon
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalTempSample(kSsd, 60.0, true, seen, latched));
        assert(seen);
        assert(!latched);

        int balloons = 0;
        if (EvalTempSample(kSsd, 70.0, true, seen, latched)) ++balloons;
        if (EvalTempSample(kSsd, 76.0, true, seen, latched)) ++balloons;
        assert(balloons == 1);
        assert(latched);

        // Latch prevents a second balloon at 77
        assert(!EvalTempSample(kSsd, 77.0, true, seen, latched));
        assert(latched);

        // Dropping to 60 resets the latch
        assert(!EvalTempSample(kSsd, 60.0, true, seen, latched));
        assert(!latched);

        // Rising again yields a new balloon
        assert(EvalTempSample(kSsd, 76.0, true, seen, latched));
        assert(latched);
    }

    // 4. ok=false never notifies and never touches the latch (or seen state)
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalTempSample(kSsd, 79.0, false, seen, latched));
        assert(!seen);
        assert(!latched);

        assert(!EvalTempSample(kSsd, 60.0, true, seen, latched));
        assert(seen);
        assert(!latched);

        assert(!EvalTempSample(kSsd, 80.0, false, seen, latched));
        assert(!latched);

        assert(EvalTempSample(kSsd, 70.0, true, seen, latched));
        assert(latched);

        // Cold reading with ok=false must not reset latch
        assert(!EvalTempSample(kSsd, 40.0, false, seen, latched));
        assert(latched);
    }

    // 5. Direct severity 2 still yields one balloon
    {
        bool seen = false;
        bool latched = false;
        assert(!EvalTempSample(kCpu, 50.0, true, seen, latched));
        assert(EvalTempSample(kCpu, 95.0, true, seen, latched));
        assert(latched);
        assert(!EvalTempSample(kCpu, 96.0, true, seen, latched));
    }

    std::puts("Temperature alert policy tests passed");
}
