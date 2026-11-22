#pragma once

#include <cmath>

namespace metrics {

inline bool UsageAbove(double value, double threshold) {
    return std::isfinite(value) && std::isfinite(threshold) && value >= 0.0 && value >= threshold;
}

inline bool UsageShouldNotify(double value, double threshold, bool latched) {
    return UsageAbove(value, threshold) && !latched;
}

inline bool UsageResetLatch(double value, double threshold) {
    return std::isfinite(value) && std::isfinite(threshold) && value >= 0.0 && value < threshold - 5.0;
}

inline bool UsageFirstSampleHot(double value, double threshold) {
    return UsageAbove(value, threshold);
}

}  // namespace metrics
