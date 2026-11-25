#include "opencode_token.h"

#include <dpapi.h>

namespace {

constexpr wchar_t kRegSubkey[] = L"Software\\WirelessBattery";
constexpr wchar_t kTokenValue[] = L"OpenCodeToken";
constexpr wchar_t kDescription[] = L"Monitor OpenCode API key";
constexpr DWORD kMaxBlobBytes = 64 * 1024;

}  // namespace

bool ProtectToken(const std::wstring& token, std::vector<BYTE>& out) {
    if (token.empty()) return false;
    DATA_BLOB in{
        static_cast<DWORD>(token.size() * sizeof(wchar_t)),
        reinterpret_cast<BYTE*>(const_cast<wchar_t*>(token.data()))
    };
    DATA_BLOB enc{};
    if (!CryptProtectData(&in, kDescription, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &enc)) {
        return false;
    }
    out.assign(enc.pbData, enc.pbData + enc.cbData);
    LocalFree(enc.pbData);
    return true;
}

bool UnprotectToken(const std::vector<BYTE>& in, std::wstring& token) {
    if (in.empty() || in.size() > kMaxBlobBytes) return false;
    DATA_BLOB blob{
        static_cast<DWORD>(in.size()),
        const_cast<BYTE*>(in.data())
    };
    DATA_BLOB dec{};
    if (!CryptUnprotectData(&blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dec)) {
        return false;
    }
    const bool ok = dec.cbData > 0 && (dec.cbData % sizeof(wchar_t)) == 0;
    if (ok) {
        token.assign(reinterpret_cast<const wchar_t*>(dec.pbData), dec.cbData / sizeof(wchar_t));
    }
    if (dec.pbData) {
        SecureZeroMemory(dec.pbData, dec.cbData);
        LocalFree(dec.pbData);
    }
    return ok;
}

bool SaveOpenCodeToken(const std::wstring& token) {
    std::vector<BYTE> blob;
    if (!ProtectToken(token, blob) || blob.size() > kMaxBlobBytes) return false;
    LSTATUS st = RegSetKeyValueW(
        HKEY_CURRENT_USER, kRegSubkey, kTokenValue, REG_BINARY,
        blob.data(), static_cast<DWORD>(blob.size()));
    return st == ERROR_SUCCESS;
}

bool LoadOpenCodeToken(std::wstring& token) {
    DWORD bytes = 0;
    LSTATUS st = RegGetValueW(
        HKEY_CURRENT_USER, kRegSubkey, kTokenValue, RRF_RT_REG_BINARY,
        nullptr, nullptr, &bytes);
    if (st != ERROR_SUCCESS || bytes == 0 || bytes > kMaxBlobBytes) return false;
    std::vector<BYTE> blob(bytes);
    st = RegGetValueW(
        HKEY_CURRENT_USER, kRegSubkey, kTokenValue, RRF_RT_REG_BINARY,
        nullptr, blob.data(), &bytes);
    if (st != ERROR_SUCCESS || bytes == 0 || bytes > kMaxBlobBytes) return false;
    blob.resize(bytes);
    return UnprotectToken(blob, token);
}

bool HasOpenCodeToken() {
    DWORD bytes = 0;
    LSTATUS st = RegGetValueW(
        HKEY_CURRENT_USER, kRegSubkey, kTokenValue, RRF_RT_REG_BINARY,
        nullptr, nullptr, &bytes);
    return st == ERROR_SUCCESS && bytes > 0 && bytes <= kMaxBlobBytes;
}

void ClearOpenCodeToken() {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegSubkey, kTokenValue);
}
