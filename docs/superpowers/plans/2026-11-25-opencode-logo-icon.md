# OpenCode Logo Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Segoe `0xE943` glyph with the official OpenCode logo on every surface Monitor owns: taskbar strip, tray icon, usage alert balloon and the API-key dialog icon.

**Architecture:** Two committed transparent PNGs (light ink for dark taskbars, dark ink for light taskbars) are converted by a PowerShell script into generated BGRA sprite tables in `src/opencode_icon_data.cpp`. `src/opencode_icon.*` exposes those sprites and builds square `HICON`s. The strip renderer gains a full-colour sprite path beside its tinted-mask path, so the logo keeps its exact greys; square logo variants are used for tray/balloon/dialog because those slots are square.

**Tech Stack:** C++17/Win32, GDI, MinGW-w64, PowerShell + System.Drawing for the asset generator (same pattern as `scripts/make-app-icon.ps1`). No runtime dependency.

**Spec:** `docs/superpowers/plans/2026-11-23-monitor-opencode-usage.md` and the OpenCode brand guidelines (https://opencode.ai/brand, fetched 2026-11-25). Assets supplied at `C:\Users\Ignacio Revainera\Downloads\OpenCode Brand Assets`.

## Global Constraints

- Use the official logo unmodified: proportions 4:5 in the strip, square variants only for square slots; sizes in the brand table (16×20, 20×25, 24×30, 32×40, 48×60). Never recolor, stretch, add glow/shadow, or put words next to the logo.
- Light ink (`opencode-logo-transparent-light.png`) on dark taskbars; dark ink (`...-dark.png`) on light taskbars. Square equivalents come from the official square files (download from `https://opencode.ai/brand/files/opencode-logo-light-square.png` and `...-dark-square.png`).
- No new runtime dependencies. Committed generated data, reproducible with the script.
- Existing glyphs, other metrics, layout, alerts and IDs unchanged; OpenCode keeps `Where::Hidden` default and the same menu entries.
- No icon changes for the Monitor exe/setup or the notification header (those are Monitor identity).
- `make test` green; `build.bat` without new warnings. Work lands as **v1.1.6**.

## Review Focus

1. **Theme choice:** dark taskbar must show light ink, light taskbar dark ink; a swapped file is a brand violation and looks broken. Pin: Task 1 data test + Task 2/3 captures.
2. **Alpha/premultiplication:** sprites must composite like the existing masks (no dark halos, no white boxes) over the layered taskbar. Pin: Task 2 capture on both themes.
3. **DPI sizes:** strip uses 16/24/32 px widths at 100/150/200%; nearest generated 4:5 size, never scaled. Pin: Task 1 lookup test.
4. **Square slots:** tray/balloon/dialog use square assets, never the 4:5 logo squashed. Pin: Task 3 icon-size test.
5. **Brand copy rule:** no caption/word beside the logo; word label styles fall back to the Segoe glyph. Pin: Task 2 logic + visual check.
6. **Layout/overflow:** the 16×20 icon is taller than 16×16 glyphs; S0–S6 must still fit and align. Pin: Task 2 captures at compact widths.

---

### Task 1: Assets, generator, sprite API

**Files:**
- Create: `third_party/opencode/opencode-logo-transparent-light.png`
- Create: `third_party/opencode/opencode-logo-transparent-dark.png`
- Create: `third_party/opencode/opencode-logo-light-square.png`
- Create: `third_party/opencode/opencode-logo-dark-square.png`
- Create: `scripts/make-opencode-icon.ps1`
- Create: `src/opencode_icon_data.cpp` (generated)
- Create: `src/opencode_icon.h`
- Create: `src/opencode_icon.cpp`
- Create: `tests/opencode_icon_test.cpp`
- Modify: `Makefile` (LIBS unchanged; add `test-opencode-icon` to `test:` and `.PHONY`)
- Modify: `build.bat` (compile wildcard already covers `src\*.cpp`; no edit expected — verify)
- Modify: `installer/THIRD_PARTY_NOTICES.txt`

**Interfaces:**
- Produces:
```cpp
namespace opencode_icon {
struct Sprite { int w = 0, h = 0; const uint32_t* pixels = nullptr; };  // 0xAARRGGBB, straight alpha
const Sprite& Strip(bool lightTaskbar, int want);   // 4:5, nearest generated width (16/20/24/32/48)
const Sprite& Square(bool lightTaskbar, int want);  // square, nearest generated size (16/20/24/32/40/48)
HICON CreateSquareIcon(bool lightTaskbar, int size);  // caller DestroyIcon()
}
```
- `lightTaskbar == true` selects the dark-ink files.
- Consumes: nothing.

- [x] **Step 1: Copy the official assets**

Copy the four PNGs listed above from `C:\Users\Ignacio Revainera\Downloads\OpenCode Brand Assets\Logo Transparent\` and download the two square files from the URLs in Global Constraints into `third_party/opencode/`. Do not edit their pixels.

- [x] **Step 2: Write the generator**

`scripts/make-opencode-icon.ps1` reads the six PNGs with `System.Drawing`, resizes with `HighQualityBicubic` onto transparent canvases at exactly these sizes, and writes `src/opencode_icon_data.cpp`:

```
strip widths:  16, 20, 24, 32, 48   (heights 20, 25, 30, 40, 60)
square sizes:  16, 20, 24, 32, 40, 48
```

Output shape (namespace `opencode_icon_data`):
```cpp
struct Raw { int w, h; const uint32_t* px; };
extern const Raw kStripLight[];   extern const int kStripLightCount;
extern const Raw kStripDark[];    extern const int kStripDarkCount;
extern const Raw kSquareLight[];  extern const int kSquareLightCount;
extern const Raw kSquareDark[];   extern const int kSquareDarkCount;
```
Each pixel is `0xAARRGGBB` with straight alpha (`A = PNG alpha`). `light`/`dark` follow the brand naming: light ink is for dark taskbars.

Run: `powershell -File scripts\make-opencode-icon.ps1`
Expected: `src/opencode_icon_data.cpp` written; compiling it has no warnings.

- [x] **Step 3: Write the failing test**

`tests/opencode_icon_test.cpp`:
```cpp
#include "../src/opencode_icon.h"
#include <cassert>
#include <cstdio>

int main() {
    using namespace opencode_icon;
    const Sprite& s16 = Strip(false, 16);   // dark taskbar -> light ink
    assert(s16.w == 16 && s16.h == 20 && s16.pixels);
    const Sprite& s32 = Strip(true, 32);    // light taskbar -> dark ink
    assert(s32.w == 32 && s32.h == 40);
    const Sprite& big = Strip(false, 100);  // nearest, never scaled
    assert(big.w == 48 && big.h == 60);
    const Sprite& q = Square(false, 24);
    assert(q.w == 24 && q.h == 24);
    HICON icon = CreateSquareIcon(true, 32);
    assert(icon != nullptr);
    DestroyIcon(icon);
    std::puts("opencode_icon_test: all passed");
    return 0;
}
```

- [x] **Step 4: Run it to verify it fails**

Run: `make test-opencode-icon`
Expected: FAIL — `opencode_icon.h` / functions do not exist yet.

- [x] **Step 5: Implement the API**

`src/opencode_icon.cpp`: nearest-size lookup (ties pick the larger), return a static empty sprite only if no data exists. `CreateSquareIcon` creates a 32-bit top-down DIB, copies the sprite, builds a 1-bit AND mask of zeros, and returns `CreateIconIndirect`.

`src/opencode_icon.h`: exactly the interface above.

Add to `Makefile`:
```make
test-opencode-icon:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall tests/opencode_icon_test.cpp src/opencode_icon.cpp src/opencode_icon_data.cpp \
	    -o build/opencode_icon_test.exe -lgdi32 -luser32
	./build/opencode_icon_test.exe
```
and register it in `test:` and `.PHONY`.

- [x] **Step 6: Run test and commit**

Run: `make test-opencode-icon`
Expected: `opencode_icon_test: all passed`

```bash
git add third_party/opencode scripts/make-opencode-icon.ps1 src/opencode_icon_data.cpp \
        src/opencode_icon.h src/opencode_icon.cpp tests/opencode_icon_test.cpp Makefile \
        installer/THIRD_PARTY_NOTICES.txt
git commit -m "feat: official OpenCode logo sprites and icon helper"
```

---

### Task 2: Taskbar strip uses the 4:5 logo

**Files:**
- Modify: `src/widget.h:11-30` (`WidgetItem`)
- Modify: `src/widget.cpp` (`Canvas`, `Piece`, `Layout`, draw loop)
- Modify: `src/main.cpp` (`UpdateWidget`, around the metric item creation)

**Interfaces:**
- Consumes: `opencode_icon::Strip`, `opencode_icon::Sprite`.
- Produces: `WidgetItem::openCodeLogo` (bool); no other public API.

- [x] **Step 1: Add the item flag**

In `WidgetItem` add `bool openCodeLogo = false;`. In `UpdateWidget`, set `it.openCodeLogo = m.id == metrics::Id::OpenCodeUsage;` for every path that creates that metric item (single-row and grouped-temp paths).

- [x] **Step 2: Add the sprite draw path**

In `widget.cpp`:
- `#include "opencode_icon.h"`.
- `Piece` gains `const opencode_icon::Sprite* sprite = nullptr;`.
- `Canvas` gains `void Draw(const opencode_icon::Sprite& s, int x, int y, int alpha)` that calls `Blend(x + i, y + j, Rgb{...}, a * alpha / 255)` per pixel, reading straight RGB and alpha from the sprite.
- In `Layout`, when `showGlyph && it.openCodeLogo && !showWord`: create the icon piece from `opencode_icon::Strip(g_light, Px(16))`, set `m.w/m.h` from the sprite, `sprite = &sp`, and skip the Segoe mask. Otherwise keep the existing glyph path unchanged.
- Do not draw the tiny `it.label` caption when `sprite != nullptr` (brand: no words beside the logo).
- In the draw loop, branch: `if (p.sprite) cv.Draw(*p.sprite, xs[i], ys[i], p.alpha); else cv.Draw(p.m, ...)`. Width, centering, gaps and overflow measurement keep using `p.m.w/p.m.h`.

- [x] **Step 3: Build and look**

Run: `build.bat 1.1.6 dev`
Expected: `Built Monitor.exe`, no warnings.

Install locally, capture the strip with a dark taskbar and a light taskbar, and at 100 %/150 % DPI if available. Expect the official two-tone logo, correct ink per theme, no halo/box, aligned with the percentage, and no caption over it. Switch Component label to Word/Both: the label mode must fall back to the Segoe glyph (no logo + word).

- [x] **Step 4: Run the suite and commit**

Run: `make test`
Expected: all targets pass.

```bash
git add src/widget.h src/widget.cpp src/main.cpp
git commit -m "feat: OpenCode logo in the taskbar strip"
```

---

### Task 3: Tray, alert balloon and key dialog icons

**Files:**
- Modify: `src/main.cpp` (`ApplyMetricTray`, `ShowMetricAlert`, icon caches, `RedrawAll`)
- Modify: `src/opencode_dialog.cpp` (window icons)

**Interfaces:**
- Consumes: `opencode_icon::CreateSquareIcon`, `opencode_icon::Square`.
- Produces: cached `HICON g_openCodeTrayIcon` / `g_openCodeBalloonIcon` (file-static in `main.cpp`).

- [x] **Step 1: Tray icon**

In `ApplyMetricTray`, for `Id::OpenCodeUsage` replace the `RenderIcon` call with a cached square HICON at `GetSystemMetricsForDpi(SM_CXSMICON, dpi)` (nearest generated square). Key the cache by theme and size; destroy it in `RedrawAll` and when the theme changes. Keep the tray tooltip (`AiTrayTip`) and the single UID unchanged.

- [x] **Step 2: Alert balloon**

In `ShowMetricAlert`, when `id == Id::OpenCodeUsage`, pass a cached `CreateSquareIcon(light, GetSystemMetrics(SM_CXICON))` as `balloonIcon` (and as the transient tray header icon), so the OpenCode alert shows the logo. All other metrics keep `ComponentGlyph`. Destroy the cache in `RedrawAll`.

- [x] **Step 3: Dialog icon**

In `PromptOpenCodeKey`, after creating the dialog, set `WM_SETICON` `ICON_SMALL` (16 px square) and `ICON_BIG` (32 px square) from `opencode_icon::CreateSquareIcon`; destroy both after the message loop. The title text stays `T(L"OpenCode")`.

- [x] **Step 4: Verify and commit**

Run: `build.bat 1.1.6 dev`, then:
- Tray mode with OpenCode visible: square logo, sharp at 100 % and 125/150 %.
- Trigger an OpenCode usage alert (temporary threshold) or call `ShowMetricAlert` path: square logo in the balloon.
- Open the key dialog: logo in the title bar at small and large sizes.
- Theme flip: light ink only on dark taskbar and vice versa.

```bash
git add src/main.cpp src/opencode_dialog.cpp
git commit -m "feat: OpenCode logo on tray, alerts and key dialog"
```

---

### Task 4: Notices, docs and release

**Files:**
- Modify: `installer/THIRD_PARTY_NOTICES.txt`
- Modify: `README.md`
- Modify: `build/portable/THIRD_PARTY_NOTICES.txt` (generated by the installer script; no manual edit if regenerated)

**Interfaces:**
- Consumes: everything above.

- [x] **Step 1: Notices**

Add an OpenCode section: copyright Anomaly, trademark note ("OpenCode and the OpenCode logo are trademarks of Anomaly; used only to identify the OpenCode Go usage metric; no endorsement implied"), source links, that files are unmodified, and the generator path. Do not add license text not supplied by the brand kit.

- [x] **Step 2: README**

Add one sentence under the metrics list: "OpenCode Go usage uses the official OpenCode logo, shown only for that metric." Optionally include the official "Works with OpenCode" badge using the supplied 20 px PNG, linked to https://opencode.ai, without altering it.

- [x] **Step 3: Full verification**

Run:
```powershell
$env:MSYSTEM='MINGW64'
bash -lc "make test"
powershell -File installer\build-installer.ps1 -Version 1.1.6
```
Expected: all tests pass, installer and portable built, SHA256SUMS written. Install, restart, confirm logo survives restart, no stale Segoe glyph, no regression in other metrics/alerts.

- [x] **Step 4: Commit**

```bash
git add installer/THIRD_PARTY_NOTICES.txt README.md
git commit -m "docs: OpenCode logo notices and metric note"
```

---

## Self-Review

- Spec coverage: strip (Task 2), tray/balloon/dialog (Task 3), assets/notices (Tasks 1/4), brand rules (constraints + size test), no menu/exe icons (explicitly out).
- Type consistency: `opencode_icon::Sprite`, `Strip/Square/CreateSquareIcon`, `WidgetItem::openCodeLogo` used consistently.
- Review Focus: each line has a task test or capture step.
- Proportion: signatures and values only; bodies left to the implementer.
