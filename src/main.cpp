// Monitor - tray app showing the battery level of wireless mice, headsets and Bluetooth devices.
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "device_aliases.h"
#include "alert_rules.h"
#include "devices.h"
#include "battery_history.h"
#include "i18n.h"
#include "json_output.h"
#include "labels.h"
#include "report.h"
#include "rename_dialog.h"
#include "widget.h"
#include "update_win.h"
#include "update_apply.h"
#include "metrics/metrics.h"
#include "metrics/pawnio.h"
#include "metrics/temp_alerts.h"
#include "metrics/usage_alerts.h"

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT WM_METRICS = WM_APP + 4;
constexpr GUID kConsoleDisplayState = { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
constexpr UINT_PTR TIMER_ALERT_ICON = 3;
// "Check battery every" choices in seconds; 60 is the default. A poll resets an awake device's sleep timer,
// so a device only sleeps if its timer is shorter than the interval.
constexpr DWORD POLL_SECONDS[] = { 30, 60, 120, 300, 600, 900, 1800, 3600 };
constexpr int LOW_BATTERY = 15;
constexpr UINT PLACEHOLDER_ID = 1;
constexpr UINT ALERT_ICON_ID = 2;

enum MenuId : UINT {
    ID_REFRESH = 100, ID_AUTOSTART, ID_HIDE_LAST_KNOWN, ID_WIDGET_BACKGROUND, ID_EXIT, ID_MODE_WIDGET, ID_MODE_TRAY, ID_SUPPORT, ID_REPORT,
    ID_BRAND_COLORS, ID_STACKED, ID_SHOW_DISCONNECTED, ID_RECORD_HISTORY,
    ID_OFFSET_LEFT, ID_OFFSET_RIGHT, ID_OFFSET_RESET, ID_TASKMGR,
    ID_METRIC_GROUP = 150,
    ID_LABEL_FIRST = 151,  // "Component label": ID_LABEL_FIRST + 0..2
    ID_CPU_TEMP_ON = 154,
    ID_TEMP_ALERTS = 155,
    ID_USAGE_ALERTS = 156,
    ID_USAGE_THRESH_FIRST = 157,  // "Usage alerts at": 157..159 (85/90/95%)
    ID_TEMP_THRESH_FIRST = 160,   // "Temperature alerts at": 160..162 (70/75/80 C)
    ID_POLL_FIRST = 500,  // "Check battery every": ID_POLL_FIRST + 0..7
    ID_METRIC_INTERVAL_FIRST = 600,  // "Refresh rate": ID_METRIC_INTERVAL_FIRST + 0..2
    ID_METRIC_WHERE_FIRST = 700,     // per-metric location: ID_METRIC_WHERE_FIRST + id * 4 + where
    ID_UPDATE = 200, ID_AUTO_UPDATE,
    ID_LANG_AUTO = 400, ID_LANG_ADD, ID_LANG_FIRST,  // "Language": ID_LANG_FIRST + 0..63
    ID_MONITOR_ALL = 300, ID_MONITOR_MAIN, ID_MONITOR_FIRST,  // "Show the widget on": ID_MONITOR_FIRST + 0..31
    ID_DEVICE_FIRST = 1000
};
enum DeviceCmd : UINT {
    DEV_TOGGLE, DEV_LEFT, DEV_RIGHT, DEV_RENAME, DEV_RESET_NAME, DEV_HISTORY,
    DEV_ALERT_FIRST = 6, DEV_ALERT_DEFAULT = DEV_ALERT_FIRST + 8, DEV_ALERT_OFF, DEV_STRIDE = 16
};

const wchar_t* kAppName = L"Monitor";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"WirelessBattery";

const wchar_t* kSettingsKey = L"Software\\WirelessBattery";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker, g_metricWorker;
constexpr DWORD METRIC_POLL_SECONDS[] = { 1, 2, 5 };
constexpr int USAGE_THRESHOLDS[] = { 85, 90, 95 };
constexpr int TEMP_THRESHOLDS[] = { 70, 75, 80 };
DWORD g_metricIntervalMs = 5000;
metrics::Config g_metricCfg = metrics::kDefaultConfig;
metrics::Snapshot g_metricSnap{};
bool g_hasMetricSnap = false;
bool g_tempAlerts = true;
int g_tempAlertThreshold = 75;
bool g_tempSeenOk[metrics::kCount]{};
bool g_tempLatched[metrics::kCount]{};
bool g_usageAlerts = true;
int g_usageAlertThreshold = 90;
bool g_usageSeenOk[metrics::kCount]{};
bool g_usageLatched[metrics::kCount]{};
struct MetricTrayEntry {
    bool added = false;
    HICON icon = nullptr;
    std::wstring iconSig;
};
MetricTrayEntry g_metricTray[metrics::kCount]{};

struct MetricSpec {
    metrics::Id id;
    wchar_t glyph;
    const wchar_t* name;
    const wchar_t* label;
    const wchar_t* word;
};
constexpr MetricSpec kMetrics[] = {
    { metrics::Id::CpuUsage, 0xEEA1, L"CPU usage",       nullptr, L"CPU" },
    { metrics::Id::CpuTemp,  0xE957, L"CPU temperature", nullptr, L"CPU\u00B0" },
    { metrics::Id::RamUsage, 0xEEA0, L"RAM usage",       nullptr, L"RAM" },
    { metrics::Id::GpuUsage, 0xE950, L"GPU usage",       nullptr, L"GPU" },
    { metrics::Id::GpuTemp,  0xE957, L"GPU temperature", nullptr, L"GPU\u00B0" },
    { metrics::Id::Vram,     0xEEA0, L"VRAM",            L"V",    L"VRAM" },
    { metrics::Id::SsdTemp,  0xEDA2, L"SSD temperature", nullptr, L"SSD\u00B0" },
};

// The component an alert belongs to, so its balloon carries that part's icon (CPU, RAM...).
wchar_t ComponentGlyph(int id) {
    switch (static_cast<metrics::Id>(id)) {
    case metrics::Id::CpuTemp: return 0xEEA1;
    case metrics::Id::GpuTemp: return 0xE950;
    case metrics::Id::SsdTemp: return 0xEDA2;
    default: return kMetrics[id].glyph;
    }
}
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
bool g_wantWidget = true;      // user's choice: taskbar widget (default) or tray icons
bool g_useWidget = false;      // what is actually shown right now
bool g_hideLastKnown = false;  // hide (last known) / asleep devices from taskbar and tray
bool g_showDisconnectedDevices = false;  // opt-in, explicit driver-reported offline devices only
bool g_recordHistory = true;  // save readings for "Battery history..." (menu option, on by default)
std::wstring g_widgetMonitor = L"all";  // which taskbars show the widget: "all", "main" or one monitor id
bool g_widgetBackground = false;  // draw a plate behind the taskbar text (transparent taskbar themes)
bool g_stacked = false;      // icon above the percentage (menu option)
bool g_brandColors = false;  // off by default; known brands get their color (Razer green, Logitech blue, ...)
bool g_groupTempsWithUsage = false;
int g_metricLabelStyle = 0;
std::atomic<DWORD> g_pollIntervalMs{ 60 * 1000 };  // set from the menu, read by the worker thread

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    bool added = false;
    bool lowWarned = false;
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    HICON icon = nullptr;
};
std::map<std::wstring, TrayEntry> g_entries;
std::map<std::wstring, bool> g_alertLatches;  // survive transient removal of a device
bool g_alertIconAdded = false;  // short-lived tray icon for widget-mode notifications
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;
struct AlertGlyphIcon { wchar_t glyph; HICON icon; };
std::vector<AlertGlyphIcon> g_alertIcons;  // one 32 px icon per notification glyph
HICON g_appIcon = nullptr;  // small app icon: the notification header next to the title

// User-supplied names are presentation-only, keyed by stable DeviceStatus.key.
// Driver identity, HID polling, brand detection and device diagnostics are unchanged.
aliases::AliasMap g_aliases;

std::wstring DisplayName(const TrayEntry& e) {
    return aliases::DisplayName(g_aliases, e.st.key, e.st.name);
}

void LoadAliases() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, nullptr, &size) != ERROR_SUCCESS) return;
    if (size < 2 * sizeof(wchar_t) || size > 128 * 1024 || size % sizeof(wchar_t)) return;
    std::vector<wchar_t> data(size / sizeof(wchar_t), 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", RRF_RT_REG_MULTI_SZ,
                     nullptr, data.data(), &size) == ERROR_SUCCESS)
        g_aliases = aliases::Decode(data.data(), size / sizeof(wchar_t));
}

void SaveAliases() {
    auto data = aliases::Encode(g_aliases);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceAliases", REG_MULTI_SZ,
                    data.data(), (DWORD)(data.size() * sizeof(wchar_t)));
}

// ---- helpers ----------------------------------------------------------------------------------

UINT StableId(const std::wstring& key) {
    UINT h = 2166136261u;
    for (wchar_t c : key) { h ^= c; h *= 16777619u; }
    return (h & 0x7FFFFFFF) | 0x100;
}

bool ReadLightTaskbar() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return false;
}

void ApplyMenuTheme(bool light) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setMode = ux ? (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135)) : nullptr;
    static auto flush = ux ? (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(light ? 3 : 2);
    if (flush) flush();
}

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

void LoadHidden() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_hidden.insert(p);
}

void SaveHidden() {
    std::wstring multi;
    for (const auto& k : g_hidden) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

// The user's left-to-right order (device keys), stored as a REG_MULTI_SZ. Devices not in it follow
// in the default order (by type, then name).
std::vector<std::wstring> g_order;

void LoadOrder() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_order.push_back(p);
}

void SaveOrder() {
    if (g_order.size() > 64) g_order.resize(64);  // forget long-gone devices
    std::wstring multi;
    for (const auto& k : g_order) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"DeviceOrder", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsEntryHidden(const TrayEntry& e) {
    if (IsHidden(e.st.key)) return true;
    // An explicitly offline device is not just a stale battery reading.
    if (g_showDisconnectedDevices && !e.st.connected) return false;
    return g_hideLastKnown && (e.st.asleep || (e.st.percent < 0 && e.lastPct >= 0));
}

bool LoadBoolSetting(const wchar_t* name, bool defaultVal) {
    DWORD v = defaultVal ? 1 : 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

void SaveBoolSetting(const wchar_t* name, bool on) {
    DWORD v = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v));
}

// A REG_MULTI_SZ with one "device-key<TAB>threshold" entry per override.
// Threshold -1 disables alerts; a missing key uses the default 15%.
std::map<std::wstring, int> g_alertOverrides;

void LoadAlertThresholds() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size < 2 * sizeof(wchar_t) || size > 128 * 1024) return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    size_t limit = size / sizeof(wchar_t);
    for (size_t i = 0; i < limit && buf[i];) {
        size_t end = i;
        while (end < limit && buf[end]) ++end;
        if (end == limit) break;
        std::wstring item(buf.data() + i, end - i);
        size_t sep = item.rfind(L'\t');
        if (sep != std::wstring::npos && sep != 0) {
            std::wstring value = item.substr(sep + 1);
            wchar_t* tail = nullptr;
            long n = std::wcstol(value.c_str(), &tail, 10);
            if (tail != value.c_str() && *tail == 0 && n >= -1 && n <= 40 && alerts::ValidOverride((int)n))
                g_alertOverrides[item.substr(0, sep)] = (int)n;
        }
        i = end + 1;
    }
}

void SaveAlertThresholds() {
    std::wstring multi;
    for (const auto& [key, threshold] : g_alertOverrides) {
        multi += key + L"\t" + std::to_wstring(threshold);
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');
    if (multi.size() == 1) multi.push_back(L'\0');
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"AlertThresholds", REG_MULTI_SZ,
                    multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool LoadWidgetMode() { return LoadBoolSetting(L"TaskbarWidget", true); }
void SaveWidgetMode(bool on) { SaveBoolSetting(L"TaskbarWidget", on); }
bool LoadHideLastKnown() { return LoadBoolSetting(L"HideLastKnownDevices", false); }
void SaveHideLastKnown(bool on) { SaveBoolSetting(L"HideLastKnownDevices", on); }
bool LoadShowDisconnectedDevices() { return LoadBoolSetting(L"ShowDisconnectedDevices", false); }
void SaveShowDisconnectedDevices(bool on) { SaveBoolSetting(L"ShowDisconnectedDevices", on); }
bool LoadRecordHistory() { return LoadBoolSetting(L"RecordBatteryHistory", true); }
void SaveRecordHistory(bool on) { SaveBoolSetting(L"RecordBatteryHistory", on); }
bool LoadWidgetBackground() { return LoadBoolSetting(L"WidgetBackground", false); }
void SaveWidgetBackground(bool on) { SaveBoolSetting(L"WidgetBackground", on); }
std::wstring LoadWidgetMonitor() {
    wchar_t buf[512] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"WidgetMonitor", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0])
        return buf;
    return LoadBoolSetting(L"OtherTaskbars", true) ? L"all" : L"main";  // the earlier on/off toggle
}
void SaveWidgetMonitor(const std::wstring& v) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"WidgetMonitor", REG_SZ, v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
void SetWidgetMonitor(const std::wstring& v) {
    g_widgetMonitor = v;
    SaveWidgetMonitor(v);
    widget::SetMonitorChoice(v);
}
bool LoadStacked() { return LoadBoolSetting(L"StackedLayout", false); }
void SaveStacked(bool on) { SaveBoolSetting(L"StackedLayout", on); }
bool LoadBrandColors() { return LoadBoolSetting(L"BrandColors", false); }
void SaveBrandColors(bool on) { SaveBoolSetting(L"BrandColors", on); }
bool LoadGroupTempsWithUsage() { return LoadBoolSetting(L"GroupTempsWithUsage", false); }
void SaveGroupTempsWithUsage(bool on) { SaveBoolSetting(L"GroupTempsWithUsage", on); }
bool LoadTempAlerts() { return LoadBoolSetting(L"TempAlerts", true); }
void SaveTempAlerts(bool on) { SaveBoolSetting(L"TempAlerts", on); }
bool LoadUsageAlerts() { return LoadBoolSetting(L"UsageAlerts", true); }
void SaveUsageAlerts(bool on) { SaveBoolSetting(L"UsageAlerts", on); }
int LoadUsageAlertThreshold() {
    DWORD v = 90, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"UsageAlertThreshold", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return (v == 85 || v == 90 || v == 95) ? static_cast<int>(v) : 90;
}
void SaveUsageAlertThreshold(int pct) {
    DWORD v = static_cast<DWORD>(pct);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"UsageAlertThreshold", REG_DWORD, &v, sizeof(v));
}
int LoadTempAlertThreshold() {
    DWORD v = 75, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertThreshold", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return (v == 70 || v == 75 || v == 80) ? static_cast<int>(v) : 75;
}
void SaveTempAlertThreshold(int c) {
    DWORD v = static_cast<DWORD>(c);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TempAlertThreshold", REG_DWORD, &v, sizeof(v));
}
int LoadMetricLabelStyle() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricLabelStyle", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return (v <= 2) ? static_cast<int>(v) : 0;
}
void SaveMetricLabelStyle(int style) {
    DWORD v = static_cast<DWORD>(style);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricLabelStyle", REG_DWORD, &v, sizeof(v));
}
DWORD LoadPollSeconds() {
    DWORD v = 60, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"PollIntervalSeconds", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return std::clamp<DWORD>(v, 30, 24 * 60 * 60);
}
void SavePollSeconds(DWORD seconds) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"PollIntervalSeconds", REG_DWORD, &seconds, sizeof(seconds));
}

void LoadMetricsWhere() {
    uint8_t buf[32]{};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricsWhere", RRF_RT_REG_BINARY, nullptr, buf, &sz) == ERROR_SUCCESS) {
        metrics::ParseConfig(buf, sz, g_metricCfg);
    } else {
        g_metricCfg = metrics::kDefaultConfig;
    }
}

void SaveMetricsWhere() {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricsWhere", REG_BINARY,
                    g_metricCfg.where, static_cast<DWORD>(sizeof(g_metricCfg.where)));
}

DWORD LoadMetricIntervalMs() {
    DWORD v = 5000, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricIntervalMs", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return std::clamp<DWORD>(v, 1000, 5000);
}

void SaveMetricIntervalMs(DWORD ms) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricIntervalMs", REG_DWORD, &ms, sizeof(ms));
}

void AdjustTaskbarOffset(int delta) {
    DWORD raw = 0, sz = sizeof(raw);
    int offset = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &raw, &sz) == ERROR_SUCCESS) {
        offset = static_cast<int>(raw);
    }
    offset = delta == 0 ? 0 : std::clamp(offset + delta, -2000, 2000);
    DWORD out = static_cast<DWORD>(offset);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset", REG_DWORD, &out, sizeof(out));
    widget::Reposition();
}

void MigrateTraySense() {
    DWORD migrated = 0, sz = sizeof(migrated);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"MigratedTraySense", RRF_RT_REG_DWORD, nullptr, &migrated, &sz) == ERROR_SUCCESS && migrated != 0) {
        return;
    }
    auto setFlag = []() {
        DWORD one = 1;
        RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"MigratedTraySense", REG_DWORD, &one, sizeof(one));
    };
    DWORD whereSz = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricsWhere", RRF_RT_ANY, nullptr, nullptr, &whereSz) == ERROR_SUCCESS) {
        setFlag();
        return;
    }
    HKEY trayKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\TraySense", 0, KEY_READ, &trayKey) == ERROR_SUCCESS) {
        DWORD val = 0, type = 0, valSz = sizeof(val);
        if (RegQueryValueExW(trayKey, L"Mask2", nullptr, &type, reinterpret_cast<BYTE*>(&val), &valSz) == ERROR_SUCCESS &&
            type == REG_DWORD && valSz == sizeof(DWORD)) {
            metrics::Where where[metrics::kCount];
            for (int i = 0; i < metrics::kCount; i++) {
                where[i] = (val & (1u << i)) ? metrics::Where::Bar : metrics::Where::Hidden;
            }
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"MetricsWhere", REG_BINARY,
                            where, static_cast<DWORD>(sizeof(where)));
        }
        valSz = sizeof(val);
        if (RegQueryValueExW(trayKey, L"Interval", nullptr, &type, reinterpret_cast<BYTE*>(&val), &valSz) == ERROR_SUCCESS &&
            type == REG_DWORD && valSz == sizeof(DWORD)) {
            SaveMetricIntervalMs(std::clamp<DWORD>(val, 1000, 5000));
        }
        valSz = sizeof(val);
        if (RegQueryValueExW(trayKey, L"Offset", nullptr, &type, reinterpret_cast<BYTE*>(&val), &valSz) == ERROR_SUCCESS &&
            type == REG_DWORD && valSz == sizeof(DWORD)) {
            DWORD out = static_cast<DWORD>(std::clamp(static_cast<int>(val), -2000, 2000));
            RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarOffset", REG_DWORD, &out, sizeof(out));
        }
        RegCloseKey(trayKey);
    }
    setFlag();
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    return _wcsicmp(buf, (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
}

// ---- icon rendering ---------------------------------------------------------------------------
// Draws the percentage as large digits with a small level bar underneath. GDI text is rendered
// white-on-black into a scratch bitmap and its brightness is used as the alpha channel, which
// gives smooth edges on the transparent icon.

struct Rgb { BYTE r, g, b; };

HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim) {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 0;
    const int S = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : GetDpiForSystem());
    const int barH = std::max(2, S / 8);
    const int textH = S - barH - 1;

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // scratch bitmap for the glyphs
    const int W = S * 3, H = S * 2;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    void* sbits = nullptr;
    HBITMAP scratch = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &sbits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, scratch);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    auto* sp = static_cast<DWORD*>(sbits);

    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    for (int fh = textH * 2; fh >= 6; fh--) {
        HFONT f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(dc, f);
        SetTextCharacterExtra(dc, text.size() >= 3 ? -1 : 0);
        memset(sbits, 0, (size_t)W * H * 4);
        TextOutW(dc, 2, 0, text.c_str(), (int)text.size());
        GdiFlush();
        SelectObject(dc, oldF);
        DeleteObject(f);
        bx0 = W; by0 = H; bx1 = -1; by1 = -1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (sp[y * W + x] & 0xFF) { bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
        if (bx1 < 0) break;
        if (bx1 - bx0 + 1 <= S && by1 - by0 + 1 <= textH) break;
    }

    // final icon bitmap
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    void* ibits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &ibits, nullptr, 0);
    auto* ip = static_cast<DWORD*>(ibits);
    memset(ip, 0, (size_t)S * S * 4);
    const int alphaScale = dim ? 140 : 255;
    auto put = [&](int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= S || y >= S || a <= 0) return;
        a = a * alphaScale / 255;
        ip[y * S + x] = ((DWORD)a << 24) | ((DWORD)c.r << 16) | ((DWORD)c.g << 8) | c.b;  // straight alpha
    };
    if (bx1 >= 0) {
        int gw = bx1 - bx0 + 1, gh = by1 - by0 + 1;
        int ox = (S - gw) / 2, oy = (textH - gh) / 2;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                put(ox + x, oy + y, textColor, sp[(by0 + y) * W + (bx0 + x)] & 0xFF);
    }
    // level bar: faint track + filled part
    int fill = barPct < 0 ? 0 : std::max(barPct > 0 ? 1 : 0, (barPct * S + 50) / 100);
    for (int y = S - barH; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (x < fill) put(x, y, barColor, 255);
            else put(x, y, g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, 70);
        }

    SelectObject(dc, oldBmp);
    DeleteObject(scratch);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    {
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, mask);
        PatBlt(mdc, 0, 0, S, S, BLACKNESS);
        SelectObject(mdc, o);
        DeleteDC(mdc);
    }
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

Rgb Foreground() { return g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }; }

Accent AccentFor(const DeviceStatus& s) { return g_brandColors ? BrandOf(s) : Accent::None; }

HICON IconFor(int pct, bool charging, bool dim, Accent accent) {
    Rgb fg = Foreground();
    BrandColor bc = AccentColor(accent, g_lightTaskbar);
    Rgb bar = charging        ? Rgb{ 0, 170, 255 }
            : pct <= LOW_BATTERY ? Rgb{ 232, 32, 48 }
            : pct <= 30       ? Rgb{ 255, 150, 0 }
            : accent != Accent::None ? Rgb{ bc.r, bc.g, bc.b }
                              : Rgb{ 30, 190, 90 };
    Rgb text = (!charging && pct >= 0 && pct <= LOW_BATTERY) ? Rgb{ 240, 50, 60 } : fg;
    std::wstring t = pct < 0 ? L"?" : std::to_wstring(pct);
    return RenderIcon(t, pct, text, bar, dim);
}

// ---- tray management --------------------------------------------------------------------------

// Large notification icon: a Segoe Fluent glyph drawn white on transparent, cached per glyph.
// NIIF_USER replaces the yellow warning triangle; each alert uses its component's glyph.
HICON AlertIcon(wchar_t glyph) {
    for (const auto& e : g_alertIcons)
        if (e.glyph == glyph) return e.icon;
    const int S = GetSystemMetrics(SM_CXICON);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return nullptr; }
    HGDIOBJ old = SelectObject(dc, bmp);
    auto* px = static_cast<DWORD*>(bits);
    memset(bits, 0, (size_t)S * S * 4);
    HFONT f = CreateFontW(-S, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe Fluent Icons");
    HGDIOBJ oldF = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 0, 0, &glyph, 1);
    GdiFlush();
    SelectObject(dc, oldF);
    DeleteObject(f);
    SelectObject(dc, old);
    for (int i = 0; i < S * S; i++) {  // GDI drew white-on-black: blue channel is the coverage
        DWORD a = px[i] & 0xFF;
        px[i] = (a << 24) | 0x00FFFFFFu;
    }
    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    {
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, mask);
        PatBlt(mdc, 0, 0, S, S, BLACKNESS);
        SelectObject(mdc, o);
        DeleteDC(mdc);
    }
    ICONINFO ii{ TRUE, 0, 0, mask, bmp };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(bmp);
    DeleteDC(dc);
    if (icon) g_alertIcons.push_back({ glyph, icon });
    return icon;
}

// Small app icon for the notification header. The shell takes it from the tray icon that raises
// the balloon, so alerts point at the exe's icon resource instead of the component glyph.
HICON AppIcon() {
    if (g_appIcon) return g_appIcon;
    const int S = GetSystemMetrics(SM_CXSMICON);
    g_appIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                              S, S, LR_DEFAULTCOLOR));
    return g_appIcon;
}

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = 0;
}

std::wstring StatusText(const TrayEntry& e) {
    const auto& s = e.st;
    if (!s.connected) return T(L"off / not connected");
    std::wstring t;
    if (!s.levelText.empty() && s.percent >= 0) t = T(s.levelText);
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = TF(L"{percent}% (last known)", { { L"percent", std::to_wstring(e.lastPct) } });
    else t = T(L"unknown");
    if (!s.detail.empty() && s.percent >= 0) t += L" (" + s.detail + L")";
    if (s.full || (s.charging && s.percent >= 100)) t += std::wstring(L", ") + T(L"fully charged");
    else if (s.charging) t += std::wstring(L", ") + T(L"charging");
    if (s.asleep) t += std::wstring(L", ") + T(L"asleep");
    return t;
}

void TrayCall(DWORD msg, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"", wchar_t balloonGlyph = 0, HICON balloonIcon = nullptr) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    CopyTip(nid.szTip, ARRAYSIZE(nid.szTip), tip);
    if (infoTitle) {
        nid.uFlags |= NIF_INFO;
        CopyTip(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), infoTitle);
        CopyTip(nid.szInfo, ARRAYSIZE(nid.szInfo), info);
        nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_RESPECT_QUIET_TIME;
        nid.hBalloonIcon = balloonIcon ? balloonIcon : AlertIcon(balloonGlyph);
    }
    Shell_NotifyIconW(msg, &nid);
    if (msg == NIM_ADD) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

void TrayDelete(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowBatteryAlert(TrayEntry& e) {
    const std::wstring title = T(L"Low battery");
    const std::wstring message = TF(L"{name} is at {percent}%",
                                   { { L"name", DisplayName(e) }, { L"percent", std::to_wstring(e.st.percent) } });
    // Windows notification balloons need a tray icon, and the shell takes the header icon from
    // it. Always use a temporary entry with the app icon; the component icon goes in the balloon.
    HICON header = AppIcon();
    if (!header) g_placeholderIcon = g_placeholderIcon ? g_placeholderIcon : RenderIcon(L"-", -1, Foreground(), Foreground(), true);
    TrayCall(g_alertIconAdded ? NIM_MODIFY : NIM_ADD, ALERT_ICON_ID, header ? header : g_placeholderIcon,
             L"Monitor", title.c_str(), message, 0, e.icon);
    g_alertIconAdded = true;
    SetTimer(g_hwnd, TIMER_ALERT_ICON, 60 * 1000, nullptr);
}

void ShowMetricAlert(int id, const std::wstring& title, const std::wstring& formatted, const std::wstring& message) {
    const wchar_t glyph = ComponentGlyph(id);
    HICON header = AppIcon();
    if (!header) g_placeholderIcon = g_placeholderIcon ? g_placeholderIcon : RenderIcon(L"-", -1, Foreground(), Foreground(), true);
    TrayCall(g_alertIconAdded ? NIM_MODIFY : NIM_ADD, ALERT_ICON_ID, header ? header : g_placeholderIcon,
             L"Monitor", title.c_str(), message, glyph);
    g_alertIconAdded = true;
    SetTimer(g_hwnd, TIMER_ALERT_ICON, 60 * 1000, nullptr);
}

void ShowTempAlert(int id, double tempC) {
    const std::wstring formatted = metrics::FormatTemp(tempC, true);
    const std::wstring message = TF(L"{name}: {temp}",
                                    { { L"name", T(kMetrics[id].name) }, { L"temp", formatted } });
    ShowMetricAlert(id, T(L"High temperature"), formatted, message);
}

void ShowUsageAlert(int id, double pct) {
    const std::wstring formatted = metrics::FormatPercent(pct, true);
    const std::wstring message = TF(L"{name}: {value}",
                                    { { L"name", T(kMetrics[id].name) }, { L"value", formatted } });
    ShowMetricAlert(id, T(L"High usage"), formatted, message);
}

void EvaluateTempAlerts(const metrics::Snapshot& snap) {
    constexpr metrics::Id kTempIds[] = { metrics::Id::CpuTemp, metrics::Id::GpuTemp, metrics::Id::SsdTemp };
    for (metrics::Id mid : kTempIds) {
        const int id = static_cast<int>(mid);
        if (!snap.ok[id]) continue;
        int sev = 0;
        if (mid == metrics::Id::SsdTemp) {
            sev = metrics::SeverityFor(id, snap.val[id]);
        } else {
            const double warn = static_cast<double>(g_tempAlertThreshold);
            const double crit = warn + 10.0;
            if (snap.val[id] >= crit) sev = 2;
            else if (snap.val[id] >= warn) sev = 1;
        }
        if (!g_tempSeenOk[id]) {
            g_tempSeenOk[id] = true;
            if (metrics::FirstHotSample(sev)) g_tempLatched[id] = true;
            continue;
        }
        if (metrics::ResetLatch(sev)) {
            g_tempLatched[id] = false;
            continue;
        }
        if (metrics::ShouldNotifyTemp(sev, g_tempLatched[id])) {
            g_tempLatched[id] = true;
            if (g_tempAlerts && g_metricCfg.where[id] != metrics::Where::Hidden) {
                ShowTempAlert(id, snap.val[id]);
            }
        }
    }
}

void EvaluateUsageAlerts(const metrics::Snapshot& snap) {
    constexpr metrics::Id kUsageIds[] = { metrics::Id::RamUsage, metrics::Id::Vram };
    const double threshold = static_cast<double>(g_usageAlertThreshold);
    for (metrics::Id mid : kUsageIds) {
        const int id = static_cast<int>(mid);
        if (!snap.ok[id] || snap.val[id] < 0.0) continue;
        if (!g_usageSeenOk[id]) {
            g_usageSeenOk[id] = true;
            if (metrics::UsageFirstSampleHot(snap.val[id], threshold)) g_usageLatched[id] = true;
            continue;
        }
        if (metrics::UsageResetLatch(snap.val[id], threshold)) {
            g_usageLatched[id] = false;
            continue;
        }
        if (metrics::UsageShouldNotify(snap.val[id], threshold, g_usageLatched[id])) {
            g_usageLatched[id] = true;
            if (g_usageAlerts && g_metricCfg.where[id] != metrics::Where::Hidden) {
                ShowUsageAlert(id, snap.val[id]);
            }
        }
    }
}

void UpdatePlaceholder() {
    if (g_useWidget) {
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
        return;
    }
    bool any = false;
    for (auto& [k, e] : g_entries) any |= e.added;
    if (!any && !g_placeholder) {
        if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
        TrayCall(NIM_ADD, PLACEHOLDER_ID, g_placeholderIcon, std::wstring(L"Monitor\n") + T(L"No devices with battery info found"));
        g_placeholder = true;
    } else if (any && g_placeholder) {
        TrayDelete(PLACEHOLDER_ID);
        g_placeholder = false;
    }
}

void ApplyEntry(TrayEntry& e) {
    const auto& s = e.st;
    bool visible = !g_useWidget && !IsEntryHidden(e) && (s.connected || g_showDisconnectedDevices);
    if (!visible) {
        if (e.added) { TrayDelete(e.uid); e.added = false; }
        return;
    }
    // Never display an old percentage as a live reading for an explicitly offline device.
    int shown = !s.connected ? -1 : (s.percent >= 0 ? s.percent : e.lastPct);
    bool dim = !s.connected || s.percent < 0;
    // Fully charged on power: the normal green bar instead of the blue charging one.
    bool charging = s.connected && !s.asleep && s.charging && !s.full && shown < 100;
    Accent accent = AccentFor(s);
    std::wstring sig = std::to_wstring(shown) + (charging ? L"c" : L"") + (dim ? L"d" : L"") + (g_lightTaskbar ? L"l" : L"k") +
                       L"a" + std::to_wstring((int)accent) + (s.connected ? L"|on" : L"|off");
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = s.connected ? IconFor(shown, charging, dim, accent)
                             : RenderIcon(L"\u00D7", -1, Foreground(), Foreground(), true);
        e.iconSig = sig;
    }
    std::wstring tip = DisplayName(e) + L"\n" + StatusText(e);

    TrayCall(e.added ? NIM_MODIFY : NIM_ADD, e.uid, e.icon, tip);
    e.added = true;
}

const MetricSpec* FindMetricSpec(int id) {
    for (const auto& m : kMetrics) {
        if (static_cast<int>(m.id) == id) return &m;
    }
    return nullptr;
}

void ApplyMetricTray(int id) {
    if (id < 0 || id >= metrics::kCount) return;
    const MetricSpec* spec = FindMetricSpec(id);
    if (!spec) return;
    MetricTrayEntry& e = g_metricTray[id];
    UINT uid = metrics::kTrayUidBase + static_cast<UINT>(id);
    metrics::Where where = g_metricCfg.where[id];
    bool showInTray = !g_useWidget ? (where != metrics::Where::Hidden) : (where == metrics::Where::Tray);
    if (!showInTray) {
        if (e.added) {
            TrayDelete(uid);
            e.added = false;
        }
        return;
    }
    bool ok = g_hasMetricSnap && g_metricSnap.ok[id];
    bool isTemp = id == static_cast<int>(metrics::Id::CpuTemp) ||
                  id == static_cast<int>(metrics::Id::GpuTemp) ||
                  id == static_cast<int>(metrics::Id::SsdTemp);
    int sev = (isTemp && ok) ? metrics::SeverityFor(id, g_metricSnap.val[id]) : 0;
    int val = ok ? (isTemp ? static_cast<int>(std::lround(g_metricSnap.val[id]))
                           : std::clamp(static_cast<int>(g_metricSnap.val[id] + 0.5), 0, 100))
                 : -1;
    std::wstring sig = std::to_wstring(val) + (ok ? L"1" : L"0") + std::to_wstring(sev) + (g_lightTaskbar ? L"l" : L"k");
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        std::wstring shortVal = ok ? std::to_wstring(val) : L"--";
        Rgb fg = Foreground();
        Rgb tc = fg;
        if (ok && sev > 0) {
            auto sc = widget::SeverityColor(sev, g_lightTaskbar);
            tc = { sc.r, sc.g, sc.b };
        }
        int barPct = ok ? std::clamp(val, 0, 100) : -1;
        e.icon = RenderIcon(shortVal, barPct, tc, tc, !ok);
        e.iconSig = sig;
    }
    std::wstring tip = std::wstring(T(spec->name)) + L": " +
                       (isTemp ? metrics::FormatTemp(g_metricSnap.val[id], ok)
                               : metrics::FormatPercent(g_metricSnap.val[id], ok));
    if (id == static_cast<int>(metrics::Id::CpuTemp)) {
        tip += std::wstring(L" (") + T(L"Thermal zone reading, not a CPU die sensor") + L")";
    }
    TrayCall(e.added ? NIM_MODIFY : NIM_ADD, uid, e.icon, tip);
    e.added = true;
}

void ApplyAllMetricTray() {
    for (const auto& m : kMetrics) ApplyMetricTray(static_cast<int>(m.id));
}

int KindOrder(DeviceKind k) {  // default order: headset first, then mouse, ...
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        case DeviceKind::Phone:    return 4;
        default:                   return 5;
    }
}

bool IsShown(const TrayEntry& e) { return (e.st.connected || g_showDisconnectedDevices) && !IsEntryHidden(e); }

// Entries in left-to-right order: the user's order first, then the rest by type and name. Works the
// same for every kind of device (receivers, Bluetooth, controllers, HeadsetControl) as it only uses
// the device key.
std::vector<TrayEntry*> OrderedEntries(bool shownOnly) {
    std::vector<TrayEntry*> list;
    for (auto& [k, e] : g_entries)
        if (!shownOnly || IsShown(e)) list.push_back(&e);
    auto rank = [](const TrayEntry* e) {
        auto it = std::find(g_order.begin(), g_order.end(), e->st.key);
        return it == g_order.end() ? INT_MAX : (int)(it - g_order.begin());
    };
    std::stable_sort(list.begin(), list.end(), [&](const TrayEntry* a, const TrayEntry* b) {
        int ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        if (ka != kb) return ka < kb;
        return DisplayName(*a) != DisplayName(*b) ? DisplayName(*a) < DisplayName(*b) : a->st.key < b->st.key;  // same name: keep a fixed order
    });
    return list;
}

void UpdateWidget() {
    if (!g_useWidget) return;
    std::vector<TrayEntry*> list = OrderedEntries(true);
    // Two devices of one kind share an icon: give each a tiny caption so they can be told apart.
    std::vector<LabelInput> labelIn;
    for (auto* e : list) labelIn.push_back({ e->st.kind, DisplayName(*e) });
    std::vector<std::wstring> labels = ShortLabels(labelIn, 3);
    std::vector<WidgetItem> items;
    std::wstring tip;
    for (const auto& m : kMetrics) {
        int idx = static_cast<int>(m.id);
        if (g_metricCfg.where[idx] != metrics::Where::Bar) continue;
        bool ok = g_hasMetricSnap && g_metricSnap.ok[idx];
        bool isTemp = m.id == metrics::Id::CpuTemp || m.id == metrics::Id::GpuTemp || m.id == metrics::Id::SsdTemp;
        std::wstring valText = isTemp ? metrics::FormatTemp(g_metricSnap.val[idx], ok)
                                      : metrics::FormatPercent(g_metricSnap.val[idx], ok);
        int sev = (isTemp && ok) ? metrics::SeverityFor(idx, g_metricSnap.val[idx]) : 0;
        if (!tip.empty()) tip += L"\n";
        tip += std::wstring(T(m.name)) + L": " + valText;
        if (m.id == metrics::Id::CpuTemp) {
            tip += std::wstring(L" (") + T(L"Thermal zone reading, not a CPU die sensor") + L")";
        }
        if (g_groupTempsWithUsage && isTemp) {
            int siblingUsage = -1;
            wchar_t componentGlyph = m.glyph;
            if (m.id == metrics::Id::CpuTemp) {
                siblingUsage = static_cast<int>(metrics::Id::CpuUsage);
                componentGlyph = 0xEEA1;
            } else if (m.id == metrics::Id::GpuTemp) {
                siblingUsage = static_cast<int>(metrics::Id::GpuUsage);
                componentGlyph = 0xE950;
            } else if (m.id == metrics::Id::SsdTemp) {
                componentGlyph = 0xEDA2;
            }
            if (siblingUsage >= 0 && g_metricCfg.where[siblingUsage] == metrics::Where::Bar) {
                for (auto& existing : items) {
                    if (existing.metric == siblingUsage) {
                        existing.inlineTempText = valText;
                        existing.inlineTempSeverity = sev;
                        break;
                    }
                }
                continue;
            }
            WidgetItem it;
            it.metric = idx;
            it.glyphOverride = componentGlyph;
            it.name = T(m.name);
            it.text = valText;
            it.dim = !ok;
            it.severity = sev;
            if (m.label) it.label = m.label;
            if (g_metricLabelStyle != 0) {
                it.word = m.word;
                it.labelStyle = g_metricLabelStyle;
            }
            items.push_back(it);
            continue;
        }
        WidgetItem it;
        it.metric = idx;
        it.glyphOverride = m.glyph;
        it.name = T(m.name);
        it.text = valText;
        it.dim = !ok;
        it.severity = sev;
        if (m.label) it.label = m.label;
        if (g_metricLabelStyle != 0) {
            it.word = m.word;
            it.labelStyle = g_metricLabelStyle;
        }
        items.push_back(it);
    }
    for (size_t i = 0; i < list.size(); i++) {
        auto* e = list[i];
        WidgetItem it;
        it.kind = e->st.kind;
        it.name = DisplayName(*e);
        bool offline = !e->st.connected;
        it.percent = offline ? -1 : (e->st.percent >= 0 ? e->st.percent : e->lastPct);
        if (offline) it.text = L"\u00D7";  // clear status, not a stale percentage
        else if (e->st.percent >= 0) it.text = T(e->st.levelText);
        it.charging = !offline && !e->st.asleep && e->st.charging;
        it.full = !offline && (e->st.full || (e->st.charging && it.percent >= 100));
        it.dim = offline || e->st.percent < 0;
        it.low = !offline && !it.charging && it.percent >= 0 && it.percent <= LOW_BATTERY;
        it.bluetooth = e->st.via == L"Bluetooth";
        it.label = labels[i];
        it.accent = AccentFor(e->st);
        items.push_back(it);
        if (!tip.empty()) tip += L"\n";
        tip += DisplayName(*e) + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = std::wstring(L"Monitor: ") + T(L"No devices with battery info found");
    widget::SetItems(items, tip);
}

void OnResults(std::vector<DeviceStatus>& results) {
    std::map<std::wstring, bool> seen;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = StableId(s.key);
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = GetTickCount64();
        seen[s.key] = true;
        // Evaluate even in widget mode: ApplyEntry only manages tray icons.
        // Alert latches are kept by stable device key even if the driver briefly stops reporting it.
        if (const auto it = g_alertLatches.find(s.key); it != g_alertLatches.end())
            e.lowWarned = it->second;
        bool notify = !IsHidden(s.key) &&
                      alerts::ShouldNotify(s, alerts::EffectiveThreshold(g_alertOverrides, s.key), e.lowWarned);
        g_alertLatches[s.key] = e.lowWarned;
        if (g_alertLatches.size() > 256) g_alertLatches.erase(g_alertLatches.begin());
        if (g_recordHistory && s.connected && !s.asleep && s.percent >= 0)
            battery_history::Observe(s.key, s.percent, s.charging);
        ApplyEntry(e);
        if (notify) ShowBatteryAlert(e);
    }
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while
        // (at least two poll intervals).
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       GetTickCount64() - e.lastSeen <
                           std::max<ULONGLONG>((ULONGLONG)e.st.graceSec * 1000, 2ULL * g_pollIntervalMs);
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
            ApplyEntry(e);
        }
        if (!seen.count(it->first) && !inGrace) {
            if (it->second.added) TrayDelete(it->second.uid);
            if (it->second.icon) DestroyIcon(it->second.icon);
            it = g_entries.erase(it);
        } else {
            ++it;
        }
    }
    UpdatePlaceholder();
    UpdateWidget();
}

// Makes the actual display match the chosen mode; falls back to tray icons if the taskbar
// widget cannot be created (e.g. Explorer not ready yet). Called once a second.
void Reconcile() {
    bool alive = g_wantWidget && widget::Alive();
    if (g_wantWidget && !alive) alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    if (!g_wantWidget) widget::Destroy();
    bool changed = alive != g_useWidget;
    g_useWidget = alive;
    if (changed) {
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        ApplyAllMetricTray();
        UpdatePlaceholder();
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    }
}

void SetMode(bool useWidget) {
    g_wantWidget = useWidget;
    SaveWidgetMode(useWidget);
    Reconcile();
}

void SetWidgetBackground(bool on) {
    g_widgetBackground = on;
    SaveWidgetBackground(on);
    widget::SetBackground(on);
}

void SetHideLastKnown(bool on) {
    g_hideLastKnown = on;
    SaveHideLastKnown(on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void SetShowDisconnectedDevices(bool on) {
    g_showDisconnectedDevices = on;
    SaveShowDisconnectedDevices(on);
    for (auto& [k, e] : g_entries) ApplyEntry(e);
    UpdatePlaceholder();
    UpdateWidget();
}

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    for (const auto& m : kMetrics) {
        int id = static_cast<int>(m.id);
        g_metricTray[id].added = false;
        ApplyMetricTray(id);
    }
    UpdatePlaceholder();
}

// Switches the UI language ("" = follow Windows) and redraws everything that shows text.
void SetLanguage(const std::wstring& code);

void RedrawAll() {
    if (g_alertIconAdded) {
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        TrayDelete(ALERT_ICON_ID);
        g_alertIconAdded = false;
    }
    if (g_appIcon) {
        DestroyIcon(g_appIcon);
        g_appIcon = nullptr;
    }
    if (g_placeholderIcon) {
        DestroyIcon(g_placeholderIcon);
        g_placeholderIcon = nullptr;
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
    }
    for (auto& a : g_alertIcons) DestroyIcon(a.icon);
    g_alertIcons.clear();
    for (auto& [k, e] : g_entries) { e.iconSig.clear(); ApplyEntry(e); }
    for (const auto& m : kMetrics) {
        int id = static_cast<int>(m.id);
        g_metricTray[id].iconSig.clear();
        ApplyMetricTray(id);
    }
    UpdatePlaceholder();
}

void SetLanguage(const std::wstring& code) {
    i18n::SetChoice(code);
    RedrawAll();  // tray tooltips and the placeholder text
    UpdateWidget();
}

void SetStacked(bool on) {
    g_stacked = on;
    SaveStacked(on);
    widget::SetStacked(on);
}

void SetBrandColors(bool on) {
    g_brandColors = on;
    SaveBrandColors(on);
    RedrawAll();
    UpdateWidget();
}

void SetPollSeconds(DWORD seconds) {
    g_pollIntervalMs = seconds * 1000;
    SavePollSeconds(seconds);
    SetEvent(g_refreshEvt);  // poll now, so the new interval starts right away
}

void ToggleHidden(const std::wstring& key) {
    if (!g_hidden.erase(key)) g_hidden.insert(key);
    SaveHidden();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdatePlaceholder();
    UpdateWidget();
}

void RenameDevice(const std::wstring& key) {
    auto it = g_entries.find(key);
    if (it == g_entries.end()) return;
    const std::wstring originalName = it->second.st.name;
    std::wstring chosen;
    if (!PromptDeviceName(g_hwnd, DisplayName(it->second), chosen)) return;
    if (chosen == originalName) g_aliases.erase(key);
    else if (g_aliases.count(key) || g_aliases.size() < aliases::kMaxAliases) g_aliases[key] = chosen;
    else return;  // bounded registry value; don't silently drop other users' names
    SaveAliases();
    // The modal editor pumps the message queue, so polling may remove the entry.
    // Resolve the iterator again rather than dereferencing a potentially stale one.
    it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
}

void ResetDeviceName(const std::wstring& key) {
    if (!g_aliases.erase(key)) return;
    SaveAliases();
    auto it = g_entries.find(key);
    if (it != g_entries.end()) ApplyEntry(it->second);
    UpdateWidget();
}

void SetAlertOverride(const std::wstring& key, int threshold) {
    if (threshold == alerts::kUseDefault) g_alertOverrides.erase(key);
    else if (alerts::ValidOverride(threshold)) g_alertOverrides[key] = threshold;
    else return;
    SaveAlertThresholds();
    g_alertLatches.erase(key);
    auto it = g_entries.find(key);
    if (it != g_entries.end()) it->second.lowWarned = false;
    SetEvent(g_refreshEvt);  // reconsider with a fresh reading; never alert from stale data
}

// Swaps a shown device with its neighbour (dir -1 = left, +1 = right) and remembers the new order.
void MoveEntry(const std::wstring& key, int dir) {
    std::vector<TrayEntry*> list = OrderedEntries(true);
    auto pos = std::find_if(list.begin(), list.end(), [&](const TrayEntry* e) { return e->st.key == key; });
    if (pos == list.end()) return;
    ptrdiff_t i = pos - list.begin(), j = i + dir;
    if (j < 0 || j >= (ptrdiff_t)list.size()) return;
    std::swap(list[i], list[j]);
    std::vector<std::wstring> order;
    for (auto* e : list) order.push_back(e->st.key);
    for (auto& k : g_order)  // keep the places of devices that are not shown right now
        if (std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    g_order = order;
    SaveOrder();
    UpdateWidget();
}

void XmlEscape(wchar_t* out, size_t cap, const wchar_t* in) {
    size_t o = 0;
    for (; *in && o + 7 < cap; in++) {
        const wchar_t* r = (*in == L'&') ? L"&amp;" : (*in == L'<') ? L"&lt;" : (*in == L'>') ? L"&gt;" : nullptr;
        if (r) {
            size_t len = wcslen(r);
            std::wcsncpy(out + o, r, len);
            o += len;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = 0;
}

bool CurrentUserSid(wchar_t* out, size_t cap) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    BYTE buf[256]{};
    DWORD got = 0;
    LPWSTR str = nullptr;
    bool ok = false;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &got) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &str)) {
        wcsncpy(out, str, cap - 1);
        out[cap - 1] = 0;
        LocalFree(str);
        ok = true;
    }
    CloseHandle(tok);
    return ok;
}

void ActivateCpuTemp() {
    pawnio::PioState st = pawnio::Init();
    if (st == pawnio::PioState::Ok) return;
    if (st != pawnio::PioState::NoAdmin) return;

    const std::wstring msg = T(L"CPU temperature requires a one-time administrator prompt to install a background helper task (MonitorTempHelper) that reads the sensor at logon.\n\nClosing Monitor or hiding this metric keeps the task installed.");
    if (MessageBoxW(g_hwnd, msg.c_str(), T(L"CPU temperature"), MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        return;
    }

    wchar_t exePath[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (!slash) return;
    slash[1] = 0;

    wchar_t helperPath[MAX_PATH]{};
    if (std::swprintf(helperPath, MAX_PATH, L"%lsMonitor.TempHelper.exe", exePath) < 0) return;
    if (GetFileAttributesW(helperPath) == INVALID_FILE_ATTRIBUTES) return;

    wchar_t sid[184]{};
    if (!CurrentUserSid(sid, ARRAYSIZE(sid))) return;
    wchar_t esc[MAX_PATH * 5]{};
    XmlEscape(esc, ARRAYSIZE(esc), helperPath);

    wchar_t xml[3072]{};
    std::swprintf(xml, ARRAYSIZE(xml),
        L"\xFEFF<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>%ls</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"Author\"><UserId>%ls</UserId><LogonType>InteractiveToken</LogonType>"
        L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"
        L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><Priority>5</Priority></Settings>"
        L"<Actions Context=\"Author\"><Exec><Command>%ls</Command></Exec></Actions></Task>",
        sid, sid, esc);

    wchar_t tempDir[MAX_PATH]{};
    wchar_t xmlPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDir) ||
        std::swprintf(xmlPath, MAX_PATH, L"%lsMonitorTempHelper.xml", tempDir) < 0) {
        return;
    }

    HANDLE f = CreateFileW(xmlPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    BOOL wrote = WriteFile(f, xml, static_cast<DWORD>(wcslen(xml) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
    if (!wrote) {
        DeleteFileW(xmlPath);
        return;
    }

    wchar_t args[MAX_PATH + 64]{};
    std::swprintf(args, ARRAYSIZE(args), L"/Create /F /TN MonitorTempHelper /XML \"%ls\"", xmlPath);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = g_hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = L"schtasks.exe";
    sei.lpParameters = args;
    sei.nShow = SW_HIDE;
    bool taskOk = false;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 15000);
        DWORD code = 1;
        if (GetExitCodeProcess(sei.hProcess, &code) && code == 0) taskOk = true;
        CloseHandle(sei.hProcess);
    }
    DeleteFileW(xmlPath);
    if (!taskOk) return;

    ShellExecuteW(g_hwnd, L"runas", helperPath, nullptr, nullptr, SW_HIDE);
}

void ShowMenu() {
    ApplyMenuTheme(g_lightTaskbar);
    HMENU m = CreatePopupMenu();
    std::vector<std::wstring> keys;  // device index -> device key; menu id ID_DEVICE_FIRST + index * DEV_STRIDE + command
    std::vector<TrayEntry*> all = OrderedEntries(false), shown = OrderedEntries(true);
    {
        HMENU devMenu = CreatePopupMenu();
        if (all.empty()) AppendMenuW(devMenu, MF_STRING | MF_GRAYED, 0, T(L"No devices with battery info found"));
        else AppendMenuW(devMenu, MF_STRING | MF_GRAYED, 0, T(L"Point at a device to show, hide or move it"));
        for (TrayEntry* e : all) {
            const std::wstring& k = e->st.key;
            UINT base = ID_DEVICE_FIRST + (UINT)keys.size() * DEV_STRIDE;
            auto pos = std::find(shown.begin(), shown.end(), e);
            bool canLeft = pos != shown.end() && pos != shown.begin();
            bool canRight = pos != shown.end() && pos + 1 != shown.end();
            HMENU sub = CreatePopupMenu();
            AppendMenuW(sub, MF_STRING | (IsHidden(k) ? 0 : MF_CHECKED), base + DEV_TOGGLE,
                        g_wantWidget ? T(L"Show on the taskbar") : T(L"Show in the tray"));
            AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(sub, MF_STRING | (canLeft ? 0 : MF_GRAYED), base + DEV_LEFT, g_wantWidget ? T(L"Move left") : T(L"Move up"));
            AppendMenuW(sub, MF_STRING | (canRight ? 0 : MF_GRAYED), base + DEV_RIGHT, g_wantWidget ? T(L"Move right") : T(L"Move down"));
            AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(sub, MF_STRING, base + DEV_RENAME, T(L"Rename device..."));
            AppendMenuW(sub, MF_STRING | (g_aliases.count(k) ? 0 : MF_GRAYED),
                        base + DEV_RESET_NAME, T(L"Reset to original name"));
            HMENU alertMenu = CreatePopupMenu();
            int selectedThreshold = alerts::OverrideFor(g_alertOverrides, k);
            AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kUseDefault ? MF_CHECKED : 0),
                        base + DEV_ALERT_DEFAULT, T(L"Use default (15%)"));
            AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == alerts::kDisabled ? MF_CHECKED : 0),
                        base + DEV_ALERT_OFF, T(L"Disable notifications"));
            AppendMenuW(alertMenu, MF_SEPARATOR, 0, nullptr);
            for (int i = 0; i < alerts::kThresholdCount; ++i) {
                int level = alerts::kThresholds[i];
                std::wstring label = std::to_wstring(level) + L"%";
                AppendMenuW(alertMenu, MF_STRING | (selectedThreshold == level ? MF_CHECKED : 0),
                            base + DEV_ALERT_FIRST + i, label.c_str());
            }
            AppendMenuW(sub, MF_POPUP, (UINT_PTR)alertMenu, T(L"Battery alerts"));
            AppendMenuW(sub, MF_STRING, base + DEV_HISTORY, T(L"Battery history..."));
            std::wstring line = DisplayName(*e) + L": " + StatusText(*e) + L" [" + T(e->st.via) + L"]";
            AppendMenuW(devMenu, MF_POPUP | (IsHidden(k) ? 0 : MF_CHECKED), (UINT_PTR)sub, line.c_str());
            keys.push_back(k);
        }
        AppendMenuW(devMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(devMenu, MF_STRING | (g_hideLastKnown ? MF_CHECKED : 0), ID_HIDE_LAST_KNOWN, T(L"Hide last known / asleep devices"));
        AppendMenuW(devMenu, MF_STRING | (g_showDisconnectedDevices ? MF_CHECKED : 0), ID_SHOW_DISCONNECTED, T(L"Show disconnected devices"));
        AppendMenuW(devMenu, MF_STRING | (g_recordHistory ? MF_CHECKED : 0), ID_RECORD_HISTORY, T(L"Record battery history"));
        {
            HMENU sub = CreatePopupMenu();
            const UINT count = ARRAYSIZE(POLL_SECONDS);
            UINT checked = 0;
            for (UINT i = 0; i < count; i++) {
                DWORD s = POLL_SECONDS[i];
                std::wstring label = s < 60 ? TF(L"{seconds} s", { { L"seconds", std::to_wstring(s) } })
                                            : TF(L"{minutes} min", { { L"minutes", std::to_wstring(s / 60) } });
                AppendMenuW(sub, MF_STRING, ID_POLL_FIRST + i, label.c_str());
                if (s * 1000 == g_pollIntervalMs) checked = ID_POLL_FIRST + i;
            }
            if (checked) CheckMenuRadioItem(sub, ID_POLL_FIRST, ID_POLL_FIRST + count - 1, checked, MF_BYCOMMAND);
            AppendMenuW(devMenu, MF_POPUP, (UINT_PTR)sub, T(L"Check battery every"));
        }
        AppendMenuW(devMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(devMenu, MF_STRING, ID_REPORT, T(L"Request support for a device..."));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)devMenu, T(L"Devices"));
    }
    // With more than one monitor: choose which taskbars show the widget (all, main only, or one monitor).
    auto monitors = widget::OtherMonitors();
    if (monitors.size() > 31) monitors.resize(31);
    {
        HMENU appMenu = CreatePopupMenu();
        AppendMenuW(appMenu, MF_STRING | (g_wantWidget ? MF_CHECKED : 0), ID_MODE_WIDGET, T(L"Show on the taskbar (icon + percentage)"));
        AppendMenuW(appMenu, MF_STRING | (!g_wantWidget ? MF_CHECKED : 0), ID_MODE_TRAY, T(L"Show as tray icons"));
        CheckMenuRadioItem(appMenu, ID_MODE_WIDGET, ID_MODE_TRAY, g_wantWidget ? ID_MODE_WIDGET : ID_MODE_TRAY, MF_BYCOMMAND);
        UINT bgFlags = MF_STRING | (g_widgetBackground ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED);
        AppendMenuW(appMenu, bgFlags, ID_WIDGET_BACKGROUND, T(L"Background behind taskbar text (for transparent taskbars)"));
        AppendMenuW(appMenu, MF_STRING | (g_stacked ? MF_CHECKED : 0) | (g_wantWidget ? 0 : MF_GRAYED), ID_STACKED, T(L"Icons above percentages (narrower)"));
        if (!monitors.empty()) {
            HMENU sub = CreatePopupMenu();
            UINT f = MF_STRING | (g_wantWidget ? 0 : MF_GRAYED);
            AppendMenuW(sub, f, ID_MONITOR_ALL, T(L"All monitors"));
            AppendMenuW(sub, f, ID_MONITOR_MAIN, T(L"Main monitor only"));
            UINT checked = g_widgetMonitor == L"main" ? ID_MONITOR_MAIN : ID_MONITOR_ALL;
            bool found = g_widgetMonitor == L"all" || g_widgetMonitor == L"main";
            for (UINT i = 0; i < monitors.size(); i++) {
                AppendMenuW(sub, f, ID_MONITOR_FIRST + i, TF(L"{monitor} only", { { L"monitor", monitors[i].label } }).c_str());
                if (monitors[i].id == g_widgetMonitor) { checked = ID_MONITOR_FIRST + i; found = true; }
            }
            if (!found) {  // the chosen monitor is unplugged: the main taskbar shows the widget meanwhile
                AppendMenuW(sub, MF_STRING | MF_GRAYED, ID_MONITOR_FIRST + 31, T(L"Chosen monitor (not connected, shown on main)"));
                checked = ID_MONITOR_FIRST + 31;
            }
            CheckMenuRadioItem(sub, ID_MONITOR_ALL, ID_MONITOR_FIRST + 31, checked, MF_BYCOMMAND);
            AppendMenuW(appMenu, MF_POPUP | (g_wantWidget ? 0 : MF_GRAYED), (UINT_PTR)sub, T(L"Show the widget on"));
        }
        AppendMenuW(appMenu, MF_STRING | (g_brandColors ? MF_CHECKED : 0), ID_BRAND_COLORS, T(L"Brand colors (Razer green, Logitech blue, ...)"));
        AppendMenuW(appMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(appMenu, MF_STRING | (g_wantWidget ? 0 : MF_GRAYED), ID_OFFSET_LEFT, T(L"Move left"));
        AppendMenuW(appMenu, MF_STRING | (g_wantWidget ? 0 : MF_GRAYED), ID_OFFSET_RIGHT, T(L"Move right"));
        AppendMenuW(appMenu, MF_STRING | (g_wantWidget ? 0 : MF_GRAYED), ID_OFFSET_RESET, T(L"Reset position"));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)appMenu, T(L"Appearance"));
    }
    {
        HMENU mm = CreatePopupMenu();
        for (const auto& ms : kMetrics) {
            int id = static_cast<int>(ms.id);
            HMENU loc = CreatePopupMenu();
            UINT base = ID_METRIC_WHERE_FIRST + static_cast<UINT>(id) * 4;
            AppendMenuW(loc, MF_STRING, base + static_cast<UINT>(metrics::Where::Bar), T(L"Taskbar"));
            AppendMenuW(loc, MF_STRING, base + static_cast<UINT>(metrics::Where::Tray), T(L"Notification area"));
            AppendMenuW(loc, MF_STRING, base + static_cast<UINT>(metrics::Where::Hidden), T(L"Hidden"));
            CheckMenuRadioItem(loc, base, base + 2, base + static_cast<UINT>(g_metricCfg.where[id]), MF_BYCOMMAND);
            AppendMenuW(mm, MF_POPUP, (UINT_PTR)loc, T(ms.name));
        }
        AppendMenuW(mm, MF_SEPARATOR, 0, nullptr);
        HMENU rate = CreatePopupMenu();
        UINT checkedRate = 0;
        const UINT rateCount = ARRAYSIZE(METRIC_POLL_SECONDS);
        for (UINT i = 0; i < rateCount; i++) {
            DWORD s = METRIC_POLL_SECONDS[i];
            std::wstring label = TF(L"{seconds} s", { { L"seconds", std::to_wstring(s) } });
            AppendMenuW(rate, MF_STRING, ID_METRIC_INTERVAL_FIRST + i, label.c_str());
            if (s * 1000 == g_metricIntervalMs) checkedRate = ID_METRIC_INTERVAL_FIRST + i;
        }
        if (checkedRate) CheckMenuRadioItem(rate, ID_METRIC_INTERVAL_FIRST, ID_METRIC_INTERVAL_FIRST + rateCount - 1, checkedRate, MF_BYCOMMAND);
        AppendMenuW(mm, MF_POPUP, (UINT_PTR)rate, T(L"Refresh rate"));
        AppendMenuW(mm, MF_STRING | (g_groupTempsWithUsage ? MF_CHECKED : 0), ID_METRIC_GROUP, T(L"Group temperature with usage"));
        HMENU lbl = CreatePopupMenu();
        AppendMenuW(lbl, MF_STRING, ID_LABEL_FIRST + 0, T(L"Icon"));
        AppendMenuW(lbl, MF_STRING, ID_LABEL_FIRST + 1, T(L"Word"));
        AppendMenuW(lbl, MF_STRING, ID_LABEL_FIRST + 2, T(L"Both"));
        CheckMenuRadioItem(lbl, ID_LABEL_FIRST, ID_LABEL_FIRST + 2, ID_LABEL_FIRST + g_metricLabelStyle, MF_BYCOMMAND);
        AppendMenuW(mm, MF_POPUP, (UINT_PTR)lbl, T(L"Component label"));
        if (!g_metricSnap.ok[static_cast<int>(metrics::Id::CpuTemp)]) {
            static pawnio::PioState pioState = pawnio::PioState::Failed;
            static bool pioChecked = false;
            if (!pioChecked) {
                pioState = pawnio::Init();
                pioChecked = true;
            }
            if (pioState == pawnio::PioState::NoAdmin || pioState == pawnio::PioState::NoModule) {
                AppendMenuW(mm, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(mm, MF_STRING, ID_CPU_TEMP_ON, T(L"Turn on CPU temperature..."));
            }
        }
        AppendMenuW(m, MF_POPUP, (UINT_PTR)mm, T(L"System metrics"));
    }
    {
        HMENU alertsMenu = CreatePopupMenu();
        AppendMenuW(alertsMenu, MF_STRING | (g_tempAlerts ? MF_CHECKED : 0), ID_TEMP_ALERTS, T(L"Temperature alerts"));
        {
            HMENU tth = CreatePopupMenu();
            UINT checked = 0;
            const UINT count = ARRAYSIZE(TEMP_THRESHOLDS);
            for (UINT i = 0; i < count; i++) {
                std::wstring label = TF(L"{temperature} °C", { { L"temperature", std::to_wstring(TEMP_THRESHOLDS[i]) } });
                AppendMenuW(tth, MF_STRING, ID_TEMP_THRESH_FIRST + i, label.c_str());
                if (TEMP_THRESHOLDS[i] == g_tempAlertThreshold) checked = ID_TEMP_THRESH_FIRST + i;
            }
            if (checked) CheckMenuRadioItem(tth, ID_TEMP_THRESH_FIRST, ID_TEMP_THRESH_FIRST + count - 1, checked, MF_BYCOMMAND);
            AppendMenuW(alertsMenu, MF_POPUP, (UINT_PTR)tth, T(L"Temperature alerts at"));
        }
        AppendMenuW(alertsMenu, MF_STRING | (g_usageAlerts ? MF_CHECKED : 0), ID_USAGE_ALERTS, T(L"Usage alerts"));
        {
            HMENU uth = CreatePopupMenu();
            UINT checked = 0;
            const UINT count = ARRAYSIZE(USAGE_THRESHOLDS);
            for (UINT i = 0; i < count; i++) {
                std::wstring label = std::to_wstring(USAGE_THRESHOLDS[i]) + L"%";
                AppendMenuW(uth, MF_STRING, ID_USAGE_THRESH_FIRST + i, label.c_str());
                if (USAGE_THRESHOLDS[i] == g_usageAlertThreshold) checked = ID_USAGE_THRESH_FIRST + i;
            }
            if (checked) CheckMenuRadioItem(uth, ID_USAGE_THRESH_FIRST, ID_USAGE_THRESH_FIRST + count - 1, checked, MF_BYCOMMAND);
            AppendMenuW(alertsMenu, MF_POPUP, (UINT_PTR)uth, T(L"Usage alerts at"));
        }
        AppendMenuW(m, MF_POPUP, (UINT_PTR)alertsMenu, T(L"Alerts"));
    }
    // Language: automatic, every built-in or user-added language (in its own name), and a way to add one.
    i18n::Init();  // picks up language files added since the last time
    auto languages = i18n::Available();
    if (languages.size() > 64) languages.resize(64);
    {
        HMENU sub = CreatePopupMenu();
        AppendMenuW(sub, MF_STRING, ID_LANG_AUTO, T(L"Automatic (Windows language)"));
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        UINT checked = ID_LANG_AUTO;
        for (UINT i = 0; i < languages.size(); i++) {
            AppendMenuW(sub, MF_STRING, ID_LANG_FIRST + i, languages[i].name.c_str());
            if (!i18n::Choice().empty() && _wcsicmp(languages[i].code.c_str(), i18n::Choice().c_str()) == 0) checked = ID_LANG_FIRST + i;
        }
        CheckMenuRadioItem(sub, ID_LANG_AUTO, ID_LANG_FIRST + 63, checked, MF_BYCOMMAND);
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sub, MF_STRING, ID_LANG_ADD, T(L"Add your own language..."));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, T(L"Language"));
    }
    AppendMenuW(m, MF_STRING, ID_REFRESH, T(L"Refresh now"));
    AppendMenuW(m, MF_STRING, ID_TASKMGR, T(L"Open Task Manager"));
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, T(L"Start with Windows"));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, T(L"Exit"));
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    // The taskbar is always-on-top; without a topmost owner the rows that reach into it are
    // painted behind the bar, so the menu looks cut off at the bottom.
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    widget::SuspendHover();
    widget::SetTooltipActive(false);
    UINT cmd = TrackPopupMenu(
        m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | (i18n::Rtl() ? TPM_LAYOUTRTL : 0), pt.x,
        pt.y, 0, g_hwnd, nullptr);
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    widget::SetTooltipActive(true);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
        case ID_REFRESH: SetEvent(g_refreshEvt); break;
        case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
        case ID_HIDE_LAST_KNOWN: SetHideLastKnown(!g_hideLastKnown); break;
        case ID_SHOW_DISCONNECTED: SetShowDisconnectedDevices(!g_showDisconnectedDevices); break;
        case ID_RECORD_HISTORY: g_recordHistory = !g_recordHistory; SaveRecordHistory(g_recordHistory); break;
        case ID_WIDGET_BACKGROUND: SetWidgetBackground(!g_widgetBackground); break;
        case ID_MONITOR_ALL: SetWidgetMonitor(L"all"); break;
        case ID_LANG_AUTO: SetLanguage(L""); break;
        case ID_LANG_ADD: i18n::OpenUserFolder(); break;
        case ID_MONITOR_MAIN: SetWidgetMonitor(L"main"); break;
        case ID_BRAND_COLORS: SetBrandColors(!g_brandColors); break;
        case ID_OFFSET_LEFT: AdjustTaskbarOffset(+8); break;
        case ID_OFFSET_RIGHT: AdjustTaskbarOffset(-8); break;
        case ID_OFFSET_RESET: AdjustTaskbarOffset(0); break;
        case ID_TASKMGR: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
        case ID_STACKED: SetStacked(!g_stacked); break;
        case ID_METRIC_GROUP:
            g_groupTempsWithUsage = !g_groupTempsWithUsage;
            SaveGroupTempsWithUsage(g_groupTempsWithUsage);
            UpdateWidget();
            break;
        case ID_CPU_TEMP_ON:
            ActivateCpuTemp();
            break;
        case ID_TEMP_ALERTS:
            g_tempAlerts = !g_tempAlerts;
            SaveTempAlerts(g_tempAlerts);
            break;
        case ID_USAGE_ALERTS:
            g_usageAlerts = !g_usageAlerts;
            SaveUsageAlerts(g_usageAlerts);
            break;
        case ID_USAGE_THRESH_FIRST:
        case ID_USAGE_THRESH_FIRST + 1:
        case ID_USAGE_THRESH_FIRST + 2:
            g_usageAlertThreshold = USAGE_THRESHOLDS[cmd - ID_USAGE_THRESH_FIRST];
            SaveUsageAlertThreshold(g_usageAlertThreshold);
            break;
        case ID_TEMP_THRESH_FIRST:
        case ID_TEMP_THRESH_FIRST + 1:
        case ID_TEMP_THRESH_FIRST + 2:
            g_tempAlertThreshold = TEMP_THRESHOLDS[cmd - ID_TEMP_THRESH_FIRST];
            SaveTempAlertThreshold(g_tempAlertThreshold);
            break;
        case ID_MODE_WIDGET: SetMode(true); break;
        case ID_MODE_TRAY: SetMode(false); break;
        case ID_REPORT: report::Show(g_inst, g_hwnd); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_POLL_FIRST && cmd < ID_POLL_FIRST + ARRAYSIZE(POLL_SECONDS)) {
                SetPollSeconds(POLL_SECONDS[cmd - ID_POLL_FIRST]);
            } else if (cmd >= ID_LABEL_FIRST && cmd < ID_LABEL_FIRST + 3) {
                g_metricLabelStyle = static_cast<int>(cmd - ID_LABEL_FIRST);
                SaveMetricLabelStyle(g_metricLabelStyle);
                UpdateWidget();
            } else if (cmd >= ID_METRIC_INTERVAL_FIRST && cmd < ID_METRIC_INTERVAL_FIRST + ARRAYSIZE(METRIC_POLL_SECONDS)) {
                g_metricIntervalMs = METRIC_POLL_SECONDS[cmd - ID_METRIC_INTERVAL_FIRST] * 1000;
                SaveMetricIntervalMs(g_metricIntervalMs);
                metrics::SetSamplerInterval(g_metricIntervalMs);
            } else if (cmd >= ID_METRIC_WHERE_FIRST && cmd < ID_METRIC_WHERE_FIRST + metrics::kCount * 4) {
                UINT rel = cmd - ID_METRIC_WHERE_FIRST;
                int id = static_cast<int>(rel / 4);
                UINT loc = rel % 4;
                if (id >= 0 && id < metrics::kCount && loc <= static_cast<UINT>(metrics::Where::Hidden)) {
                    g_metricCfg.where[id] = static_cast<metrics::Where>(loc);
                    SaveMetricsWhere();
                    ApplyMetricTray(id);
                    UpdateWidget();
                }
            } else if (cmd >= ID_LANG_FIRST && cmd < ID_LANG_FIRST + languages.size()) {
                SetLanguage(languages[cmd - ID_LANG_FIRST].code);
            } else if (cmd >= ID_MONITOR_FIRST && cmd < ID_MONITOR_FIRST + monitors.size()) {
                SetWidgetMonitor(monitors[cmd - ID_MONITOR_FIRST].id);
            } else if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + keys.size() * DEV_STRIDE) {
                std::wstring key = keys[(cmd - ID_DEVICE_FIRST) / DEV_STRIDE];
                switch ((cmd - ID_DEVICE_FIRST) % DEV_STRIDE) {
                    case DEV_TOGGLE: ToggleHidden(key); break;
                    case DEV_LEFT:   MoveEntry(key, -1); break;
                    case DEV_RIGHT:  MoveEntry(key, +1); break;
                    case DEV_RENAME: RenameDevice(key); break;
                    case DEV_RESET_NAME: ResetDeviceName(key); break;
                    case DEV_ALERT_DEFAULT: SetAlertOverride(key, alerts::kUseDefault); break;
                    case DEV_ALERT_OFF: SetAlertOverride(key, alerts::kDisabled); break;
                    case DEV_HISTORY: {
                        auto it = g_entries.find(key);
                        if (it != g_entries.end())
                            battery_history::Show(g_hwnd, key, DisplayName(it->second));
                        break;
                    }
                    default: {
                        UINT slot = (cmd - ID_DEVICE_FIRST) % DEV_STRIDE;
                        if (slot >= DEV_ALERT_FIRST && slot < DEV_ALERT_FIRST + alerts::kThresholdCount)
                            SetAlertOverride(key, alerts::kThresholds[slot - DEV_ALERT_FIRST]);
                        break;
                    }
                }
            }
    }
}

// ---- worker thread ----------------------------------------------------------------------------

// `--demo`: one sample device of every brand instead of real ones, for screenshots and for checking the
// brand colors without owning the hardware. Keys, vendor ids and names are mixed on purpose, so every way
// of recognising a brand gets used.
bool g_demo = false;

std::vector<DeviceStatus> DemoDevices() {
    auto make = [](const wchar_t* key, const wchar_t* name, DeviceKind kind, int pct, unsigned short vid, bool charging) {
        DeviceStatus s;
        s.key = key;
        s.name = name;
        s.kind = kind;
        s.percent = pct;
        s.vid = vid;
        s.charging = charging;
        s.via = L"demo";
        return s;
    };
    std::vector<DeviceStatus> devices = {
        make(L"razer:demo", L"Razer DeathAdder V3 Pro", DeviceKind::Mouse, 78, 0x1532, false),
        make(L"demo:logi", L"Logitech G PRO X 2 LIGHTSPEED", DeviceKind::Headset, 64, 0x046D, false),
        make(L"steelseries:demo", L"SteelSeries Arctis Nova 7", DeviceKind::Headset, 55, 0, false),
        make(L"hsc:1b1c:0a6b", L"Corsair HS80 RGB Wireless", DeviceKind::Headset, 81, 0x1B1C, false),
        make(L"hyperx:demo", L"HyperX Cloud III Wireless", DeviceKind::Headset, 47, 0, false),
        make(L"rog:demo", L"ROG Harpe Ace", DeviceKind::Mouse, 90, 0, false),
        make(L"zowie:demo", L"ZOWIE EC2-CW", DeviceKind::Mouse, 66, 0x04A5, false),
        make(L"bt:demo-ps", L"Wireless Controller", DeviceKind::Gamepad, 38, 0x054C, false),
        make(L"xbox:demo", L"Xbox Wireless Controller", DeviceKind::Gamepad, 72, 0x045E, false),
        make(L"bt:demo-buds", L"Galaxy Buds3 Pro", DeviceKind::Headset, 59, 0, false),
        make(L"airpods:demo", L"AirPods Pro", DeviceKind::Headset, 85, 0, false),
        make(L"jabra:demo", L"Jabra Evolve2 65", DeviceKind::Headset, 23, 0x0B0E, false),
        make(L"jbl:demo", L"JBL Quantum 910", DeviceKind::Headset, 70, 0x0ECB, false),
        make(L"coolermaster:demo", L"Cooler Master MM712", DeviceKind::Mouse, 12, 0x2516, false),
        make(L"steam:demo", L"Steam Controller", DeviceKind::Gamepad, 50, 0x28DE, true),
        make(L"bt:demo-plain", L"Bluetooth Headphones", DeviceKind::Headset, 100, 0, true),  // charging at 100%: fully charged
    };
    DeviceStatus offline = make(L"demo:offline-receiver", L"Offline wireless headset", DeviceKind::Headset, -1, 0, false);
    offline.via = L"2.4 GHz receiver";
    offline.connected = false;
    devices.push_back(offline);  // hidden by default, shown when the opt-in setting is enabled
    return devices;
}

DWORD WINAPI Worker(LPVOID) {
    // At sign-in a USB receiver may enumerate before its wireless link is ready.
    constexpr DWORD startupPollDelays[] = { 3000, 7000, 15000 };
    size_t startupPoll = 0;
    HANDLE events[2] = { g_stopEvt, g_refreshEvt };
    for (;;) {
        auto* v = new std::vector<DeviceStatus>(g_demo ? DemoDevices() : PollAllDevices());
        if (!PostMessageW(g_hwnd, WM_RESULTS, 0, (LPARAM)v)) delete v;
        DWORD delay = g_pollIntervalMs;
        if (startupPoll < ARRAYSIZE(startupPollDelays)) delay = startupPollDelays[startupPoll++];
        if (WaitForMultipleObjects(2, events, FALSE, delay) == WAIT_OBJECT_0) return 0;
    }
}

void RefreshSoon(UINT ms) { SetTimer(g_hwnd, TIMER_DEBOUNCE, ms, nullptr); }

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted
        widget::Destroy();
        g_useWidget = false;
        g_alertIconAdded = false;
        KillTimer(g_hwnd, TIMER_ALERT_ICON);
        ReAddAll();
        Reconcile();
        return 0;
    }
    switch (msg) {
        case WM_RESULTS: {
            auto* v = reinterpret_cast<std::vector<DeviceStatus>*>(lp);
            OnResults(*v);
            delete v;
            return 0;
        }
        case WM_METRICS: {
            auto* s = reinterpret_cast<metrics::Snapshot*>(lp);
            if (s) {
                bool changed = !g_hasMetricSnap || !metrics::SameValues(g_metricSnap, *s);
                g_metricSnap = *s;
                g_hasMetricSnap = true;
                delete s;
                EvaluateTempAlerts(g_metricSnap);
                EvaluateUsageAlerts(g_metricSnap);
                if (changed) {
                    UpdateWidget();
                    ApplyAllMetricTray();
                }
            }
            return 0;
        }
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: ShowMenu(); break;
            }
            return 0;
        case WM_WIDGET_CLICK:
            if (wp == 1) ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
            else ShowMenu();
            return 0;
        case WM_ENDSESSION:  // sign-out / shutdown / installer restart manager
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) RefreshSoon(2500);
            return TRUE;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) RefreshSoon(5000);
            if (wp == PBT_POWERSETTINGCHANGE && lp) {
                const auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
                if (IsEqualGUID(ps->PowerSetting, kConsoleDisplayState) && ps->DataLength >= sizeof(DWORD)) {
                    DWORD state = *reinterpret_cast<const DWORD*>(ps->Data);
                    metrics::SetSamplerPaused(state == 0);
                }
            }
            return TRUE;
        case WM_TIMER:
            if (wp == TIMER_DEBOUNCE) { KillTimer(h, TIMER_DEBOUNCE); SetEvent(g_refreshEvt); }
            if (wp == TIMER_WIDGET) { Reconcile(); updates::Tick(h); }
            if (wp == TIMER_ALERT_ICON) {
                KillTimer(h, TIMER_ALERT_ICON);
                if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                bool light = ReadLightTaskbar();
                if (light != g_lightTaskbar) { g_lightTaskbar = light; ApplyMenuTheme(light); RedrawAll(); widget::SetLightTheme(light); }
            }
            return 0;
        case WM_DESTROY:
            updates::Shutdown();
            KillTimer(h, TIMER_WIDGET);
            KillTimer(h, TIMER_ALERT_ICON);
            if (g_alertIconAdded) { TrayDelete(ALERT_ICON_ID); g_alertIconAdded = false; }
            widget::Destroy();
            SetEvent(g_stopEvt);
            metrics::StopSampler();
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            if (g_metricWorker) WaitForSingleObject(g_metricWorker, 5000);
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            for (const auto& m : kMetrics) {
                int id = static_cast<int>(m.id);
                if (g_metricTray[id].added) TrayDelete(metrics::kTrayUidBase + static_cast<UINT>(id));
                if (g_metricTray[id].icon) DestroyIcon(g_metricTray[id].icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// `Monitor.exe --list` prints what it finds, `--dump` every HID collection.
int ListToConsole(bool dump) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto write = [&](const std::wstring& s) {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), n, nullptr, nullptr);
        DWORD w;
        WriteFile(out, u.data(), (DWORD)u.size(), &w, nullptr);
    };
    if (dump) {
        // Also saved to a file: PowerShell does not wait for this windowed exe, so console output can end up
        // after the prompt or get lost, and a double-click has no console at all.
        std::wstring text = DumpHidCollections();
        wchar_t path[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"Monitor-dump.txt");
        HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            std::wstring crlf;
            for (wchar_t ch : text) crlf += ch == L'\n' ? std::wstring(L"\r\n") : std::wstring(1, ch);
            int n = WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), nullptr, 0, nullptr, nullptr);
            std::string u = "\xEF\xBB\xBF" + std::string(n, 0);  // UTF-8 byte order mark, so Notepad shows it right
            WideCharToMultiByte(CP_UTF8, 0, crlf.c_str(), (int)crlf.size(), u.data() + 3, n, nullptr, nullptr);
            DWORD w;
            WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr);
            CloseHandle(f);
        }
        if (out && out != INVALID_HANDLE_VALUE) {
            write(text);
            write(std::wstring(L"\nAlso saved to ") + path + L"\n");
        } else {
            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNORMAL);  // no console: show the file
        }
        return 0;
    }
    auto list = PollAllDevices();
    if (list.empty()) write(L"No devices with battery info found.\n");
    for (auto& s : list) {
        Accent accent = BrandOf(s);
        std::wstring line = s.name + L"  [" + s.via + L"]  " +
                            (accent != Accent::None ? L"{" + std::wstring(StyleOf(accent).name) + L"}  " : L"");
        if (!s.connected) line += L"off / not connected";
        else if (s.asleep) line += L"asleep";
        else if (!s.levelText.empty()) line += s.levelText;
        else if (s.percent >= 0) line += std::to_wstring(s.percent) + L"%";
        else line += L"unknown";
        if (!s.detail.empty()) line += L" (" + s.detail + L")";
        if (s.full || (s.charging && s.percent >= 100)) line += L", fully charged";
        else if (s.charging) line += L", charging";
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        write(line + L"   (" + s.key + L")\n");
    }
    return 0;
}

// Machine-readable device list, shared with the Linux entry point (src/json_output.h).
int ListJson() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    std::string text = DevicesToJson(PollAllDevices());  // UTF-8
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, text.data(), (DWORD)text.size(), &written, nullptr);
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--json")) return ListJson();
    if (cmdLine && wcsstr(cmdLine, L"--help")) {
        MessageBoxW(nullptr,
                    L"Monitor options:\n\n"
                    L"--list   Print devices and battery levels\n"
                    L"--json   Print a JSON array of devices and battery levels\n"
                    L"--dump   Print every HID collection and device\n"
                    L"--report Open the \"Request support for a device\" window\n"
                    L"--demo   Show sample devices\n"
                    L"--quit   Quit a running instance",
                    L"Monitor", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int applyResult = argv ? updates::RunApplyMode(argc, argv) : -1;
    if (argv) LocalFree(argv);
    if (applyResult >= 0) return applyResult;
    if (cmdLine && wcsstr(cmdLine, L"--list")) return ListToConsole(false);
    if (cmdLine && wcsstr(cmdLine, L"--dump")) return ListToConsole(true);
    if (cmdLine && wcsstr(cmdLine, L"--metrics-dump")) {
        metrics::Snapshot snap{};
        metrics::TakeSnapshot(snap);
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!out || out == INVALID_HANDLE_VALUE) {
            if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        if (out && out != INVALID_HANDLE_VALUE) {
            std::string line;
            auto add = [&](const char* key, metrics::Id id) {
                int idx = static_cast<int>(id);
                if (!snap.ok[idx]) return;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s%s=%.1f", line.empty() ? "" : " ", key, snap.val[idx]);
                line += buf;
            };
            add("cpu", metrics::Id::CpuUsage);
            add("cpu_temp", metrics::Id::CpuTemp);
            add("ram", metrics::Id::RamUsage);
            add("gpu", metrics::Id::GpuUsage);
            add("gpu_temp", metrics::Id::GpuTemp);
            add("vram", metrics::Id::Vram);
            add("ssd_temp", metrics::Id::SsdTemp);
            line += "\r\n";
            DWORD w = 0;
            WriteFile(out, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
        }
        return 0;
    }
    if (cmdLine && wcsstr(cmdLine, L"--report")) {  // the "Request support for a device" window on its own
        i18n::Init();
        report::Show(inst, nullptr);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (report::PreTranslate(&msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {  // used by the installer before upgrading/uninstalling
        if (HWND w = FindWindowW(L"WirelessBatteryWnd", nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            HANDLE running = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
            if (!running) return 1;
            PostMessageW(w, WM_CLOSE, 0, 0);
            DWORD exited = WaitForSingleObject(running, 30000);
            CloseHandle(running);
            if (exited != WAIT_OBJECT_0) return 1;
        }
        return 0;
    }
    g_inst = inst;
    g_demo = cmdLine && wcsstr(cmdLine, L"--demo");

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\WirelessBatteryTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    updates::Initialize();

    g_lightTaskbar = ReadLightTaskbar();
    ApplyMenuTheme(g_lightTaskbar);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"WirelessBatteryWnd";
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    DEV_BROADCAST_DEVICEINTERFACE_W filter{ sizeof(filter) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY powerNotify = RegisterPowerSettingNotification(g_hwnd, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_wantWidget = LoadWidgetMode();
    g_hideLastKnown = LoadHideLastKnown();
    g_showDisconnectedDevices = LoadShowDisconnectedDevices();
    g_recordHistory = LoadRecordHistory();
    g_widgetBackground = LoadWidgetBackground();
    g_brandColors = LoadBrandColors();
    g_groupTempsWithUsage = LoadGroupTempsWithUsage();
    g_tempAlerts = LoadTempAlerts();
    g_tempAlertThreshold = LoadTempAlertThreshold();
    g_usageAlerts = LoadUsageAlerts();
    g_usageAlertThreshold = LoadUsageAlertThreshold();
    g_metricLabelStyle = LoadMetricLabelStyle();
    g_stacked = LoadStacked();
    g_pollIntervalMs = LoadPollSeconds() * 1000;
    MigrateTraySense();
    LoadMetricsWhere();
    g_metricIntervalMs = LoadMetricIntervalMs();
    widget::SetStacked(g_stacked);
    widget::SetBackground(g_widgetBackground);  // remembered by the widget across re-creation
    i18n::Init();
    g_widgetMonitor = LoadWidgetMonitor();
    widget::SetMonitorChoice(g_widgetMonitor);
    LoadHidden();
    LoadOrder();
    LoadAliases();
    LoadAlertThresholds();
    battery_history::Initialize(g_demo);
    Reconcile();
    ApplyAllMetricTray();
    UpdatePlaceholder();  // something to click on until the first poll finishes
    SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    metrics::SetSamplerTarget(g_hwnd, WM_METRICS);
    metrics::SetSamplerInterval(g_metricIntervalMs);
    g_metricWorker = CreateThread(nullptr, 0, metrics::MetricsWorker, nullptr, 0, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (report::PreTranslate(&m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // drain results posted after shutdown started
    while (PeekMessageW(&m, nullptr, WM_RESULTS, WM_RESULTS, PM_REMOVE)) delete reinterpret_cast<std::vector<DeviceStatus>*>(m.lParam);
    while (PeekMessageW(&m, nullptr, WM_METRICS, WM_METRICS, PM_REMOVE)) delete reinterpret_cast<metrics::Snapshot*>(m.lParam);
    if (powerNotify) UnregisterPowerSettingNotification(powerNotify);
    if (notify) UnregisterDeviceNotification(notify);
    if (g_metricWorker) CloseHandle(g_metricWorker);
    if (g_worker) CloseHandle(g_worker);
    ReleaseMutex(mutex);
    return 0;
}
