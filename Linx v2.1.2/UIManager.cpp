#include "UIManager.h"
#include "FontManager.h"
#include "Logger.h"
#include "EventLog.h"
#include "UILayout.h"
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include "CommonTypes.h"
// ================== Lifecycle ==================
UIManager::UIManager() {}

UIManager::~UIManager() {}

// ================== Init / Create UI ==================
bool UIManager::Initialize(HWND hParent) {
    hParent_ = hParent;
	// Đảm bảo rằng thư viện Rich Edit được tải
    LoadLibraryW(L"Msftedit.dll");
	// Khởi tạo các điều khiển tùy chỉnh
    toggleSwitch_ = std::make_unique<ToggleSwitch>();
    messageLogger_ = std::make_unique<MessageLogger>();

    return true;
}
// Tạo và cấu hình tất cả các điều khiển giao diện người dùng
void UIManager::CreateControls() {
    if (!hParent_) return;
    // [2026-11-22 R43] DPI của cửa sổ: font + kích thước control co theo DPI; vị trí do Layout() đặt theo kích thước thật.
    dpi_ = (int)GetDpiForWindow(hParent_);
    if (dpi_ < 48) dpi_ = 96;
    FontManager::GetInstance().SetDpi(dpi_);

    // IP Address + toggle + biểu tượng Cài đặt (gộp Gia hạn / Nhật ký / Kết nối vào đây)
    lblIp_ = CreateStatic(0, 0, 10, 10, L"Địa chỉ IP:");
    // Control IP tính bố cục các ô con lúc tạo -> tạo với kích thước thật (theo DPI), Layout() chỉ đặt lại vị trí
    hIpAddress_ = CreateWindowW(WC_IPADDRESSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, MulDiv(150, dpi_, 96), MulDiv(23, dpi_, 96), hParent_, NULL, NULL, NULL);
    toggleSwitch_->Create(hParent_, 0, 0, 60, 26, IDC_TOGGLE);
    hBtnSettings_ = CreateButton(0, 0, 36, 30, L"⚙", IDC_BTN_SETTINGS);
    // Nội dung in
    lblContent_ = CreateStatic(0, 0, 10, 10, L"Nội dung in:");
    hInputValue_ = CreateEdit(0, 0, 10, 10, L"");
    hBtnUpload_ = CreateButton(0, 0, 10, 10, L"Upload", IDC_BTN_UPLOAD);
    hBtnDownload_ = CreateButton(0, 0, 10, 10, L"Dowload", IDC_BTN_DOWNLOAD);   // [2026-11-18 R36]
    hFieldInfo_ = CreateStatic(0, 0, 10, 10, L"Remote field: (chưa đọc từ máy - bấm Tải Về)");
    // [2026-11-22 R44] Chọn bản tin từ danh sách trên máy (97D)
    lblMessage_ = CreateStatic(0, 0, 10, 10, L"Bản tin:");
    hMessageCombo_ = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        0, 0, 10, 10, hParent_, (HMENU)(INT_PTR)IDC_MSG_COMBO, NULL, NULL);
    hBtnMsgRefresh_ = CreateButton(0, 0, 10, 10, L"Làm mới", IDC_BTN_MSGREFRESH);
    // Số lượng
    lblCount_ = CreateStatic(0, 0, 10, 10, L"Số lượng:");
    hCount_ = CreateEdit(0, 0, 10, 10, L"1");
    // Hiển thị số lượng đã đạt được
    lblReached_ = CreateStatic(0, 0, 10, 10, L"Đã đạt được:");
    hCurrentCount_ = CreateWindowW(L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 10, 10, hParent_, (HMENU)IDC_CURRENT_COUNT, NULL, NULL);
    // [2026-11-18 R37] In bù (+) / trừ (-) khi máy đang chạy
    lblAdjust_ = CreateStatic(0, 0, 10, 10, L"Bù/Trừ (+/-):");
    hAdjust_ = CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 10, 10, hParent_, NULL, NULL, NULL);
    hBtnAdjust_ = CreateButton(0, 0, 10, 10, L"Điều chỉnh", IDC_BTN_ADJUST);
    // Modern buttons
    btnStart_.Create(hParent_, 0, 0, 10, 10, L"KHỞI ĐỘNG", IDC_BTN_START);
    btnPrint_.Create(hParent_, 0, 0, 10, 10, L"IN", IDC_BTN_PRINT);
    btnStop_.Create(hParent_, 0, 0, 10, 10, L"DỪNG", IDC_BTN_STOP);
    btnClear_.Create(hParent_, 0, 0, 10, 10, L"XÓA NHẬT KÝ", IDC_BTN_CLEAR);
    btnSet_.Create(hParent_, 0, 0, 10, 10, L"SET", IDC_BTN_SET);
    // Subclass các nút
    btnStart_.Subclass();
    btnPrint_.Subclass();
    btnStop_.Subclass();
    btnClear_.Subclass();
    btnSet_.Subclass();
    // Status display
    hStatusDisplay_ = CreateStatic(0, 0, 10, 10, L"CHƯA KẾT NỐI");
    SetWindowLongPtr(hStatusDisplay_, GWL_STYLE, GetWindowLongPtr(hStatusDisplay_, GWL_STYLE) | SS_CENTER);
    // [2026-11-18 R35] Cảnh báo/lỗi của máy in
    lblWarn_ = CreateStatic(0, 0, 10, 10, L"CẢNH BÁO MÁY IN:");
    hWarnings_ = CreateWindowW(L"EDIT", L"(không có)", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 0, 0, 10, 10, hParent_, NULL, NULL, NULL);
    // Log box
    lblLog_ = CreateStatic(0, 0, 10, 10, L"NHẬT KÝ:");
    messageLogger_->Create(hParent_, 0, 0, 10, 10);

    // Gợi ý khi rê chuột vào biểu tượng Cài đặt
    hTip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hParent_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hTip_) {
        TTTOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hParent_;
        ti.uId = (UINT_PTR)hBtnSettings_;
        ti.lpszText = (LPWSTR)L"Cài đặt: kết nối, nhật ký, gia hạn bản quyền";
        SendMessageW(hTip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    // Apply fonts
    ApplyFontToAllControls();
    // Set default IP
    SendMessage(hIpAddress_, IPM_SETADDRESS, 0, (LPARAM)MAKEIPADDRESS(127, 0, 0, 1));
    // Cập nhật giao diện theo trạng thái ban đầu
    PrinterState initialState;
    initialState.status = PrinterStateType::Disconnected;
    initialState.statusText = L"Chưa kết nối";
    UpdateButtonStates(initialState);
    UpdatePrinterUIState(initialState);
    // Đặt vị trí / kích thước mọi control theo kích thước client hiện tại
    RECT rc{};
    GetClientRect(hParent_, &rc);
    Layout(rc.right, rc.bottom);
}

// [2026-11-22 R43] Đặt lại vị trí / kích thước mọi control khi cửa sổ đổi kích thước (WM_SIZE) hoặc đổi DPI.
void UIManager::Layout(int clientW, int clientH) {
    if (!hIpAddress_ || !messageLogger_ || !messageLogger_->GetHandle() || clientW <= 0 || clientH <= 0) return;
    const UiRects r = ComputeLayout(clientW, clientH, dpi_);
    HDWP dwp = BeginDeferWindowPos(32);
    auto mv = [&](HWND c, const RECT& rc) {
        if (!c || !dwp) return;
        dwp = DeferWindowPos(dwp, c, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    mv(lblIp_, r.lblIp);          mv(hIpAddress_, r.ip);
    mv(toggleSwitch_ ? toggleSwitch_->GetHandle() : nullptr, r.toggle);
    mv(hBtnSettings_, r.settings);
    mv(lblContent_, r.lblContent); mv(hInputValue_, r.content);
    mv(hBtnUpload_, r.upload);    mv(hBtnDownload_, r.download);   mv(hFieldInfo_, r.fieldInfo);
    mv(lblMessage_, r.lblMessage); mv(hBtnMsgRefresh_, r.msgRefresh);
    if (hMessageCombo_ && dwp) {   // chiều cao của combobox gồm cả phần danh sách thả xuống
        RECT cr = r.message;
        cr.bottom = cr.top + MulDiv(220, dpi_, 96);
        mv(hMessageCombo_, cr);
    }
    mv(lblCount_, r.lblCount);    mv(hCount_, r.count);
    mv(lblReached_, r.lblReached); mv(hCurrentCount_, r.reached);  mv(btnSet_.GetHandle(), r.set);
    mv(lblAdjust_, r.lblAdjust);  mv(hAdjust_, r.adjust);          mv(hBtnAdjust_, r.adjustBtn);
    mv(btnStart_.GetHandle(), r.start); mv(btnPrint_.GetHandle(), r.print); mv(btnStop_.GetHandle(), r.stop);
    mv(hStatusDisplay_, r.status);
    mv(lblWarn_, r.lblWarn);      mv(hWarnings_, r.warn);
    mv(lblLog_, r.lblLog);        mv(btnClear_.GetHandle(), r.clear);
    mv(messageLogger_->GetHandle(), r.log);
    if (dwp) EndDeferWindowPos(dwp);
    InvalidateRect(hParent_, nullptr, TRUE);
}

// [2026-11-22 R43] Đổi DPI (kéo cửa sổ sang màn hình khác tỉ lệ): tạo lại font; WindowManager gọi Layout() sau đó.
void UIManager::SetDpi(int dpi) {
    if (dpi < 48) dpi = 96;
    dpi_ = dpi;
    FontManager::GetInstance().SetDpi(dpi);
    ApplyFontToAllControls();
}

// [2026-11-22 R44] Đổ danh sách bản tin vào ô chọn và chọn đúng bản tin; không đổi khi người dùng đang mở danh sách
// hoặc vừa chọn (đợi máy xác nhận) để ô chọn không bị giật.
void UIManager::UpdateMessageList(const PrinterState& state) {
    if (!hMessageCombo_) return;
    if (SendMessageW(hMessageCombo_, CB_GETDROPPEDSTATE, 0, 0)) return;
    if (state.messages != lastMessages_) {
        lastMessages_ = state.messages;
        SendMessageW(hMessageCombo_, CB_RESETCONTENT, 0, 0);
        for (const auto& m : lastMessages_) SendMessageW(hMessageCombo_, CB_ADDSTRING, 0, (LPARAM)m.c_str());
    }
    if (GetTickCount64() - lastPickTick_ < 1500) return;
    int want = -1;
    for (size_t i = 0; i < lastMessages_.size(); ++i)
        if (_wcsicmp(lastMessages_[i].c_str(), state.selectedMessage.c_str()) == 0) { want = (int)i; break; }
    if ((int)SendMessageW(hMessageCombo_, CB_GETCURSEL, 0, 0) != want) SendMessageW(hMessageCombo_, CB_SETCURSEL, want, 0);
}

std::wstring UIManager::GetSelectedMessage() {
    lastPickTick_ = GetTickCount64();
    if (!hMessageCombo_) return L"";
    const int i = (int)SendMessageW(hMessageCombo_, CB_GETCURSEL, 0, 0);
    if (i < 0) return L"";
    const int n = (int)SendMessageW(hMessageCombo_, CB_GETLBTEXTLEN, i, 0);
    if (n <= 0) return L"";
    std::wstring s(n + 1, L'\0');
    SendMessageW(hMessageCombo_, CB_GETLBTEXT, i, (LPARAM)&s[0]);
    s.resize(n);
    return s;
}

// [2026-11-22 R43] Đặt địa chỉ IP vào ô IP (từ cài đặt đã lưu). Sai định dạng thì bỏ qua.
void UIManager::SetIPAddress(const std::wstring& ip) {
    if (!hIpAddress_) return;
    int a = 0, b = 0, c = 0, d = 0;
    if (swscanf_s(ip.c_str(), L"%d.%d.%d.%d", &a, &b, &c, &d) != 4) return;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return;
    SendMessage(hIpAddress_, IPM_SETADDRESS, 0, (LPARAM)MAKEIPADDRESS(a, b, c, d));
}

// ================== Log / Messaging ==================
// Append 1 dòng log vào MessageLogger. level dùng để tô màu/biểu tượng (info/warn/error...)
void UIManager::AddMessage(const std::wstring& text, int level) {
    EventLog::Append(level, text);   // [2026-11-22 R41] lưu mọi dòng hiện trên màn hình để xem lại
    if (messageLogger_) {
        messageLogger_->AddMessage(text, level);
    }
}
// Xóa toàn bộ log trong MessageLogger.
void UIManager::ClearMessages() {
	if (messageLogger_) {   // Kiểm tra con trỏ thông điệp nhật ký không rỗng trước khi sử dụng
		messageLogger_->Clear();    // Xóa tất cả thông điệp khỏi nhật ký
    }
}

// ================== UI updates from App state ==================
// Cập nhật text trạng thái (ví dụ: "Connected", "Printing", "Error: ...") lên label status.
void UIManager::UpdatePrinterStatus(const std::wstring& text) {
	if (hStatusDisplay_) {  // Kiểm tra handle của dòng trạng thái không rỗng trước khi sử dụng
		SetWindowTextW(hStatusDisplay_, text.c_str());  // Cập nhật văn bản của dòng trạng thái
    }
}

// Cập nhật "UI skin/state" theo trạng thái máy: đổi label, icon, màu nền, highlight...
void UIManager::UpdatePrinterUIState(PrinterState state){
    // Không làm gì nếu không đổi trạng thái
    bool statusChanged = (state.status != lastState_);
    lastState_ = state.status;
    std::wstring stateText = L"TRẠNG THÁI: ";
    COLORREF bgColor = RGB(240, 240, 240); // default xám
    switch (state.status)
    {
    case PrinterStateType::Disconnected:
        stateText += L"CHƯA KẾT NỐI";
        bgColor = RGB(240, 240, 240); // xám
        break;
    case PrinterStateType::Idle:
    case PrinterStateType::Connected:
        stateText += L"ĐÃ KẾT NỐI";
        bgColor = RGB(102, 205, 170); // xanh lá
        break;
    case PrinterStateType::Reconnecting:
    case PrinterStateType::Connecting:
        stateText += L"ĐANG KẾT NỐI...";
        bgColor = RGB(250, 230, 150); // vàng nhạt
        break;

    case PrinterStateType::Ready: // jetOn
        stateText += L"JET ON";
        bgColor = RGB(32, 178, 170); // xanh dương nhạt
        break;

    case PrinterStateType::Printing:
        stateText += L"ĐANG IN...";
        bgColor = RGB(150, 210, 255); // xanh dương nhạt
        break;

    case PrinterStateType::Error:
        stateText += L"LỖI MÁY IN";
        bgColor = RGB(255, 140, 140); // đỏ
        break;
    }
    // Cập nhật text trạng thái
    if (hPrinterState_)
        SetWindowTextW(hPrinterState_, stateText.c_str());
    // ⭐ TỐI ƯU: KHÔNG ĐỔI BRUSH NẾU MÀU KHÔNG ĐỔI
    static COLORREF lastColor = RGB(255, 255, 255);
    if (bgColor == lastColor) return;
    lastColor = bgColor;
    // Đổi nền cửa sổ
    HBRUSH brush = CreateSolidBrush(bgColor);
    SetClassLongPtr(hParent_, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
    InvalidateRect(hParent_, NULL, TRUE);
}

// Wrapper để cập nhật enable/disable của các nút theo trạng thái máy.
void UIManager::UpdateButtonStates(PrinterState state) {
	UpdateButtonStateForPrinterState(state);    // Cập nhật trạng thái nút dựa trên trạng thái máy in
}

// Cập nhật các trường job: nội dung in, target count, printed count.
void UIManager::UpdateJobFields(const std::wstring& content, int target, int printed){
    // 0) Nếu user đang sửa và đã dirty, tuyệt đối không overwrite
    if (editingJobFields_ && jobDirtyByUser_) {
        // nhưng vẫn cho phép cập nhật printedCount nếu bạn muốn:
        if (hCurrentCount_ && printed != lastPrinted_) {
            lastPrinted_ = printed;
            SetWindowTextW(hCurrentCount_, std::to_wstring(printed).c_str());
        }
        return;
    }
    // 1) Nếu nội dung giống hệt lần trước thì bỏ qua để chống spam
    if (content == lastJobContent_ && target == lastTarget_ && printed == lastPrinted_) {
        return;
    }
    // 2) Chỉ SetWindowText cho field nào THẬT SỰ đổi
    if (hInputValue_ && content != lastJobContent_) {
        lastJobContent_ = content;
        SetWindowTextW(hInputValue_, content.c_str());
    }
    if (hCount_ && target != lastTarget_) {
        lastTarget_ = target;
        SetWindowTextW(hCount_, std::to_wstring(target).c_str());
    }
    if (hCurrentCount_ && printed != lastPrinted_) {
        lastPrinted_ = printed;
        SetWindowTextW(hCurrentCount_, std::to_wstring(printed).c_str());
    }
}

// [2026-11-18 R35] Hiển thị cảnh báo/lỗi của máy in lên ô CẢNH BÁO.
void UIManager::UpdateWarnings(const std::vector<std::wstring>& warnings) {
    if (!hWarnings_) return;
    std::wstring text;
    for (const auto& w : warnings) { if (!text.empty()) text += L"\r\n"; text += w; }
    if (text.empty()) text = L"(không có)";
    if (text == lastWarningsText_) return;   // không đổi thì không vẽ lại
    lastWarningsText_ = text;
    SetWindowTextW(hWarnings_, text.c_str());
}

// [2026-11-18 R33/R36] Nhãn remote field + giới hạn ô nhập theo kích thước field (dữ liệu phải ĐÚNG bằng kích thước này).
void UIManager::UpdateRemoteFieldInfo(const PrinterState& state, const std::wstring& messageName) {
    if (hFieldInfo_) {
        std::wstring t = L"Remote field: " + state.remoteFieldName;
        t += (state.remoteFieldMax > 0) ? (L" - " + std::to_wstring(state.remoteFieldMax) + L" ký tự") : L" - (chưa rõ kích thước, bấm Tải Về)";
        if (!state.messageName.empty()) t += L" | Message: " + state.messageName;
        if (!state.selectedMessage.empty() && _wcsicmp(state.selectedMessage.c_str(), state.messageName.c_str()) != 0)
            t += L" | sẽ tải '" + state.selectedMessage + L"' khi in";
        if (t != lastFieldInfoText_) { lastFieldInfoText_ = t; SetWindowTextW(hFieldInfo_, t.c_str()); }
    }
    if (hInputValue_ && state.remoteFieldMax != lastFieldMax_) {
        lastFieldMax_ = state.remoteFieldMax;
        SendMessage(hInputValue_, EM_SETLIMITTEXT, state.remoteFieldMax > 0 ? (WPARAM)state.remoteFieldMax : 0, 0);
    }
    (void)messageName;
}

// [2026-11-18 R37] Đọc độ lệch in bù/trừ (cho phép dấu + hoặc -).
int UIManager::GetAdjustValue(bool& ok) const {
    ok = false;
    if (!hAdjust_) return 0;
    wchar_t buf[24] = { 0 };
    GetWindowTextW(hAdjust_, buf, 24);
    wchar_t* end = nullptr;
    long v = wcstol(buf, &end, 10);
    if (end == buf || (end && *end != 0L)) return 0;
    ok = true;
    return (int)v;
}

// ================== Toggle switch ==================
// Set ON/OFF cho toggle trên UI (ví dụ: Connected = ON).
void UIManager::SetToggleState(bool state) {
    Logger::GetInstance().Write(L"UIManager::SetToggleState: " +
        std::wstring(state ? L"ON" : L"OFF") +
        L", toggleSwitch_ exists: " + std::wstring(toggleSwitch_ ? L"YES" : L"NO"));
    isToggleOn_ = state;    // Cập nhật trạng thái bên trong
    if (toggleSwitch_) {    // Kiểm tra con trỏ toggle không rỗng trước khi sử dụng
        toggleSwitch_->SetState(state); // Cập nhật trạng thái của toggle switch
    }
}

// Đọc trạng thái toggle hiện tại (UI state) - không phải trạng thái socket thật.
bool UIManager::IsToggleOn() const {
    return isToggleOn_;
}

// ================== Data access (read user inputs) ==================
// Lấy IP từ edit box (hIpAddress_).
std::wstring UIManager::GetIPAddress() const {
	if (!hIpAddress_) return L"";   // Kiểm tra handle của điều khiển IP Address không rỗng trước khi sử dụng
	DWORD ip = 0;   // Biến để lưu địa chỉ IP
	SendMessage(hIpAddress_, IPM_GETADDRESS, 0, (LPARAM)&ip);   // Lấy địa chỉ IP từ điều khiển
	BYTE a = (ip >> 0) & 0xFF;  // Trích xuất từng byte của địa chỉ IP
    BYTE b = (ip >> 8) & 0xFF;
    BYTE c = (ip >> 16) & 0xFF;
    BYTE d = (ip >> 24) & 0xFF;
	wchar_t buf[32];    // Bộ đệm để định dạng địa chỉ IP
	swprintf_s(buf, L"%d.%d.%d.%d", d, c, b, a);    // Định dạng địa chỉ IP thành chuỗi
	return std::wstring(buf);   // Trả về địa chỉ IP dưới dạng chuỗi
}

// Lấy nội dung từ edit box nội dung in (hInputValue_).
std::wstring UIManager::GetInputText() const {
	if (!hInputValue_) return L"";  // Kiểm tra handle của điều khiển nhập liệu không rỗng trước khi sử dụng
	int len = GetWindowTextLengthW(hInputValue_);   // Lấy độ dài văn bản trong điều khiển
	std::wstring s(len + 1, L'\0'); // Tạo chuỗi với kích thước đủ lớn để chứa văn bản
	GetWindowTextW(hInputValue_, &s[0], (int)s.size()); // Lấy văn bản từ điều khiển
	s.resize(len);  // Thay đổi kích thước chuỗi để loại bỏ ký tự null thừa
	return s;   // Trả về văn bản nhập dưới dạng chuỗi
}

// Lấy số lượng in từ edit box count (hCount_).
int UIManager::GetCountValue() const {
	if (!hCount_) return 1; // Kiểm tra handle của điều khiển số lượng không rỗng trước khi sử dụng
	wchar_t buf[16] = { 0 };    // Bộ đệm để lưu văn bản số lượng
	GetWindowTextW(hCount_, buf, 16);   // Lấy văn bản từ điều khiển số lượng
	return _wtoi(buf);  // Chuyển đổi văn bản thành số nguyên và trả về
}

// Kiểm tra dữ liệu nhập hợp lệ (IP đúng định dạng, count > 0...)
bool UIManager::ValidateInput() const {
	std::wstring ip = GetIPAddress();   // Lấy địa chỉ IP từ điều khiển
	return !ip.empty() && ip != L"0.0.0.0" && ip != L"..";  // Kiểm tra địa chỉ IP không rỗng và không phải là địa chỉ mặc định không hợp lệ
}

// Xử lý WM_DRAWITEM: vẽ các controls kiểu owner-draw (ModernButton/ToggleSwitch).
    // WindowManager sẽ gọi hàm này từ HandleDrawItem().
void UIManager::HandleOwnerDraw(LPDRAWITEMSTRUCT dis) {
	if (!dis) return;   // Kiểm tra con trỏ DRAWITEMSTRUCT không rỗng trước khi sử dụng
	// Xác định điều khiển dựa trên CtlID và gọi hàm vẽ tương ứng
    switch (dis->CtlID) {
    case IDC_TOGGLE:
		if (toggleSwitch_) toggleSwitch_->Draw(dis);    // Vẽ toggle switch
        break;
	case IDC_BTN_START: // Vẽ nút Start
        btnStart_.Draw(dis);
        break;
	case IDC_BTN_PRINT: // Vẽ nút Print
        btnPrint_.Draw(dis);
        break;  
	case IDC_BTN_STOP:  // Vẽ nút Stop
        btnStop_.Draw(dis);
        break;
	case IDC_BTN_SET:   // Vẽ nút Set
        btnSet_.btnSetDraw(dis);
        break;  
	case IDC_BTN_CLEAR: // Vẽ nút Clear
        btnClear_.Draw(dis);
        break;
    default:
        break;
    }
}

// ================== Helper methods (control creation / layout) ==================
// Tạo static label đơn giản (WS_CHILD | WS_VISIBLE)
HWND UIManager::CreateStatic(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, hParent_, NULL, NULL, NULL);
}

// Tạo button chuẩn Win32 (nếu không dùng ModernButton cho nút đó)
HWND UIManager::CreateButton(int x, int y, int w, int h, const wchar_t* text, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, hParent_, (HMENU)(UINT_PTR)id, NULL, NULL);
}

// Tạo edit box (cho IP/content/count). text mặc định trống.
HWND UIManager::CreateEdit(int x, int y, int w, int h, const wchar_t* text) {
    return CreateWindowW(L"EDIT", text, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
        x, y, w, h, hParent_, NULL, NULL, NULL);
}

// Apply font thống nhất lên tất cả controls để UI đồng bộ.
void UIManager::ApplyFontToAllControls() {
    HFONT defaultFont = FontManager::GetInstance().GetDefaultFont();    // Lấy font mặc định từ FontManager
    // [2026-11-22 R43] Gán font cho MỌI control con (kể cả nhãn) để co theo DPI; công tắc và biểu tượng Cài đặt dùng font riêng.
    struct Ctx { HFONT font; HWND toggle; HWND gear; HWND log; } ctx{
        defaultFont, toggleSwitch_ ? toggleSwitch_->GetHandle() : nullptr, hBtnSettings_,
        messageLogger_ ? messageLogger_->GetHandle() : nullptr };
    // Chỉ các control con TRỰC TIẾP (không đụng vào ô con bên trong control IP, tránh hỏng cách bố trí của nó)
    for (HWND h = GetWindow(hParent_, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h != ctx.toggle && h != ctx.gear && h != ctx.log && ctx.font)
            SendMessage(h, WM_SETFONT, (WPARAM)ctx.font, TRUE);
    }
    if (ctx.toggle && FontManager::GetInstance().GetToggleFont())
        SendMessage(ctx.toggle, WM_SETFONT, (WPARAM)FontManager::GetInstance().GetToggleFont(), TRUE);
    if (hBtnSettings_ && FontManager::GetInstance().GetIconFont())
        SendMessage(hBtnSettings_, WM_SETFONT, (WPARAM)FontManager::GetInstance().GetIconFont(), TRUE);
    if (messageLogger_) {
        messageLogger_->SetFont(defaultFont);
    }
}

// Core logic enable/disable từng nút theo PrinterState:
void UIManager::UpdateButtonStateForPrinterState(PrinterState state) {
    // 🔒 HARD LOCK: Jet đang chuyển trạng thái
    if (state.jetTransitioning)
    {
        EnableWindow(btnStart_.GetHandle(), FALSE);
        EnableWindow(btnStop_.GetHandle(), FALSE);
        EnableWindow(btnPrint_.GetHandle(), FALSE);
        EnableWindow(hBtnUpload_, FALSE);
        EnableWindow(btnSet_.GetHandle(), FALSE);
        EnableWindow(hBtnDownload_, FALSE);
        EnableWindow(hBtnAdjust_, FALSE);
        EnableWindow(hMessageCombo_, FALSE);
        EnableWindow(hBtnMsgRefresh_, FALSE);
        return; // ⛔ TUYỆT ĐỐI KHÔNG CHẠY LOGIC DƯỚI
    }
    // ===== logic bình thường =====
    bool upload = false;
    bool startJet = false;
    bool stopJet = false;
    bool print = false;
    bool stopPrint = false;
    bool setCount = false;
    bool download = false;   // [2026-11-18 R36]
    bool adjust = false;     // [2026-11-18 R37]
    switch (state.status)
    {
    case PrinterStateType::Disconnected:
    case PrinterStateType::Connecting:
    case PrinterStateType::Error:
        break;
    case PrinterStateType::Idle:
        upload = true;
        startJet = true;
        setCount = true;
        download = true;
        btnPrint_.SetCaption(L"IN");
        btnPrint_.SetBaseColor(RGB(200, 200, 200));
        break;
    case PrinterStateType::Ready:
        upload = true;
        startJet = false;
        stopJet = true;
        print = true;
        stopPrint = true;
        setCount = true;
        download = true;
        adjust = true;
        btnPrint_.SetCaption(L"BẮT ĐẦU IN");
        btnPrint_.SetBaseColor(RGB(0, 180, 0));
        break;
    case PrinterStateType::Printing:
        stopJet = true;
        print = true;
        stopPrint = true;
        setCount = false;
        download = true;
        adjust = true;   // in bù/trừ khi máy đang in
        btnPrint_.SetCaption(L"TẠM DỪNG IN");
        btnPrint_.SetBaseColor(RGB(255, 160, 0));
        break;
    }
    EnableWindow(hBtnUpload_, upload);
    EnableWindow(btnStart_.GetHandle(), startJet);
    EnableWindow(btnPrint_.GetHandle(), print || stopPrint);
    EnableWindow(btnStop_.GetHandle(), stopJet);
    EnableWindow(btnSet_.GetHandle(), setCount);
    EnableWindow(hBtnDownload_, download);
    EnableWindow(hBtnAdjust_, adjust);
    // [R44] chọn bản tin: chỉ khi đã kết nối và máy không in (97D / 30D không hợp lệ khi đang in)
    const bool pickMsg = (state.status == PrinterStateType::Idle || state.status == PrinterStateType::Ready);
    EnableWindow(hMessageCombo_, pickMsg);
    EnableWindow(hBtnMsgRefresh_, pickMsg);
}
