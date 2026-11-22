#pragma once

namespace metrics {

inline bool ShouldNotifyTemp(int sev, bool latched) {
    return sev >= 1 && !latched;
}

inline bool ResetLatch(int sev) {
    return sev == 0;
}

inline bool FirstHotSample(int sev) {
    return sev >= 1;
}

}  // namespace metrics
