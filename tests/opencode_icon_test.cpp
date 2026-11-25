#include "../src/opencode_icon.h"
#include <cassert>
#include <cstdio>

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
    HICON icon = CreateSquareIcon(true, 32);
    assert(icon != nullptr);
    DestroyIcon(icon);
    std::puts("opencode_icon_test: all passed");
    return 0;
}
