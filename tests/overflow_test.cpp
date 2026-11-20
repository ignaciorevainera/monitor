#include "../src/metrics/overflow.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    using metrics::PickHidden;

    // 1. Everything fits -> nothing hidden
    {
        std::vector<int> w = { 40, 40, 40, 40, 50 };
        std::vector<uint8_t> isM = { 1, 1, 1, 1, 0 };
        auto h = PickHidden(w, 250, isM, -1);
        assert(h.empty());
    }

    // 2. Tight space -> hides metrics from last to first
    {
        std::vector<int> w = { 40, 40, 40, 40, 50 };  // total = 210
        std::vector<uint8_t> isM = { 1, 1, 1, 1, 0 };
        // available = 140 -> need to shed at least 70 -> hides idx 3 (40) and idx 2 (40), leaving 130
        auto h = PickHidden(w, 140, isM, -1);
        assert(h.size() == 2);
        assert(std::find(h.begin(), h.end(), 3) != h.end());
        assert(std::find(h.begin(), h.end(), 2) != h.end());
        assert(std::find(h.begin(), h.end(), 0) == h.end());
        assert(std::find(h.begin(), h.end(), 4) == h.end());
    }

    // 3. Extreme: single low-battery device is never hidden
    {
        std::vector<int> w = { 40, 40, 60 };  // idx 2 is low battery
        std::vector<uint8_t> isM = { 1, 1, 0 };
        auto h = PickHidden(w, 10, isM, 2);
        assert(std::find(h.begin(), h.end(), 2) == h.end());
        assert(h.size() == 2);

        std::vector<int> singleW = { 60 };
        std::vector<uint8_t> singleM = { 0 };
        auto hSingle = PickHidden(singleW, 10, singleM, 0);
        assert(hSingle.empty());
    }

    std::puts("overflow_test: all passed");
    return 0;
}
