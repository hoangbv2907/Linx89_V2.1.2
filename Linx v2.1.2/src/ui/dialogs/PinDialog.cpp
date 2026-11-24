#include "ui/dialogs/PinDialog.h"
#include "common/EncodingUtils.h"
#include <string>

namespace {

enum { IDC_PIN_EDIT = 6001, IDC_PIN_OK, IDC_PIN_CANCEL, IDC_PIN_MSG, IDC_PIN_ROLE };

struct PinState {
    const std::function<bool(const std::string&, int)>* verify = nullptr;
    int fixedRole = -1;
    int role = 0;
    bool ok = false;
    bool done = false;
    HWND hEdit = nullptr, hMsg = nullptr, hRole = nullptr;
};

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

int SelectedRole(PinState* st) {
    if (st->fixedRole > 0) return st->fixedRole;
    const int i = (int)SendMessageW(st->hRole, CB_GETCURSEL, 0, 0);
    return i == 1 ? 2 : 1;                      // 0 = Vận hành (1), 1 = Giám sát (2)
}

void OnOk(HWND hwnd, PinState* st) {
    const std::string pin = WideToUtf8(GetText(st->hEdit));
    const int role = SelectedRole(st);
    if (st->verify && (*st->verify)(pin, role)) {
        st->ok = true;
        st->role = role;
        st->done = true;
        DestroyWindow(hwnd);
        return;
    }
    SetWindowTextW(st->hMsg, L"Mật khẩu sai - hãy nhập lại.");   // không giới hạn số lần thử
    SetWindowTextW(st->hEdit, L"");
    SetFocus(st->hEdit);
}

LRESULT CALLBACK PinProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<PinState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (m) {
    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(w)) {
        case IDC_PIN_OK: OnOk(hwnd, st); break;
        case IDCANCEL:
        case IDC_PIN_CANCEL: st->done = true; DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_CLOSE:
        if (st) st->done = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

} // namespace

bool PinDialog::Run(HWND parent, const std::wstring& title, int fixedRole,
    const std::function<bool(const std::string&, int)>& verify, int* roleOut)
{
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PinProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"LinxPinDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc)) return false;
        registered = true;
    }
    PinState st;
    st.verify = &verify;
    st.fixedRole = fixedRole;

    const bool chooseRole = fixedRole <= 0;
    const int dpi = parent ? (int)GetDpiForWindow(parent) : (int)GetDpiForSystem();
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const int H = chooseRole ? 220 : 170;
    RECT rc{ 0, 0, S(340), S(H) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | (parent ? 0 : WS_EX_TOPMOST), L"LinxPinDlg", title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, inst, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    HFONT font = CreateFontIndirectW(&lf);

    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD sty, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | sty, S(x), S(y), S(w), S(h), hwnd,
            (HMENU)(INT_PTR)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    int y = 18;
    if (chooseRole) {
        mk(L"STATIC", L"Cấp sử dụng:", SS_LEFT, 20, y, 300, 20, 0);
        st.hRole = mk(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 20, y + 24, 300, 120, IDC_PIN_ROLE);
        SendMessageW(st.hRole, CB_ADDSTRING, 0, (LPARAM)L"Vận hành viên");
        SendMessageW(st.hRole, CB_ADDSTRING, 0, (LPARAM)L"Giám sát viên");
        SendMessageW(st.hRole, CB_SETCURSEL, 0, 0);
        y += 62;
    }
    mk(L"STATIC", fixedRole == 2 ? L"Nhập mật khẩu Giám sát viên:" : L"Mật khẩu:", SS_LEFT, 20, y, 300, 20, 0);
    st.hEdit = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL, 20, y + 26, 300, 26, IDC_PIN_EDIT);
    SendMessageW(st.hEdit, EM_SETLIMITTEXT, 8, 0);
    st.hMsg = mk(L"STATIC", L"", SS_LEFT, 20, y + 60, 300, 20, IDC_PIN_MSG);
    mk(L"BUTTON", L"Đăng nhập", WS_TABSTOP | BS_DEFPUSHBUTTON, 110, y + 96, 100, 30, IDC_PIN_OK);
    mk(L"BUTTON", L"Hủy", WS_TABSTOP | BS_PUSHBUTTON, 220, y + 96, 100, 30, IDC_PIN_CANCEL);

    // căn giữa theo cửa sổ cha, hoặc giữa màn hình khi chưa có cửa sổ cha (đăng nhập lúc khởi động)
    RECT pr{}, wr{};
    GetWindowRect(hwnd, &wr);
    if (parent && GetWindowRect(parent, &pr)) {
        SetWindowPos(hwnd, nullptr, pr.left + ((pr.right - pr.left) - (wr.right - wr.left)) / 2,
            pr.top + ((pr.bottom - pr.top) - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    else {
        const int sw = GetSystemMetricsForDpi(SM_CXSCREEN, dpi), sh = GetSystemMetricsForDpi(SM_CYSCREEN, dpi);
        SetWindowPos(hwnd, nullptr, (sw - (wr.right - wr.left)) / 2, (sh - (wr.bottom - wr.top)) / 3, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(st.hEdit);

    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }
        if (r < 0) break;
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (font) DeleteObject(font);
    if (st.ok && roleOut) *roleOut = st.role;
    return st.ok;
}
