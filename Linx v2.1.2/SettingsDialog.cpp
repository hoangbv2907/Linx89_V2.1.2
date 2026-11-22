#include "SettingsDialog.h"
#include "LicenseDialog.h"
#include "LogViewerDialog.h"
#include "AppPaths.h"
#include "EncodingUtils.h"
#include <commctrl.h>
#include <shellapi.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

enum {
    IDC_ST_TAB = 5001, IDC_ST_SAVE, IDC_ST_CLOSE,
    IDC_ST_IP, IDC_ST_AUTO, IDC_ST_MSG,
    IDC_ST_VIEWLOG, IDC_ST_OPENDIR, IDC_ST_KEEP, IDC_ST_MAXMB,
    IDC_ST_LICINFO, IDC_ST_MACHINE, IDC_ST_PRINTER, IDC_ST_COPY, IDC_ST_CODE, IDC_ST_ACTIVATE,
    IDC_ST_ABOUT
};

struct Page { HWND h; int tab; };

struct State {
    SettingsContext* ctx = nullptr;
    std::vector<Page> pages;
    bool done = false;
    HWND hTab = nullptr, hIp = nullptr, hAuto = nullptr, hMsg = nullptr, hKeep = nullptr, hMax = nullptr;
    HWND hLicInfo = nullptr, hCode = nullptr, hAbout = nullptr;
};

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

void CopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (g) {
        memcpy(GlobalLock(g), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
    }
    CloseClipboard();
}

HWND Make(State* st, int tab, HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id, DWORD ex = 0)
{
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | (tab < 0 ? WS_VISIBLE : 0) | style, x, y, w, h, parent,
        (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    if (tab >= 0) st->pages.push_back({ c, tab });
    return c;
}

void ShowTab(State* st, int tab) {
    for (const auto& p : st->pages) ShowWindow(p.h, p.tab == tab ? SW_SHOW : SW_HIDE);
    TabCtrl_SetCurSel(st->hTab, tab);
}

void RefreshLicenseInfo(State* st) {
    if (st->ctx->license) SetWindowTextW(st->hLicInfo, LicenseDialog::Describe(*st->ctx->license).c_str());
}

void RefreshAbout(State* st) {
    std::wstring t = L"Linx Controller v2.1.2\r\nBY BUI_VAN_HOANG\r\n\r\n";
    t += L"Điều khiển máy in phun Linx 8800 / 8900 / CJ400 / Linx 10 qua giao thức RCI (cổng 29043).\r\n\r\n";
    t += std::wstring(L"Bản build: ") + L"" __DATE__ L" " __TIME__ + L"\r\n";
    t += L"Thư mục dữ liệu: " + LinxDataDir().wstring() + L"\r\n";
    SetWindowTextW(st->hAbout, t.c_str());
}

void OnActivate(HWND hwnd, State* st) {
    SettingsContext* c = st->ctx;
    if (!c->license) return;
    const std::string code = WideToUtf8(GetText(st->hCode));
    if (code.find_first_not_of(" \r\n\t") == std::string::npos) {
        MessageBoxW(hwnd, L"Hãy dán mã gia hạn vào ô bên trên.", L"Gia hạn", MB_ICONWARNING);
        return;
    }
    uint32_t added = 0;
    const lic::CodeResult r = c->license->Activate(code, WideToUtf8(c->printerSerial), &added);
    const wchar_t* msg = nullptr;
    switch (r) {
    case lic::CodeResult::Ok: break;
    case lic::CodeResult::BadFormat: msg = L"Mã không đúng định dạng. Hãy kiểm tra đã dán đủ và đúng mã."; break;
    case lic::CodeResult::BadSignature: msg = L"Mã không hợp lệ (sai chữ ký)."; break;
    case lic::CodeResult::WrongMachine: msg = L"Mã này không dành cho máy tính này (sai mã máy)."; break;
    case lic::CodeResult::WrongPrinter: msg = L"Mã này không dành cho máy in đang kết nối (sai serial máy in)."; break;
    case lic::CodeResult::AlreadyUsed: msg = L"Mã này đã được sử dụng."; break;
    case lic::CodeResult::NoPrinter: msg = L"Chưa đọc được serial máy in. Hãy kết nối máy in rồi nhập lại mã."; break;
    }
    if (msg) {
        MessageBoxW(hwnd, msg, L"Gia hạn", MB_ICONWARNING);
        if (c->log) c->log(std::wstring(L"⚠️ Gia hạn thất bại: ") + msg, 1);
        return;
    }
    RefreshLicenseInfo(st);
    RefreshAbout(st);
    SetWindowTextW(st->hCode, L"");
    const std::wstring ok = L"Đã gia hạn thêm " + std::to_wstring(added) + L" giờ. " + LicenseDialog::Describe(*c->license);
    if (c->log) c->log(L"✅ Gia hạn thành công", 4);
    MessageBoxW(hwnd, ok.c_str(), L"Gia hạn", MB_ICONINFORMATION);
}

bool ParseInt(const std::wstring& s, int lo, int hi, int& out) {
    if (s.empty()) return false;
    wchar_t* e = nullptr;
    long v = wcstol(s.c_str(), &e, 10);
    if (e && *e) return false;
    if (v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

void OnSave(HWND hwnd, State* st) {
    AppSettings s = st->ctx->settings;
    // IP
    DWORD ip = 0;
    SendMessageW(st->hIp, IPM_GETADDRESS, 0, (LPARAM)&ip);
    char buf[32];
    sprintf_s(buf, "%d.%d.%d.%d", (int)((ip >> 24) & 255), (int)((ip >> 16) & 255), (int)((ip >> 8) & 255), (int)(ip & 255));
    if (ip == 0) { ShowTab(st, SettingsDialog::Connection); MessageBoxW(hwnd, L"Địa chỉ IP không hợp lệ.", L"Cài đặt", MB_ICONWARNING); return; }
    s.ip = buf;
    s.autoReconnect = SendMessageW(st->hAuto, BM_GETCHECK, 0, 0) == BST_CHECKED;
    // tên bản tin
    const std::string name = WideToUtf8(GetText(st->hMsg));
    bool ascii = name.size() <= 15;
    for (unsigned char c : name) if (c < 0x20 || c > 0x7E) ascii = false;
    if (!ascii) { ShowTab(st, SettingsDialog::Connection); MessageBoxW(hwnd, L"Tên bản tin gồm tối đa 15 ký tự ASCII (không dấu).", L"Cài đặt", MB_ICONWARNING); return; }
    s.printMessageName = name;
    // nhật ký
    int keep = 0, mb = 0;
    if (!ParseInt(GetText(st->hKeep), 7, 3650, keep)) { ShowTab(st, SettingsDialog::Logs); MessageBoxW(hwnd, L"Số ngày giữ nhật ký phải từ 7 đến 3650.", L"Cài đặt", MB_ICONWARNING); return; }
    if (!ParseInt(GetText(st->hMax), 20, 5000, mb)) { ShowTab(st, SettingsDialog::Logs); MessageBoxW(hwnd, L"Dung lượng nhật ký tối đa phải từ 20 đến 5000 MB.", L"Cài đặt", MB_ICONWARNING); return; }
    s.logKeepDays = keep;
    s.logMaxMB = mb;
    st->ctx->settings = s;
    if (st->ctx->apply) st->ctx->apply(s);
    st->done = true;
    DestroyWindow(hwnd);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (m) {
    case WM_NOTIFY: {
        auto* nh = reinterpret_cast<NMHDR*>(l);
        if (st && nh->idFrom == IDC_ST_TAB && nh->code == TCN_SELCHANGE) ShowTab(st, TabCtrl_GetCurSel(st->hTab));
        return 0;
    }
    case WM_COMMAND: {
        if (!st) break;
        switch (LOWORD(w)) {
        case IDC_ST_SAVE: OnSave(hwnd, st); break;
        case IDCANCEL:
        case IDC_ST_CLOSE: st->done = true; DestroyWindow(hwnd); break;
        case IDC_ST_VIEWLOG: LogViewerDialog::Run(hwnd); break;
        case IDC_ST_OPENDIR: ShellExecuteW(hwnd, L"open", LinxLogDir().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        case IDC_ST_COPY: {
            const std::wstring mid = st->ctx->license ? st->ctx->license->Machine() : L"";
            CopyToClipboard(hwnd, L"Mã máy tính: " + mid + L"\r\nSerial máy in: " +
                (st->ctx->printerSerial.empty() ? std::wstring(L"(chưa kết nối máy in)") : st->ctx->printerSerial));
            MessageBoxW(hwnd, L"Đã sao chép. Gửi nội dung này cho nhà cung cấp để nhận mã gia hạn.", L"Gia hạn", MB_ICONINFORMATION);
            break;
        }
        case IDC_ST_ACTIVATE: OnActivate(hwnd, st); break;
        }
        return 0;
    }
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

void SettingsDialog::Run(HWND parent, SettingsContext& ctx, int initialTab) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"LinxSettingsDlg";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc)) return;
        registered = true;
    }
    State st;
    st.ctx = &ctx;

    const int dpi = parent ? (int)GetDpiForWindow(parent) : 96;
    RECT rc{ 0, 0, MulDiv(600, dpi, 96), MulDiv(445, dpi, 96) };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME, dpi);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LinxSettingsDlg", L"Cài đặt", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, inst, nullptr);
    if (!hwnd) return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    // Nội dung dùng toạ độ 96 DPI; Windows tự co theo DPI vì cửa sổ con của lớp riêng dùng font hệ thống,
    // nên ở đây tạo bằng toạ độ thô rồi nhân tỉ lệ.
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    HFONT font = nullptr;
    {
        LOGFONTW lf{};
        lf.lfHeight = -MulDiv(9, dpi, 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        font = CreateFontIndirectW(&lf);
    }

    st.hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
        S(10), S(10), S(580), S(375), hwnd, (HMENU)(INT_PTR)IDC_ST_TAB, inst, nullptr);
    const wchar_t* names[] = { L"Kết nối", L"Nhật ký", L"Bản quyền", L"Giới thiệu" };
    for (int i = 0; i < 4; ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)names[i];
        TabCtrl_InsertItem(st.hTab, i, &ti);
    }

    auto mk = [&](int tab, const wchar_t* cls, const wchar_t* text, DWORD sty, int x, int y, int w, int h, int id) {
        HWND c = Make(&st, tab, hwnd, cls, text, sty, S(x), S(y), S(w), S(h), id);
        return c;
    };

    // ---- Thẻ 0: Kết nối ----
    mk(0, L"STATIC", L"Địa chỉ IP máy in mặc định:", 0, 30, 60, 300, 20, 0);
    st.hIp = mk(0, WC_IPADDRESSW, L"", WS_TABSTOP, 30, 84, 170, 24, IDC_ST_IP);
    {
        int a = 127, b = 0, c = 0, d = 1;
        sscanf_s(ctx.settings.ip.c_str(), "%d.%d.%d.%d", &a, &b, &c, &d);
        SendMessageW(st.hIp, IPM_SETADDRESS, 0, (LPARAM)MAKEIPADDRESS(a, b, c, d));
    }
    st.hAuto = mk(0, L"BUTTON", L"Tự động kết nối lại khi mất kết nối", WS_TABSTOP | BS_AUTOCHECKBOX, 30, 128, 400, 24, IDC_ST_AUTO);
    SendMessageW(st.hAuto, BM_SETCHECK, ctx.settings.autoReconnect ? BST_CHECKED : BST_UNCHECKED, 0);
    mk(0, L"STATIC", L"Bản tin dùng khi in (tự cập nhật khi chọn ở màn hình chính):", 0, 30, 176, 300, 20, 0);
    st.hMsg = mk(0, L"EDIT", Utf8ToWide(ctx.settings.printMessageName).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 30, 200, 200, 24, IDC_ST_MSG);
    SendMessageW(st.hMsg, EM_SETLIMITTEXT, 15, 0);
    mk(0, L"STATIC", L"Thường chọn bản tin trực tiếp từ danh sách ở màn hình chính; ô này dùng khi cần gõ tay (máy không đọc được danh sách). Để trống = dùng bản tin đang chọn trên máy. Tối đa 15 ký tự ASCII.",
        SS_LEFT, 30, 232, 520, 40, 0);

    // ---- Thẻ 1: Nhật ký ----
    mk(1, L"BUTTON", L"Xem lại nhật ký...", WS_TABSTOP | BS_PUSHBUTTON, 30, 60, 200, 30, IDC_ST_VIEWLOG);
    mk(1, L"BUTTON", L"Mở thư mục nhật ký", WS_TABSTOP | BS_PUSHBUTTON, 245, 60, 200, 30, IDC_ST_OPENDIR);
    mk(1, L"STATIC", L"Giữ file nhật ký tối đa (ngày):", 0, 30, 118, 240, 20, 0);
    st.hKeep = mk(1, L"EDIT", std::to_wstring(ctx.settings.logKeepDays).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER, 280, 114, 80, 24, IDC_ST_KEEP);
    mk(1, L"STATIC", L"Dung lượng nhật ký tối đa (MB):", 0, 30, 154, 240, 20, 0);
    st.hMax = mk(1, L"EDIT", std::to_wstring(ctx.settings.logMaxMB).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER, 280, 150, 80, 24, IDC_ST_MAXMB);
    mk(1, L"STATIC",
        L"Nhật ký kỹ thuật theo ngày (linx_*.log) tự xoá khi quá số ngày hoặc quá dung lượng. Nhật ký sự kiện "
        L"(events_*.csv, xem bằng \"Xem lại nhật ký\") nhỏ và được giữ lâu dài. Nút XÓA NHẬT KÝ ở màn hình chính chỉ xoá phần hiển thị.",
        SS_LEFT, 30, 196, 520, 70, 0);

    // ---- Thẻ 2: Bản quyền ----
    st.hLicInfo = mk(2, L"STATIC", L"", SS_LEFT, 30, 58, 520, 36, IDC_ST_LICINFO);
    mk(2, L"STATIC", L"Mã máy tính:", 0, 30, 104, 100, 20, 0);
    mk(2, L"EDIT", ctx.license ? ctx.license->Machine().c_str() : L"", WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, 135, 100, 220, 24, IDC_ST_MACHINE);
    mk(2, L"STATIC", L"Serial máy in:", 0, 30, 136, 100, 20, 0);
    mk(2, L"EDIT", ctx.printerSerial.empty() ? L"(chưa kết nối máy in)" : ctx.printerSerial.c_str(),
        WS_BORDER | ES_READONLY | ES_AUTOHSCROLL, 135, 132, 220, 24, IDC_ST_PRINTER);
    mk(2, L"BUTTON", L"Sao chép thông tin", WS_TABSTOP | BS_PUSHBUTTON, 370, 114, 180, 30, IDC_ST_COPY);
    mk(2, L"STATIC",
        L"Để gia hạn: bấm \"Sao chép thông tin\", gửi cho nhà cung cấp để mua mã. Mã chỉ dùng được cho đúng máy tính này và "
        L"đúng máy in đang kết nối, mỗi mã chỉ dùng một lần.", SS_LEFT, 30, 170, 520, 40, 0);
    mk(2, L"STATIC", L"Mã gia hạn:", 0, 30, 216, 100, 20, 0);
    st.hCode = mk(2, L"EDIT", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL, 30, 238, 520, 82, IDC_ST_CODE);
    mk(2, L"BUTTON", L"Kích hoạt", WS_TABSTOP | BS_PUSHBUTTON, 440, 328, 110, 30, IDC_ST_ACTIVATE);

    // ---- Thẻ 3: Giới thiệu ----
    st.hAbout = mk(3, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 30, 60, 520, 290, IDC_ST_ABOUT);

    // nút dưới cùng
    Make(&st, -1, hwnd, L"BUTTON", L"Lưu", WS_TABSTOP | BS_DEFPUSHBUTTON, S(390), S(398), S(90), S(30), IDC_ST_SAVE);
    Make(&st, -1, hwnd, L"BUTTON", L"Đóng", WS_TABSTOP | BS_PUSHBUTTON, S(490), S(398), S(90), S(30), IDC_ST_CLOSE);

    // font theo DPI cho mọi control
    for (HWND h = GetWindow(hwnd, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);

    RefreshLicenseInfo(&st);
    RefreshAbout(&st);
    ShowTab(&st, (initialTab >= 0 && initialTab <= 3) ? initialTab : 0);

    RECT pr{}, wr{};
    if (parent && GetWindowRect(parent, &pr) && GetWindowRect(hwnd, &wr))
        SetWindowPos(hwnd, nullptr, pr.left + ((pr.right - pr.left) - (wr.right - wr.left)) / 2,
            pr.top + ((pr.bottom - pr.top) - (wr.bottom - wr.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

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
}
