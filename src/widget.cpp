#include "widget.h"

#include "i18n.h"
#include "metrics/overflow.h"

#include <commctrl.h>
#include <uiautomation.h>
#include <algorithm>
#include <climits>

namespace {

HWND g_wnd, g_taskbar, g_tip, g_notify, g_rebar;
UINT g_clickMsg;
bool g_light;
constexpr int kHoverInMs = 100, kHoverOutMs = 180;
constexpr UINT_PTR TIMER_HOVER = 4;
float g_hoverT = 0.0f, g_hoverFrom = 0.0f;
ULONGLONG g_hoverStart = 0;
int g_hoverDir = 0;
bool g_background;  // opaque rounded plate behind the text, for transparent / restyled taskbars
bool g_stacked;     // icon above the percentage (menu option, PR #525)
bool g_vertical;    // the primary taskbar is docked left or right: one block per device, stacked (PR #321)
std::vector<WidgetItem> g_items;
std::wstring g_tipText;
RECT g_lastTray{}, g_lastBar{}, g_lastWidgets{};
int g_lastW = -1, g_lastDpi = 0, g_lastLeft = -1;
int g_maxW = INT_MAX;
std::wstring g_hiddenTip;
SIZE g_size{};
HINSTANCE g_inst;

// Copies of the strip on secondary-monitor taskbars (Shell_SecondaryTrayWnd), so the levels stay visible
// while a game runs fullscreen on the main screen. Same drawing, same click and tooltip; each one sits
// left of its taskbar's clock. The primary strip above is unchanged, and with one monitor none exist.
struct Mirror {
    HWND bar = nullptr, wnd = nullptr;
    float hoverT = 0.0f, hoverFrom = 0.0f;
    ULONGLONG hoverStart = 0;
    int hoverDir = 0;
    SIZE size{};
    int dpi = 0;
    int lastLeft = INT_MIN;  // client x it was last placed at; INT_MIN = not placed (hidden)
};
std::vector<Mirror> g_mirrors;
// Which taskbars show the strip: "all", "main", or one monitor's id (MonitorId), e.g. only monitor 3.
// With one monitor chosen, the primary strip is hidden; if that monitor is not connected, the primary
// strip is shown instead so the levels never disappear.
std::wstring g_choice = L"all";
bool g_primaryHidden = false;
HWND g_dpiBar;  // while a mirror is painted or placed, Px() uses its taskbar's DPI

const wchar_t* kClass = L"WirelessBatteryWidget";

int Dpi() {
    HWND bar = g_dpiBar ? g_dpiBar : g_taskbar;
    UINT d = bar ? GetDpiForWindow(bar) : 0;
    return d ? (int)d : 96;
}
int Px(int v) { return MulDiv(v, Dpi(), 96); }

// Windows 11 puts the Widgets (weather) button right next to the tray when the taskbar is
// left-aligned (start-aligned on right-to-left Windows). It is XAML, so only UI Automation knows
// where it is. The element is looked up once per taskbar; its rectangle is re-read every few
// seconds because the weather text changes its width.
//
// UI Automation calls go to Explorer and wait for its UI thread, while our widget is a child window
// of the taskbar, so Explorer in turn waits for our UI thread whenever it lays out or sends input to
// the widget. Making those calls from our UI thread deadlocks both: Explorer is reported hung
// ("Cross-process", waiting on Monitor.exe) and restarted, again and again. So the lookup
// runs on its own thread, which owns no windows, and the UI thread only reads the last rectangle.
SRWLOCK g_wbLock = SRWLOCK_INIT;
RECT g_wbRect{};             // last Widgets button rectangle (screen), empty when unknown
HWND g_wbRectBar;            // taskbar g_wbRect was found in (guarded by g_wbLock)
int g_appsR = 0;             // right edge of primary taskbar app buttons (screen x, guarded by g_wbLock)
bool g_hasAppsR = false;
HWND g_appsRBar = nullptr;
HWND g_wbTaskbar;            // taskbar to search, null while there is no widget (guarded by g_wbLock)
HANDLE g_wbWake;             // auto-reset: wakes the lookup thread early

// Where a Windows 11 secondary taskbar's clock, widgets button and task buttons are. Its XAML has no child
// windows for them, so the same lookup thread reads them with UI Automation (the clock has AutomationId
// "SystemTrayIcon", widgets button "WidgetsButton", task buttons class "Taskbar.TaskListButtonAutomationPeer";
// found by PR #107).
struct Span {
    HWND bar = nullptr;
    bool measured = false;   // UI Automation answered for this taskbar
    bool hasClock = false;   // the clock can be turned off on secondary taskbars
    int clockLeft = 0;       // screen x
    bool hasWidgets = false; // Widgets (weather) button on secondary taskbar
    RECT widgets{};          // bounding rectangle of WidgetsButton (screen coords)
    bool hasTasks = false;
    int tasksRight = 0;      // right edge of the last task button left of the clock (screen x)
};
std::vector<Span> g_spans;   // guarded by g_wbLock
bool g_wbMirrors;            // guarded by g_wbLock: measure secondary taskbars too

IUIAutomationCondition* StringCondition(IUIAutomation* uia, PROPERTYID prop, const wchar_t* value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value);
    IUIAutomationCondition* c = nullptr;
    if (v.bstrVal) uia->CreatePropertyCondition(prop, v, &c);
    VariantClear(&v);
    return c;
}

Span MeasureSpan(IUIAutomation* uia, HWND bar) {
    Span s;
    s.bar = bar;
    RECT br{};
    IUIAutomationElement* root = nullptr;
    if (!GetWindowRect(bar, &br) || FAILED(uia->ElementFromHandle(bar, &root)) || !root) return s;
    s.measured = true;
    auto inBar = [&](const RECT& r) { return r.right > r.left && r.left >= br.left && r.right <= br.right; };
    if (IUIAutomationCondition* clock = StringCondition(uia, UIA_AutomationIdPropertyId, L"SystemTrayIcon")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, clock, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {  // leftmost of the clock area's buttons
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.left < s.clockLeft)) {
                    s.clockLeft = r.left;
                    s.hasClock = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        clock->Release();
    }
    if (IUIAutomationCondition* wbCond = StringCondition(uia, UIA_AutomationIdPropertyId, L"WidgetsButton")) {
        IUIAutomationElement* wbEl = nullptr;
        RECT r{};
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, wbCond, &wbEl)) && wbEl &&
            SUCCEEDED(wbEl->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
            s.widgets = r;
            s.hasWidgets = true;
        }
        if (wbEl) wbEl->Release();
        wbCond->Release();
    }
    if (IUIAutomationCondition* task = StringCondition(uia, UIA_ClassNamePropertyId, L"Taskbar.TaskListButtonAutomationPeer")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, task, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.right <= s.clockLeft) && (!s.hasWidgets || r.right <= s.widgets.left) &&
                    (!s.hasTasks || r.right > s.tasksRight)) {
                    s.tasksRight = r.right;
                    s.hasTasks = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        task->Release();
    }
    root->Release();
    return s;
}

bool IsXamlTaskbar(HWND bar) {
    return FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
}

bool StartsWith(const wchar_t* s, const wchar_t* p) {
    return s && !wcsncmp(s, p, wcslen(p));
}

bool MeasurePrimaryAppsRight(IUIAutomation* uia, IUIAutomationCondition* allCond, IUIAutomationCacheRequest* cache,
                             HWND tb, int& outAppsR) {
    RECT tr{};
    if (!GetWindowRect(tb, &tr) || !IsXamlTaskbar(tb)) return false;
    IUIAutomationElement* root = nullptr;
    IUIAutomationElementArray* arr = nullptr;
    int cnt = 0;
    if (SUCCEEDED(uia->ElementFromHandle(tb, &root)) && root &&
        SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, allCond, cache, &arr)) && arr)
        arr->get_Length(&cnt);
    int tbw = tr.right - tr.left;
    int appsR = tr.left;
    bool hasApps = false;
    HWND tn = FindWindowExW(tb, nullptr, L"TrayNotifyWnd", nullptr);
    RECT nr{};
    bool hasTray = tn && GetWindowRect(tn, &nr) && nr.right > nr.left;
    for (int i = 0; i < cnt; i++) {
        IUIAutomationElement* e = nullptr;
        if (FAILED(arr->GetElement(i, &e)) || !e) continue;
        BSTR cls = nullptr;
        RECT r{};
        BOOL off = FALSE;
        e->get_CachedClassName(&cls);
        e->get_CachedBoundingRectangle(&r);
        e->get_CachedIsOffscreen(&off);
        int w = r.right - r.left;
        if (!off && w > 0 && r.bottom > r.top && w < tbw / 3 && r.top < tr.bottom && r.bottom > tr.top &&
            (!hasTray || r.right <= nr.left)) {
            if (StartsWith(cls, L"Taskbar.") || StartsWith(cls, L"StartButton") || StartsWith(cls, L"SearchBox")) {
                if (r.right > appsR) {
                    appsR = r.right;
                    hasApps = true;
                }
            }
        }
        if (cls) SysFreeString(cls);
        e->Release();
    }
    if (arr) arr->Release();
    if (root) root->Release();
    if (!hasApps) return false;
    outAppsR = appsR;
    return true;
}

DWORD WINAPI WidgetsButtonThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    IUIAutomationElement* btn = nullptr;
    IUIAutomationCondition* allCond = nullptr;
    IUIAutomationCacheRequest* cache = nullptr;
    HWND btnBar = nullptr;
    for (;;) {
        WaitForSingleObject(g_wbWake, btnBar ? 5000 : INFINITE);  // no widget (tray-icon mode): sleep until one
        AcquireSRWLockShared(&g_wbLock);
        HWND bar = g_wbTaskbar;
        ReleaseSRWLockShared(&g_wbLock);
        if (btn && bar != btnBar) { btn->Release(); btn = nullptr; }
        btnBar = bar;
        RECT r{};
        int appsR = 0;
        bool hasApps = false;
        if (bar && IsWindow(bar)) {
            if (!uia)
                CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
            if (uia && !allCond) uia->CreateTrueCondition(&allCond);
            if (uia && !cache && SUCCEEDED(uia->CreateCacheRequest(&cache)) && cache) {
                cache->AddProperty(UIA_ClassNamePropertyId);
                cache->AddProperty(UIA_BoundingRectanglePropertyId);
                cache->AddProperty(UIA_IsOffscreenPropertyId);
            }
            if (uia && !btn) {
                IUIAutomationElement* root = nullptr;
                IUIAutomationCondition* cond = nullptr;
                VARIANT id;
                VariantInit(&id);
                id.vt = VT_BSTR;
                id.bstrVal = SysAllocString(L"WidgetsButton");
                if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root &&
                    SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond)
                    root->FindFirst(TreeScope_Descendants, cond, &btn);
                VariantClear(&id);
                if (cond) cond->Release();
                if (root) root->Release();
            }
            if (btn && FAILED(btn->get_CurrentBoundingRectangle(&r))) {
                btn->Release();  // stale (Widgets turned off, Explorer restarted): look it up again next time
                btn = nullptr;
                r = {};
            }
            if (uia && allCond && cache) hasApps = MeasurePrimaryAppsRight(uia, allCond, cache, bar, appsR);
        }
        AcquireSRWLockShared(&g_wbLock);
        bool mirrors = g_wbMirrors;
        ReleaseSRWLockShared(&g_wbLock);
        std::vector<Span> spans;
        if (bar && mirrors && uia)
            for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
                 t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr))
                if (IsXamlTaskbar(t)) spans.push_back(MeasureSpan(uia, t));  // Windows 10 bars have real child windows
        AcquireSRWLockExclusive(&g_wbLock);
        if (bar && g_wbTaskbar == bar) {  // drop a result for a taskbar we no longer sit on
            g_wbRect = r;
            g_wbRectBar = bar;
            g_appsR = appsR;
            g_hasAppsR = hasApps;
            g_appsRBar = bar;
        }
        g_spans = std::move(spans);
        ReleaseSRWLockExclusive(&g_wbLock);
    }
}

// Points the lookup thread at `taskbar` (null to idle it), starting the thread on first use.
// The last rectangle is kept for its taskbar, so switching from tray-icon back to taskbar mode on the
// same taskbar steps past the Widgets button straight away instead of after the next lookup.
void WatchWidgetsButton(HWND taskbar) {
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbTaskbar = taskbar;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (!g_wbWake) {
        g_wbWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_wbWake ? CreateThread(nullptr, 0, WidgetsButtonThread, nullptr, 0, nullptr) : nullptr;
        if (t) CloseHandle(t);
    }
    if (g_wbWake) SetEvent(g_wbWake);
}

// Never blocks: returns what the lookup thread last found on the current taskbar.
RECT WidgetsButtonRect() {
    AcquireSRWLockShared(&g_wbLock);
    RECT r = g_wbRectBar && g_wbRectBar == g_wbTaskbar ? g_wbRect : RECT{};
    ReleaseSRWLockShared(&g_wbLock);
    return r;
}

bool PrimaryAppsRight(int& out) {
    AcquireSRWLockShared(&g_wbLock);
    bool ok = g_hasAppsR && g_appsRBar && g_appsRBar == g_wbTaskbar;
    if (ok) out = g_appsR;
    ReleaseSRWLockShared(&g_wbLock);
    return ok;
}

void ApplyTipTheme() {
    if (!g_tip) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setTheme = ux ? (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme") : nullptr;
    if (setTheme) {
        setTheme(g_tip, g_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        SendMessageW(g_tip, WM_THEMECHANGED, 0, 0);
    }
}

wchar_t Glyph(DeviceKind k, bool bluetooth, wchar_t glyphOverride = 0) {
    if (glyphOverride) return glyphOverride;
    switch (k) {
        case DeviceKind::Headset:  return 0xE7F6;  // Headphone
        case DeviceKind::Mouse:    return 0xE962;  // Mouse
        case DeviceKind::Keyboard: return 0xE765;  // KeyboardClassic
        case DeviceKind::Gamepad:  return 0xE7FC;  // Game
        case DeviceKind::Phone:    return 0xE8EA;  // CellPhone
        case DeviceKind::Microphone: return 0xE720;  // Microphone
        default:                   return bluetooth ? 0xE702 : 0xE772;  // Bluetooth / Devices
    }
}
constexpr wchar_t kBolt = 0xE945;            // LightningBolt
constexpr wchar_t kCheck = 0xE73E;           // CheckMark, in place of the bolt once fully charged
constexpr wchar_t kBatteryUnknown = 0xE996;  // shown when there is nothing to display

// Creates a font, falling back when the preferred face is not installed (GDI would silently substitute).
HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0) {
    for (const wchar_t* f : { face, fallback }) {
        int h_px = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

// Grayscale coverage mask of a string, cropped to its ink bounds.
struct Mask {
    int w = 0, h = 0;
    std::vector<BYTE> a;
};

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight) {
    Mask m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz;
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), (int)s.size());
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        int c = (int)((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return (BYTE)(darkOnLight ? 255 - c : c);
    };
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (cover(p[y * W + x])) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.a.resize((size_t)m.w * m.h);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) m.a[y * m.w + x] = cover(p[(y0 + y) * W + x0 + x]);
    }
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return m;
}

struct Rgb { int r, g, b; };

struct Canvas {
    int w, h;
    std::vector<DWORD> px;  // premultiplied BGRA
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0) {}
    void Blend(int x, int y, Rgb c, int a) {  // source-over, a = 0..255
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        DWORD d = px[y * w + x];
        int inv = 255 - a;
        int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int oa = a + da * inv / 255;
        int orr = c.r * a / 255 + dr * inv / 255;
        int og = c.g * a / 255 + dg * inv / 255;
        int ob = c.b * a / 255 + db * inv / 255;
        px[y * w + x] = ((DWORD)oa << 24) | ((DWORD)orr << 16) | ((DWORD)og << 8) | (DWORD)ob;
    }
    void Draw(const Mask& m, int x, int y, Rgb c, int alpha) {
        for (int j = 0; j < m.h; j++)
            for (int i = 0; i < m.w; i++) Blend(x + i, y + j, c, m.a[j * m.w + i] * alpha / 255);
    }
};

// Soft glow around a glyph, like a lit RGB device: the coverage mask padded by r pixels, box-blurred
// twice and brightened so a thin icon outline still gives a visible glow.
Mask Halo(const Mask& m, int r) {
    Mask g;
    g.w = m.w + 2 * r;
    g.h = m.h + 2 * r;
    std::vector<int> a((size_t)g.w * g.h, 0), tmp(a.size(), 0);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) a[(y + r) * g.w + x + r] = m.a[y * m.w + x];
    int n = 2 * r + 1;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (x + k >= 0 && x + k < g.w) s += a[y * g.w + x + k];
                tmp[y * g.w + x] = s / n;
            }
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (y + k >= 0 && y + k < g.h) s += tmp[(y + k) * g.w + x];
                a[y * g.w + x] = s / n;
            }
    }
    g.a.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) g.a[i] = (BYTE)std::min(255, a[i] * 3);
    return g;
}

// One drawn piece of the strip. Normal pieces follow each other from left to right. A piece with an
// `anchor` (index of an earlier piece) is a caption: drawn over that piece, `bx`/`by` from its top-left
// corner, and taking no room of its own. Branded devices add a glow behind the glyph and a level bar.
struct Piece {
    Mask m;
    Rgb color;
    int alpha;
    int gapAfter;
    int anchor = -1;
    int bx = 0, by = 0;
    int barPct = -1;  // >= 0: thin level bar under the text (branded devices)
    Rgb barColor{};
    Mask halo;        // glow drawn behind the glyph (branded devices)
    int haloR = 0, haloAlpha = 0;
    int px = INT_MIN, py = 0;  // fixed top-left position (stacked and vertical layouts); INT_MIN = flow left to right
};

// Three arrangements of the same pieces:
//  * normal: "[icon] 30%  [icon] 93%" in one row (unchanged default)
//  * stacked (menu option, from PR #525): each device's icon above its percentage, to save width;
//    used only when the taskbar is tall enough, otherwise the normal row
//  * vertical (left / right taskbar, from PR #321): one device per row block, stacked top to bottom
// `width` and `height` return the strip's size; in the vertical layout the width is the bar's.
std::vector<Piece> Layout(int barW, int barH, int& width, int& height,
                          metrics::Phase phase = metrics::Phase::S0_Full,
                          const std::vector<uint8_t>* skip = nullptr,
                          std::vector<int>* itemWidths = nullptr) {
    Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    Rgb red = g_light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    auto sc1 = widget::SeverityColor(1, g_light);
    auto sc2 = widget::SeverityColor(2, g_light);
    Rgb warnColor{ sc1.r, sc1.g, sc1.b };
    Rgb critColor{ sc2.r, sc2.g, sc2.b };
    Rgb charge = g_light ? Rgb{ 0, 120, 212 } : Rgb{ 96, 205, 255 };
    Rgb full = g_light ? Rgb{ 16, 124, 16 } : Rgb{ 108, 203, 95 };  // Windows "success" green
    HFONT icons = MakeFont(Px(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT bolt = MakeFont(Px(11), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT text = MakeFont(Px(13), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI", Px(12));
    HFONT caption = MakeFont(std::max(8, Px(7)), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");  // never under 8 px

    int itemGap = phase >= metrics::Phase::S2_Compact ? Px(6) : Px(14);
    std::vector<Piece> out;
    struct Slot { int icon, word, label, bolt, inlineTemp, itemIdx; };  // indices of one device's pieces (-1 for absent)
    std::vector<Slot> slots;
    if (itemWidths) itemWidths->assign(g_items.size(), 0);
    if (g_items.empty()) out.push_back({ RenderMask(std::wstring(1, kBatteryUnknown), icons, g_light), fg, 110, 0 });
    size_t lastVisible = g_items.size();
    for (size_t i = g_items.size(); i-- > 0;) {
        if (!skip || !(*skip)[i]) { lastVisible = i; break; }
    }
    for (size_t i = 0; i < g_items.size(); i++) {
        if (skip && (*skip)[i]) continue;
        const auto& it = g_items[i];
        int a = it.dim ? 120 : 255;
        Rgb tc = it.severity >= 2 ? critColor : it.severity == 1 ? warnColor : it.low ? red : fg;
        bool mark = it.charging || it.full;  // bolt while charging, green check mark once full
        bool hasInline = !it.inlineTempText.empty();
        bool showWord = (phase == metrics::Phase::S0_Full) && !it.word.empty() && it.labelStyle != 0;
        bool showGlyph = false;
        switch (phase) {
            case metrics::Phase::S0_Full:          showGlyph = (it.labelStyle != 1); break;
            case metrics::Phase::S1_NoWords:
            case metrics::Phase::S2_Compact:       showGlyph = true; break;
            case metrics::Phase::S3_NoMetricGlyph: showGlyph = (it.metric < 0); break;
            default:                               showGlyph = false; break;
        }
        bool branded = it.accent != Accent::None;
        BrandColor bc = AccentColor(it.accent, g_light);
        Rgb accent{ bc.r, bc.g, bc.b };
        int glyphIdx = -1;
        int iw = 0;
        if (showGlyph) {
            glyphIdx = (int)out.size();
            Piece icon{ RenderMask(std::wstring(1, Glyph(it.kind, it.bluetooth, it.glyphOverride)), icons, g_light), branded ? accent : fg, a, Px(5) };
            if (branded && !it.dim) {
                icon.haloR = Px(3);
                icon.halo = Halo(icon.m, icon.haloR);
                icon.haloAlpha = g_light ? 60 : 140;
            }
            iw += icon.m.w + icon.gapAfter;
            out.push_back(std::move(icon));
        }
        int wordIdx = -1;
        if (showWord) {
            wordIdx = (int)out.size();
            Piece wp{ RenderMask(it.word, text, g_light), fg, std::min(a, 170), Px(4) };
            iw += wp.m.w + wp.gapAfter;
            out.push_back(std::move(wp));
        }
        int labelIdx = (int)out.size();
        std::wstring t = !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + L"%" : L"?";
        Piece label{ RenderMask(t, text, g_light), tc, a, mark ? Px(2) : (hasInline ? Px(4) : 0) };
        if (branded && it.percent >= 0) {
            label.barPct = it.percent;
            label.barColor = it.low ? red : accent;
        }
        iw += label.m.w + label.gapAfter;
        out.push_back(std::move(label));
        int boltIdx = -1;
        if (mark) {
            boltIdx = (int)out.size();
            Piece mp{ RenderMask(std::wstring(1, it.full ? kCheck : kBolt), bolt, g_light), it.full ? full : charge, a, hasInline ? Px(4) : 0 };
            iw += mp.m.w + mp.gapAfter;
            out.push_back(std::move(mp));
        }
        int inlineIdx = -1;
        if (hasInline) {
            inlineIdx = (int)out.size();
            Rgb itc = it.inlineTempSeverity >= 2 ? critColor : it.inlineTempSeverity == 1 ? warnColor : fg;
            Piece ip{ RenderMask(it.inlineTempText, text, g_light), itc, a, 0 };
            iw += ip.m.w;
            out.push_back(std::move(ip));
        }
        slots.push_back({ glyphIdx, wordIdx, labelIdx, boltIdx, inlineIdx, (int)i });
        int gap = i < lastVisible ? itemGap : 0;
        out.back().gapAfter = gap;
        if (itemWidths) (*itemWidths)[i] = iw;
        if (showGlyph && phase <= metrics::Phase::S1_NoWords && !it.label.empty()) {  // tiny caption in the bottom-right corner of the icon
            Piece c{ RenderMask(it.label, caption, g_light), fg, a, 0 };
            c.anchor = glyphIdx;
            c.bx = out[glyphIdx].m.w - c.m.w + Px(3);
            c.by = out[glyphIdx].m.h - c.m.h + Px(3);
            out.push_back(c);
        }
    }
    DeleteObject(icons);
    DeleteObject(bolt);
    DeleteObject(text);
    DeleteObject(caption);

    height = barH;
    const int rowGap = Px(2), barRoom = Px(5);  // barRoom: space for a brand level bar under the text
    int iconH = 0, textH = 0;
    for (const Slot& s : slots) {
        if (s.icon >= 0) iconH = std::max(iconH, out[s.icon].m.h);
        if (s.label >= 0) textH = std::max(textH, out[s.label].m.h);
    }
    bool stacked = !g_vertical && g_stacked && !slots.empty() && barH >= iconH + rowGap + textH + barRoom + Px(6);
    if (g_vertical && !slots.empty()) {
        // One block per device, centred in the bar's width: icon, then the percentage (and bolt) under it.
        width = barW;
        int y = Px(6);
        for (const Slot& s : slots) {
            if (s.label < 0) {
                Piece& ic = out[s.icon];
                ic.px = (barW - ic.m.w) / 2;
                ic.py = y;
                y += ic.m.h + itemGap;
                continue;
            }
            if (s.icon >= 0) {
                Piece& ic = out[s.icon];
                ic.px = (barW - ic.m.w) / 2;
                ic.py = y;
                y += ic.m.h + rowGap;
            }
            Piece& lb = out[s.label];
            int wordW = s.word >= 0 ? out[s.word].m.w + Px(4) : 0;
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            int inlineW = s.inlineTemp >= 0 ? Px(4) + out[s.inlineTemp].m.w : 0;
            int rowW = wordW + lb.m.w + boltW + inlineW;
            int rx = (barW - rowW) / 2;
            int rowH = s.word >= 0 ? std::max(out[s.word].m.h, lb.m.h) : lb.m.h;
            if (s.word >= 0) {
                out[s.word].px = rx;
                out[s.word].py = y + (rowH - out[s.word].m.h) / 2;
                rx += wordW;
            }
            lb.px = rx;
            lb.py = y + (rowH - lb.m.h) / 2;
            rx += lb.m.w;
            if (s.bolt >= 0) {
                out[s.bolt].px = rx + Px(2);
                out[s.bolt].py = y + (rowH - out[s.bolt].m.h) / 2;
                rx += boltW;
            }
            if (s.inlineTemp >= 0) {
                out[s.inlineTemp].px = rx + Px(4);
                out[s.inlineTemp].py = y + (rowH - out[s.inlineTemp].m.h) / 2;
            }
            y += rowH + (lb.barPct >= 0 ? barRoom : 0) + itemGap;
        }
        height = y;
        return out;
    }
    if (stacked) {
        // Icon on top (bolt to its right), percentage centred under it; the pair centred in the bar's height.
        int top = (barH - (iconH + rowGap + textH + barRoom)) / 2;
        int x = Px(8);
        for (const Slot& s : slots) {
            if (s.label < 0) {
                Piece& ic = out[s.icon];
                ic.px = x;
                ic.py = (barH - ic.m.h) / 2;
                x += ic.m.w + itemGap;
                continue;
            }
            Piece& lb = out[s.label];
            int wordW = s.word >= 0 ? out[s.word].m.w + Px(4) : 0;
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            int inlineW = s.inlineTemp >= 0 ? Px(4) + out[s.inlineTemp].m.w : 0;
            int textW = wordW + lb.m.w + inlineW + (s.icon >= 0 ? 0 : boltW);
            int iconW = s.icon >= 0 ? out[s.icon].m.w + boltW : 0;
            int slotW = std::max(iconW, textW);
            if (itemWidths && s.itemIdx >= 0) (*itemWidths)[s.itemIdx] = slotW;
            if (s.icon >= 0) {
                Piece& ic = out[s.icon];
                ic.px = x + (slotW - ic.m.w - boltW) / 2;
                ic.py = top + (iconH - ic.m.h) / 2;
                if (s.bolt >= 0) {
                    out[s.bolt].px = ic.px + ic.m.w + Px(2);
                    out[s.bolt].py = top + (iconH - out[s.bolt].m.h) / 2;
                }
            }
            int rowH = s.word >= 0 ? std::max(out[s.word].m.h, lb.m.h) : lb.m.h;
            int tx = x + (slotW - textW) / 2;
            int ty = s.icon >= 0 ? (top + iconH + rowGap) : ((barH - rowH) / 2);
            if (s.word >= 0) {
                out[s.word].px = tx;
                out[s.word].py = ty + (rowH - out[s.word].m.h) / 2;
                tx += wordW;
            }
            lb.px = tx;
            lb.py = ty + (rowH - lb.m.h) / 2;
            if (s.inlineTemp >= 0) {
                out[s.inlineTemp].px = lb.px + lb.m.w + Px(4);
                out[s.inlineTemp].py = ty + (rowH - out[s.inlineTemp].m.h) / 2;
            }
            if (s.bolt >= 0 && s.icon < 0) {  // value-only rows: the bolt rides after the text
                int ex = lb.px + lb.m.w;
                if (s.inlineTemp >= 0) ex += Px(4) + out[s.inlineTemp].m.w;
                out[s.bolt].px = ex + Px(2);
                out[s.bolt].py = ty + (rowH - out[s.bolt].m.h) / 2;
            }
            x += slotW + itemGap;
        }
        width = x - itemGap + Px(8);
        return out;
    }
    width = Px(8) * 2;
    for (auto& p : out)
        if (p.anchor < 0) width += p.m.w + p.gapAfter;
    return out;
}

struct LayoutCache {
    bool valid = false;
    bool light = false;
    int dpi = 0;
    bool stacked = false;
    bool vertical = false;
    int barW = 0, barH = 0;
    int maxW = INT_MAX;
    std::vector<WidgetItem> items;
    std::vector<Piece> pieces;
    int w = 0, h = 0;
    int padLeft = 0;
    bool rtl = false;
    std::wstring hiddenTip;
};

LayoutCache g_primaryCache;
std::vector<std::pair<HWND, LayoutCache>> g_mirrorCaches;

bool SameItems(const std::vector<WidgetItem>& a, const std::vector<WidgetItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.kind != y.kind || x.percent != y.percent || x.text != y.text ||
            x.charging != y.charging || x.full != y.full || x.dim != y.dim ||
            x.low != y.low || x.bluetooth != y.bluetooth || x.label != y.label ||
            x.accent != y.accent || x.metric != y.metric || x.glyphOverride != y.glyphOverride ||
            x.name != y.name || x.severity != y.severity ||
            x.inlineTempText != y.inlineTempText || x.inlineTempSeverity != y.inlineTempSeverity ||
            x.word != y.word || x.labelStyle != y.labelStyle)
            return false;
    }
    return true;
}

LayoutCache& CacheFor(HWND wnd) {
    if (wnd == g_wnd) return g_primaryCache;
    for (auto& kv : g_mirrorCaches)
        if (kv.first == wnd) return kv.second;
    g_mirrorCaches.push_back({ wnd, LayoutCache{} });
    return g_mirrorCaches.back().second;
}

void InvalidateAllCaches() {
    g_primaryCache.valid = false;
    for (auto& kv : g_mirrorCaches) kv.second.valid = false;
}

void PaintStrip(HWND wnd, HWND taskbar, SIZE& size, int maxW, float hoverT) {
    RECT bar;
    GetClientRect(taskbar, &bar);
    int barW = bar.right - bar.left, barH = bar.bottom - bar.top;
    int dpi = Dpi();
    bool rtl = (GetWindowLongW(taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    LayoutCache& cache = CacheFor(wnd);
    bool cacheHit = cache.valid && cache.light == g_light && cache.dpi == dpi &&
                    cache.stacked == g_stacked && cache.vertical == g_vertical && cache.rtl == rtl &&
                    cache.barW == barW && cache.barH == barH && cache.maxW == maxW &&
                    SameItems(cache.items, g_items);
    if (!cacheHit) {
        int W = 0, H = 0;
        int padLeft = 0;
        std::vector<int> wFull, wGlyph, wValue;
        metrics::Phase phase = metrics::Phase::S0_Full;
        auto pieces = Layout(barW, barH, W, H, phase, nullptr, &wFull);
        std::wstring hiddenTip;
        if (maxW < INT_MAX && !g_vertical && W > maxW && !g_items.empty()) {
            int dummyW = 0, dummyH = 0;
            Layout(barW, barH, dummyW, dummyH, metrics::Phase::S1_NoWords, nullptr, &wGlyph);
            Layout(barW, barH, dummyW, dummyH, metrics::Phase::S4_ValueOnly, nullptr, &wValue);
            std::vector<metrics::OverflowItem> ovItems(g_items.size());
            for (size_t i = 0; i < g_items.size(); i++) {
                ovItems[i].wFull = wFull[i];
                ovItems[i].wGlyph = wGlyph[i];
                ovItems[i].wValue = wValue[i];
                ovItems[i].isMetric = g_items[i].metric >= 0;
                ovItems[i].low = g_items[i].low;
                ovItems[i].alertSeverity = std::max(g_items[i].severity, g_items[i].inlineTempSeverity);
            }
            auto ov = metrics::SelectOverflowPhase(ovItems, maxW, Px(8) * 2, Px(14), Px(6));
            phase = ov.phase;
            std::vector<uint8_t> skip(g_items.size(), 0);
            if (!ov.hidden.empty()) {
                std::wstring names;
                for (int idx : ov.hidden) {
                    if (idx >= 0 && idx < static_cast<int>(g_items.size())) {
                        skip[idx] = 1;
                        if (!g_items[idx].name.empty()) {
                            if (!names.empty()) names += L", ";
                            names += g_items[idx].name;
                        }
                    }
                }
                if (wnd == g_wnd && !names.empty()) hiddenTip = T(L"Hidden") + std::wstring(L": ") + names;
            }
            if (phase != metrics::Phase::S0_Full) {
                pieces = Layout(barW, barH, W, H, phase, ov.hidden.empty() ? nullptr : &skip);
            }
        }
        if (wnd == g_wnd && !g_vertical && !rtl && maxW < INT_MAX && phase >= metrics::Phase::S4_ValueOnly) {
            int pitch = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
            int alignedW = pitch > 0 ? ((W + pitch - 1) / pitch) * pitch : W;
            if (alignedW > W && alignedW <= maxW) {  // snap to the taskbar grid only while it still fits
                padLeft = alignedW - W;
                W = alignedW;
            }
        }
        cache.valid = true;
        cache.light = g_light;
        cache.dpi = dpi;
        cache.stacked = g_stacked;
        cache.vertical = g_vertical;
        cache.rtl = rtl;
        cache.barW = barW;
        cache.barH = barH;
        cache.maxW = maxW;
        cache.items = g_items;
        cache.pieces = std::move(pieces);
        cache.w = W;
        cache.h = H;
        cache.padLeft = padLeft;
        cache.hiddenTip = std::move(hiddenTip);
    }
    const auto& pieces = cache.pieces;
    int W = cache.w, H = cache.h;
    if (wnd == g_wnd) {
        g_hiddenTip = cache.hiddenTip;
        if (!cacheHit && g_tip && !g_tipText.empty()) {
            std::wstring fullTip = g_hiddenTip.empty() ? g_tipText : (g_hiddenTip + L"\n" + g_tipText);
            TTTOOLINFOW ti{ sizeof(ti) };
            ti.hwnd = g_notify;
            ti.lpszText = const_cast<wchar_t*>(fullTip.c_str());
            ti.uId = (UINT_PTR)g_wnd;
            SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        }
    }
    if (W <= 0 || H <= 0) return;
    Canvas cv(W, H);

    // Nearly transparent background so the whole strip receives mouse input,
    // plus a rounded hover highlight like the system tray icons have.
    for (auto& p : cv.px) p = 0x01000000;
    auto roundRect = [&](int inset, int r, Rgb c, int a) {
        for (int y = inset; y < H - inset; y++)
            for (int x = 0; x < W; x++) {
                int dx = x < r ? r - x : (x >= W - r ? x - (W - r - 1) : 0);
                int dy = y < inset + r ? inset + r - y : (y >= H - inset - r ? y - (H - inset - r - 1) : 0);
                if (dx * dx + dy * dy <= r * r) cv.Blend(x, y, c, a);
            }
    };
    // Optional plate in the taskbar's own colours: keeps the text readable when a taskbar theme
    // (e.g. Windhawk's Taskbar Styler) makes the bar transparent over the wallpaper.
    Rgb barColor = g_light ? Rgb{ 243, 243, 243 } : Rgb{ 32, 32, 32 };
    if (g_background) roundRect(Px(4), Px(6), barColor, 240);
    if (hoverT > 0.0f) {
        int a = static_cast<int>((g_light ? 20 : 26) * hoverT);
        if (a > 0) roundRect(Px(4), Px(4), g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, a);
    }

    int x = Px(8) + cache.padLeft;
    Rgb track = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    std::vector<int> xs(pieces.size()), ys(pieces.size());
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (p.anchor < 0) {
            bool fixed = p.px != INT_MIN;  // stacked / vertical layout: Layout() placed it
            xs[i] = fixed ? p.px + cache.padLeft : x;
            ys[i] = fixed ? p.py : (H - p.m.h) / 2;
            if (p.haloR) cv.Draw(p.halo, xs[i] - p.haloR, ys[i] - p.haloR, p.color, p.haloAlpha * p.alpha / 255);
            if (p.barPct >= 0) {
                // The bar spans the text's width, like Synapse's battery bar; in the row layout text and bar
                // are centred together.
                int bh = std::max(2, Px(2)), gap = Px(3);
                if (!fixed) ys[i] = (H - (p.m.h + gap + bh)) / 2;
                int by = ys[i] + p.m.h + gap;
                int fill = std::max(p.barPct > 0 ? 1 : 0, (p.barPct * p.m.w + 50) / 100);
                for (int yy = by; yy < by + bh; yy++)
                    for (int xx = 0; xx < p.m.w; xx++)
                        cv.Blend(xs[i] + xx, yy, xx < fill ? p.barColor : track, xx < fill ? p.alpha : 45 * p.alpha / 255);
            }
            if (!fixed) x += p.m.w + p.gapAfter;
        } else {  // caption: one-pixel outline in the taskbar's colour so it reads over the icon's lines
            xs[i] = xs[p.anchor] + p.bx;
            ys[i] = ys[p.anchor] + p.by;
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++)
                    if (ox || oy) cv.Draw(p.m, xs[i] + ox, ys[i] + oy, barColor, 255);
        }
        cv.Draw(p.m, xs[i], ys[i], p.color, p.alpha);
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, cv.px.data(), (size_t)W * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    size = { W, H };
    SIZE sz{ W, H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Paint() {
    if (!g_wnd) return;
    SIZE prev = g_size;
    PaintStrip(g_wnd, g_taskbar, g_size, g_maxW, g_hoverT);
    if (g_size.cx != prev.cx || g_size.cy != prev.cy) g_lastW = -1;  // force a re-position with the new size
}

void PaintMirror(Mirror& m) {
    g_dpiBar = m.bar;
    m.dpi = Dpi();
    SIZE prev = m.size;
    PaintStrip(m.wnd, m.bar, m.size, INT_MAX, m.hoverT);
    g_dpiBar = nullptr;
    if (m.size.cx != prev.cx || m.size.cy != prev.cy) m.lastLeft = INT_MIN;  // force a re-position with the new size
}

void HideMirror(Mirror& m) {
    if (m.lastLeft != INT_MIN || IsWindowVisible(m.wnd)) ShowWindow(m.wnd, SW_HIDE);
    m.lastLeft = INT_MIN;
}

// Places a mirror left of its taskbar's clock (and left of the Widgets button if present), or hides it
// when that spot is unknown or taken by task buttons: never guess and cover something.
void PositionMirror(Mirror& m) {
    RECT bar{};
    if (!GetWindowRect(m.bar, &bar) || (GetWindowLongW(m.bar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)) { HideMirror(m); return; }
    g_dpiBar = m.bar;
    if (Dpi() != m.dpi || m.size.cy != bar.bottom - bar.top) {
        CacheFor(m.wnd).valid = false;
        PaintMirror(m), g_dpiBar = m.bar;
    }
    int right = 0, minLeft = bar.left;
    bool known = false;
    // StartAllBack can put a full notification area on each secondary taskbar.
    // Use its left edge, as on the primary taskbar, to clear all of the tray icons.
    HWND tray = FindWindowExW(m.bar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (tray && IsWindowVisible(tray) && GetWindowRect(tray, &tr) &&
        tr.left > bar.left && tr.right <= bar.right && tr.right > tr.left &&
        tr.bottom > bar.top && tr.top < bar.bottom) {
        right = tr.left;
        known = true;
    } else if (!IsXamlTaskbar(m.bar)) {  // Windows 10: the clock is a real child window (absent when it is turned off)
        HWND clock = FindWindowExW(m.bar, nullptr, L"ClockButton", nullptr);
        RECT cr{};
        right = clock && IsWindowVisible(clock) && GetWindowRect(clock, &cr) ? cr.left : bar.right - Px(8);
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(m.bar, nullptr, cls, nullptr);
            RECT fr{};
            if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && fr.left < right && fr.right > fr.left)
                right = fr.left;
        }
        known = true;
    } else {
        AcquireSRWLockShared(&g_wbLock);
        for (const auto& s : g_spans)
            if (s.bar == m.bar && s.measured) {
                right = s.hasClock ? s.clockLeft : bar.right - Px(8);
                if (s.hasWidgets && s.widgets.left < right && s.widgets.right > right - m.size.cx - Px(2))
                    right = s.widgets.left;
                if (s.hasTasks) minLeft = s.tasksRight + Px(4);
                known = true;
            }
        ReleaseSRWLockShared(&g_wbLock);
    }
    DWORD offset = 0, sz = sizeof(offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"SecondaryTaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    int left = right - m.size.cx - Px(2);
    g_dpiBar = nullptr;
    if (!known || m.size.cx <= 0 || left < minLeft) { HideMirror(m); return; }
    POINT p{ left, bar.top };
    ScreenToClient(m.bar, &p);
    bool covered = GetWindow(m.wnd, GW_HWNDPREV) != nullptr;
    if (!covered && p.x == m.lastLeft) return;
    m.lastLeft = p.x;
    SetWindowPos(m.wnd, HWND_TOP, p.x, 0, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void AddTool(HWND w) {
    if (!g_tip) return;
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_notify;
    ti.uId = (UINT_PTR)w;
    ti.lpszText = const_cast<wchar_t*>(g_tipText.empty() ? L"Monitor" : g_tipText.c_str());
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void DestroyMirror(Mirror& m) {
    if (g_tip) {
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.uId = (UINT_PTR)m.wnd;
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    g_mirrorCaches.erase(
        std::remove_if(g_mirrorCaches.begin(), g_mirrorCaches.end(),
                       [&m](const auto& kv) { return kv.first == m.wnd; }),
        g_mirrorCaches.end());
    if (m.wnd && IsWindow(m.wnd)) {
        KillTimer(m.wnd, TIMER_HOVER);
        DestroyWindow(m.wnd);
    }
    m.wnd = nullptr;
}

void SetMeasureMirrors(bool on) {
    AcquireSRWLockExclusive(&g_wbLock);
    bool changed = g_wbMirrors != on;
    g_wbMirrors = on;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (changed && on && g_wbWake) SetEvent(g_wbWake);  // measure the new taskbar now, not in 5 s
}

std::vector<HWND> SecondaryTaskbars() {
    std::vector<HWND> out;
    for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
         t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr)) {
        RECT r{};
        if (IsWindowVisible(t) && GetWindowRect(t, &r) && r.right - r.left > r.bottom - r.top) out.push_back(t);  // horizontal only
    }
    return out;
}

// Keeps one mirror per secondary taskbar: adds them for new monitors, removes them for gone ones.
// Called from Reposition() about once a second; with one monitor it is a single FindWindowEx call.
// A monitor's lasting identity: its device interface name, which survives reboots, Explorer restarts and
// Windows renumbering the displays (falls back to "\\.\DISPLAYn").
std::wstring MonitorId(HWND bar) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return L"";
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME) && dd.DeviceID[0]) return dd.DeviceID;
    return mi.szDevice;
}

// "Display 3 (right)": the number Windows gives the display and where it sits next to the main one.
std::wstring MonitorLabel(HWND bar) {
    MONITORINFOEXW mi{}, pm{};
    mi.cbSize = pm.cbSize = sizeof(MONITORINFOEXW);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi)) return T(L"Other monitor");
    std::wstring dev = mi.szDevice;
    size_t d = dev.find(L"DISPLAY");
    std::wstring label = d != std::wstring::npos ? TF(L"Display {number}", { { L"number", dev.substr(d + 7) } }) : T(L"Other monitor");
    POINT origin{ 0, 0 };
    if (GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &pm)) {
        int dx = (mi.rcMonitor.left + mi.rcMonitor.right - pm.rcMonitor.left - pm.rcMonitor.right) / 2;
        int dy = (mi.rcMonitor.top + mi.rcMonitor.bottom - pm.rcMonitor.top - pm.rcMonitor.bottom) / 2;
        label += std::wstring(L" (") + T(abs(dx) >= abs(dy) ? (dx < 0 ? L"left" : L"right") : (dy < 0 ? L"above" : L"below")) + L")";
    }
    return label;
}

// Hides or shows the strip on the main taskbar (it keeps running, just out of sight).
void SetPrimaryHidden(bool hide) {
    if (hide == g_primaryHidden || !g_wnd) return;
    g_primaryHidden = hide;
    if (hide) {
        ShowWindow(g_wnd, SW_HIDE);
        if (g_rebar && IsWindow(g_rebar)) {  // Windows 10: give the task buttons their full width back
            RECT r;
            GetClientRect(g_taskbar, &r);
            PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
        }
    }
    g_lastW = -1;  // re-place (and show) on the next pass
}

void SyncMirrors() {
    std::vector<HWND> all = g_wnd ? SecondaryTaskbars() : std::vector<HWND>{}, bars;
    bool chosenFound = false;
    if (g_choice == L"all") {
        bars = all;
    } else if (g_choice != L"main") {
        for (HWND b : all)
            if (MonitorId(b) == g_choice) {
                bars.push_back(b);
                chosenFound = true;
                break;
            }
    }
    SetPrimaryHidden(chosenFound);
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (std::find(bars.begin(), bars.end(), it->bar) == bars.end() || !IsWindow(it->wnd)) {
            DestroyMirror(*it);
            it = g_mirrors.erase(it);
        } else {
            ++it;
        }
    }
    for (HWND b : bars) {
        if (std::any_of(g_mirrors.begin(), g_mirrors.end(), [b](const Mirror& m) { return m.bar == b; })) continue;
        Mirror m;
        m.bar = b;
        m.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, b, nullptr,
                                g_inst, nullptr);
        if (!m.wnd) continue;
        AddTool(m.wnd);
        g_mirrors.push_back(m);
        PaintMirror(g_mirrors.back());
    }
    SetMeasureMirrors(!g_mirrors.empty());
    for (auto& m : g_mirrors) PositionMirror(m);
}

void PaintAll() {
    Paint();
    for (auto& m : g_mirrors) PaintMirror(m);
}

float HoverStep(ULONGLONG now, float from, ULONGLONG start, int dir, bool& done) {
    int dur = dir > 0 ? kHoverInMs : kHoverOutMs;
    float p = std::clamp((float)(now - start) / (float)dur, 0.0f, 1.0f);
    done = p >= 1.0f;
    float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    return dir > 0 ? from + (1.0f - from) * e : from * (1.0f - e);
}

bool FindHoverState(HWND h, float*& t, float*& from, ULONGLONG*& start, int*& dir, Mirror*& mirror) {
    mirror = nullptr;
    if (h == g_wnd) {
        t = &g_hoverT;
        from = &g_hoverFrom;
        start = &g_hoverStart;
        dir = &g_hoverDir;
        return true;
    }
    for (auto& m : g_mirrors) {
        if (m.wnd == h) {
            mirror = &m;
            t = &m.hoverT;
            from = &m.hoverFrom;
            start = &m.hoverStart;
            dir = &m.hoverDir;
            return true;
        }
    }
    return false;
}

void StartHover(HWND h, bool on) {
    float* t = nullptr;
    float* from = nullptr;
    ULONGLONG* start = nullptr;
    int* dir = nullptr;
    Mirror* mirror = nullptr;
    if (!FindHoverState(h, t, from, start, dir, mirror)) return;

    int targetDir = on ? 1 : -1;
    if (*dir == targetDir) return;
    if (*dir == 0 && (on ? *t >= 1.0f : *t <= 0.0f)) return;

    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    if (!anim) {
        *t = on ? 1.0f : 0.0f;
        *dir = 0;
        KillTimer(h, TIMER_HOVER);
        if (mirror) PaintMirror(*mirror);
        else Paint();
        return;
    }

    *from = *t;
    *start = GetTickCount64();
    *dir = targetDir;
    SetTimer(h, TIMER_HOVER, 16, nullptr);
}

void OnHoverTimer(HWND h) {
    float* t = nullptr;
    float* from = nullptr;
    ULONGLONG* start = nullptr;
    int* dir = nullptr;
    Mirror* mirror = nullptr;
    if (!FindHoverState(h, t, from, start, dir, mirror) || *dir == 0) {
        KillTimer(h, TIMER_HOVER);
        return;
    }
    bool done = false;
    *t = HoverStep(GetTickCount64(), *from, *start, *dir, done);
    if (done) {
        *t = *dir > 0 ? 1.0f : 0.0f;
        *dir = 0;
        KillTimer(h, TIMER_HOVER);
    }
    if (mirror) PaintMirror(*mirror);
    else Paint();
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            StartHover(h, true);
            return 0;
        }
        case WM_MOUSELEAVE:
            StartHover(h, false);
            return 0;
        case WM_TIMER:
            if (wp == TIMER_HOVER) {
                OnHoverTimer(h);
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            PostMessageW(g_notify, g_clickMsg, 1, 0);
            return 0;
        case WM_RBUTTONUP:
            PostMessageW(g_notify, g_clickMsg, 0, 0);
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// The taskbar is topmost too, so a tooltip shown while the bar is active ends up behind it and
// loses its last lines. Re-raise the tip whenever the control makes it visible.
WNDPROC g_tipOrig = nullptr;
LRESULT CALLBACK TipProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SHOWWINDOW && wp)
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return CallWindowProcW(g_tipOrig, h, msg, wp, lp);
}

}  // namespace

namespace widget {

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme) {
    Destroy();
    g_taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!g_taskbar || !FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr)) { g_taskbar = nullptr; return false; }
    RECT tb;
    GetWindowRect(g_taskbar, &tb);
    g_vertical = tb.bottom - tb.top > tb.right - tb.left;  // left / right taskbar: devices stacked in a column
    // Windows 11's taskbar is a XAML island; on Windows 10 the task buttons live in ReBarWindow32
    // and have to be narrowed so they never slide under the widget.
    g_rebar = FindWindowExW(g_taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)
                  ? nullptr
                  : FindWindowExW(g_taskbar, nullptr, L"ReBarWindow32", nullptr);
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    g_light = lightTheme;
    g_inst = inst;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1,
                            g_taskbar, nullptr, inst, nullptr);
    if (!g_wnd) { g_taskbar = nullptr; return false; }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                            notifyWnd, nullptr, inst, nullptr);
    if (g_tip) {
        g_tipOrig = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_tip, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TipProc)));
        ApplyTipTheme();
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = notifyWnd;
        ti.uId = (UINT_PTR)g_wnd;
        ti.lpszText = const_cast<wchar_t*>(L"Monitor");
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 600);
    }
    WatchWidgetsButton(g_taskbar);
    g_lastDpi = Dpi();
    Paint();
    g_lastW = -1;
    Reposition();
    if (!g_primaryHidden) ShowWindow(g_wnd, SW_SHOWNOACTIVATE);  // hidden when only another monitor is chosen
    return true;
}

void Destroy() {
    if (g_rebar && IsWindow(g_rebar) && g_taskbar && IsWindow(g_taskbar)) {
        // Let Explorer lay the task buttons out again over the full width.
        RECT r;
        GetClientRect(g_taskbar, &r);
        PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    g_rebar = nullptr;
    for (auto& m : g_mirrors) DestroyMirror(m);
    g_mirrors.clear();
    SetMeasureMirrors(false);
    g_primaryHidden = false;  // the next Create() decides again
    if (g_wbWake) WatchWidgetsButton(nullptr);
    if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
    if (g_wnd) { KillTimer(g_wnd, TIMER_HOVER); DestroyWindow(g_wnd); g_wnd = nullptr; }
    g_taskbar = nullptr;
    g_hoverT = 0.0f;
    g_hoverFrom = 0.0f;
    g_hoverStart = 0;
    g_hoverDir = 0;
    g_primaryCache = {};
    g_mirrorCaches.clear();
}

bool Alive() { return g_wnd && IsWindow(g_wnd) && g_taskbar && IsWindow(g_taskbar); }

void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip) {
    g_items = items;
    InvalidateAllCaches();
    if (tooltip != g_tipText && g_tip) {
        g_tipText = tooltip;
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.lpszText = const_cast<wchar_t*>(g_tipText.c_str());
        ti.uId = (UINT_PTR)g_wnd;
        SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        for (const auto& m : g_mirrors) {
            ti.uId = (UINT_PTR)m.wnd;
            SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
        }
    }
    PaintAll();
    Reposition();
}

void SetLightTheme(bool light) {
    g_light = light;
    InvalidateAllCaches();
    ApplyTipTheme();
    PaintAll();
}

void SuspendHover() {
    bool changed = g_hoverT != 0.0f || g_hoverDir != 0;
    g_hoverT = 0.0f;
    g_hoverFrom = 0.0f;
    g_hoverStart = 0;
    g_hoverDir = 0;
    if (g_wnd) {
        KillTimer(g_wnd, TIMER_HOVER);
        if (changed) Paint();
    }
    for (auto& m : g_mirrors) {
        bool mirrorChanged = m.hoverT != 0.0f || m.hoverDir != 0;
        m.hoverT = 0.0f;
        m.hoverFrom = 0.0f;
        m.hoverStart = 0;
        m.hoverDir = 0;
        if (m.wnd) {
            KillTimer(m.wnd, TIMER_HOVER);
            if (mirrorChanged) PaintMirror(m);
        }
    }
}

void SetTooltipActive(bool active) {
    if (g_tip) SendMessageW(g_tip, TTM_ACTIVATE, active ? TRUE : FALSE, 0);
}

void SetBackground(bool on) {
    g_background = on;
    PaintAll();
}

void SetStacked(bool on) {
    g_stacked = on;
    InvalidateAllCaches();
    PaintAll();
    if (g_wnd) Reposition();
}

void SetMonitorChoice(const std::wstring& choice) {
    g_choice = choice.empty() ? L"all" : choice;
    SyncMirrors();  // adds, removes or hides strips right away
    if (g_wnd) Reposition();
}

std::vector<MonitorOption> OtherMonitors() {
    std::vector<MonitorOption> out;
    for (HWND b : SecondaryTaskbars()) out.push_back({ MonitorId(b), MonitorLabel(b) });
    std::sort(out.begin(), out.end(), [](const MonitorOption& a, const MonitorOption& b) { return a.label < b.label; });
    return out;
}

bool Reposition() {
    if (!Alive()) return false;
    SyncMirrors();
    if (g_primaryHidden) return true;  // only another monitor's taskbar shows the strip
    if (Dpi() != g_lastDpi) { g_lastDpi = Dpi(); g_primaryCache.valid = false; Paint(); }
    HWND tray = FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray) return true;
    RECT tr, bar;
    GetWindowRect(tray, &tr);
    GetWindowRect(g_taskbar, &bar);
    // The taskbar can be moved between a horizontal and a vertical edge while the app runs.
    bool vertical = bar.bottom - bar.top > bar.right - bar.left;
    if (vertical != g_vertical) {
        g_vertical = vertical;
        g_primaryCache.valid = false;
        Paint();
    }
    if (g_vertical) {
        // Left or right taskbar (PR #321): the column sits just above the notification area, centred in
        // the bar's width.
        RECT client{};
        GetClientRect(g_taskbar, &client);
        POINT anchor{ bar.left, tr.top };
        ScreenToClient(g_taskbar, &anchor);
        int top = anchor.y - g_size.cy - Px(2);
        DWORD offset = 0, osz = sizeof(offset);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset,
                         &osz) == ERROR_SUCCESS)
            top -= Px((int)offset);
        top = std::clamp(top, 0, (int)std::max<LONG>(0, client.bottom - g_size.cy));
        int left = (int)std::max<LONG>(0, (client.right - g_size.cx) / 2);
        // Windows 10: shorten the task-button area when it would reach behind the column.
        if (g_rebar && IsWindow(g_rebar)) {
            RECT rr{};
            GetWindowRect(g_rebar, &rr);
            POINT tl{ rr.left, rr.top };
            ScreenToClient(g_taskbar, &tl);
            if (tl.y < top && tl.y + (rr.bottom - rr.top) > top)
                MoveWindow(g_rebar, tl.x, tl.y, rr.right - rr.left, top - tl.y, TRUE);
        }
        bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
        if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && g_lastW == g_size.cy && g_lastLeft == top)
            return true;
        g_lastTray = tr;
        g_lastBar = bar;
        g_lastW = g_size.cy;
        g_lastLeft = top;
        SetWindowPos(g_wnd, HWND_TOP, left, top, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }
    // Right-to-left Windows (Hebrew, Arabic) mirrors the taskbar: client x runs from the screen's right
    // edge, so a window's client-space left edge is its screen-space right edge.
    bool rtl = GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL;
    int edge = rtl ? tr.right : tr.left;  // screen x we sit next to
    // Windows 10: News and Interests / Weather sits immediately left of the tray. The window that hosts it
    // depends on the build: ShellFeedsTaskbarViewWindow on some, DynamicContent2 on others (e.g. 19045,
    // where the search highlights button is DynamicContent1). Both are direct children of the taskbar and
    // only a visible one counts. Its rectangle is read on every pass because the weather text changes its
    // width, so the widget follows it.
    if (!rtl)
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(g_taskbar, nullptr, cls, nullptr);
            if (!feeds || !IsWindowVisible(feeds)) continue;
            RECT fr;
            GetWindowRect(feeds, &fr);
            if (fr.left < edge && fr.right > fr.left) edge = fr.left;
        }
    // Step past the Widgets button when it sits where we would go.
    RECT wb = WidgetsButtonRect();
    if (wb.right > wb.left && wb.top < bar.bottom && wb.bottom > bar.top &&
        (rtl ? wb.left < edge + g_size.cx + Px(2) && wb.right > edge : wb.right > edge - g_size.cx - Px(2) && wb.left < edge))
        edge = rtl ? wb.right : wb.left;
    POINT p{ edge, bar.top };
    ScreenToClient(g_taskbar, &p);
    int rightClient = p.x - Px(2);

    // Optional user override in DPI-independent pixels (e.g. for custom padding).
    DWORD customOffset = 0, sz = sizeof(customOffset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset",
                     RRF_RT_REG_DWORD, nullptr, &customOffset, &sz) == ERROR_SUCCESS) {
        rightClient -= Px((int)customOffset);
    }

    int newMaxW = INT_MAX;
    int minLeft = 0;
    int appsR = 0;
    if (!rtl && PrimaryAppsRight(appsR)) {
        POINT pa{ appsR, bar.top };
        ScreenToClient(g_taskbar, &pa);
        minLeft = pa.x + Px(4);
        newMaxW = std::max(0, rightClient - minLeft);
    }
    if (newMaxW != g_maxW) {
        g_maxW = newMaxW;
        g_primaryCache.valid = false;
        Paint();
    }
    int left = rightClient - g_size.cx;
    if (minLeft > 0 && left < minLeft) left = minLeft;

    // Windows 10: Explorer re-expands the task buttons on its own layout passes; narrow them again.
    if (g_rebar && IsWindow(g_rebar)) {
        RECT rr;
        GetWindowRect(g_rebar, &rr);
        POINT tl{ rtl ? rr.right : rr.left, rr.top };
        ScreenToClient(g_taskbar, &tl);
        if (tl.x + (rr.right - rr.left) > left && left > tl.x)
            MoveWindow(g_rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
    }

    // Keep us above the taskbar's own content (our sibling windows).
    bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
    if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && EqualRect(&wb, &g_lastWidgets) &&
        g_lastW == g_size.cx && g_lastLeft == left)
        return true;
    g_lastTray = tr;
    g_lastWidgets = wb;
    g_lastBar = bar;
    g_lastW = g_size.cx;
    g_lastLeft = left;
    SetWindowPos(g_wnd, HWND_TOP, left, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

}  // namespace widget