#include "../src/metrics/overflow.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    using metrics::OverflowItem;
    using metrics::Phase;
    using metrics::SelectOverflowPhase;

    // 1. Everything fits -> S0, nothing hidden
    {
        std::vector<OverflowItem> items = {
            { 40, 30, 20, true,  false, 0 },
            { 40, 30, 20, true,  false, 0 },
            { 50, 40, 25, false, false, 0 },
        };
        auto r = SelectOverflowPhase(items, 400, 16, 14, 6);
        assert(r.phase == Phase::S0_Full);
        assert(r.hidden.empty());
        assert(r.keepIndex == -1);
    }

    // 2. S5 hides metrics from last to first, before any battery
    {
        std::vector<OverflowItem> items = {
            { 70, 50, 20, true,  false, 0 },
            { 70, 50, 20, true,  false, 0 },
            { 70, 50, 20, true,  false, 0 },
            { 80, 50, 25, false, false, 0 },
        };
        // value-only total = 16 + 3*6 + 20*3 + 25 = 119; maxW = 80 -> hide idx 2, then idx 1
        auto r = SelectOverflowPhase(items, 80, 16, 14, 6);
        assert(r.phase == Phase::S5_HideItems);
        assert(r.hidden.size() == 2);
        assert(std::find(r.hidden.begin(), r.hidden.end(), 2) != r.hidden.end());
        assert(std::find(r.hidden.begin(), r.hidden.end(), 1) != r.hidden.end());
        assert(std::find(r.hidden.begin(), r.hidden.end(), 0) == r.hidden.end());
        assert(std::find(r.hidden.begin(), r.hidden.end(), 3) == r.hidden.end());
    }

    // 3. S5 never hides the low battery while others remain; a lone low item falls to S6
    {
        std::vector<OverflowItem> items = {
            { 50, 40, 25, false, false, 0 },
            { 50, 40, 25, false, false, 0 },
            { 50, 40, 25, false, true,  0 },
        };
        // one item fits exactly: 16 + 25 = 41
        auto r5 = SelectOverflowPhase(items, 41, 16, 14, 6);
        assert(r5.phase == Phase::S5_HideItems);
        assert(r5.hidden.size() == 2);
        assert(std::find(r5.hidden.begin(), r5.hidden.end(), 2) == r5.hidden.end());

        std::vector<OverflowItem> single = { { 50, 40, 25, false, true, 0 } };
        auto r6 = SelectOverflowPhase(single, 10, 16, 14, 6);
        assert(r6.phase == Phase::S6_SingleItem);
        assert(r6.keepIndex == 0);
        assert(r6.hidden.empty());
    }

    // 4. Phase ladder S0 -> S1 -> S2 -> S3 -> S4 -> S5 as maxW shrinks
    {
        // 2 items: idx 0 is metric, idx 1 is battery
        // padTotal = 16, normalGap = 14, compactGap = 6
        // S0: 16 + 14 + 70 + 80 = 180
        // S1: 16 + 14 + 50 + 50 = 130
        // S2: 16 +  6 + 50 + 50 = 122
        // S3: 16 +  6 + 30 + 50 = 102 (metric value-only, battery keeps glyph)
        // S4: 16 +  6 + 30 + 30 = 82  (all value-only)
        // S5: hide metric idx 0 -> 16 + 30 = 46
        std::vector<OverflowItem> items = {
            { 70, 50, 30, true,  false, 0 },
            { 80, 50, 30, false, false, 0 },
        };
        assert(SelectOverflowPhase(items, 180, 16, 14, 6).phase == Phase::S0_Full);
        assert(SelectOverflowPhase(items, 179, 16, 14, 6).phase == Phase::S1_NoWords);
        assert(SelectOverflowPhase(items, 130, 16, 14, 6).phase == Phase::S1_NoWords);
        assert(SelectOverflowPhase(items, 129, 16, 14, 6).phase == Phase::S2_Compact);
        assert(SelectOverflowPhase(items, 122, 16, 14, 6).phase == Phase::S2_Compact);
        assert(SelectOverflowPhase(items, 121, 16, 14, 6).phase == Phase::S3_NoMetricGlyph);
        assert(SelectOverflowPhase(items, 102, 16, 14, 6).phase == Phase::S3_NoMetricGlyph);
        assert(SelectOverflowPhase(items, 101, 16, 14, 6).phase == Phase::S4_ValueOnly);
        assert(SelectOverflowPhase(items,  82, 16, 14, 6).phase == Phase::S4_ValueOnly);

        auto r5 = SelectOverflowPhase(items, 81, 16, 14, 6);
        assert(r5.phase == Phase::S5_HideItems);
        assert(r5.hidden.size() == 1 && r5.hidden[0] == 0);

        // Exact fit at S5 without reserving ellipsis width (46 fits in 46)
        auto r5Exact = SelectOverflowPhase(items, 46, 16, 14, 6);
        assert(r5Exact.phase == Phase::S5_HideItems);
        assert(r5Exact.hidden.size() == 1 && r5Exact.hidden[0] == 0);
    }

    // 5. S6 prioritizes low battery, then the highest alert severity, then the first item
    {
        std::vector<OverflowItem> items = {
            { 50, 40, 25, false, false, 0 },  // idx 0: normal battery
            { 50, 40, 25, false, true,  0 },  // idx 1: low battery
            { 50, 40, 25, true,  false, 1 },  // idx 2: metric with warning alert
        };
        // S6 when even 1 item doesn't fit (maxW = 40 < 41): keeps low battery idx 1
        auto r6Low = SelectOverflowPhase(items, 40, 16, 14, 6);
        assert(r6Low.phase == Phase::S6_SingleItem);
        assert(r6Low.keepIndex == 1);
        assert(std::find(r6Low.hidden.begin(), r6Low.hidden.end(), 1) == r6Low.hidden.end());

        // without low battery: the severity-2 alert wins over the severity-1 alert
        items[1].low = false;
        items[0].alertSeverity = 2;
        auto r6Alert = SelectOverflowPhase(items, 40, 16, 14, 6);
        assert(r6Alert.phase == Phase::S6_SingleItem);
        assert(r6Alert.keepIndex == 0);

        // without low or alerts: keeps first active item
        items[0].alertSeverity = 0;
        items[2].alertSeverity = 0;
        auto r6First = SelectOverflowPhase(items, 40, 16, 14, 6);
        assert(r6First.phase == Phase::S6_SingleItem);
        assert(r6First.keepIndex == 0);
    }

    // 6. Empty items
    {
        std::vector<OverflowItem> empty;
        auto rEmpty = SelectOverflowPhase(empty, 0, 16, 14, 6);
        assert(rEmpty.phase == Phase::S0_Full);
        assert(rEmpty.hidden.empty());
        assert(rEmpty.keepIndex == -1);
    }

    std::puts("overflow_test: all passed");
    return 0;
}
