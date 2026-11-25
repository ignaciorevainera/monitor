#pragma once

#include <windows.h>
#include <cstdint>

namespace opencode_icon {

struct Sprite {
    int w = 0;
    int h = 0;
    const uint32_t* pixels = nullptr;
};

const Sprite& Strip(bool lightTaskbar, int want);
const Sprite& Square(bool lightTaskbar, int want);
HICON CreateSquareIcon(bool lightTaskbar, int size);

}
