#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <string>

#include "../app_version.h"
#include "metrics.h"
#include "opencode_token.h"
#include "opencode_usage.h"

namespace metrics {

namespace {

std::atomic<bool> g_aiStop{ false };
std::atomic<bool> g_aiHalted{ false };
SRWLOCK g_aiLock = SRWLOCK_INIT;
AiUsage g_aiCache{};
HANDLE g_aiWake = nullptr;
HANDLE g_aiThread = nullptr;
std::atomic<HINTERNET> g_aiActiveRequest{ nullptr };

bool IsValidToken(const std::wstring& token) {
    if (token.empty()) return false;
    for (wchar_t c : token) {
        if (c <= 0x20 || c >= 0x7f) return false;
    }
    return true;
}

void ZeroWString(std::wstring& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size() * sizeof(wchar_t));
        s.clear();
    }
}

void ZeroString(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
        s.clear();
    }
}

unsigned long __stdcall AiWorker(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        if (g_aiStop.load(std::memory_order_acquire)) break;
        if (!g_aiHalted.load(std::memory_order_acquire)) {
            std::wstring token;
            if (LoadOpenCodeToken(token)) {
                AiUsage fetched{};
                FetchOpenCodeUsage(token, fetched);
                ZeroWString(token);
                if (fetched.error == AiError::Unauthorized || fetched.error == AiError::Forbidden) {
                    g_aiHalted.store(true, std::memory_order_release);
                }
                AcquireSRWLockExclusive(&g_aiLock);
                g_aiCache = fetched;
                ReleaseSRWLockExclusive(&g_aiLock);
            } else {
                AcquireSRWLockExclusive(&g_aiLock);
                g_aiCache = AiUsage{};
                ReleaseSRWLockExclusive(&g_aiLock);
            }
        }
        if (g_aiStop.load(std::memory_order_acquire)) break;
        if (g_aiWake) {
            WaitForSingleObject(g_aiWake, 5 * 60 * 1000);
        } else {
            Sleep(5 * 60 * 1000);
        }
    }
    return 0;
}

}  // namespace

bool FetchOpenCodeUsage(const std::wstring& token, AiUsage& out) {
    out = AiUsage{};
    if (!IsValidToken(token)) {
        out.error = AiError::Unauthorized;
        return false;
    }

    int utf8Len = WideCharToMultiByte(
        CP_UTF8, 0, token.data(), static_cast<int>(token.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        out.error = AiError::Unauthorized;
        return false;
    }
    std::string utf8(static_cast<size_t>(utf8Len), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, token.data(), static_cast<int>(token.size()), &utf8[0], utf8Len, nullptr, nullptr);

    std::wstring header = L"Authorization: Bearer ";
    header.reserve(header.size() + utf8.size() + 2);
    for (unsigned char c : utf8) {
        header.push_back(static_cast<wchar_t>(c));
    }
    header += L"\r\n";
    ZeroString(utf8);

    HINTERNET session = WinHttpOpen(
        L"Monitor/" WB_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        ZeroWString(header);
        out.error = AiError::Network;
        return false;
    }

    if (!WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000)) {
        ZeroWString(header);
        WinHttpCloseHandle(session);
        out.error = AiError::Network;
        return false;
    }

    HINTERNET connection = WinHttpConnect(session, L"opencode.ai", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = connection
        ? WinHttpOpenRequest(
              connection, L"GET", L"/inference/go/v1/usage", nullptr,
              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    bool configured = req && WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (req) g_aiActiveRequest.store(req, std::memory_order_release);  // abortable from StopAiSampler

    bool sent = configured &&
                !g_aiStop.load(std::memory_order_acquire) &&
                WinHttpSendRequest(
                    req, header.c_str(), static_cast<DWORD>(header.size()),
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    ZeroWString(header);

    bool ok = false;
    if (sent && !g_aiStop.load(std::memory_order_acquire) && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(
            req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &size, nullptr);

        if (status == 200) {
            std::string body;
            char buffer[4096];
            DWORD bytes = 0;
            bool complete = false;
            while (!g_aiStop.load(std::memory_order_acquire)) {
                if (!WinHttpReadData(req, buffer, sizeof(buffer), &bytes)) break;
                if (bytes == 0) {
                    complete = true;
                    break;
                }
                body.append(buffer, bytes);
                if (body.size() > 16 * 1024) break;
            }
            if (complete) {
                ok = ParseAiUsage(body, out);
            } else {
                out.error = AiError::Network;
            }
        } else if (status == 401) {
            out.error = AiError::Unauthorized;
        } else if (status == 403) {
            out.error = AiError::Forbidden;
        } else if (status == 429) {
            out.error = AiError::RateLimited;
        } else {
            out.error = AiError::Network;
        }
    } else {
        out.error = AiError::Network;
    }

    g_aiActiveRequest.store(nullptr, std::memory_order_release);
    if (req) WinHttpCloseHandle(req);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

bool AiSnapshot(AiUsage& out) {
    AcquireSRWLockShared(&g_aiLock);
    out = g_aiCache;
    ReleaseSRWLockShared(&g_aiLock);
    return true;
}

bool IsValidOpenCodeToken(const std::wstring& token) {
    return IsValidToken(token);
}

void StartAiSampler() {
    if (g_aiThread) return;
    g_aiStop.store(false, std::memory_order_release);
    if (!g_aiWake) g_aiWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_aiThread = CreateThread(nullptr, 0, AiWorker, nullptr, 0, nullptr);
}

void StopAiSampler() {
    g_aiStop.store(true, std::memory_order_release);
    // Aborting the in-flight request makes every blocking WinHTTP call in the worker return at
    // once, so an INFINITE join still finishes well inside the 5 s WM_DESTROY allows.
    HINTERNET inflight = g_aiActiveRequest.exchange(nullptr, std::memory_order_acq_rel);
    if (inflight) WinHttpCloseHandle(inflight);
    if (g_aiWake) SetEvent(g_aiWake);
    if (g_aiThread) {
        WaitForSingleObject(g_aiThread, INFINITE);
        CloseHandle(g_aiThread);
        g_aiThread = nullptr;
    }
    // Only the worker touches g_aiWake besides this point; after the join it is safe to close.
    if (g_aiWake) {
        CloseHandle(g_aiWake);
        g_aiWake = nullptr;
    }
}

void ClearAiUsage() {
    g_aiHalted.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&g_aiLock);
    g_aiCache = AiUsage{};
    ReleaseSRWLockExclusive(&g_aiLock);
}

void RequestAiRefresh() {
    g_aiHalted.store(false, std::memory_order_release);
    if (g_aiWake) SetEvent(g_aiWake);
}

}  // namespace metrics
