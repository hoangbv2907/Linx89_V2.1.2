#include "ui/dialogs/LogViewerDialog.h"
#include "logging/EventLog.h"
#include "settings/AppPaths.h"
#include <commdlg.h>
#include <shellapi.h>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

namespace {

enum {
    IDC_LV_MONTH = 3001, IDC_LV_LEVEL, IDC_LV_DAY, IDC_LV_KEY, IDC_LV_APPLY,
    IDC_LV_TEXT, IDC_LV_COUNT, IDC_LV_EXPORT, IDC_LV_FOLDER, IDC_LV_CLOSE
};

struct ViewState {
    bool done = false;
    HWND hMonth = nullptr, hLevel = nullptr, hDay = nullptr, hKey = nullptr, hText = nullptr, hCount = nullptr;
    std::vector<EventLog::Entry> loaded;     // cả tháng đang chọn
    std::vector<EventLog::Entry> shown;      // sau khi lọc
};

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

std::wstring MonthSelected(ViewState* st) {
    int i = (int)SendMessageW(st->hMonth, CB_GETCURSEL, 0, 0);
    if (i < 0) return L"";
    wchar_t buf[32] = { 0 };
    SendMessageW(st->hMonth, CB_GETLBTEXT, i, (LPARAM)buf);
    return buf;
}

void ApplyFilter(ViewState* st) {
    st->shown = EventLog::Filter(st->loaded, (int)SendMessageW(st->hLevel, CB_GETCURSEL, 0, 0),
        GetText(st->hDay), GetText(st->hKey));
    std::wstring text;
    text.reserve(st->shown.size() * 80);
    for (const auto& e : st->shown) {
        text += e.time + L"  [" + EventLog::LevelName(e.level) + L"]  " + e.text + L"\r\n";
    }
    SetWindowTextW(st->hText, text.c_str());
    SendMessageW(st->hText, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);   // cuộn xuống dòng mới nhất
    SendMessageW(st->hText, EM_SCROLLCARET, 0, 0);
    std::wstring c = std::to_wstring(st->shown.size()) + L" / " + std::to_wstring(st->loaded.size()) + L" dòng";
    SetWindowTextW(st->hCount, c.c_str());
}

void LoadMonth(ViewState* st) {
    const std::wstring m = MonthSelected(st);
    st->loaded = m.empty() ? std::vector<EventLog::Entry>() : EventLog::Load(m);
    ApplyFilter(st);
}

void ExportCsv(HWND hwnd, ViewState* st) {
    wchar_t file[MAX_PATH] = L"nhat_ky.csv";
    const std::wstring m = MonthSelected(st);
    if (!m.empty()) { std::wstring n = L"nhat_ky_" + m + L".csv"; wcsncpy_s(file, n.c_str(), _TRUNCATE); }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    std::ofstream f(file, std::ios::binary);
    if (!f) { MessageBoxW(hwnd, L"Không ghi được file.", L"Xuất CSV", MB_ICONWARNING); return; }
    f.write("\xEF\xBB\xBF", 3);
    auto u8 = [](const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string o(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
        return o;
    };
    f << u8(L"Thời gian,Mức độ,Nội dung") << "\n";
    for (const auto& e : st->shown) f << u8(EventLog::ToCsvLine(e)) << "\n";
    MessageBoxW(hwnd, (L"Đã xuất " + std::to_wstring(st->shown.size()) + L" dòng.").c_str(), L"Xuất CSV", MB_ICONINFORMATION);
}

LRESULT CALLBACK ViewProc(HWND hwnd, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<ViewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (m) {
    case WM_COMMAND: {
        if (!st) break;
        const int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_LV_MONTH && code == CBN_SELCHANGE) LoadMonth(st);
        else if (id == IDC_LV_LEVEL && code == CBN_SELCHANGE) ApplyFilter(st);
        else if (id == IDC_LV_APPLY) ApplyFilter(st);
        else if ((id == IDC_LV_DAY || id == IDC_LV_KEY) && code == EN_CHANGE) ApplyFilter(st);
        else if (id == IDC_LV_EXPORT) ExportCsv(hwnd, st);
        else if (id == IDC_LV_FOLDER) ShellExecuteW(hwnd, L"open", LinxLogDir().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        else if (id == IDC_LV_CLOSE) { st->done = true; DestroyWindow(hwnd); }
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

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    return c;
}

} // namespace

void LogViewerDialog::Run(HWND parent) {
    static bool registered = false;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ViewProc;
        wc.hInstance = inst;
        wc.lpszClassName = L"LinxLogViewer";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc)) return;
        registered = true;
    }
    ViewState st;
    RECT rc{ 0, 0, 760, 520 };
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_DLGMODALFRAME);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LinxLogViewer", L"Xem lại nhật ký", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, inst, nullptr);
    if (!hwnd) return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    Make(hwnd, L"STATIC", L"Tháng:", 0, 12, 15, 50, 20, 0);
    st.hMonth = Make(hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 65, 11, 95, 200, IDC_LV_MONTH);
    Make(hwnd, L"STATIC", L"Mức độ:", 0, 172, 15, 55, 20, 0);
    st.hLevel = Make(hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 230, 11, 150, 200, IDC_LV_LEVEL);
    Make(hwnd, L"STATIC", L"Ngày (YYYY-MM-DD):", 0, 392, 15, 125, 20, 0);
    st.hDay = Make(hwnd, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 520, 11, 100, 23, IDC_LV_DAY);
    Make(hwnd, L"STATIC", L"Từ khoá:", 0, 12, 48, 55, 20, 0);
    st.hKey = Make(hwnd, L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 70, 44, 550, 23, IDC_LV_KEY);
    Make(hwnd, L"BUTTON", L"Lọc", WS_TABSTOP | BS_DEFPUSHBUTTON, 635, 43, 110, 25, IDC_LV_APPLY);
    st.hText = Make(hwnd, L"EDIT", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
        12, 78, 733, 380, IDC_LV_TEXT);
    st.hCount = Make(hwnd, L"STATIC", L"", 0, 12, 470, 200, 20, IDC_LV_COUNT);
    Make(hwnd, L"BUTTON", L"Xuất CSV...", WS_TABSTOP | BS_PUSHBUTTON, 395, 466, 110, 28, IDC_LV_EXPORT);
    Make(hwnd, L"BUTTON", L"Mở thư mục", WS_TABSTOP | BS_PUSHBUTTON, 515, 466, 110, 28, IDC_LV_FOLDER);
    Make(hwnd, L"BUTTON", L"Đóng", WS_TABSTOP | BS_PUSHBUTTON, 635, 466, 110, 28, IDC_LV_CLOSE);

    SendMessageW(st.hLevel, CB_ADDSTRING, 0, (LPARAM)L"Tất cả");
    SendMessageW(st.hLevel, CB_ADDSTRING, 0, (LPARAM)L"Cảnh báo và lỗi");
    SendMessageW(st.hLevel, CB_ADDSTRING, 0, (LPARAM)L"Chỉ lỗi");
    SendMessageW(st.hLevel, CB_SETCURSEL, 0, 0);
    for (const auto& mo : EventLog::ListMonths()) SendMessageW(st.hMonth, CB_ADDSTRING, 0, (LPARAM)mo.c_str());
    if (SendMessageW(st.hMonth, CB_GETCOUNT, 0, 0) > 0) SendMessageW(st.hMonth, CB_SETCURSEL, 0, 0);
    else SetWindowTextW(st.hText, L"(Chưa có nhật ký nào được lưu)");
    LoadMonth(&st);

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
        if (r == 0) { PostQuitMessage((int)msg.wParam); break; }   // WM_QUIT: trả lại cho vòng lặp chính
        if (r < 0) break;
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (parent) { EnableWindow(parent, TRUE); SetForegroundWindow(parent); }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
}
