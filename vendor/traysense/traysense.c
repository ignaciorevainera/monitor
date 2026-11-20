#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <dxgi.h>
#include <wchar.h>
#include <math.h>
#include <winioctl.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <winhttp.h>
#include <bcrypt.h>
#include <sddl.h>
#include <uiautomationclient.h>

static HMODULE sysdll(const wchar_t *name)
{
    return LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

typedef PDH_STATUS (WINAPI *T_PdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY *);
typedef PDH_STATUS (WINAPI *T_PdhAddEnglishCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *);
typedef PDH_STATUS (WINAPI *T_PdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *T_PdhGetFormattedCounterArrayW)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD, PPDH_FMT_COUNTERVALUE_ITEM_W);
typedef PDH_STATUS (WINAPI *T_PdhCloseQuery)(PDH_HQUERY);
typedef HINTERNET (WINAPI *T_WinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET (WINAPI *T_WinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET (WINAPI *T_WinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef BOOL (WINAPI *T_WinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *T_WinHttpReceiveResponse)(HINTERNET, LPVOID);
typedef BOOL (WINAPI *T_WinHttpQueryHeaders)(HINTERNET, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef BOOL (WINAPI *T_WinHttpQueryDataAvailable)(HINTERNET, LPDWORD);
typedef BOOL (WINAPI *T_WinHttpReadData)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL (WINAPI *T_WinHttpCloseHandle)(HINTERNET);
typedef NTSTATUS (WINAPI *T_BCryptOpenAlgorithmProvider)(BCRYPT_ALG_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
typedef NTSTATUS (WINAPI *T_BCryptCreateHash)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *T_BCryptHashData)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *T_BCryptFinishHash)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *T_BCryptDestroyHash)(BCRYPT_HASH_HANDLE);
typedef NTSTATUS (WINAPI *T_BCryptCloseAlgorithmProvider)(BCRYPT_ALG_HANDLE, ULONG);

#define FN(n) static T_##n p_##n;
FN(PdhOpenQueryW) FN(PdhAddEnglishCounterW) FN(PdhCollectQueryData) FN(PdhGetFormattedCounterArrayW) FN(PdhCloseQuery)
FN(WinHttpOpen) FN(WinHttpConnect) FN(WinHttpOpenRequest) FN(WinHttpSendRequest) FN(WinHttpReceiveResponse)
FN(WinHttpQueryHeaders) FN(WinHttpQueryDataAvailable) FN(WinHttpReadData) FN(WinHttpCloseHandle)
FN(BCryptOpenAlgorithmProvider) FN(BCryptCreateHash) FN(BCryptHashData) FN(BCryptFinishHash)
FN(BCryptDestroyHash) FN(BCryptCloseAlgorithmProvider)
#undef FN
#define GETFN(m, n) ((p_##n = (T_##n)(void *)GetProcAddress(m, #n)) != NULL)

static int load_pdh(void)
{
    static int done, ok;
    if (done) return ok;
    done = 1;
    HMODULE m = sysdll(L"pdh.dll");
    ok = m && GETFN(m, PdhOpenQueryW) && GETFN(m, PdhAddEnglishCounterW) && GETFN(m, PdhCollectQueryData) &&
         GETFN(m, PdhGetFormattedCounterArrayW) && GETFN(m, PdhCloseQuery);
    return ok;
}

static int load_net(void)
{
    static int done, ok;
    if (done) return ok;
    done = 1;
    HMODULE w = sysdll(L"winhttp.dll"), b = sysdll(L"bcrypt.dll");
    ok = w && b && GETFN(w, WinHttpOpen) && GETFN(w, WinHttpConnect) && GETFN(w, WinHttpOpenRequest) &&
         GETFN(w, WinHttpSendRequest) && GETFN(w, WinHttpReceiveResponse) && GETFN(w, WinHttpQueryHeaders) &&
         GETFN(w, WinHttpQueryDataAvailable) && GETFN(w, WinHttpReadData) && GETFN(w, WinHttpCloseHandle) &&
         GETFN(b, BCryptOpenAlgorithmProvider) && GETFN(b, BCryptCreateHash) && GETFN(b, BCryptHashData) &&
         GETFN(b, BCryptFinishHash) && GETFN(b, BCryptDestroyHash) && GETFN(b, BCryptCloseAlgorithmProvider);
    return ok;
}

#define PdhOpenQueryW                p_PdhOpenQueryW
#define PdhAddEnglishCounterW        p_PdhAddEnglishCounterW
#define PdhCollectQueryData          p_PdhCollectQueryData
#define PdhGetFormattedCounterArrayW p_PdhGetFormattedCounterArrayW
#define PdhCloseQuery                p_PdhCloseQuery
#define WinHttpOpen                  p_WinHttpOpen
#define WinHttpConnect               p_WinHttpConnect
#define WinHttpOpenRequest           p_WinHttpOpenRequest
#define WinHttpSendRequest           p_WinHttpSendRequest
#define WinHttpReceiveResponse       p_WinHttpReceiveResponse
#define WinHttpQueryHeaders          p_WinHttpQueryHeaders
#define WinHttpQueryDataAvailable    p_WinHttpQueryDataAvailable
#define WinHttpReadData              p_WinHttpReadData
#define WinHttpCloseHandle           p_WinHttpCloseHandle
#define BCryptOpenAlgorithmProvider  p_BCryptOpenAlgorithmProvider
#define BCryptCreateHash             p_BCryptCreateHash
#define BCryptHashData               p_BCryptHashData
#define BCryptFinishHash             p_BCryptFinishHash
#define BCryptDestroyHash            p_BCryptDestroyHash
#define BCryptCloseAlgorithmProvider p_BCryptCloseAlgorithmProvider

static int sys_path(wchar_t *out, const wchar_t *file)
{
    UINT n = GetSystemDirectoryW(out, MAX_PATH);
    if (!n || n + lstrlenW(file) + 2 >= MAX_PATH) return 0;
    lstrcatW(out, L"\\");
    lstrcatW(out, file);
    return 1;
}

static HANDLE g_self = INVALID_HANDLE_VALUE;

static void lock_self(void)
{
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, p, MAX_PATH);
    if (n && n < MAX_PATH)
        g_self = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
}

static int copy_self(const wchar_t *dst)
{
    LARGE_INTEGER sz, zero = { 0 };
    DWORD got, w;
    int ok = 0;
    if (g_self == INVALID_HANDLE_VALUE || !GetFileSizeEx(g_self, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16 << 20))
        return 0;
    BYTE *buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sz.QuadPart);
    if (!buf) return 0;
    if (SetFilePointerEx(g_self, zero, NULL, FILE_BEGIN) &&
        ReadFile(g_self, buf, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart) {
        HANDLE f = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            ok = WriteFile(f, buf, got, &w, NULL) && w == got;
            CloseHandle(f);
            if (!ok) DeleteFileW(dst);
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

static int app_dir(wchar_t *out)
{
    DWORD sz = MAX_PATH * sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion", L"ProgramFilesDir",
                     RRF_RT_REG_SZ, NULL, out, &sz) != ERROR_SUCCESS) return 0;
    if (lstrlenW(out) > MAX_PATH - 48) return 0;
    lstrcatW(out, L"\\TraySense");
    if (!CreateDirectoryW(out, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    DWORD a = GetFileAttributesW(out);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

#define APP      L"TraySense"
#define REGKEY   L"Software\\TraySense"
#define RUNKEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define WM_TRAY  (WM_APP + 1)

enum { M_CPU, M_CPUT, M_RAM, M_GPU, M_GPUT, M_VRAM, M_DISKT, M_N };
static const wchar_t *NAMES[M_N] = { L"CPU usage", L"CPU temperature", L"RAM usage",
                                     L"GPU usage", L"GPU temperature", L"VRAM usage", L"SSD temperature" };

static const COLORREF COL_D[M_N] = { RGB(80,190,255), 0, RGB(185,145,255), RGB(118,215,90), 0, RGB(118,215,90), 0 };
static const COLORREF COL_L[M_N] = { RGB(0,100,180),  0, RGB(110,60,200),  RGB(35,135,30),  0, RGB(35,135,30), 0 };

static HWND     g_wnd;
static UINT     g_taskbarCreated;
static DWORD    g_mask = (1u << M_N) - 1, g_interval = 2000;
static double   g_val[M_N];
static int      g_ok[M_N], g_shown[M_N], g_size, g_light;
static wchar_t  g_txt[M_N][4];
static HICON    g_icon[M_N];
static wchar_t  g_gpuName[64];
static double   g_ramUsed, g_ramTotal, g_vramUsed;

static PDH_HQUERY   g_q;
static PDH_HCOUNTER g_cTherm, g_cEng, g_cMem;
static BYTE        *g_buf;
static DWORD        g_bufSz;

static LUID       g_luid;
static int        g_hasGpu;
static ULONGLONG  g_vramTotal;
static wchar_t    g_luidStr[40];
static UINT       g_kmt;
typedef LONG (APIENTRY *PFN_KMT)(void *);
static PFN_KMT    pKmtQuery;
typedef struct { LUID Luid; UINT hAdapter; } KMT_OPEN;
typedef struct { UINT hAdapter; UINT Type; void *pData; UINT Size; } KMT_QAI;
typedef struct { UINT32 PhysIdx; ULONGLONG MemFreq, MaxMemFreq, MaxMemFreqOC, MemBw, PcieBw;
                 ULONG FanRPM, Power, Temperature; UCHAR PowerStateOverride; } KMT_PERF;
#define KMTQAITYPE_ADAPTERPERFDATA 62
typedef int (*PFN_NVTEMP)(void *, int, unsigned *);
static PFN_NVTEMP pNvTemp;
static void      *g_nvDev;

static const GUID IID_F1 = {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const GUID G_DISPLAY = {0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};

static ULONGLONG ft(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static void read_cpu(void)
{
    static ULONGLONG pi, pt;
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return;
    ULONGLONG id = ft(i), tot = ft(k) + ft(u);
    if (pt && tot > pt) {
        g_val[M_CPU] = 100.0 * (1.0 - (double)(id - pi) / (double)(tot - pt));
        g_ok[M_CPU] = 1;
    }
    pi = id; pt = tot;
}

static void read_ram(void)
{
    MEMORYSTATUSEX m = { sizeof m };
    if (!GlobalMemoryStatusEx(&m)) return;
    g_ramTotal = m.ullTotalPhys / 1073741824.0;
    g_ramUsed  = (m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;
    g_val[M_RAM] = 100.0 * (double)(m.ullTotalPhys - m.ullAvailPhys) / (double)m.ullTotalPhys;
    g_ok[M_RAM] = 1;
}

static PDH_FMT_COUNTERVALUE_ITEM_W *pdh_items(PDH_HCOUNTER c, DWORD *n)
{
    DWORD sz = g_bufSz;
    *n = 0;
    if (!c) return NULL;
    PDH_STATUS s = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &sz, n,
                                                (PPDH_FMT_COUNTERVALUE_ITEM_W)g_buf);
    if ((DWORD)s == (DWORD)PDH_MORE_DATA) {
        BYTE *nb = g_buf ? HeapReAlloc(GetProcessHeap(), 0, g_buf, sz)
                         : HeapAlloc(GetProcessHeap(), 0, sz);
        if (!nb) return NULL;
        g_buf = nb; g_bufSz = sz;
        s = PdhGetFormattedCounterArrayW(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &sz, n,
                                         (PPDH_FMT_COUNTERVALUE_ITEM_W)g_buf);
    }
    return s == ERROR_SUCCESS ? (PDH_FMT_COUNTERVALUE_ITEM_W *)g_buf : NULL;
}

static int good(PDH_FMT_COUNTERVALUE_ITEM_W *it)
{
    return it->FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it->FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
}

static int has_luid(const wchar_t *s)
{
    size_t L = wcslen(g_luidStr);
    for (; *s; s++)
        if (_wcsnicmp(s, g_luidStr, L) == 0) return 1;
    return 0;
}

static void read_therm(void)
{
    DWORD n; double mx = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W *it = pdh_items(g_cTherm, &n);
    g_ok[M_CPUT] = 0;
    if (!it) return;
    for (DWORD i = 0; i < n; i++)
        if (good(&it[i]) && it[i].FmtValue.doubleValue > mx) mx = it[i].FmtValue.doubleValue;
    if (mx > 200.0) { g_val[M_CPUT] = mx - 273.15; g_ok[M_CPUT] = 1; }
}

static void read_gpu_usage(void)
{
    DWORD n; double eng[64] = {0}, mx = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W *it = pdh_items(g_cEng, &n);
    g_ok[M_GPU] = 0;
    if (!it) return;
    for (DWORD i = 0; i < n; i++) {
        if (!good(&it[i]) || !has_luid(it[i].szName)) continue;
        const wchar_t *e = wcsstr(it[i].szName, L"_eng_");
        if (!e) continue;
        int k = _wtoi(e + 5);
        if (k >= 0 && k < 64) eng[k] += it[i].FmtValue.doubleValue;
    }
    for (int k = 0; k < 64; k++) if (eng[k] > mx) mx = eng[k];
    g_val[M_GPU] = mx > 100.0 ? 100.0 : mx;
    g_ok[M_GPU] = 1;
}

static void read_vram(void)
{
    DWORD n; double used = 0; int found = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W *it = pdh_items(g_cMem, &n);
    g_ok[M_VRAM] = 0;
    if (!it || !g_vramTotal) return;
    for (DWORD i = 0; i < n; i++)
        if (good(&it[i]) && has_luid(it[i].szName)) { used += it[i].FmtValue.doubleValue; found = 1; }
    if (!found) return;
    g_vramUsed = used / 1073741824.0;
    g_val[M_VRAM] = 100.0 * used / (double)g_vramTotal;
    g_ok[M_VRAM] = 1;
}

static int kmt_temp(double *t)
{
    if (!g_kmt || !pKmtQuery) return 0;
    KMT_PERF p; ZeroMemory(&p, sizeof p);
    KMT_QAI q = { g_kmt, KMTQAITYPE_ADAPTERPERFDATA, &p, sizeof p };
    if (pKmtQuery(&q) != 0 || p.Temperature == 0) return 0;
    *t = p.Temperature / 10.0;
    return 1;
}

static void read_gpu_temp(void)
{
    double t; unsigned nt;
    g_ok[M_GPUT] = 0;
    if (kmt_temp(&t)) { g_val[M_GPUT] = t; g_ok[M_GPUT] = 1; }
    else if (pNvTemp && pNvTemp(g_nvDev, 0, &nt) == 0) { g_val[M_GPUT] = nt; g_ok[M_GPUT] = 1; }
}

static void init_gpu(void)
{
    HMODULE d = sysdll(L"dxgi.dll");
    if (d) {
        typedef HRESULT (WINAPI *CF)(const GUID *, void **);
        CF cf = (CF)(void *)GetProcAddress(d, "CreateDXGIFactory1");
        IDXGIFactory1 *f;
        if (cf && SUCCEEDED(cf(&IID_F1, (void **)&f))) {
            IDXGIAdapter1 *a;
            for (UINT i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
                DXGI_ADAPTER_DESC1 dd;
                if (SUCCEEDED(IDXGIAdapter1_GetDesc1(a, &dd)) && !(dd.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    && dd.VendorId != 0x1414 && (!g_hasGpu || dd.DedicatedVideoMemory > g_vramTotal)) {
                    g_vramTotal = dd.DedicatedVideoMemory;
                    g_luid = dd.AdapterLuid;
                    g_hasGpu = 1;
                    lstrcpynW(g_gpuName, dd.Description, 64);
                }
                IDXGIAdapter1_Release(a);
            }
            IDXGIFactory1_Release(f);
        }
    }
    if (!g_hasGpu) return;
    _snwprintf(g_luidStr, 40, L"luid_0x%08X_0x%08X", (unsigned)g_luid.HighPart, (unsigned)g_luid.LowPart);

    HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
    PFN_KMT pOpen = (PFN_KMT)(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    pKmtQuery     = (PFN_KMT)(void *)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
    KMT_OPEN o = { g_luid, 0 };
    if (pOpen && pOpen(&o) == 0) g_kmt = o.hAdapter;

    double t;
    if (!kmt_temp(&t)) {
        HMODULE nv = sysdll(L"nvml.dll");
        if (!nv) nv = LoadLibraryExW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll", NULL,
                                     LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (nv) {
            int (*init)(void) = (int (*)(void))(void *)GetProcAddress(nv, "nvmlInit_v2");
            int (*hnd)(unsigned, void **) = (int (*)(unsigned, void **))(void *)GetProcAddress(nv, "nvmlDeviceGetHandleByIndex_v2");
            PFN_NVTEMP tp = (PFN_NVTEMP)(void *)GetProcAddress(nv, "nvmlDeviceGetTemperature");
            if (init && hnd && tp && init() == 0 && hnd(0, &g_nvDev) == 0) pNvTemp = tp;
            else FreeLibrary(nv);
        }
    }
}

#define IOCTL_PIO_LOAD 0xA1B22084u
#define IOCTL_PIO_EXEC 0xA1B22104u
enum { PIO_UNTRIED, PIO_OK, PIO_NODRIVER, PIO_NOMODULE, PIO_NOADMIN, PIO_FAILED, PIO_UNSUPPORTED };
static int              g_pioState;
static HANDLE           g_pio = INVALID_HANDLE_VALUE, g_pciMutex;
static int              g_cpuVendor, g_tjmax;
static CRITICAL_SECTION g_pioCs;

static int pio_exec(const char *fn, ULONGLONG in, ULONGLONG *out)
{
    BYTE buf[40]; ULONGLONG o = 0; DWORD got = 0;
    ZeroMemory(buf, sizeof buf);
    lstrcpynA((char *)buf, fn, 32);
    memcpy(buf + 32, &in, 8);
    if (!DeviceIoControl(g_pio, IOCTL_PIO_EXEC, buf, sizeof buf, &o, sizeof o, &got, NULL) || got < 8) return 0;
    *out = o;
    return 1;
}

static void pio_init(void)
{
    EnterCriticalSection(&g_pioCs);
    if (g_pio != INVALID_HANDLE_VALUE) { CloseHandle(g_pio); g_pio = INVALID_HANDLE_VALUE; }
    unsigned b;
#ifdef _MSC_VER
    int r[4]; __cpuid(r, 0); b = (unsigned)r[1];
#else
    unsigned a, c, d;
    if (!__get_cpuid(0, &a, &b, &c, &d)) b = 0;
#endif
    g_cpuVendor = b == 0x756e6547 ? 1 : b == 0x68747541 ? 2 : 0;
    if (!g_cpuVendor) { g_pioState = PIO_UNSUPPORTED; goto out; }

    g_pio = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_pio == INVALID_HANDLE_VALUE) {
        g_pioState = GetLastError() == ERROR_ACCESS_DENIED ? PIO_NOADMIN : PIO_NODRIVER;
        goto out;
    }

    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(g_cpuVendor == 1 ? 101 : 102), (LPCWSTR)RT_RCDATA);
    HGLOBAL hg = r ? LoadResource(NULL, r) : NULL;
    const void *src = hg ? LockResource(hg) : NULL;
    DWORD sz = r ? SizeofResource(NULL, r) : 0, got = 0;
    int ok = src && sz;
    if (!ok) { g_pioState = PIO_NOMODULE; }
    else if (!DeviceIoControl(g_pio, IOCTL_PIO_LOAD, (void *)src, sz, NULL, 0, &got, NULL)) g_pioState = PIO_FAILED;
    else {
        ULONGLONG v;
        g_pioState = PIO_OK;
        if (g_cpuVendor == 1) {
            g_tjmax = pio_exec("ioctl_read_msr", 0x1A2, &v) ? (int)((v >> 16) & 0xFF) : 0;
            if (g_tjmax < 60 || g_tjmax > 130) g_tjmax = 100;
        } else if (!g_pciMutex) g_pciMutex = CreateMutexW(NULL, FALSE, L"Global\\Access_PCI");
    }
    if (g_pioState != PIO_OK) { CloseHandle(g_pio); g_pio = INVALID_HANDLE_VALUE; }
out:
    LeaveCriticalSection(&g_pioCs);
}

static int pio_temp(double *t)
{
    ULONGLONG v; int ok = 0;
    if (g_pioState != PIO_OK) return 0;
    EnterCriticalSection(&g_pioCs);
    if (g_pio != INVALID_HANDLE_VALUE) {
        if (g_cpuVendor == 1) {
            if (pio_exec("ioctl_read_msr", 0x1B1, &v)) { *t = g_tjmax - (double)((v >> 16) & 0x7F); ok = 1; }
        } else {
            DWORD w = g_pciMutex ? WaitForSingleObject(g_pciMutex, 20) : WAIT_OBJECT_0;
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
                if (pio_exec("ioctl_read_smn", 0x00059800, &v)) {
                    double c = ((v >> 21) & 0x7FF) / 8.0;
                    if (v & 0x80000) c -= 49.0;
                    *t = c; ok = 1;
                }
                if (g_pciMutex) ReleaseMutex(g_pciMutex);
            }
        }
    }
    LeaveCriticalSection(&g_pioCs);
    return ok && *t > 0 && *t < 125;
}

#define PAWNIO_HOST L"github.com"
#define PAWNIO_PATH L"/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe"
static const BYTE PAWNIO_SHA256[32] = {
    0x1f,0x51,0x9a,0x22,0xe4,0x71,0x87,0xf7,0x0a,0x13,0x79,0xa4,0x8c,0xa6,0x04,0x98,
    0x1c,0x4f,0xcf,0x69,0x4f,0x4e,0x65,0xb7,0x34,0xaa,0xa7,0x4a,0x9f,0xba,0x30,0x32 };

static BYTE *http_get(const wchar_t *host, const wchar_t *path, DWORD *len)
{
    BYTE *buf = NULL; DWORD cap = 0, n = 0;
    HINTERNET s = WinHttpOpen(L"TraySense", 4 , NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"TraySense", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    DWORD status = 0, ssz = sizeof status;
    if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &ssz, NULL) &&
        status == 200) {
        for (;;) {
            DWORD avail = 0, rd = 0;
            if (!WinHttpQueryDataAvailable(r, &avail) || !avail) break;
            if (n + avail > (16u << 20)) { n = 0; break; }
            if (n + avail > cap) {
                cap = (n + avail) * 2;
                BYTE *nb = buf ? HeapReAlloc(GetProcessHeap(), 0, buf, cap) : HeapAlloc(GetProcessHeap(), 0, cap);
                if (!nb) { n = 0; break; }
                buf = nb;
            }
            if (!WinHttpReadData(r, buf + n, avail, &rd) || !rd) break;
            n += rd;
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    if (!n && buf) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    *len = n;
    return buf;
}

static int sha256_is(const BYTE *data, DWORD len, const BYTE *want)
{
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h; BYTE out[32]; int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) return 0;
    if (!BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0)) {
        if (!BCryptHashData(h, (PUCHAR)data, len, 0) && !BCryptFinishHash(h, out, 32, 0)) ok = !memcmp(out, want, 32);
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static int pawnio_install(const wchar_t **msg)
{
    DWORD len = 0, w, code = 1;
    if (!load_net()) { *msg = L"Couldn't load the Windows networking components."; return 0; }
    BYTE *bin = http_get(PAWNIO_HOST, PAWNIO_PATH, &len);
    if (!bin) { *msg = L"Couldn't download the PawnIO installer from GitHub. Check your internet connection."; return 0; }
    if (!sha256_is(bin, len, PAWNIO_SHA256)) {
        HeapFree(GetProcessHeap(), 0, bin);
        *msg = L"The downloaded PawnIO installer didn't match its known checksum, so it was not run.";
        return 0;
    }
    wchar_t dir[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 32];
    if (!app_dir(dir)) {
        HeapFree(GetProcessHeap(), 0, bin);
        *msg = L"Couldn't create the TraySense folder in Program Files.";
        return 0;
    }
    _snwprintf(exe, MAX_PATH, L"%s\\PawnIO_setup.exe", dir); exe[MAX_PATH - 1] = 0;
    HANDLE f = CreateFileW(exe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    int wrote = f != INVALID_HANDLE_VALUE && WriteFile(f, bin, len, &w, NULL) && w == len;
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    HeapFree(GetProcessHeap(), 0, bin);
    if (!wrote) { DeleteFileW(exe); *msg = L"Couldn't save the PawnIO installer."; return 0; }

    _snwprintf(cmd, MAX_PATH + 32, L"\"%s\" -install -silent", exe); cmd[MAX_PATH + 31] = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 180000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    DeleteFileW(exe);
    if (code != 0 && code != 3010) { *msg = L"The PawnIO installer reported an error."; return 0; }
    return 1;
}

static HANDLE g_disk = INVALID_HANDLE_VALUE;

static void init_disk(void)
{
    DWORD num = 0, got;
    wchar_t sysdrv[8] = L"\\\\.\\C:", p[40];
    wchar_t win[MAX_PATH];
    if (GetWindowsDirectoryW(win, MAX_PATH) >= 2) sysdrv[4] = win[0];
    HANDLE v = CreateFileW(sysdrv, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (v != INVALID_HANDLE_VALUE) {
        BYTE b[256];
        if (DeviceIoControl(v, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, b, sizeof b, &got, NULL))
            num = ((VOLUME_DISK_EXTENTS *)b)->Extents[0].DiskNumber;
        CloseHandle(v);
    }
    _snwprintf(p, 40, L"\\\\.\\PhysicalDrive%u", (unsigned)num); p[39] = 0;
    g_disk = CreateFileW(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

static void read_disk(void)
{
    STORAGE_PROPERTY_QUERY q; BYTE b[512]; DWORD got = 0;
    g_ok[M_DISKT] = 0;
    if (g_disk == INVALID_HANDLE_VALUE) return;
    ZeroMemory(&q, sizeof q);
    q.PropertyId = StorageDeviceTemperatureProperty;
    q.QueryType = PropertyStandardQuery;
    if (!DeviceIoControl(g_disk, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, b, sizeof b, &got, NULL)) return;
    STORAGE_TEMPERATURE_DATA_DESCRIPTOR *d = (STORAGE_TEMPERATURE_DATA_DESCRIPTOR *)b;
    if (got < sizeof *d || !d->InfoCount) return;
    int mx = -1000;
    for (WORD i = 0; i < d->InfoCount; i++) {
        if ((BYTE *)&d->TemperatureInfo[i + 1] > b + got) break;
        if (d->TemperatureInfo[i].Temperature > mx) mx = d->TemperatureInfo[i].Temperature;
    }
    if (mx > 0 && mx < 150) { g_val[M_DISKT] = mx; g_ok[M_DISKT] = 1; }
}

static void build_query(void)
{
    if (g_q) { PdhCloseQuery(g_q); g_q = 0; }
    g_cTherm = g_cEng = g_cMem = 0;
    if (!(g_mask & ((1u << M_CPUT) | (1u << M_GPU) | (1u << M_VRAM))) || !load_pdh()) return;
    if (PdhOpenQueryW(NULL, 0, &g_q) != ERROR_SUCCESS) { g_q = 0; return; }
    if ((g_mask & (1u << M_CPUT)) && g_pioState != PIO_OK &&
        PdhAddEnglishCounterW(g_q, L"\\Thermal Zone Information(*)\\Temperature", 0, &g_cTherm)) g_cTherm = 0;
    if (g_hasGpu && (g_mask & (1u << M_GPU)) &&
        PdhAddEnglishCounterW(g_q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_cEng)) g_cEng = 0;
    if (g_hasGpu && (g_mask & (1u << M_VRAM)) &&
        PdhAddEnglishCounterW(g_q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &g_cMem)) g_cMem = 0;
    PdhCollectQueryData(g_q);
}

static void sample(void)
{
    if (g_mask & (1u << M_CPU)) read_cpu();
    if (g_mask & (1u << M_RAM)) read_ram();
    if (g_q && PdhCollectQueryData(g_q) == ERROR_SUCCESS) {
        if (g_cTherm) read_therm();
        if (g_cEng)   read_gpu_usage();
        if (g_cMem)   read_vram();
    }
    if (g_mask & (1u << M_GPUT)) read_gpu_temp();
    if (g_mask & (1u << M_CPUT)) {
        double t = 0;
        if (pio_temp(&t)) { g_val[M_CPUT] = t; g_ok[M_CPUT] = 1; }
        else if (!g_cTherm) g_ok[M_CPUT] = 0;
    }
    if (g_mask & (1u << M_DISKT)) {
        static unsigned k;
        if (k++ % 5 == 0 || !g_ok[M_DISKT]) read_disk();
    }
}

enum { SH_CPU, SH_RAM, SH_GPU };
static const int SHAPE[M_N]  = { SH_CPU, SH_CPU, SH_RAM, SH_GPU, SH_GPU, SH_RAM, SH_RAM };
static const int ISTEMP[M_N] = { 0, 1, 0, 0, 1, 0, 1 };
#define SS 4
static HDC   g_ldc;
static int   g_S;
static HFONT g_font[3][4];

static void reset_fonts(void)
{
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 4; b++)
            if (g_font[a][b]) { DeleteObject(g_font[a][b]); g_font[a][b] = 0; }
}

static void read_env(void)
{
    DWORD v = 0, sz = sizeof v;
    g_size = GetSystemMetrics(SM_CXSMICON);
    if (g_size < 16) g_size = 16;
    if (g_size > 64) g_size = 64;
    g_light = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) == ERROR_SUCCESS && v;
    reset_fonts();
}

static int P(float v) { return (int)(v * g_S / 16.0f + 0.5f); }
static void box(float x0, float y0, float x1, float y1, int on)
{
    RECT r = { P(x0), P(y0), P(x1), P(y1) };
    FillRect(g_ldc, &r, (HBRUSH)GetStockObject(on ? WHITE_BRUSH : BLACK_BRUSH));
}
static void rbox(float x0, float y0, float x1, float y1, float rad, int on)
{
    SelectObject(g_ldc, GetStockObject(on ? WHITE_BRUSH : BLACK_BRUSH));
    SelectObject(g_ldc, GetStockObject(NULL_PEN));
    RoundRect(g_ldc, P(x0), P(y0), P(x1) + 1, P(y1) + 1, P(rad * 2), P(rad * 2));
}

static void shape(int sh, int solid, float in[4])
{
    switch (sh) {
    case SH_CPU:
        for (int i = 0; i < 4; i++) {
            float a = (float)(3 + 3 * i);
            box(a, 0, a + 1, 1.25f, 1);  box(a, 14.75f, a + 1, 16, 1);
            box(0, a, 1.25f, a + 1, 1);  box(14.75f, a, 16, a + 1, 1);
        }
        rbox(1, 1, 15, 15, 1.5f, 1);
        if (!solid) rbox(2, 2, 14, 14, 0.6f, 0);
        in[0] = 1.75f; in[1] = 1.75f; in[2] = 14.25f; in[3] = 14.25f;
        break;
    case SH_RAM: {
        static const float tx[6] = { 1, 3, 5, 10, 12, 14 };
        for (int i = 0; i < 6; i++) box(tx[i], 13, tx[i] + 1, 15.5f, 1);
        rbox(0, 0.5f, 16, 13.5f, 1, 1);
        if (!solid) rbox(1, 1.5f, 15, 12.5f, 0.4f, 0);
        in[0] = 0.75f; in[1] = 1.25f; in[2] = 15.25f; in[3] = 12.75f;
        break; }
    default:
        box(0, 0.5f, 1, 16, 1);
        box(0, 0.5f, 2, 1.5f, 1);
        box(5, 13, 9, 15.5f, 1);
        box(10, 13, 14, 15.5f, 1);
        rbox(1.75f, 0.5f, 16, 13.5f, 1, 1);
        if (!solid) rbox(2.75f, 1.5f, 15, 12.5f, 0.4f, 0);
        in[0] = 2.5f; in[1] = 1.25f; in[2] = 15.25f; in[3] = 12.75f;
    }
}

static HFONT font_for(int sh, int len, int iw, int ih)
{
    if (g_font[sh][len]) return g_font[sh][len];
    static const wchar_t z[] = L"000";
    for (int h = ih * 3 / 2; h > 4; h -= 1 + h / 24) {
        HFONT f = CreateFontW(-h, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              NONANTIALIASED_QUALITY, 0, L"Bahnschrift SemiBold Condensed");
        HGDIOBJ o = SelectObject(g_ldc, f);
        SIZE s; TEXTMETRICW tm;
        GetTextExtentPoint32W(g_ldc, z, len, &s);
        GetTextMetricsW(g_ldc, &tm);
        SelectObject(g_ldc, o);
        if (s.cx <= iw && (tm.tmAscent - tm.tmInternalLeading) <= ih) return g_font[sh][len] = f;
        DeleteObject(f);
    }
    return g_font[sh][len] = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static COLORREF heat(double t)
{
    static const double   T[5] = { 35, 55, 70, 80, 90 };
    static const COLORREF C[5] = { RGB(60,170,255), RGB(70,210,110), RGB(250,205,50), RGB(255,135,40), RGB(255,45,45) };
    if (t <= T[0]) return C[0];
    for (int i = 1; i < 5; i++)
        if (t < T[i]) {
            double k = (t - T[i - 1]) / (T[i] - T[i - 1]);
            COLORREF a = C[i - 1], b = C[i];
            return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * k,
                       GetGValue(a) + (GetGValue(b) - GetGValue(a)) * k,
                       GetBValue(a) + (GetBValue(b) - GetBValue(a)) * k);
        }
    return C[4];
}

static void over(double *r, double *g, double *b, double *a, COLORREF c, double ca)
{
    double na = ca + *a * (1 - ca);
    if (na <= 0) return;
    *r = (GetRValue(c) * ca + *r * *a * (1 - ca)) / na;
    *g = (GetGValue(c) * ca + *g * *a * (1 - ca)) / na;
    *b = (GetBValue(c) * ca + *b * *a * (1 - ca)) / na;
    *a = na;
}

static HICON make_icon(int m, const wchar_t *txt)
{
    static BYTE maskBits[8 * 64];
    static BYTE cov[3][64 * 64];
    int s = g_size, S = s * SS, len = lstrlenW(txt), sh = SHAPE[m];
    int solid = ISTEMP[m] && g_ok[m];
    g_S = S;

    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = S; bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    DWORD *px;
    g_ldc = CreateCompatibleDC(NULL);
    HBITMAP big = CreateDIBSection(g_ldc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!big) { DeleteDC(g_ldc); return NULL; }
    HGDIOBJ ob = SelectObject(g_ldc, big);
    SetBkMode(g_ldc, TRANSPARENT);
    SetTextColor(g_ldc, RGB(255, 255, 255));

    float in[4] = { 0 };
    for (int layer = 0; layer < 3; layer++) {
        ZeroMemory(px, (SIZE_T)S * S * 4);
        if (layer == 0) shape(sh, solid, in);
        else if (layer == 1) {
            if (!ISTEMP[m] && g_ok[m]) {
                double p = g_val[m] < 0 ? 0 : g_val[m] > 100 ? 100 : g_val[m];
                float y = in[3] - (in[3] - in[1]) * (float)p / 100.0f;
                if (p > 0.5) box(in[0], y, in[2], in[3], 1);
            }
        } else {
            int ix = P(in[0]), iy = P(in[1]), iw = P(in[2]) - ix, ih = P(in[3]) - iy;
            int pad = S / 64;
            HGDIOBJ of = SelectObject(g_ldc, font_for(sh, len > 3 ? 3 : len, iw - 2 * pad, ih - 2 * pad));
            TEXTMETRICW tm; SIZE ts;
            GetTextMetricsW(g_ldc, &tm);
            GetTextExtentPoint32W(g_ldc, txt, len, &ts);
            int cap = tm.tmAscent - tm.tmInternalLeading;
            TextOutW(g_ldc, ix + (iw - ts.cx) / 2, iy + (ih - cap) / 2 - tm.tmInternalLeading, txt, len);
            SelectObject(g_ldc, of);
        }
        GdiFlush();
        for (int y = 0; y < s; y++)
            for (int x = 0; x < s; x++) {
                unsigned sum = 0;
                for (int j = 0; j < SS; j++) {
                    const DWORD *row = px + (y * SS + j) * S + x * SS;
                    for (int i = 0; i < SS; i++) sum += row[i] & 0xFF;
                }
                cov[layer][y * s + x] = (BYTE)(sum / (SS * SS));
            }
    }
    SelectObject(g_ldc, ob);
    DeleteObject(big);
    DeleteDC(g_ldc);

    COLORREF col = !g_ok[m] ? RGB(140, 140, 140) : ISTEMP[m] ? heat(g_val[m]) : (g_light ? COL_L : COL_D)[m];
    COLORREF fg  = g_light ? RGB(25, 25, 25) : RGB(255, 255, 255);
    bi.bmiHeader.biWidth = s; bi.bmiHeader.biHeight = -s;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!bmp) return NULL;
    for (int i = 0; i < s * s; i++) {
        double F = cov[0][i] / 255.0, B = cov[1][i] / 255.0, T = cov[2][i] / 255.0;
        double r = 0, g = 0, b = 0, a = 0;
        if (solid) over(&r, &g, &b, &a, col, F * (1 - T));
        else {
            over(&r, &g, &b, &a, col, B * 0.42);
            over(&r, &g, &b, &a, col, F);
            over(&r, &g, &b, &a, fg, T);
        }
        DWORD A = (DWORD)(a * 255 + 0.5);
        px[i] = A ? (A << 24) | ((DWORD)(r + .5) << 16) | ((DWORD)(g + .5) << 8) | (DWORD)(b + .5) : 0;
    }
    HBITMAP mask = CreateBitmap(s, s, 1, 1, maskBits);
    ICONINFO ii = { TRUE, 0, 0, mask, bmp };
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(bmp);
    return ic;
}

static void tip_for(int m, wchar_t *t)
{
    if (!g_ok[m]) { _snwprintf(t, 128, L"%s: n/a", NAMES[m]); return; }
    switch (m) {
    case M_RAM:  _snwprintf(t, 128, L"RAM: %.0f%%  (%.1f / %.1f GB)", g_val[m], g_ramUsed, g_ramTotal); break;
    case M_VRAM: _snwprintf(t, 128, L"VRAM: %.0f%%  (%.1f / %.1f GB)", g_val[m], g_vramUsed, g_vramTotal / 1073741824.0); break;
    case M_GPU:  _snwprintf(t, 128, L"GPU: %.0f%%  %s", g_val[m], g_gpuName); break;
    case M_GPUT: _snwprintf(t, 128, L"GPU temp: %.0f\x00B0" L"C", g_val[m]); break;
    case M_CPUT: _snwprintf(t, 128, L"CPU temp: %.0f\x00B0" L"C", g_val[m]); break;
    case M_DISKT: _snwprintf(t, 128, L"SSD temp: %.0f\x00B0" L"C", g_val[m]); break;
    default:     _snwprintf(t, 128, L"CPU: %.0f%%", g_val[m]);
    }
    t[127] = 0;
}

static void tray(int m, DWORD op)
{
    NOTIFYICONDATAW n; ZeroMemory(&n, sizeof n);
    n.cbSize = sizeof n; n.hWnd = g_wnd; n.uID = m + 1;
    n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    n.uCallbackMessage = WM_TRAY;
    n.hIcon = g_icon[m];
    tip_for(m, n.szTip);
    Shell_NotifyIconW(op, &n);
}

enum { MODE_BAR, MODE_TRAY };
static DWORD          g_mode = MODE_BAR;
static LONG           g_offset;
static volatile int   g_paused;
static volatile LONG  g_rebuild = 1, g_quit;
static HANDLE         g_wake;
#define WM_SAMPLED (WM_APP + 2)

static void tray_refresh(int force)
{
    for (int m = 0; m < M_N; m++) {
        int on = g_mode == MODE_TRAY && ((g_mask >> m) & 1);
        if (!on) {
            if (g_shown[m]) { tray(m, NIM_DELETE); g_shown[m] = 0; }
            continue;
        }
        wchar_t t[4];
        if (!g_ok[m]) lstrcpyW(t, L"--");
        else {
            int v = (int)(g_val[m] + 0.5);
            if (v < 0) v = 0;
            if (v > 999) v = 999;
            _snwprintf(t, 4, L"%d", v); t[3] = 0;
        }
        if (!force && g_shown[m] && !lstrcmpW(t, g_txt[m])) continue;
        lstrcpyW(g_txt[m], t);
        HICON old = g_icon[m];
        g_icon[m] = make_icon(m, t);
        if (!g_shown[m]) { tray(m, NIM_ADD); g_shown[m] = 1; }
        else tray(m, NIM_MODIFY);
        if (old) DestroyIcon(old);
    }
}

#define MAX_BARS 8
typedef struct {
    HWND wnd, tb;
    int child, primary, W, H, compact, wideW, fontDpi, icoOK[4];
    RECT lastPos;
    wchar_t sig[160];
    HFONT font, small, ico;
} BAR;
enum { SHOW_MAIN, SHOW_ALL, SHOW_OTHERS };
static BAR   g_bars[MAX_BARS];
static BAR  *bar = g_bars;
static int   g_dpi = 96;
static DWORD g_where = SHOW_MAIN;

typedef struct { int glyph, sh, x, y, w, al, f, row; wchar_t t[8]; COLORREF c; } SEG;
enum { G_CPU, G_RAM, G_GPU, G_DISK };

static int SC(int v) { return MulDiv(v, g_dpi, 96); }

static void get_dpi(void)
{
    typedef UINT (WINAPI *GDW)(HWND);
    static GDW f; static int tried;
    if (!tried) { tried = 1; f = (GDW)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"); }
    UINT d = (f && bar->tb) ? f(bar->tb) : 0;
    if (!d) { HDC dc = GetDC(NULL); d = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); }
    g_dpi = d < 96 ? 96 : (int)d;
}

static void glyph_cov(int sh, int gs, BYTE *out)
{
    int S = gs * SS;
    g_S = S;
    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = S; bi.bmiHeader.biHeight = -S;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    DWORD *px;
    g_ldc = CreateCompatibleDC(NULL);
    HBITMAP big = CreateDIBSection(g_ldc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    ZeroMemory(out, (SIZE_T)gs * gs);
    if (big) {
        HGDIOBJ ob = SelectObject(g_ldc, big);
        ZeroMemory(px, (SIZE_T)S * S * 4);
        float in[4];
        shape(sh, 0, in);
        GdiFlush();
        for (int y = 0; y < gs; y++)
            for (int x = 0; x < gs; x++) {
                unsigned sum = 0;
                for (int j = 0; j < SS; j++) {
                    const DWORD *row = px + (y * SS + j) * S + x * SS;
                    for (int i = 0; i < SS; i++) sum += row[i] & 0xFF;
                }
                out[y * gs + x] = (BYTE)(sum / (SS * SS));
            }
        SelectObject(g_ldc, ob);
        DeleteObject(big);
    }
    DeleteDC(g_ldc);
}

static LRESULT CALLBACK barproc(HWND w, UINT msg, WPARAM wp, LPARAM lp);

static void bar_create(HWND tb, int primary)
{
    if (bar->font) DeleteObject(bar->font);
    if (bar->small) DeleteObject(bar->small);
    if (bar->ico) DeleteObject(bar->ico);
    ZeroMemory(bar, sizeof *bar);
    bar->tb = tb;
    bar->primary = primary;
    bar->wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"TraySenseBar", L"",
                               WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (bar->wnd) {
        SetWindowLongPtrW(bar->wnd, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
        if (SetParent(bar->wnd, tb)) bar->child = 1;
        else SetWindowLongPtrW(bar->wnd, GWL_STYLE, WS_POPUP);
    }
}

static void bar_to_popup(void)
{
    SetParent(bar->wnd, NULL);
    SetWindowLongPtrW(bar->wnd, GWL_STYLE, WS_POPUP);
    SetWindowLongPtrW(bar->wnd, GWL_EXSTYLE, GetWindowLongPtrW(bar->wnd, GWL_EXSTYLE) | WS_EX_TOPMOST);
    bar->child = 0;
    ZeroMemory(&bar->lastPos, sizeof bar->lastPos);
}

static void bar_destroy(BAR *b)
{
    HWND w = b->wnd;
    b->wnd = NULL;
    if (w && IsWindow(w)) DestroyWindow(w);
    if (b->font) DeleteObject(b->font);
    if (b->small) DeleteObject(b->small);
    if (b->ico) DeleteObject(b->ico);
    ZeroMemory(b, sizeof *b);
}

static void bars_destroy(void)
{
    for (int i = 0; i < MAX_BARS; i++) bar_destroy(&g_bars[i]);
}

static int find_taskbars(HWND *out, int max)
{
    int n = 0;
    HWND p = FindWindowW(L"Shell_TrayWnd", NULL), s = NULL;
    if (p) out[n++] = p;
    while (n < max && (s = FindWindowExW(NULL, s, L"Shell_SecondaryTrayWnd", NULL))) out[n++] = s;
    return n;
}

typedef struct { HWND tb; LONG ok, trayL, appsR; } SCAN;
static SCAN g_scan[MAX_BARS];
static CRITICAL_SECTION g_scanCs;
static IUIAutomation *g_uia;
static IUIAutomationCacheRequest *g_uiaCache;
static IUIAutomationCondition *g_uiaAll;

static const GUID CLSID_UIA = {0xff48dba4,0x60ef,0x4201,{0xaa,0x87,0x54,0x10,0x3e,0xef,0x59,0x4e}};
static const GUID IID_UIA   = {0x30cbe57d,0xd9d0,0x452a,{0xab,0x13,0x7a,0xc5,0xac,0x48,0x25,0xee}};
static const GUID IID_CF    = {0x00000001,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static int uia_init(void)
{
    static int tried;
    if (tried) return g_uia != NULL;
    tried = 1;
    HMODULE m = sysdll(L"UIAutomationCore.dll");
    typedef HRESULT (WINAPI *DGCO)(const GUID *, const GUID *, void **);
    DGCO get = m ? (DGCO)(void *)GetProcAddress(m, "DllGetClassObject") : NULL;
    IClassFactory *cf = NULL;
    if (!get || FAILED(get(&CLSID_UIA, &IID_CF, (void **)&cf)) || !cf) return 0;
    HRESULT hr = IClassFactory_CreateInstance(cf, NULL, &IID_UIA, (void **)&g_uia);
    IClassFactory_Release(cf);
    if (FAILED(hr) || !g_uia) { g_uia = NULL; return 0; }
    if (FAILED(IUIAutomation_CreateCacheRequest(g_uia, &g_uiaCache)) ||
        FAILED(IUIAutomation_CreateTrueCondition(g_uia, &g_uiaAll))) {
        IUIAutomation_Release(g_uia); g_uia = NULL; return 0;
    }
    IUIAutomationCacheRequest_AddProperty(g_uiaCache, UIA_ClassNamePropertyId);
    IUIAutomationCacheRequest_AddProperty(g_uiaCache, UIA_BoundingRectanglePropertyId);
    IUIAutomationCacheRequest_AddProperty(g_uiaCache, UIA_IsOffscreenPropertyId);
    IUIAutomationCacheRequest_put_AutomationElementMode(g_uiaCache, AutomationElementMode_None);
    return 1;
}

static int starts(const wchar_t *s, const wchar_t *p)
{
    return s && !wcsncmp(s, p, wcslen(p));
}

static int scan_one(HWND tb, int primary, LONG *outTray, LONG *outApps)
{
    static RECT rc[256];
    static BYTE app[256];
    RECT tr;
    if (!GetWindowRect(tb, &tr) ||
        !FindWindowExW(tb, NULL, L"Windows.UI.Composition.DesktopWindowContentBridge", NULL)) return 0;
    IUIAutomationElement *root = NULL;
    IUIAutomationElementArray *arr = NULL;
    int n = 0, cnt = 0;
    if (SUCCEEDED(IUIAutomation_ElementFromHandle(g_uia, (UIA_HWND)tb, &root)) && root &&
        SUCCEEDED(IUIAutomationElement_FindAllBuildCache(root, TreeScope_Descendants, g_uiaAll, g_uiaCache, &arr)) && arr)
        IUIAutomationElementArray_get_Length(arr, &cnt);
    int tbw = tr.right - tr.left;
    for (int i = 0; i < cnt && n < 256; i++) {
        IUIAutomationElement *e = NULL;
        if (FAILED(IUIAutomationElementArray_GetElement(arr, i, &e)) || !e) continue;
        BSTR cls = NULL; RECT r; BOOL off = FALSE;
        IUIAutomationElement_get_CachedClassName(e, &cls);
        IUIAutomationElement_get_CachedBoundingRectangle(e, &r);
        IUIAutomationElement_get_CachedIsOffscreen(e, &off);
        int w = r.right - r.left;
        if (!off && w > 0 && r.bottom > r.top && w < tbw / 3 && r.top < tr.bottom && r.bottom > tr.top &&
            !(cls && !lstrcmpW(cls, L"TraySenseBar"))) {
            rc[n] = r;
            app[n] = starts(cls, L"Taskbar.") || starts(cls, L"StartButton") || starts(cls, L"SearchBox");
            n++;
        }
        if (cls) SysFreeString(cls);
        IUIAutomationElement_Release(e);
    }
    if (arr) IUIAutomationElementArray_Release(arr);
    if (root) IUIAutomationElement_Release(root);

    int appsR = tr.left, trayL = tr.right, apps = 0;
    for (int i = 0; i < n; i++)
        if (app[i] && rc[i].right > appsR) { appsR = rc[i].right; apps = 1; }
    for (int i = 0; i < n; i++)
        if (!app[i] && rc[i].left >= appsR && rc[i].left < trayL) trayL = rc[i].left;
    HWND tn = FindWindowExW(tb, NULL, L"TrayNotifyWnd", NULL);
    RECT nr;
    if (tn && GetWindowRect(tn, &nr) && nr.right > nr.left && nr.left < trayL) trayL = nr.left;
    if ((primary && !apps) || trayL <= appsR) return 0;
    *outTray = trayL;
    *outApps = appsR;
    return 1;
}

static void uia_scan(void)
{
    HWND tbs[MAX_BARS];
    SCAN res[MAX_BARS];
    HWND main = FindWindowW(L"Shell_TrayWnd", NULL);
    int n = find_taskbars(tbs, MAX_BARS), ok = uia_init();
    for (int i = 0; i < n; i++) {
        res[i].tb = tbs[i];
        res[i].trayL = res[i].appsR = 0;
        res[i].ok = ok && scan_one(tbs[i], tbs[i] == main, &res[i].trayL, &res[i].appsR);
    }
    EnterCriticalSection(&g_scanCs);
    ZeroMemory(g_scan, sizeof g_scan);
    memcpy(g_scan, res, sizeof res[0] * n);
    LeaveCriticalSection(&g_scanCs);
}

static int scan_for(HWND tb, int *trayL, int *appsR)
{
    int ok = 0;
    EnterCriticalSection(&g_scanCs);
    for (int i = 0; i < MAX_BARS; i++)
        if (g_scan[i].tb == tb && g_scan[i].ok) { *trayL = g_scan[i].trayL; *appsR = g_scan[i].appsR; ok = 1; break; }
    LeaveCriticalSection(&g_scanCs);
    return ok;
}

static void bar_place(void)
{
    RECT tr, nr;
    if (!bar->wnd) return;
    if (!bar->tb || !GetWindowRect(bar->tb, &tr) || tr.bottom - tr.top > tr.right - tr.left) {
        ShowWindow(bar->wnd, SW_HIDE);
        return;
    }
    get_dpi();
    int trayL, appsR, scan = scan_for(bar->tb, &trayL, &appsR);
    if (!scan) {
        HWND tn = FindWindowExW(bar->tb, NULL, bar->primary ? L"TrayNotifyWnd" : L"ClockButton", NULL);
        if (tn && GetWindowRect(tn, &nr) && nr.right > nr.left) trayL = nr.left;
        else trayL = tr.right - (bar->primary ? SC(280) : SC(12));
        appsR = tr.left;
    }
    int h = tr.bottom - tr.top;
    int right = trayL - SC(10) + SC(g_offset);
    int avail = right - (scan ? appsR + SC(6) : tr.left);
    int compact = bar->compact ? bar->wideW > avail - SC(10) : bar->wideW > avail;
    if (compact != bar->compact) {
        bar->compact = compact;
        bar->sig[0] = 0;
        PostMessageW(g_wnd, WM_SAMPLED, 0, 0);
    }
    int x = right - bar->W, y = tr.top;
    if (bar->child) { POINT p = { x, y }; ScreenToClient(bar->tb, &p); x = p.x; y = p.y; }
    RECT want = { x, y, x + bar->W, y + h };
    int top = bar->child ? GetWindow(bar->tb, GW_CHILD) == bar->wnd : 1;
    if (memcmp(&want, &bar->lastPos, sizeof want) || !top || !IsWindowVisible(bar->wnd)) {
        SetWindowPos(bar->wnd, bar->child ? HWND_TOP : HWND_TOPMOST, x, y, bar->W, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        bar->lastPos = want;
    } else if (!bar->child)
        SetWindowPos(bar->wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void seg_pct(SEG *s, int m, COLORREF fg)
{
    s->glyph = 0; s->c = fg; s->al = 255;
    if (!g_ok[m]) lstrcpyW(s->t, L"--");
    else _snwprintf(s->t, 8, L"%d%%", (int)(g_val[m] + 0.5));
    s->t[7] = 0;
}
static void seg_temp(SEG *s, int m, COLORREF fg)
{
    double hot = m == M_DISKT ? 65 : 80, crit = m == M_DISKT ? 75 : 90;
    s->glyph = 0; s->c = fg; s->al = 255;
    if (!g_ok[m]) { lstrcpyW(s->t, L"--\x00B0"); return; }
    double t = g_val[m];
    _snwprintf(s->t, 8, L"%d\x00B0", (int)(t + 0.5)); s->t[7] = 0;
    if (t >= crit) s->c = g_light ? RGB(200, 25, 25) : RGB(255, 75, 65);
    else if (t >= hot) s->c = g_light ? RGB(195, 95, 0) : RGB(255, 170, 55);
}
static void seg_label(SEG *s, const wchar_t *t, COLORREF fg)
{
    s->glyph = 0; s->c = fg; s->al = 150;
    lstrcpynW(s->t, t, 8);
}

static void draw_gpu(HDC dc, int x0, int y0, int sz)
{
#define GX(v) (x0 + (int)((v) * sz / 16.0f + 0.5f))
#define GY(v) (y0 + (int)((v) * sz / 16.0f + 0.5f))
    int pw = sz / 16; if (pw < 1) pw = 1;
    HPEN pen = CreatePen(PS_SOLID, pw, RGB(255, 255, 255));
    HGDIOBJ op = SelectObject(dc, pen), obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, GX(2.5f), GY(3.5f), GX(14.5f), GY(12.5f), GX(3) - GX(0), GY(3) - GY(0));
    Ellipse(dc, GX(6), GY(5.5f), GX(11), GY(10.5f));
    SelectObject(dc, GetStockObject(NULL_PEN));
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Ellipse(dc, GX(7.7f), GY(7.2f), GX(9.3f) + 1, GY(8.8f) + 1);
    RECT r1 = { GX(0), GY(2), GX(1), GY(14.5f) };
    RECT r2 = { GX(0), GY(2), GX(2.5f), GY(3) };
    RECT r3 = { GX(5), GY(13), GX(11), GY(14.5f) };
    FillRect(dc, &r1, (HBRUSH)GetStockObject(WHITE_BRUSH));
    FillRect(dc, &r2, (HBRUSH)GetStockObject(WHITE_BRUSH));
    FillRect(dc, &r3, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SelectObject(dc, op); SelectObject(dc, obr);
    DeleteObject(pen);
#undef GX
#undef GY
}

static const wchar_t ICO_CH[4] = { 0xEEA1, 0xEEA0, 0, 0xEDA2 };
static const MAT2 MAT_ID = { {0,1}, {0,0}, {0,0}, {0,1} };

static HFONT mkfont(HDC dc, const wchar_t *face, int px, int weight)
{
    HFONT f = CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
    wchar_t got[LF_FACESIZE] = {0};
    HGDIOBJ o = SelectObject(dc, f);
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, o);
    if (lstrcmpiW(got, face)) { DeleteObject(f); return NULL; }
    return f;
}

static void bar_fonts(HDC dc)
{
    if (bar->font && bar->fontDpi == g_dpi) return;
    if (bar->font) DeleteObject(bar->font);
    if (bar->small) DeleteObject(bar->small);
    if (bar->ico) DeleteObject(bar->ico);
    int tp = SC(14) * SS, sp = SC(12) * SS, ip = SC(16) * SS;
    bar->font = mkfont(dc, L"Segoe UI Variable Text", tp, FW_NORMAL);
    if (!bar->font) bar->font = mkfont(dc, L"Segoe UI", tp, FW_NORMAL);
    if (!bar->font) bar->font = CreateFontW(-tp, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Tahoma");
    bar->small = mkfont(dc, L"Segoe UI Variable Text", sp, FW_NORMAL);
    if (!bar->small) bar->small = mkfont(dc, L"Segoe UI", sp, FW_NORMAL);
    if (!bar->small) bar->small = CreateFontW(-sp, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Tahoma");
    bar->ico = mkfont(dc, L"Segoe Fluent Icons", ip, FW_NORMAL);
    if (!bar->ico) bar->ico = mkfont(dc, L"Segoe MDL2 Assets", ip, FW_NORMAL);
    for (int k = 0; k < 4; k++) {
        WORD gi = 0xFFFF;
        bar->icoOK[k] = 0;
        if (!bar->ico || !ICO_CH[k]) continue;
        HGDIOBJ o = SelectObject(dc, bar->ico);
        if (GetGlyphIndicesW(dc, &ICO_CH[k], 1, &gi, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && gi != 0xFFFF) bar->icoOK[k] = 1;
        SelectObject(dc, o);
    }
    bar->fontDpi = g_dpi;
}

static void ink(HDC dc, wchar_t ch, int *top, int *h, int *left, int *w)
{
    GLYPHMETRICS gm; TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    if (GetGlyphOutlineW(dc, ch, GGO_METRICS, &gm, 0, NULL, &MAT_ID) == GDI_ERROR) {
        *top = tm.tmInternalLeading; *h = tm.tmAscent - tm.tmInternalLeading; *left = 0; *w = tm.tmAveCharWidth; return;
    }
    *top = tm.tmAscent - gm.gmptGlyphOrigin.y; *h = gm.gmBlackBoxY;
    *left = gm.gmptGlyphOrigin.x; *w = gm.gmBlackBoxX;
}

static int text_w(HDC dc, const wchar_t *t)
{
    SIZE s;
    GetTextExtentPoint32W(dc, t, lstrlenW(t), &s);
    return s.cx;
}

static int layout(HDC dc, SEG *sg, int n, int compact, int HB)
{
    int gs = SC(16) * SS, top, h, d1, d2, X = SC(4) * SS;
    if (!compact) {
        SelectObject(dc, bar->font);
        ink(dc, L'0', &top, &h, &d1, &d2);
        for (int i = 0; i < n; i++) {
            if (sg[i].glyph) { if (i) X += SC(14) * SS; sg[i].w = gs; }
            else {
                X += (sg[i - 1].glyph ? SC(6) : sg[i].al < 255 ? SC(9) : sg[i - 1].al < 255 ? SC(4) : SC(5)) * SS;
                sg[i].w = text_w(dc, sg[i].t);
                sg[i].y = HB / 2 - (top + h / 2);
                sg[i].f = 0; sg[i].row = -1;
            }
            sg[i].x = X; X += sg[i].w;
        }
        return X + SC(4) * SS;
    }
    SelectObject(dc, bar->small);
    ink(dc, L'0', &top, &h, &d1, &d2);
    int gap = SC(3) * SS, yTop = HB / 2 - gap / 2 - h - top, yBot = HB / 2 + gap / 2 - top, yMid = HB / 2 - h / 2 - top;
    for (int i = 0; i < n;) {
        if (sg[i].glyph) {
            if (i) X += SC(10) * SS;
            sg[i].x = X; sg[i].w = gs; X += gs;
            i++;
            continue;
        }
        int a = i, b = (i + 1 < n && !sg[i + 1].glyph && sg[i + 1].al == 255) ? i + 1 : -1;
        X += (sg[a - 1].glyph ? SC(5) : SC(7)) * SS;
        int wa = text_w(dc, sg[a].t), wb = b >= 0 ? text_w(dc, sg[b].t) : 0;
        sg[a].x = X; sg[a].w = wa; sg[a].f = 1;
        if (b >= 0) {
            sg[a].y = yTop; sg[a].row = 0;
            sg[b].x = X; sg[b].w = wb; sg[b].f = 1; sg[b].y = yBot; sg[b].row = 1;
        } else { sg[a].y = yMid; sg[a].row = -1; }
        X += wa > wb ? wa : wb;
        i = b >= 0 ? b + 1 : a + 1;
    }
    return X + SC(4) * SS;
}

static void bar_draw(int force)
{
    static COLORREF colOf[2][1024];
    static BYTE alOf[2][1024];
    static BYTE gc[64 * 64], lut[256];
    if (!lut[255]) for (int i = 0; i < 256; i++) lut[i] = (BYTE)(255.0 * pow(i / 255.0, 0.8) + 0.5);
    if (!bar->wnd) return;
    RECT tr; int H = 40;
    if (bar->tb && GetWindowRect(bar->tb, &tr)) H = tr.bottom - tr.top;
    if (H < 16) H = 16;
    if (H > 400) H = 400;
    get_dpi();
    COLORREF fg = g_light ? RGB(0, 0, 0) : RGB(255, 255, 255);

    SEG sg[14]; int n = 0;
    if ((g_mask & (1u << M_CPU)) || ((g_mask & (1u << M_CPUT)) && g_ok[M_CPUT])) {
        sg[n].glyph = 1; sg[n].sh = G_CPU; n++;
        if (g_mask & (1u << M_CPU)) seg_pct(&sg[n++], M_CPU, fg);
        if ((g_mask & (1u << M_CPUT)) && g_ok[M_CPUT]) seg_temp(&sg[n++], M_CPUT, fg);
    }
    if (g_mask & (1u << M_RAM)) {
        sg[n].glyph = 1; sg[n].sh = G_RAM; n++;
        seg_pct(&sg[n++], M_RAM, fg);
    }
    int gpuT = (g_mask & (1u << M_GPUT)) && g_ok[M_GPUT];
    if ((g_mask & ((1u << M_GPU) | (1u << M_VRAM))) || gpuT) {
        sg[n].glyph = 1; sg[n].sh = G_GPU; n++;
        if (g_mask & (1u << M_GPU)) seg_pct(&sg[n++], M_GPU, fg);
        if (gpuT) seg_temp(&sg[n++], M_GPUT, fg);
        if (g_mask & (1u << M_VRAM)) {
            seg_label(&sg[n++], L"VRAM", fg);
            seg_pct(&sg[n++], M_VRAM, fg);
        }
    }
    if ((g_mask & (1u << M_DISKT)) && g_ok[M_DISKT]) {
        sg[n].glyph = 1; sg[n].sh = G_DISK; n++;
        seg_temp(&sg[n++], M_DISKT, fg);
    }

    wchar_t sig[160]; int p = _snwprintf(sig, 160, L"%d|%d|%d|%d|%d|", H, g_dpi, g_light, n, bar->compact);
    for (int i = 0; i < n && p > 0 && p < 140; i++)
        if (!sg[i].glyph) p += _snwprintf(sig + p, 160 - p, L"%s%06X|", sg[i].t, (unsigned)sg[i].c);
    sig[159] = 0;
    if (!force && !lstrcmpW(sig, bar->sig)) { bar_place(); return; }
    lstrcpyW(bar->sig, sig);

    HDC mdc = CreateCompatibleDC(NULL);
    bar_fonts(mdc);
    int HB = H * SS;

    HGDIOBJ of = SelectObject(mdc, bar->font);
    bar->wideW = (layout(mdc, sg, n, 0, HB) + SS - 1) / SS;
    int W = bar->compact ? (layout(mdc, sg, n, 1, HB) + SS - 1) / SS : bar->wideW;
    if (W > 1024) W = 1024;
    int WB = W * SS;

    BITMAPINFO bi; ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = WB; bi.bmiHeader.biHeight = -HB;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    DWORD *bp, *px;
    HBITMAP big = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&bp, NULL, 0);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    HBITMAP bmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!big || !bmp) {
        if (big) DeleteObject(big);
        if (bmp) DeleteObject(bmp);
        SelectObject(mdc, of); DeleteDC(mdc); return;
    }
    HGDIOBJ ob = SelectObject(mdc, big);
    ZeroMemory(bp, (SIZE_T)WB * HB * 4);
    SetBkMode(mdc, TRANSPARENT);
    SetTextColor(mdc, RGB(255, 255, 255));
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < W; i++) { colOf[r][i] = fg; alOf[r][i] = 255; }

    for (int i = 0; i < n; i++) {
        if (sg[i].glyph) continue;
        SelectObject(mdc, sg[i].f ? bar->small : bar->font);
        TextOutW(mdc, sg[i].x, sg[i].y, sg[i].t, lstrlenW(sg[i].t));
        for (int r = 0; r < 2; r++) {
            if (sg[i].row >= 0 && sg[i].row != r) continue;
            for (int k = sg[i].x / SS - 1; k <= (sg[i].x + sg[i].w) / SS + 1 && k < W; k++)
                if (k >= 0) { colOf[r][k] = sg[i].c; alOf[r][k] = (BYTE)sg[i].al; }
        }
    }
    for (int i = 0; i < n; i++)
        if (sg[i].glyph && sg[i].sh == G_GPU) draw_gpu(mdc, sg[i].x, HB / 2 - sg[i].w / 2, sg[i].w);
    SelectObject(mdc, bar->ico ? bar->ico : bar->font);
    for (int i = 0; i < n; i++) {
        if (!sg[i].glyph || sg[i].sh == G_GPU || !bar->icoOK[sg[i].sh]) continue;
        int top, h, left, w;
        ink(mdc, ICO_CH[sg[i].sh], &top, &h, &left, &w);
        TextOutW(mdc, sg[i].x + (sg[i].w - w) / 2 - left, HB / 2 - (top + h / 2), &ICO_CH[sg[i].sh], 1);
    }
    GdiFlush();

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned sum = 0;
            for (int j = 0; j < SS; j++) {
                const DWORD *row = bp + (y * SS + j) * WB + x * SS;
                for (int i = 0; i < SS; i++) sum += row[i] & 0xFF;
            }
            int r = y >= H / 2;
            DWORD a = lut[sum / (SS * SS)] * alOf[r][x] / 255;
            COLORREF c = colOf[r][x];
            px[y * W + x] = a ? (a << 24) | ((GetRValue(c) * a / 255) << 16) | ((GetGValue(c) * a / 255) << 8) | (GetBValue(c) * a / 255)
                              : 0x01000000;
        }
    SelectObject(mdc, bmp);
    DeleteObject(big);

    int gs = SC(16), gy = (H - gs) / 2;
    for (int i = 0; i < n; i++) {
        if (!sg[i].glyph || sg[i].sh == G_GPU || bar->icoOK[sg[i].sh]) continue;
        glyph_cov(sg[i].sh == G_CPU ? SH_CPU : SH_RAM, gs > 64 ? 64 : gs, gc);
        int X0 = sg[i].x / SS;
        for (int y = 0; y < gs && y < 64; y++)
            for (int xx = 0; xx < gs && xx < 64; xx++) {
                DWORD a = gc[y * gs + xx];
                int Xp = X0 + xx, Y = gy + y;
                if (!a || Xp >= W || Y < 0 || Y >= H) continue;
                px[Y * W + Xp] = (a << 24) | ((GetRValue(fg) * a / 255) << 16) | ((GetGValue(fg) * a / 255) << 8) | (GetBValue(fg) * a / 255);
            }
    }

    POINT src = { 0, 0 }; SIZE sz = { W, H };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    bar->W = W; bar->H = H;
    if (!UpdateLayeredWindow(bar->wnd, NULL, NULL, &sz, mdc, &src, 0, &bf, ULW_ALPHA) && bar->child) {
        bar_to_popup();
        UpdateLayeredWindow(bar->wnd, NULL, NULL, &sz, mdc, &src, 0, &bf, ULW_ALPHA);
    }
    SelectObject(mdc, ob);
    SelectObject(mdc, of);
    DeleteObject(bmp);
    DeleteDC(mdc);
    bar_place();
}

static int wanted(HWND tb, HWND main, int others)
{
    if (g_where == SHOW_ALL) return 1;
    if (g_where == SHOW_OTHERS && others) return tb != main;
    return tb == main;
}

static void refresh(int force)
{
    HWND tbs[MAX_BARS], main = FindWindowW(L"Shell_TrayWnd", NULL);
    tray_refresh(force);
    if (g_mode != MODE_BAR) { bars_destroy(); return; }
    int n = find_taskbars(tbs, MAX_BARS), others = n > (main != NULL);
    for (int i = 0; i < MAX_BARS; i++) {
        BAR *b = &g_bars[i];
        if (!b->wnd) continue;
        int keep = IsWindow(b->wnd) && IsWindow(b->tb) && wanted(b->tb, main, others);
        for (int k = 0; keep && k < n; k++) if (tbs[k] == b->tb) keep = 2;
        if (keep != 2) bar_destroy(b);
    }
    for (int k = 0; k < n; k++) {
        if (!wanted(tbs[k], main, others)) continue;
        int have = 0, free = -1;
        for (int i = 0; i < MAX_BARS; i++) {
            if (g_bars[i].wnd && g_bars[i].tb == tbs[k]) have = 1;
            else if (!g_bars[i].wnd && free < 0) free = i;
        }
        if (have || free < 0) continue;
        bar = &g_bars[free];
        bar_create(tbs[k], tbs[k] == main);
    }
    for (int i = 0; i < MAX_BARS; i++)
        if (g_bars[i].wnd) { bar = &g_bars[i]; bar_draw(force); }
}

static DWORD WINAPI sampler(void *unused)
{
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    int com = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    unsigned tick = 0;
    read_cpu();
    for (;;) {
        if (g_quit) break;
        if (InterlockedExchange(&g_rebuild, 0)) build_query();
        if (!g_paused) {
            sample();
            if (com && g_mode == MODE_BAR && tick++ % (g_interval < 2000 ? 2 : 1) == 0) uia_scan();
            PostMessageW(g_wnd, WM_SAMPLED, 0, 0);
        }
        WaitForSingleObject(g_wake, g_paused ? INFINITE : g_interval);
    }
    if (g_q) PdhCloseQuery(g_q);
    if (g_uiaAll) IUIAutomationCondition_Release(g_uiaAll);
    if (g_uiaCache) IUIAutomationCacheRequest_Release(g_uiaCache);
    if (g_uia) IUIAutomation_Release(g_uia);
    if (com) CoUninitialize();
    return 0;
}

static void save(void)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Mask2", REG_DWORD, &g_mask, sizeof g_mask);
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Interval", REG_DWORD, &g_interval, sizeof g_interval);
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Mode", REG_DWORD, &g_mode, sizeof g_mode);
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Offset", REG_DWORD, &g_offset, sizeof g_offset);
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"ShowOn", REG_DWORD, &g_where, sizeof g_where);
}
static void load(void)
{
    DWORD v, sz = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"Mask2", RRF_RT_REG_DWORD, NULL, &v, &sz) && (v & ((1u << M_N) - 1)))
        g_mask = v & ((1u << M_N) - 1);
    sz = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"Interval", RRF_RT_REG_DWORD, NULL, &v, &sz) && v >= 500 && v <= 60000)
        g_interval = v;
    sz = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"Mode", RRF_RT_REG_DWORD, NULL, &v, &sz) && v <= MODE_TRAY)
        g_mode = v;
    sz = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"Offset", RRF_RT_REG_DWORD, NULL, &v, &sz))
        g_offset = (LONG)v;
    sz = sizeof v;
    if (!RegGetValueW(HKEY_CURRENT_USER, REGKEY, L"ShowOn", RRF_RT_REG_DWORD, NULL, &v, &sz) && v <= SHOW_OTHERS)
        g_where = v;
}
static int is_admin(void)
{
    BOOL a = FALSE; PSID g;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &g)) {
        CheckTokenMembership(NULL, g, &a);
        FreeSid(g);
    }
    return a;
}
static DWORD reg_dword(const wchar_t *name)
{
    DWORD v = 0, sz = sizeof v;
    RegGetValueW(HKEY_CURRENT_USER, REGKEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz);
    return v;
}
static void schtasks(const wchar_t *args)
{
    wchar_t app[MAX_PATH], cmd[MAX_PATH * 2 + 64];
    if (!sys_path(app, L"schtasks.exe")) return;
    _snwprintf(cmd, MAX_PATH * 2 + 64, L"\"%s\" %s", app, args); cmd[MAX_PATH * 2 + 63] = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    if (CreateProcessW(app, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}
static void task_create(const wchar_t *exe);
static void task_remove_copy(void);
static void relaunch_admin(const wchar_t *args);
static int autostart_on(void)
{
    return reg_dword(L"Task") ||
           RegGetValueW(HKEY_CURRENT_USER, RUNKEY, APP, RRF_RT_REG_SZ, NULL, NULL, NULL) == ERROR_SUCCESS;
}
static void autostart_toggle(void)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH * 2 + 128];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH), zero = 0;
    if (!n || n >= MAX_PATH) return;
    if (autostart_on()) {
        if (reg_dword(L"Task") && !is_admin()) {
            if (MessageBoxW(g_wnd, L"TraySense starts as administrator at login. Turning that off needs administrator "
                                   L"rights.\n\nRestart TraySense as administrator?", APP, MB_YESNO | MB_ICONQUESTION) == IDYES)
                relaunch_admin(L"/restart");
            return;
        }
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUNKEY, APP);
        if (reg_dword(L"Task")) {
            schtasks(L"/Delete /F /TN TraySense");
            task_remove_copy();
            RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Task", REG_DWORD, &zero, sizeof zero);
        }
        return;
    }
    if (!is_admin()) {
        _snwprintf(cmd, MAX_PATH + 4, L"\"%s\"", exe); cmd[MAX_PATH + 3] = 0;
        RegSetKeyValueW(HKEY_CURRENT_USER, RUNKEY, APP, REG_SZ, cmd, (lstrlenW(cmd) + 1) * sizeof(wchar_t));
        return;
    }
    task_create(exe);
}

static void xml_escape(wchar_t *out, int n, const wchar_t *in)
{
    int o = 0;
    for (; *in && o < n - 7; in++) {
        const wchar_t *r = *in == L'&' ? L"&amp;" : *in == L'<' ? L"&lt;" : *in == L'>' ? L"&gt;" : NULL;
        if (r) { lstrcpyW(out + o, r); o += lstrlenW(r); }
        else out[o++] = *in;
    }
    out[o] = 0;
}

static int user_sid(wchar_t *out, int n)
{
    HANDLE t; BYTE b[256]; DWORD got; LPWSTR str; int ok = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return 0;
    if (GetTokenInformation(t, TokenUser, b, sizeof b, &got) && ConvertSidToStringSidW(((TOKEN_USER *)b)->User.Sid, &str)) {
        lstrcpynW(out, str, n);
        LocalFree(str);
        ok = 1;
    }
    CloseHandle(t);
    return ok;
}

static void task_remove_copy(void)
{
    wchar_t dir[MAX_PATH], dst[MAX_PATH], self[MAX_PATH];
    if (!is_admin() || !app_dir(dir)) return;
    _snwprintf(dst, MAX_PATH, L"%s\\TraySense.exe", dir); dst[MAX_PATH - 1] = 0;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    if (!lstrcmpiW(self, dst) || !DeleteFileW(dst)) {
        MoveFileExW(dst, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        MoveFileExW(dir, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    } else RemoveDirectoryW(dir);
}

static void task_create(const wchar_t *exe)
{
    wchar_t dir[MAX_PATH], dst[MAX_PATH], xmlPath[MAX_PATH], sid[184], esc[MAX_PATH * 5], xml[3072], args[MAX_PATH + 48];
    DWORD one = 1, w;
    if (!app_dir(dir) || !user_sid(sid, 184)) return;
    _snwprintf(dst, MAX_PATH, L"%s\\TraySense.exe", dir); dst[MAX_PATH - 1] = 0;
    if (lstrcmpiW(exe, dst) && !copy_self(dst)) return;
    xml_escape(esc, MAX_PATH * 5, dst);
    _snwprintf(xmlPath, MAX_PATH, L"%s\\task.xml", dir); xmlPath[MAX_PATH - 1] = 0;
    _snwprintf(xml, 3072,
        L"\xFEFF<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>%s</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"Author\"><UserId>%s</UserId><LogonType>InteractiveToken</LogonType>"
        L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
        L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"
        L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><Priority>5</Priority></Settings>"
        L"<Actions Context=\"Author\"><Exec><Command>%s</Command></Exec></Actions></Task>", sid, sid, esc);
    xml[3071] = 0;
    HANDLE f = CreateFileW(xmlPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, xml, lstrlenW(xml) * sizeof(wchar_t), &w, NULL);
    CloseHandle(f);
    _snwprintf(args, MAX_PATH + 48, L"/Create /F /TN TraySense /XML \"%s\"", xmlPath); args[MAX_PATH + 47] = 0;
    schtasks(args);
    DeleteFileW(xmlPath);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, RUNKEY, APP);
    RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"Task", REG_DWORD, &one, sizeof one);
}

static void autostart_migrate(void)
{
    wchar_t exe[MAX_PATH];
    if (!is_admin() || reg_dword(L"Task") ||
        RegGetValueW(HKEY_CURRENT_USER, RUNKEY, APP, RRF_RT_REG_SZ, NULL, NULL, NULL) != ERROR_SUCCESS) return;
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (n && n < MAX_PATH) task_create(exe);
}

static void relaunch_admin(const wchar_t *args)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    SHELLEXECUTEINFOW e; ZeroMemory(&e, sizeof e);
    e.cbSize = sizeof e; e.lpVerb = L"runas"; e.lpFile = exe; e.lpParameters = args; e.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&e)) DestroyWindow(g_wnd);
}

static void cpu_temp_setup(int ask)
{
    DWORD one = 1;
    if (ask) {
        if (reg_dword(L"AskedCpuTemp")) return;
        RegSetKeyValueW(HKEY_CURRENT_USER, REGKEY, L"AskedCpuTemp", REG_DWORD, &one, sizeof one);
    }
    pio_init();
    InterlockedExchange(&g_rebuild, 1);
    SetEvent(g_wake);
    switch (g_pioState) {
    case PIO_OK:
        if (!ask) MessageBoxW(g_wnd, L"CPU temperature is on.", APP, MB_ICONINFORMATION);
        return;
    case PIO_UNSUPPORTED:
    case PIO_FAILED:
    case PIO_NOMODULE:
        if (!ask) MessageBoxW(g_wnd, L"CPU temperature isn't supported on this processor "
                                     L"(Intel Core or AMD Ryzen / EPYC are supported).", APP, MB_ICONINFORMATION);
        return;
    case PIO_NOADMIN:
        if (MessageBoxW(g_wnd, L"Reading the CPU temperature sensor needs administrator rights.\n\n"
                               L"Restart TraySense as administrator? It will start that way automatically from now on.",
                        APP, MB_YESNO | MB_ICONQUESTION) == IDYES)
            relaunch_admin(L"/restart");
        return;
    default:
        if (MessageBoxW(g_wnd, L"Show CPU temperature?\n\n"
                               L"Windows doesn't let apps read the CPU's temperature sensor on its own, so TraySense "
                               L"installs PawnIO: a small, signed, open-source driver (the one LibreHardwareMonitor uses). "
                               L"It's verified before it runs and can be removed any time from Settings > Apps.\n\n"
                               L"Windows will ask for permission.",
                        APP L" \x2013 CPU temperature", MB_YESNO | MB_ICONQUESTION) == IDYES)
            relaunch_admin(L"/restart /install-pawnio");
    }
}

static void menu(void)
{
    static const DWORD iv[3] = { 1000, 2000, 5000 };
    HMENU h = CreatePopupMenu(), sub = CreatePopupMenu();
    for (int m = 0; m < M_N; m++) {
        wchar_t lbl[80];
        lstrcpyW(lbl, NAMES[m]);
        if (m == M_CPUT && g_pioState == PIO_OK) lstrcatW(lbl, L"  (CPU sensor)");
        else if ((m == M_CPUT || m == M_GPUT || m == M_DISKT) && (g_mask >> m & 1) && !g_ok[m]) lstrcatW(lbl, L"  (no sensor found)");
        AppendMenuW(h, MF_STRING | ((g_mask >> m & 1) ? MF_CHECKED : 0), 100 + m, lbl);
    }
    if (g_pioState != PIO_OK && g_pioState != PIO_UNSUPPORTED) AppendMenuW(h, MF_STRING, 320, L"Turn on CPU temperature...");
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING | (g_mode == MODE_BAR ? MF_CHECKED : 0), 310, L"Big labels on taskbar");
    if (g_mode == MODE_BAR) {
        HMENU on = CreatePopupMenu();
        AppendMenuW(on, MF_STRING | (g_where == SHOW_MAIN ? MF_CHECKED : 0), 330, L"Main taskbar");
        AppendMenuW(on, MF_STRING | (g_where == SHOW_ALL ? MF_CHECKED : 0), 331, L"All taskbars");
        AppendMenuW(on, MF_STRING | (g_where == SHOW_OTHERS ? MF_CHECKED : 0), 332, L"Other monitors only");
        AppendMenuW(h, MF_POPUP, (UINT_PTR)on, L"Show on");
        AppendMenuW(h, MF_STRING, 311, L"Move left");
        AppendMenuW(h, MF_STRING, 312, L"Move right");
        AppendMenuW(h, MF_STRING, 313, L"Reset position");
    }
    AppendMenuW(sub, MF_STRING | (g_interval == 1000 ? MF_CHECKED : 0), 200, L"1 second");
    AppendMenuW(sub, MF_STRING | (g_interval == 2000 ? MF_CHECKED : 0), 201, L"2 seconds");
    AppendMenuW(sub, MF_STRING | (g_interval == 5000 ? MF_CHECKED : 0), 202, L"5 seconds (lowest power)");
    AppendMenuW(h, MF_POPUP, (UINT_PTR)sub, L"Refresh rate");
    AppendMenuW(h, MF_STRING | (autostart_on() ? MF_CHECKED : 0), 300, L"Start with Windows");
    AppendMenuW(h, MF_SEPARATOR, 0, NULL);
    AppendMenuW(h, MF_STRING, 399, L"Exit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_wnd);
    int cmd = TrackPopupMenu(h, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(h);

    if (cmd >= 100 && cmd < 100 + M_N) {
        DWORD nm = g_mask ^ (1u << (cmd - 100));
        if (!nm) return;
        g_mask = nm;
        InterlockedExchange(&g_rebuild, 1);
        SetEvent(g_wake);
        refresh(1);
        save();
    } else if (cmd >= 200 && cmd <= 202) {
        g_interval = iv[cmd - 200];
        SetEvent(g_wake);
        save();
    } else if (cmd == 300) autostart_toggle();
    else if (cmd == 310) { g_mode = g_mode == MODE_BAR ? MODE_TRAY : MODE_BAR; refresh(1); save(); }
    else if (cmd >= 311 && cmd <= 313) {
        g_offset = cmd == 313 ? 0 : g_offset + (cmd == 311 ? -10 : 10);
        for (int i = 0; i < MAX_BARS; i++)
            if (g_bars[i].wnd) {
                bar = &g_bars[i];
                ZeroMemory(&bar->lastPos, sizeof bar->lastPos);
                bar_place();
            }
        save();
    }
    else if (cmd >= 330 && cmd <= 332) { g_where = cmd - 330; refresh(1); save(); }
    else if (cmd == 320) cpu_temp_setup(0);
    else if (cmd == 399) DestroyWindow(g_wnd);
}

static LRESULT CALLBACK barproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_RBUTTONUP:     menu(); return 0;
    case WM_LBUTTONUP: {
        wchar_t tm[MAX_PATH];
        if (sys_path(tm, L"taskmgr.exe")) ShellExecuteW(NULL, L"open", tm, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    case WM_NCDESTROY:
        for (int i = 0; i < MAX_BARS; i++) if (g_bars[i].wnd == w) g_bars[i].wnd = NULL;
        break;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SAMPLED:
        refresh(0);
        return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP || lp == WM_CONTEXTMENU) menu();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        read_env(); refresh(1);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING *ps = (POWERBROADCAST_SETTING *)lp;
            if (IsEqualGUID(&ps->PowerSetting, &G_DISPLAY)) {
                g_paused = *(DWORD *)ps->Data == 0;
                SetEvent(g_wake);
            }
        }
        return TRUE;
    case WM_DESTROY:
        for (int m = 0; m < M_N; m++) if (g_shown[m]) tray(m, NIM_DELETE);
        bars_destroy();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_taskbarCreated && msg) {
        for (int m = 0; m < M_N; m++) g_shown[m] = 0;
        bars_destroy();
        refresh(1);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    {
        typedef BOOL (WINAPI *SDDD)(DWORD);
        SDDD f = (SDDD)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetDefaultDllDirectories");
        if (f) f(LOAD_LIBRARY_SEARCH_SYSTEM32);
        SetDllDirectoryW(L"");
    }
    if (is_admin()) lock_self();
    int restart = wcsstr(GetCommandLineW(), L"/restart") != NULL;
    for (int tries = 0;; tries++) {
        HANDLE mx = CreateMutexW(NULL, TRUE, L"Local\\TraySense.single");
        if (GetLastError() != ERROR_ALREADY_EXISTS) break;
        CloseHandle(mx);
        if (!restart || tries > 40) return 0;
        Sleep(150);
    }

    SetProcessDPIAware();
    {
        typedef BOOL (WINAPI *SPI)(HANDLE, int, void *, DWORD);
        SPI spi = (SPI)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
        struct { ULONG Version, ControlMask, StateMask; } pt = { 1, 1, 1 };
        if (spi) spi(GetCurrentProcess(), 4 , &pt, sizeof pt);
    }

    InitializeCriticalSection(&g_pioCs);
    InitializeCriticalSection(&g_scanCs);
    load();
    read_env();
    init_gpu();
    init_disk();
    if (wcsstr(GetCommandLineW(), L"/install-pawnio") && is_admin()) {
        HANDLE probe = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
        if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
        else {
            const wchar_t *why = L"";
            if (!pawnio_install(&why)) MessageBoxW(NULL, why, APP L" \x2013 CPU temperature", MB_ICONWARNING);
        }
    }
    pio_init();
    autostart_migrate();

    WNDCLASSW wc; ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = APP;
    RegisterClassW(&wc);
    wc.lpfnWndProc = barproc; wc.lpszClassName = L"TraySenseBar";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW, APP, APP, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    {
        typedef BOOL (WINAPI *CWMF)(HWND, UINT, DWORD, void *);
        CWMF f = (CWMF)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ChangeWindowMessageFilterEx");
        if (f) { f(g_wnd, g_taskbarCreated, 1, NULL); f(g_wnd, WM_TRAY, 1, NULL); }
    }
    RegisterPowerSettingNotification(g_wnd, &G_DISPLAY, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 64 * 1024, sampler, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    refresh(1);

    MSG m;
    int trimmed = 0;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        DispatchMessageW(&m);
        if (!trimmed && m.message == WM_SAMPLED) {
            trimmed = 1;
            if ((g_mask & (1u << M_CPUT)) && g_pioState != PIO_OK && g_pioState != PIO_UNSUPPORTED && !g_ok[M_CPUT])
                cpu_temp_setup(1);
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        }
    }
    g_quit = 1;
    SetEvent(g_wake);
    if (th) WaitForSingleObject(th, 2000);
    reset_fonts();
    return 0;
}
