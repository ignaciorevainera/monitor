#pragma once

#include <cstdint>
#include <vector>

namespace metrics {

enum class Phase {
    S0_Full = 0,
    S1_NoWords = 1,
    S2_Compact = 2,
    S3_NoMetricGlyph = 3,
    S4_ValueOnly = 4,
    S5_HideItems = 5,
    S6_SingleItem = 6,
};

struct OverflowItem {
    int wFull = 0;
    int wGlyph = 0;
    int wValue = 0;
    bool isMetric = false;
    bool low = false;
    int alertSeverity = 0;
};

struct OverflowResult {
    Phase phase = Phase::S0_Full;
    std::vector<int> hidden;
    int keepIndex = -1;
};

inline int PickPriorityItem(const std::vector<OverflowItem>& items) {
    int n = static_cast<int>(items.size());
    if (n == 0) return -1;
    for (int i = 0; i < n; ++i) {
        if (!items[i].isMetric && items[i].low) return i;
    }
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (items[i].alertSeverity > 0 && (best < 0 || items[i].alertSeverity > items[best].alertSeverity)) best = i;
    }
    if (best >= 0) return best;
    return 0;
}

inline OverflowResult SelectOverflowPhase(const std::vector<OverflowItem>& items, int maxW,
                                            int padTotal, int normalGap, int compactGap) {
    int n = static_cast<int>(items.size());
    if (n == 0) return {};

    auto totalWidth = [&](int gap, auto&& itemWidth) {
        int sum = padTotal + (n > 1 ? (n - 1) * gap : 0);
        for (int i = 0; i < n; ++i) sum += itemWidth(items[i]);
        return sum;
    };

    if (totalWidth(normalGap, [](const OverflowItem& it) { return it.wFull; }) <= maxW) {
        return { Phase::S0_Full, {}, -1 };
    }
    if (totalWidth(normalGap, [](const OverflowItem& it) { return it.wGlyph; }) <= maxW) {
        return { Phase::S1_NoWords, {}, -1 };
    }
    if (totalWidth(compactGap, [](const OverflowItem& it) { return it.wGlyph; }) <= maxW) {
        return { Phase::S2_Compact, {}, -1 };
    }
    if (totalWidth(compactGap, [](const OverflowItem& it) { return it.isMetric ? it.wValue : it.wGlyph; }) <= maxW) {
        return { Phase::S3_NoMetricGlyph, {}, -1 };
    }

    int total = totalWidth(compactGap, [](const OverflowItem& it) { return it.wValue; });
    if (total <= maxW) {
        return { Phase::S4_ValueOnly, {}, -1 };
    }

    int visible = n;
    std::vector<uint8_t> isHidden(n, 0);
    std::vector<int> hidden;
    for (int i = n - 1; i >= 0 && total > maxW && visible > 1; --i) {
        if (items[i].isMetric) {
            total -= items[i].wValue + compactGap;
            isHidden[i] = 1;
            hidden.push_back(i);
            --visible;
        }
    }
    for (int i = n - 1; i >= 0 && total > maxW && visible > 1; --i) {
        if (isHidden[i] || items[i].isMetric || items[i].low) continue;
        total -= items[i].wValue + compactGap;
        isHidden[i] = 1;
        hidden.push_back(i);
        --visible;
    }
    if (total <= maxW) {
        return { Phase::S5_HideItems, std::move(hidden), -1 };
    }

    int keep = PickPriorityItem(items);
    std::vector<int> s6Hidden;
    s6Hidden.reserve(n > 0 ? n - 1 : 0);
    for (int i = 0; i < n; ++i) {
        if (i != keep) s6Hidden.push_back(i);
    }
    return { Phase::S6_SingleItem, std::move(s6Hidden), keep };
}

}  // namespace metrics
