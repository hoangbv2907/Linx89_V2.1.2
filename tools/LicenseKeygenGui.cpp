// [2026-11-24 R50] Công cụ GUI của NHÀ CUNG CẤP: cấp mã gia hạn bằng cửa sổ Windows (thay cho dòng lệnh LicenseKeygen.exe,
// vốn là chương trình console nên bấm đúp sẽ chớp cửa sổ rồi tắt). KHÔNG phát hành cùng phần mềm.
//   - Chọn khoá riêng (mặc định license_private.key cạnh file .exe).
//   - Dán nguyên thông tin khách gửi (nút "Sao chép thông tin" trong ⚙ Cài đặt › Bản quyền) -> tự tách mã máy + serial.
//   - Số thứ tự mã (seq) tự tăng, lưu trong license_seq.txt; mỗi mã tạo ra được ghi vào issued_codes.csv (UTF-8 có BOM).
//   - Tự kiểm tra lại mã vừa tạo bằng khoá công khai của phần mềm (LicensePublicKey.h) để phát hiện nhầm khoá.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>
#include "license/LicenseCore.h"
#include "license/LicensePublicKey.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

enum {
    IDC_KEYPATH = 100, IDC_BROWSE, IDC_PASTE, IDC_MACHINE, IDC_SERIAL, IDC_HOURS, IDC_SEQ,
    IDC_GEN, IDC_COPY, IDC_OUT, IDC_STATUS, IDC_OPENDIR
};

HWND g_wnd = nullptr;
HFONT g_font = nullptr;
int g_dpi = 96;
int S(int v) { return MulDiv(v, g_dpi, 96); }

std::wstring Text(int id) {
    HWND h = GetDlgItem(g_wnd, id);
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}
void SetStatus(const std::wstring& t) { SetWindowTextW(GetDlgItem(g_wnd, IDC_STATUS), t.c_str()); }
std::wstring Trim(std::wstring s) {
    const wchar_t* ws = L" \t\r\n";
    size_t a = s.find_first_not_of(ws);
    if (a == std::wstring::npos) return L"";
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}
std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring ExeDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/"));
}
bool ReadBin(const std::wstring& path, lic::Bytes& b) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    b.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !b.empty();
}
std::wstring SeqFile() { return ExeDir() + L"\\license_seq.txt"; }
unsigned LoadNextSeq() {
    std::ifstream f(SeqFile());
    unsigned v = 0;
    if (f >> v) return v + 1;
    return 1001;
}
void SaveLastSeq(unsigned v) { std::ofstream f(SeqFile()); f << v << "\n"; }

std::string CsvEscape(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}
void AppendHistory(const std::wstring& machine, const std::string& serial, unsigned hours, unsigned seq, const std::string& code) {
    const std::wstring path = ExeDir() + L"\\issued_codes.csv";
    const bool fresh = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f) return;
    if (fresh) f << "\xEF\xBB\xBF" << "Thoi gian,Ma may tinh,Serial may in,So gio,Seq,Ma\r\n";
    time_t t = time(nullptr); tm lt{}; localtime_s(&lt, &t);
    char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &lt);
    f << ts << "," << CsvEscape(Narrow(machine)) << "," << CsvEscape(serial) << "," << hours << "," << seq << "," << CsvEscape(code) << "\r\n";
}

// Tách mã máy + serial từ đoạn khách gửi: "Mã máy tính: XXXX-...\r\nSerial máy in: XXXXXXXX"
void ParsePaste() {
    const std::wstring all = Text(IDC_PASTE);
    std::wstring machine, serial;
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t e = all.find_first_of(L"\r\n", pos);
        if (e == std::wstring::npos) e = all.size();
        std::wstring line = all.substr(pos, e - pos);
        pos = e + 1;
        const size_t c = line.find_last_of(L':');
        std::wstring val = Trim(c == std::wstring::npos ? line : line.substr(c + 1));
        if (val.empty()) continue;
        if (line.find(L"erial") != std::wstring::npos) serial = val;
        else if (val.size() == 19 && val[4] == L'-') machine = val;
    }
    if (!machine.empty()) SetWindowTextW(GetDlgItem(g_wnd, IDC_MACHINE), machine.c_str());
    if (!serial.empty()) SetWindowTextW(GetDlgItem(g_wnd, IDC_SERIAL), serial.c_str());
}

void CopyText(const std::wstring& t) {
    if (!OpenClipboard(g_wnd)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
    if (g) {
        memcpy(GlobalLock(g), t.c_str(), (t.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

void OnBrowse() {
    wchar_t buf[MAX_PATH]{};
    wcsncpy_s(buf, Text(IDC_KEYPATH).c_str(), _TRUNCATE);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = g_wnd;
    o.lpstrFilter = L"Khóa riêng (*.key)\0*.key\0Tất cả\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = MAX_PATH;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&o)) SetWindowTextW(GetDlgItem(g_wnd, IDC_KEYPATH), buf);
}

void OnGenerate() {
    if (!Trim(Text(IDC_PASTE)).empty()) ParsePaste();   // phòng khi nội dung dán chưa kịp tách
    const std::wstring keyPath = Trim(Text(IDC_KEYPATH));
    std::wstring machine = Trim(Text(IDC_MACHINE));
    const std::string serial = Narrow(Trim(Text(IDC_SERIAL)));
    for (auto& c : machine) c = (wchar_t)towupper(c);
    const unsigned long hours = wcstoul(Trim(Text(IDC_HOURS)).c_str(), nullptr, 10);
    const unsigned long seq = wcstoul(Trim(Text(IDC_SEQ)).c_str(), nullptr, 10);

    lic::Bytes priv;
    if (keyPath.empty() || !ReadBin(keyPath, priv)) { SetStatus(L"Không đọc được file khóa riêng. Bấm \"Chọn...\" để chỉ đúng license_private.key."); return; }
    if (machine.size() != 19) { SetStatus(L"Mã máy tính phải có dạng XXXX-XXXX-XXXX-XXXX (19 ký tự)."); return; }
    if (lic::NormalizeSerial(serial).empty()) { SetStatus(L"Serial máy in đang trống (khách phải KẾT NỐI máy in rồi mới sao chép thông tin)."); return; }
    if (hours == 0 || hours > 100000) { SetStatus(L"Số giờ phải từ 1 đến 100000."); return; }
    if (seq == 0) { SetStatus(L"Số thứ tự mã (seq) phải lớn hơn 0 và KHÔNG trùng mã đã bán."); return; }

    const std::string code = lic::MakeCode(priv, machine, serial, (uint32_t)hours, (uint32_t)seq);
    if (code.empty()) { SetStatus(L"Tạo mã thất bại (khóa riêng không hợp lệ?)."); return; }

    // tự kiểm tra bằng khoá công khai nhúng trong phần mềm
    lic::Bytes pub(kLicensePublicKey, kLicensePublicKey + sizeof(kLicensePublicKey));
    lic::CodeInfo info;
    const lic::CodeResult r = lic::VerifyCode(pub, code, machine, serial, info);
    SetWindowTextW(GetDlgItem(g_wnd, IDC_OUT), std::wstring(code.begin(), code.end()).c_str());
    if (r != lic::CodeResult::Ok) {
        SetStatus(L"CẢNH BÁO: mã không qua kiểm tra bằng khóa công khai của phần mềm - khóa riêng này KHÔNG khớp phần mềm. ĐỪNG gửi mã này!");
        return;
    }
    AppendHistory(machine, serial, (unsigned)hours, (unsigned)seq, code);
    SaveLastSeq((unsigned)seq);
    SetWindowTextW(GetDlgItem(g_wnd, IDC_SEQ), std::to_wstring(seq + 1).c_str());
    CopyText(std::wstring(code.begin(), code.end()));
    SetStatus(L"Đã tạo mã (+" + std::to_wstring(hours) + L" giờ, seq " + std::to_wstring(seq) + L"), kiểm tra hợp lệ, đã sao chép vào clipboard và ghi vào issued_codes.csv.");
}

HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd,
        (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_BROWSE: OnBrowse(); return 0;
        case IDC_GEN: OnGenerate(); return 0;
        case IDC_COPY: CopyText(Text(IDC_OUT)); SetStatus(L"Đã sao chép mã vào clipboard."); return 0;
        case IDC_OPENDIR: ShellExecuteW(h, L"open", ExeDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
        case IDC_PASTE: if (HIWORD(w) == EN_CHANGE || HIWORD(w) == EN_KILLFOCUS) ParsePaste(); return 0;
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX ic{ sizeof(ic), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&ic);
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);

    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, g_dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g_font = CreateFontIndirectW(&lf);

    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"LinxKeygenGui";
    RegisterClassW(&wc);

    RECT rc{ 0, 0, S(600), S(470) };
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Linx Controller - Cấp mã gia hạn (nhà cung cấp)", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);

    Mk(L"STATIC", L"Khóa riêng (license_private.key):", 0, 15, 12, 400, 18, 0);
    const std::wstring defKey = ExeDir() + L"\\license_private.key";
    Mk(L"EDIT", defKey.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 15, 32, 465, 24, IDC_KEYPATH);
    Mk(L"BUTTON", L"Chọn...", WS_TABSTOP | BS_PUSHBUTTON, 490, 31, 95, 26, IDC_BROWSE);

    Mk(L"STATIC", L"Dán thông tin khách gửi (từ nút \"Sao chép thông tin\") - tự tách mã máy và serial:", 0, 15, 68, 570, 18, 0);
    Mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 15, 88, 570, 52, IDC_PASTE);

    Mk(L"STATIC", L"Mã máy tính:", 0, 15, 152, 120, 18, 0);
    Mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_UPPERCASE, 15, 172, 270, 24, IDC_MACHINE);
    Mk(L"STATIC", L"Serial máy in:", 0, 305, 152, 120, 18, 0);
    Mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_UPPERCASE, 305, 172, 280, 24, IDC_SERIAL);

    Mk(L"STATIC", L"Số giờ cấp thêm:", 0, 15, 208, 140, 18, 0);
    Mk(L"EDIT", L"6000", WS_BORDER | WS_TABSTOP | ES_NUMBER, 15, 228, 130, 24, IDC_HOURS);
    Mk(L"STATIC", L"Số thứ tự mã (seq, tự tăng, không được trùng):", 0, 175, 208, 360, 18, 0);
    Mk(L"EDIT", std::to_wstring(LoadNextSeq()).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER, 175, 228, 130, 24, IDC_SEQ);

    Mk(L"BUTTON", L"Tạo mã", WS_TABSTOP | BS_DEFPUSHBUTTON, 15, 268, 150, 32, IDC_GEN);
    Mk(L"BUTTON", L"Sao chép mã", WS_TABSTOP | BS_PUSHBUTTON, 175, 268, 150, 32, IDC_COPY);
    Mk(L"BUTTON", L"Mở thư mục lịch sử", WS_TABSTOP | BS_PUSHBUTTON, 435, 268, 150, 32, IDC_OPENDIR);

    Mk(L"STATIC", L"Mã gia hạn (gửi nguyên cho khách):", 0, 15, 312, 400, 18, 0);
    Mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 15, 332, 570, 84, IDC_OUT);
    Mk(L"STATIC", L"Chưa tạo mã.", SS_LEFT, 15, 424, 570, 40, IDC_STATUS);

    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DeleteObject(g_font);
    return (int)msg.wParam;
}
