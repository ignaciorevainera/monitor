#include "opencode_dialog.h"
#include "i18n.h"
#include "metrics/opencode_usage.h"

#include <shellapi.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"WirelessBatteryOpenCodeDialog";
constexpr UINT IDC_KEY_EDIT = 100;
constexpr UINT IDC_GET_KEY = 101;
constexpr int kMaxKeyLength = 512;

struct DialogState {
    std::wstring result;
    HWND edit = nullptr;
    bool accepted = false;
};

DialogState* State(HWND hwnd) {
    return reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetControlFont(HWND control) {
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

void ZeroWString(std::wstring& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size() * sizeof(wchar_t));
        s.clear();
    }
}

std::wstring TrimWhitespace(const wchar_t* text) {
    if (!text) return L"";
    const std::wstring_view s(text);
    const wchar_t* ws = L" \t\r\n";
    size_t start = s.find_first_not_of(ws);
    if (start == std::wstring_view::npos) return L"";
    size_t end = s.find_last_not_of(ws);
    return std::wstring(s.substr(start, end - start + 1));  // no lingering copy of the key
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    DialogState* state = State(hwnd);
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            const int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
            HWND label = CreateWindowExW(0, L"STATIC", T(L"OpenCode API key:"),
                                         WS_CHILD | WS_VISIBLE, scale(16), scale(18), scale(382), scale(22),
                                         hwnd, nullptr, inst, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                                          scale(16), scale(45), scale(382), scale(25),
                                          hwnd, (HMENU)(INT_PTR)IDC_KEY_EDIT, inst, nullptr);
            HWND getKey = CreateWindowExW(0, L"BUTTON", T(L"Get an API key..."),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          scale(16), scale(85), scale(140), scale(29),
                                          hwnd, (HMENU)(INT_PTR)IDC_GET_KEY, inst, nullptr);
            HWND save = CreateWindowExW(0, L"BUTTON", T(L"Save"),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                        scale(220), scale(85), scale(85), scale(29),
                                        hwnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", T(L"Cancel"),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          scale(313), scale(85), scale(85), scale(29),
                                          hwnd, (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
            SetControlFont(label);
            SetControlFont(state->edit);
            SetControlFont(getKey);
            SetControlFont(save);
            SetControlFont(cancel);
            SendMessageW(state->edit, EM_SETLIMITTEXT, kMaxKeyLength, 0);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_GET_KEY) {
                ShellExecuteW(nullptr, L"open", L"https://opencode.ai/console/api", nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (LOWORD(wp) == IDOK) {
                const int length = GetWindowTextLengthW(state->edit);
                std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, 0);
                GetWindowTextW(state->edit, buffer.data(), static_cast<int>(buffer.size()));
                std::wstring trimmed = TrimWhitespace(buffer.data());
                SecureZeroMemory(buffer.data(), buffer.size() * sizeof(wchar_t));
                if (trimmed.empty()) {
                    MessageBoxW(hwnd, T(L"Enter the API key."), T(L"OpenCode"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                if (!metrics::IsValidOpenCodeToken(trimmed)) {
                    ZeroWString(trimmed);
                    MessageBoxW(hwnd, T(L"Invalid API key"), T(L"OpenCode"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                state->result = std::move(trimmed);
                ZeroWString(trimmed);  // a moved-from SSO buffer can still hold the key
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

bool PromptOpenCodeKey(HWND owner, std::wstring& key) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.lpfnWndProc = DialogProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    const int dpi = owner ? GetDpiForWindow(owner) : 96;
    const int width = MulDiv(420, dpi, 96);
    const int height = MulDiv(157, dpi, 96);
    POINT p{};
    GetCursorPos(&p);
    HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT area{};
    if (GetMonitorInfoW(monitor, &mi)) area = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int x = area.left + ((area.right - area.left - width) / 2);
    int y = area.top + ((area.bottom - area.top - height) / 2);
    x = std::max(x, static_cast<int>(area.left));
    y = std::max(y, static_cast<int>(area.top));

    DialogState state;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, kWindowClass,
                                  T(L"OpenCode"), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                  x, y, width, height, owner, nullptr, inst, &state);
    if (!dialog) return false;

    const bool ownerEnabled = owner && IsWindowEnabled(owner);
    if (ownerEnabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    if (state.edit) SetFocus(state.edit);

    MSG msg{};
    while (IsWindow(dialog)) {
        BOOL code = GetMessageW(&msg, nullptr, 0, 0);
        if (code <= 0) {
            if (code == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (ownerEnabled) EnableWindow(owner, TRUE);
    if (state.accepted) {
        key = std::move(state.result);
    } else {
        ZeroWString(state.result);  // a rejected key must not linger in memory
    }
    return state.accepted;
}
