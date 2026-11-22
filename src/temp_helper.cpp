#include "metrics/pawnio.h"
#include "metrics/temp_file.h"

#include <windows.h>
#include <cstdio>
#include <cwchar>

namespace {

bool BuildTempPaths(wchar_t* dirPath, size_t dirCap, wchar_t* tmpPath, size_t tmpCap,
                    wchar_t* finalPath, size_t finalCap) {
    wchar_t localAppData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    if (std::swprintf(dirPath, dirCap, L"%ls\\Monitor", localAppData) < 0) return false;
    if (std::swprintf(tmpPath, tmpCap, L"%ls\\cpu_temp.bin.tmp", dirPath) < 0) return false;
    if (std::swprintf(finalPath, finalCap, L"%ls\\cpu_temp.bin", dirPath) < 0) return false;
    return true;
}

bool WriteAtomicTempFile(const wchar_t* dirPath, const wchar_t* tmpPath, const wchar_t* finalPath,
                         const TempFileData& data) {
    if (!CreateDirectoryW(dirPath, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    HANDLE h = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(h, &data, sizeof(data), &written, nullptr);
    CloseHandle(h);
    if (!ok || written != sizeof(data)) {
        DeleteFileW(tmpPath);
        return false;
    }
    if (!MoveFileExW(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmpPath);
        return false;
    }
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE singleMutex = CreateMutexW(nullptr, FALSE, L"Local\\MonitorTempHelper");
    if (!singleMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleMutex);
        return 0;
    }

    pawnio::PioState state = pawnio::Init();
    if (state != pawnio::PioState::Ok) {
        wchar_t msg[128]{};
        std::swprintf(msg, 128, L"Monitor.TempHelper: PawnIO init failed (state=%d); requires elevation and PawnIO driver.\n",
                      static_cast<int>(state));
        OutputDebugStringW(msg);
        std::fwprintf(stderr, L"%ls", msg);
        CloseHandle(singleMutex);
        return 1;
    }

    wchar_t dirPath[MAX_PATH]{};
    wchar_t tmpPath[MAX_PATH]{};
    wchar_t finalPath[MAX_PATH]{};
    if (!BuildTempPaths(dirPath, MAX_PATH, tmpPath, MAX_PATH, finalPath, MAX_PATH)) {
        CloseHandle(singleMutex);
        return 1;
    }
    wchar_t stopPath[MAX_PATH]{};
    std::swprintf(stopPath, MAX_PATH, L"%ls\\stop.flag", dirPath);

    for (;;) {
        // The installer asks the elevated helper to exit by creating this flag, so its
        // own exe is unlocked when the setup replaces it.
        if (GetFileAttributesW(stopPath) != INVALID_FILE_ATTRIBUTES) {
            DeleteFileW(stopPath);
            CloseHandle(singleMutex);
            return 0;
        }
        TempFileData data{};
        data.magic = kTempFileMagic;
        double tempC = 0.0;
        if (pawnio::ReadTemp(tempC)) {
            data.tick = GetTickCount64();
            data.tempC = tempC;
        } else {
            data.tick = 0;
            data.tempC = 0.0;
        }
        WriteAtomicTempFile(dirPath, tmpPath, finalPath, data);
        Sleep(2000);
    }

    CloseHandle(singleMutex);
    return 0;
}
