#include "opencode_icon.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace opencode_icon_data {
struct Raw {
    int w;
    int h;
    const uint32_t* px;
};
extern const Raw kStripLight[];
extern const int kStripLightCount;
extern const Raw kStripDark[];
extern const int kStripDarkCount;
extern const Raw kSquareLight[];
extern const int kSquareLightCount;
extern const Raw kSquareDark[];
extern const int kSquareDarkCount;
}

namespace opencode_icon {

namespace {

std::vector<Sprite> MakeSprites(const opencode_icon_data::Raw* raw, int count) {
    std::vector<Sprite> out;
    out.reserve(count > 0 ? count : 0);
    for (int i = 0; i < count; ++i) {
        out.push_back(Sprite{raw[i].w, raw[i].h, raw[i].px});
    }
    return out;
}

const Sprite& PickNearest(const std::vector<Sprite>& list, int want) {
    static const Sprite kEmpty{};
    if (list.empty()) return kEmpty;
    int best = 0;
    int bestDist = std::abs(list[0].w - want);
    for (int i = 1; i < static_cast<int>(list.size()); ++i) {
        int dist = std::abs(list[i].w - want);
        if (dist < bestDist || (dist == bestDist && list[i].w > list[best].w)) {
            best = i;
            bestDist = dist;
        }
    }
    return list[best];
}

}

const Sprite& Strip(bool lightTaskbar, int want) {
    static const std::vector<Sprite> light = MakeSprites(opencode_icon_data::kStripLight, opencode_icon_data::kStripLightCount);
    static const std::vector<Sprite> dark = MakeSprites(opencode_icon_data::kStripDark, opencode_icon_data::kStripDarkCount);
    return PickNearest(lightTaskbar ? dark : light, want);
}

const Sprite& Square(bool lightTaskbar, int want) {
    static const std::vector<Sprite> light = MakeSprites(opencode_icon_data::kSquareLight, opencode_icon_data::kSquareLightCount);
    static const std::vector<Sprite> dark = MakeSprites(opencode_icon_data::kSquareDark, opencode_icon_data::kSquareDarkCount);
    return PickNearest(lightTaskbar ? dark : light, want);
}

HICON CreateSquareIcon(bool lightTaskbar, int size) {
    const Sprite& s = Square(lightTaskbar, size);
    if (!s.pixels || s.w <= 0 || s.h <= 0) return nullptr;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s.w;
    bi.bmiHeader.biHeight = -s.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hColor = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hColor || !bits) {
        if (hColor) DeleteObject(hColor);
        return nullptr;
    }
    std::memcpy(bits, s.pixels, static_cast<size_t>(s.w) * s.h * sizeof(uint32_t));

    int maskStride = ((s.w + 15) / 16) * 2;
    std::vector<uint8_t> mask(static_cast<size_t>(maskStride) * s.h, 0);
    HBITMAP hMask = CreateBitmap(s.w, s.h, 1, 1, mask.data());
    if (!hMask) {
        DeleteObject(hColor);
        return nullptr;
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hColor;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);
    return icon;
}

}
