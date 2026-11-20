#pragma once

#include <cstdint>
#include <vector>

namespace metrics {

inline std::vector<int> PickHidden(const std::vector<int>& widths, int available,
                                   const std::vector<uint8_t>& isMetric, int lowBatteryIdx) {
    int total = 0;
    for (int w : widths) total += w;
    std::vector<int> hidden;
    if (total <= available) return hidden;

    int n = static_cast<int>(widths.size());
    for (int i = n - 1; i >= 0 && total > available; --i) {
        if (i < static_cast<int>(isMetric.size()) && isMetric[i]) {
            total -= widths[i];
            hidden.push_back(i);
        }
    }
    for (int i = n - 1; i >= 0 && total > available; --i) {
        if (i == lowBatteryIdx) continue;
        if (i < static_cast<int>(isMetric.size()) && isMetric[i]) continue;
        total -= widths[i];
        hidden.push_back(i);
    }
    return hidden;
}

}  // namespace metrics
