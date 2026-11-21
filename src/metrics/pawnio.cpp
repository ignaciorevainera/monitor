#include "pawnio.h"

#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include <cstring>

namespace pawnio {
namespace {

constexpr DWORD kIoctlPioLoad = 0xA1B22084u;
constexpr DWORD kIoctlPioExec = 0xA1B22104u;

PioState g_pioState = PioState::Failed;
HANDLE g_pio = INVALID_HANDLE_VALUE;
HANDLE g_pciMutex = nullptr;
int g_cpuVendor = 0;
int g_tjmax = 0;
CRITICAL_SECTION g_pioCs;

struct CsInit {
    CsInit() { InitializeCriticalSection(&g_pioCs); }
    ~CsInit() { DeleteCriticalSection(&g_pioCs); }
};
CsInit g_csInit;

bool PioExec(const char* fn, ULONGLONG in, ULONGLONG* out) {
    BYTE buf[40];
    ULONGLONG o = 0;
    DWORD got = 0;
    ZeroMemory(buf, sizeof(buf));
    lstrcpynA(reinterpret_cast<char*>(buf), fn, 32);
    std::memcpy(buf + 32, &in, 8);
    if (!DeviceIoControl(g_pio, kIoctlPioExec, buf, sizeof(buf), &o, sizeof(o), &got, nullptr) || got < 8) {
        return false;
    }
    *out = o;
    return true;
}

}  // namespace

PioState Init() {
    EnterCriticalSection(&g_pioCs);
    if (g_pio != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pio);
        g_pio = INVALID_HANDLE_VALUE;
    }

    unsigned b = 0;
#ifdef _MSC_VER
    int r[4];
    __cpuid(r, 0);
    b = static_cast<unsigned>(r[1]);
#else
    unsigned a = 0, c = 0, d = 0;
    if (!__get_cpuid(0, &a, &b, &c, &d)) b = 0;
#endif
    g_cpuVendor = (b == 0x756e6547) ? 1 : (b == 0x68747541) ? 2 : 0;
    if (!g_cpuVendor) {
        g_pioState = PioState::Unsupported;
        LeaveCriticalSection(&g_pioCs);
        return g_pioState;
    }

    g_pio = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (g_pio == INVALID_HANDLE_VALUE) {
        g_pioState = (GetLastError() == ERROR_ACCESS_DENIED) ? PioState::NoAdmin : PioState::NoDriver;
        LeaveCriticalSection(&g_pioCs);
        return g_pioState;
    }

    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(g_cpuVendor == 1 ? 101 : 102),
                            reinterpret_cast<LPCWSTR>(RT_RCDATA));
    HGLOBAL hg = r ? LoadResource(nullptr, r) : nullptr;
    const void* src = hg ? LockResource(hg) : nullptr;
    DWORD sz = r ? SizeofResource(nullptr, r) : 0;
    DWORD got = 0;

    if (!src || !sz) {
        g_pioState = PioState::NoModule;
    } else if (!DeviceIoControl(g_pio, kIoctlPioLoad, const_cast<void*>(src), sz, nullptr, 0, &got, nullptr)) {
        g_pioState = PioState::Failed;
    } else {
        ULONGLONG v = 0;
        g_pioState = PioState::Ok;
        if (g_cpuVendor == 1) {
            g_tjmax = PioExec("ioctl_read_msr", 0x1A2, &v) ? static_cast<int>((v >> 16) & 0xFF) : 0;
            if (g_tjmax < 60 || g_tjmax > 130) g_tjmax = 100;
        } else if (!g_pciMutex) {
            g_pciMutex = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
        }
    }

    if (g_pioState != PioState::Ok) {
        CloseHandle(g_pio);
        g_pio = INVALID_HANDLE_VALUE;
    }

    LeaveCriticalSection(&g_pioCs);
    return g_pioState;
}

bool ReadTemp(double& c) {
    if (g_pioState != PioState::Ok) return false;
    ULONGLONG v = 0;
    double t = 0.0;
    bool ok = false;

    EnterCriticalSection(&g_pioCs);
    if (g_pio != INVALID_HANDLE_VALUE) {
        if (g_cpuVendor == 1) {
            if (PioExec("ioctl_read_msr", 0x1B1, &v)) {
                t = g_tjmax - static_cast<double>((v >> 16) & 0x7F);
                ok = true;
            }
        } else {
            DWORD w = g_pciMutex ? WaitForSingleObject(g_pciMutex, 20) : WAIT_OBJECT_0;
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
                if (PioExec("ioctl_read_smn", 0x00059800, &v)) {
                    double val = ((v >> 21) & 0x7FF) / 8.0;
                    if (v & 0x80000) val -= 49.0;
                    t = val;
                    ok = true;
                }
                if (g_pciMutex) ReleaseMutex(g_pciMutex);
            }
        }
    }
    LeaveCriticalSection(&g_pioCs);

    if (ok && t > 0.0 && t < 125.0) {
        c = t;
        return true;
    }
    return false;
}

}  // namespace pawnio
