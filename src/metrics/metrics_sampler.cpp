#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "metrics.h"

namespace metrics {
namespace {

using T_PdhOpenQueryW = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
using T_PdhAddEnglishCounterW = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
using T_PdhCollectQueryData = PDH_STATUS(WINAPI*)(PDH_HQUERY);
using T_PdhGetFormattedCounterArrayW = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
using T_PdhCloseQuery = PDH_STATUS(WINAPI*)(PDH_HQUERY);

T_PdhOpenQueryW p_PdhOpenQueryW = nullptr;
T_PdhAddEnglishCounterW p_PdhAddEnglishCounterW = nullptr;
T_PdhCollectQueryData p_PdhCollectQueryData = nullptr;
T_PdhGetFormattedCounterArrayW p_PdhGetFormattedCounterArrayW = nullptr;
T_PdhCloseQuery p_PdhCloseQuery = nullptr;

HMODULE SysDll(const wchar_t* name) {
    return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

bool LoadPdh() {
    static int done = 0;
    static bool ok = false;
    if (done) return ok;
    done = 1;
    HMODULE m = SysDll(L"pdh.dll");
    if (!m) return false;
    p_PdhOpenQueryW = reinterpret_cast<T_PdhOpenQueryW>(reinterpret_cast<void*>(GetProcAddress(m, "PdhOpenQueryW")));
    p_PdhAddEnglishCounterW = reinterpret_cast<T_PdhAddEnglishCounterW>(reinterpret_cast<void*>(GetProcAddress(m, "PdhAddEnglishCounterW")));
    p_PdhCollectQueryData = reinterpret_cast<T_PdhCollectQueryData>(reinterpret_cast<void*>(GetProcAddress(m, "PdhCollectQueryData")));
    p_PdhGetFormattedCounterArrayW = reinterpret_cast<T_PdhGetFormattedCounterArrayW>(reinterpret_cast<void*>(GetProcAddress(m, "PdhGetFormattedCounterArrayW")));
    p_PdhCloseQuery = reinterpret_cast<T_PdhCloseQuery>(reinterpret_cast<void*>(GetProcAddress(m, "PdhCloseQuery")));
    ok = p_PdhOpenQueryW && p_PdhAddEnglishCounterW && p_PdhCollectQueryData &&
         p_PdhGetFormattedCounterArrayW && p_PdhCloseQuery;
    return ok;
}

const GUID kIidIdxgifactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };

PDH_HQUERY g_q = nullptr;
PDH_HCOUNTER g_cEng = nullptr;
PDH_HCOUNTER g_cMem = nullptr;
BYTE* g_buf = nullptr;
DWORD g_bufSz = 0;

LUID g_luid{};
bool g_hasGpu = false;
ULONGLONG g_vramTotal = 0;
wchar_t g_luidStr[40]{};

ULONGLONG g_prevIdle = 0;
ULONGLONG g_prevTot = 0;
bool g_inited = false;

std::atomic<HWND> g_hwnd{ nullptr };
std::atomic<UINT> g_msg{ 0 };
std::atomic<bool> g_paused{ false };
std::atomic<bool> g_quit{ false };
std::atomic<DWORD> g_interval{ 5000 };
HANDLE g_wake = nullptr;

ULONGLONG FtToU64(FILETIME f) {
    return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

void ReadCpu(Snapshot& out) {
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return;
    ULONGLONG id = FtToU64(i);
    ULONGLONG tot = FtToU64(k) + FtToU64(u);
    if (g_prevTot && tot > g_prevTot) {
        out.val[static_cast<int>(Id::CpuUsage)] =
            100.0 * (1.0 - static_cast<double>(id - g_prevIdle) / static_cast<double>(tot - g_prevTot));
        out.ok[static_cast<int>(Id::CpuUsage)] = true;
    }
    g_prevIdle = id;
    g_prevTot = tot;
}

void ReadRam(Snapshot& out) {
    MEMORYSTATUSEX m{ sizeof(m) };
    if (!GlobalMemoryStatusEx(&m)) return;
    out.val[static_cast<int>(Id::RamUsage)] =
        100.0 * static_cast<double>(m.ullTotalPhys - m.ullAvailPhys) / static_cast<double>(m.ullTotalPhys);
    out.ok[static_cast<int>(Id::RamUsage)] = true;
}

PDH_FMT_COUNTERVALUE_ITEM_W* PdhItems(PDH_HCOUNTER c, DWORD* n) {
    DWORD sz = g_bufSz;
    *n = 0;
    if (!c) return nullptr;
    PDH_STATUS s = p_PdhGetFormattedCounterArrayW(
        c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &sz, n, reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(g_buf));
    if (static_cast<DWORD>(s) == static_cast<DWORD>(PDH_MORE_DATA)) {
        BYTE* nb = g_buf ? static_cast<BYTE*>(HeapReAlloc(GetProcessHeap(), 0, g_buf, sz))
                         : static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, sz));
        if (!nb) return nullptr;
        g_buf = nb;
        g_bufSz = sz;
        s = p_PdhGetFormattedCounterArrayW(
            c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &sz, n, reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(g_buf));
    }
    return s == ERROR_SUCCESS ? reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(g_buf) : nullptr;
}

bool GoodPdhItem(const PDH_FMT_COUNTERVALUE_ITEM_W* it) {
    return it->FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it->FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
}

bool HasLuid(const wchar_t* s) {
    size_t len = wcslen(g_luidStr);
    for (; *s; s++) {
        if (_wcsnicmp(s, g_luidStr, len) == 0) return true;
    }
    return false;
}

void ReadGpuUsage(Snapshot& out) {
    DWORD n = 0;
    double eng[64]{};
    double mx = 0.0;
    PDH_FMT_COUNTERVALUE_ITEM_W* it = PdhItems(g_cEng, &n);
    if (!it) return;
    for (DWORD i = 0; i < n; i++) {
        if (!GoodPdhItem(&it[i]) || !HasLuid(it[i].szName)) continue;
        const wchar_t* e = wcsstr(it[i].szName, L"_eng_");
        if (!e) continue;
        int k = _wtoi(e + 5);
        if (k >= 0 && k < 64) eng[k] += it[i].FmtValue.doubleValue;
    }
    for (int k = 0; k < 64; k++) {
        if (eng[k] > mx) mx = eng[k];
    }
    out.val[static_cast<int>(Id::GpuUsage)] = mx > 100.0 ? 100.0 : mx;
    out.ok[static_cast<int>(Id::GpuUsage)] = true;
}

void ReadVram(Snapshot& out) {
    DWORD n = 0;
    double used = 0.0;
    bool found = false;
    PDH_FMT_COUNTERVALUE_ITEM_W* it = PdhItems(g_cMem, &n);
    if (!it || !g_vramTotal) return;
    for (DWORD i = 0; i < n; i++) {
        if (GoodPdhItem(&it[i]) && HasLuid(it[i].szName)) {
            used += it[i].FmtValue.doubleValue;
            found = true;
        }
    }
    if (!found) return;
    out.val[static_cast<int>(Id::Vram)] = 100.0 * used / static_cast<double>(g_vramTotal);
    out.ok[static_cast<int>(Id::Vram)] = true;
}

void InitGpu() {
    HMODULE d = SysDll(L"dxgi.dll");
    if (d) {
        using CF = HRESULT(WINAPI*)(const GUID*, void**);
        auto cf = reinterpret_cast<CF>(reinterpret_cast<void*>(GetProcAddress(d, "CreateDXGIFactory1")));
        IDXGIFactory1* f = nullptr;
        if (cf && SUCCEEDED(cf(&kIidIdxgifactory1, reinterpret_cast<void**>(&f)))) {
            IDXGIAdapter1* a = nullptr;
            for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
                DXGI_ADAPTER_DESC1 dd{};
                if (SUCCEEDED(a->GetDesc1(&dd)) && !(dd.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    dd.VendorId != 0x1414 && (!g_hasGpu || dd.DedicatedVideoMemory > g_vramTotal)) {
                    g_vramTotal = dd.DedicatedVideoMemory;
                    g_luid = dd.AdapterLuid;
                    g_hasGpu = true;
                }
                a->Release();
            }
            f->Release();
        }
    }
    if (!g_hasGpu) return;
    _snwprintf(g_luidStr, ARRAYSIZE(g_luidStr), L"luid_0x%08X_0x%08X",
               static_cast<unsigned>(g_luid.HighPart), static_cast<unsigned>(g_luid.LowPart));
}

void BuildQuery() {
    if (g_q) {
        p_PdhCloseQuery(g_q);
        g_q = nullptr;
    }
    g_cEng = nullptr;
    g_cMem = nullptr;
    if (!g_hasGpu || !LoadPdh()) return;
    if (p_PdhOpenQueryW(nullptr, 0, &g_q) != ERROR_SUCCESS) {
        g_q = nullptr;
        return;
    }
    if (p_PdhAddEnglishCounterW(g_q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_cEng) != ERROR_SUCCESS) {
        g_cEng = nullptr;
    }
    if (p_PdhAddEnglishCounterW(g_q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_cMem) != ERROR_SUCCESS) {
        g_cMem = nullptr;
    }
    p_PdhCollectQueryData(g_q);
}

void EnsureInit() {
    if (g_inited) return;
    g_inited = true;
    InitGpu();
    BuildQuery();
    Snapshot dummy{};
    ReadCpu(dummy);
    Sleep(250);
}

}  // namespace

bool TakeSnapshot(Snapshot& out) {
    out = Snapshot{};
    EnsureInit();
    ReadCpu(out);
    ReadRam(out);
    if (g_q && p_PdhCollectQueryData(g_q) == ERROR_SUCCESS) {
        if (g_cEng) ReadGpuUsage(out);
        if (g_cMem) ReadVram(out);
    }
    return true;
}

void SetSamplerTarget(void* hwnd, unsigned int msg) {
    g_hwnd.store(static_cast<HWND>(hwnd), std::memory_order_release);
    g_msg.store(msg, std::memory_order_release);
}

void SetSamplerPaused(bool paused) {
    bool prev = g_paused.exchange(paused, std::memory_order_acq_rel);
    if (prev && !paused && g_wake) SetEvent(g_wake);
}

void SetSamplerInterval(unsigned long ms) {
    if (ms < 500) ms = 500;
    g_interval.store(ms, std::memory_order_release);
    if (g_wake) SetEvent(g_wake);
}

void StopSampler() {
    g_quit.store(true, std::memory_order_release);
    if (g_wake) SetEvent(g_wake);
}

unsigned long __stdcall MetricsWorker(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    g_quit.store(false, std::memory_order_release);
    if (!g_wake) g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    EnsureInit();
    for (;;) {
        if (g_quit.load(std::memory_order_acquire)) break;
        if (!g_paused.load(std::memory_order_acquire)) {
            auto* snap = new Snapshot{};
            TakeSnapshot(*snap);
            HWND w = g_hwnd.load(std::memory_order_acquire);
            UINT m = g_msg.load(std::memory_order_acquire);
            if (!w || !m || !PostMessageW(w, m, 0, reinterpret_cast<LPARAM>(snap))) delete snap;
        }
        if (g_wake) {
            WaitForSingleObject(g_wake, g_paused.load(std::memory_order_acquire) ? INFINITE : g_interval.load(std::memory_order_acquire));
        } else {
            Sleep(g_interval.load(std::memory_order_acquire));
        }
    }
    if (g_q && p_PdhCloseQuery) {
        p_PdhCloseQuery(g_q);
        g_q = nullptr;
    }
    if (g_buf) {
        HeapFree(GetProcessHeap(), 0, g_buf);
        g_buf = nullptr;
        g_bufSz = 0;
    }
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = nullptr;
    }
    return 0;
}

}  // namespace metrics
