#include "../src/opencode_icon.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
int MaxLum(const opencode_icon::Sprite& s) {  // brand ink check: light ink is near #EEEEEE
    int best = 0;
    for (int i = 0; i < s.w * s.h; i++) {
        const uint32_t p = s.pixels[i];
        if (!(p >> 24)) continue;  // fully transparent
        const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        best = std::max(best, (r + g + b) / 3);
    }
    return best;
}
}  // namespace

int main() {
    using namespace opencode_icon;
    const Sprite& s16 = Strip(false, 16);
    assert(s16.w == 16 && s16.h == 20 && s16.pixels);
    const Sprite& s32 = Strip(true, 32);
    assert(s32.w == 32 && s32.h == 40);
    const Sprite& big = Strip(false, 100);
    assert(big.w == 48 && big.h == 60);
    const Sprite& q = Square(false, 24);
    assert(q.w == 24 && q.h == 24);

    // Dark taskbar must get light ink, light taskbar dark ink (brand: file suffix is the background).
    // Square() shares Strip()'s lightTaskbar mapping, so these pin both; the square PNGs carry the
    // two-tone inner shade, which makes post-resize luminance thresholds unreliable.
    assert(MaxLum(Strip(false, 16)) > 200);
    assert(MaxLum(Strip(true, 16)) < 64);

    HICON icon = CreateSquareIcon(true, 32);
    assert(icon != nullptr);
    DestroyIcon(icon);
    std::puts("opencode_icon_test: all passed");
    return 0;
}
