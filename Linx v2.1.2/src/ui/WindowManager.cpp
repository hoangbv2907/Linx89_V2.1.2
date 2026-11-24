
#include "ui/WindowManager.h"
#include "logging/Logger.h"
#include "ui/UIManager.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/PinDialog.h"
#include "ui/dialogs/LicenseDialog.h"
#include "ui/UILayout.h"
#include "common/EncodingUtils.h"
#include "logging/Logger.h"
#include "license/LicensePublicKey.h"
#include <commctrl.h>
// ================== Lifecycle / Window creation ==================
WindowManager::WindowManager() {}

WindowManager::~WindowManager() {
    if (appController_) {
        appController_->ComprehensiveCleanup();
        appController_.reset();
    }
    // Cleanup UI components
    if (uiManager_) {
        uiManager_.reset();
    }
}

//đăng ký window class, lưu hInstance, init common stuff
bool WindowManager::Initialize(HINSTANCE hInstance) {
    hInstance_ = hInstance;     //lưu handle instance để dùng cho CreateWindowEx, RegisterClass
    // xác định loại common controls cần dùng.
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_INTERNET_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    //khởi tạo control trên Windows (button, edit box, progress bar…).
    InitCommonControlsEx(&icex);
    // tạo UIManager để quản lý toàn bộ control.
    uiManager_ = std::make_unique<UIManager>();
    // [2026-11-22 R43] cài đặt người dùng (vị trí cửa sổ phải biết trước khi tạo cửa sổ)
    settings_ = AppSettings::Load(AppSettings::DefaultPath());
    Logger::GetInstance().SetRetention(settings_.logKeepDays);
    Logger::GetInstance().Write(L"WindowManager initialized");
    return true;
}

// Tạo cửa sổ chính (CreateWindowEx) với title/size
bool WindowManager::CreateMainWindow(const std::wstring& title, int width, int height) {
    WNDCLASS wc = {};                               //khởi tạo struct WNDCLASS
    wc.lpfnWndProc = StaticWndProc;                 //chỉ định callback tĩnh để nhận message Windows.
    wc.hInstance = hInstance_;                      //instance của chương trình
    wc.lpszClassName = L"LinxMainWindow";           //tên class cửa sổ
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);    //con trỏ chuột mặc định
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);  //màu nền cửa sổ
    wc.style = CS_HREDRAW | CS_VREDRAW;             //vẽ lại khi thay đổi kích thước
    //đăng ký class, nếu fail → log và return false.
    if (!RegisterClass(&wc)) return false;
    //tạo cửa sổ chính với CreateWindowEx//Vị trí gọi: main.cpp → sau đó ShowWindow + UpdateWindow.
    // [2026-11-22 R43] Cửa sổ co giãn được: viền kéo + nút phóng to; kích thước/vị trí lần trước được khôi phục.
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    const UINT sysDpi = GetDpiForSystem();
    RECT rc{ 0, 0, MulDiv(width, sysDpi, 96), MulDiv(height, sysDpi, 96) };
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, sysDpi);

    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    int winX = CW_USEDEFAULT, winY = CW_USEDEFAULT;
    if (settings_.window.w > 0 && settings_.window.h > 0) {
        const RECT saved{ settings_.window.x, settings_.window.y, settings_.window.x + settings_.window.w, settings_.window.y + settings_.window.h };
        MONITORINFO mi{ sizeof(mi) };
        HMONITOR mon = MonitorFromRect(&saved, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(mon, &mi)) {
            RECT minR{ 0, 0, MulDiv(kMinClientW96, sysDpi, 96), MulDiv(kMinClientH96, sysDpi, 96) };
            AdjustWindowRectExForDpi(&minR, style, FALSE, 0, sysDpi);
            const RECT r = ClampWindowToWorkArea(saved.left, saved.top, settings_.window.w, settings_.window.h, mi.rcWork,
                minR.right - minR.left, minR.bottom - minR.top);   // đưa về trong màn hình nếu màn hình đã đổi
            winX = r.left; winY = r.top; winW = r.right - r.left; winH = r.bottom - r.top;
        }
    }

    hwnd_ = CreateWindowEx(
        0, wc.lpszClassName, title.c_str(),
        style,
        winX, winY, winW, winH,
        nullptr, nullptr, hInstance_, this
    );
    if (!hwnd_) {
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return false;
    }
    return true;
}

// Entry point sau khi WndProc forward vào object instance
LRESULT WindowManager::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:     //tạo window
        HandleCreate();
        return 0;
    case WM_COMMAND: {  //xử lý lệnh từ button, menu, v.v.
        int id = LOWORD(wParam);
        if (id == IDC_MSG_COMBO) {        // [2026-11-22 R44] ô chọn bản tin: chỉ quan tâm khi chọn xong
            if (HIWORD(wParam) == CBN_SELCHANGE) OnMessageSelected();
            return 0;
        }
        HandleCommand(id);
        return 0;
    }
    case WM_DRAWITEM:   //vẽ nút custom (Owner-Draw button)
        HandleDrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
        return TRUE;

    case WM_APP_LOG: {  //nhận log message từ AppController thread, update UI.
        auto* log = (LogMessage*)wParam;
        if (uiManager_) uiManager_->AddMessage(log->text, log->level);
        delete log;
        return 0;
    }
    case WM_CLOSE:
    {
        try {
            appController_->SavePrintDataOnExit();
        }
        catch (const std::exception&) {   // [2026-11-13 R14] bỏ biến không dùng
        }
        DestroyWindow(hwnd_);
        return 0;
    }
    case WM_APP_PRINTER_UPDATE: {   //cập nhật trạng thái máy in trên UI
        PrinterStateMessage* stateMsg = reinterpret_cast<PrinterStateMessage*>(wParam);
        HandlePrinterUpdate(stateMsg);
        delete stateMsg;
        return 0;
    }
    case WM_APP_CONNECTION_UPDATE: {    //nhận thông tin connected/disconnected
        ConnectionMessage* msg = reinterpret_cast<ConnectionMessage*>(wParam);
        HandleConnectionUpdate(msg);   // dùng đúng hàm WindowManager đã viết
        delete msg;
        return 0;
    }
    case WM_APP_BUTTON_STATE: {     //cập nhật trạng thái button (active, disabled, running…)
        ButtonStateMessage* btnMsg = reinterpret_cast<ButtonStateMessage*>(wParam);
        HandleButtonState(btnMsg);
        delete btnMsg;
        return 0;
    }
    case WM_GETMINMAXINFO: {       // [2026-11-22 R43] kích thước tối thiểu: client 520x560 (đơn vị 96 DPI)
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = GetDpiForWindow(hwnd_);
        RECT rc{ 0, 0, MulDiv(kMinClientW96, dpi, 96), MulDiv(kMinClientH96, dpi, 96) };
        AdjustWindowRectExForDpi(&rc, (DWORD)GetWindowLongPtr(hwnd_, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtr(hwnd_, GWL_EXSTYLE), dpi);
        mm->ptMinTrackSize.x = rc.right - rc.left;
        mm->ptMinTrackSize.y = rc.bottom - rc.top;
        return 0;
    }
    case WM_SIZE:                  // [2026-11-22 R43] đổi kích thước -> bố trí lại mọi control
        if (wParam != SIZE_MINIMIZED && uiManager_) uiManager_->Layout(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DPICHANGED: {          // [2026-11-22 R43] kéo sang màn hình khác tỉ lệ: đổi font + kích thước
        if (uiManager_) uiManager_->SetDpi((int)HIWORD(wParam));
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_TIMER:                 // [2026-11-22 R42] 1 = mỗi phút cộng giờ; 2 = kiểm tra một lần lúc khởi động
        if (wParam == 1) LicenseTick();
        else if (wParam == 3) RefreshRoleUi();      // [2026-11-23 R45] hết phiên giám sát -> về Vận hành
        else if (wParam == 2) {
            if (!session_.IsLoggedIn()) return 0;   // [R45] còn ở màn hình đăng nhập: chờ
            KillTimer(hwnd_, 2);
            if (license_ && license_->Expired()) OnSettingsClicked(SettingsDialog::License);   // hết hạn: mở sẵn thẻ Bản quyền
        }
        return 0;
    case WM_DESTROY:            //xử lý dọn dẹp khi cửa sổ bị đóng
        HandleDestroy();
        return 0;
    default:
        return DefWindowProc(hwnd_, msg, wParam, lParam);
    }
}

// WndProc tĩnh đăng ký với Windows
LRESULT CALLBACK WindowManager::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WindowManager* pThis = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);      // Lấy con trỏ instance từ lpCreateParams
        pThis = reinterpret_cast<WindowManager*>(cs->lpCreateParams);   // trỏ this của WindowManager
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);         // lưu con trỏ this vào dữ liệu cửa sổ     
        pThis->hwnd_ = hwnd;                                            // lưu HWND vào instance
        // Tạo AppController với HWND của cửa sổ chính
        pThis->appController_ = std::make_unique<AppController>(hwnd);
    }
    else {
        pThis = reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)); // Lấy con trỏ this từ dữ liệu cửa sổ
    }

    return pThis ? pThis->HandleMessage(msg, wParam, lParam)     // Chuyển message đến instance cụ thể
        : DefWindowProc(hwnd, msg, wParam, lParam);
}

// ================== Core WM_* handlers ==================
// ========================================================
// WM_CREATE: init UI controls (UIManager), bind state, load persisted data
void WindowManager::HandleCreate() {
    if (uiManager_) {
        Logger::GetInstance().Write(L"HandleCreate: Khởi tạo UI");
        uiManager_->Initialize(hwnd_);  // Khởi tạo UIManager với HWND của cửa sổ chính
        uiManager_->CreateControls();   // Tạo các control trên cửa sổ
        Logger::GetInstance().Write(L"HandleCreate: UI controls đã tạo");
        appController_->LoadPrintDataOnStart();
    }
    if (appController_) {   //kiểm tra AppController đã khởi tạo chưa
        Logger::GetInstance().Write(L"HandleCreate: Loading backup data");
        appController_->LoadPrintDataOnStart();
        appController_->SavePrintDataOnExit();
        ApplySettings(settings_, /*save=*/false);   // [2026-11-22 R43] IP, tên bản tin, tự kết nối lại, thời gian giữ nhật ký
        // [2026-11-22 R42] Giới hạn thời gian sử dụng: nạp bộ đếm giờ + đặt cổng chặn điều khiển khi hết hạn
        license_ = std::make_unique<lic::Service>(lic::Bytes(kLicensePublicKey, kLicensePublicKey + sizeof(kLicensePublicKey)));
        license_->Load();
        appController_->SetLicenseGate([this]() { return license_ && !license_->Expired(); });
        UpdateTitle();
        SetTimer(hwnd_, 1, 60000, nullptr);
        SetTimer(hwnd_, 3, 10000, nullptr);   // [2026-11-23 R45]
        SetTimer(hwnd_, 2, 1500, nullptr);
        Logger::GetInstance().Write(L"HandleCreate: Start worker thread");
        appController_->StartWorkerThread();
    }
}

// WM_COMMAND: dispatch theo control id (button/menu) sang OnXxxClicked()
void WindowManager::HandleCommand(int id) {
    switch (id) {
    case IDC_TOGGLE:        // nút toggle kết nối
        OnToggleClicked();
        break;
    case IDC_BTN_UPLOAD:    // nút upload nội dung
        OnUploadClicked();
        break;
    case IDC_BTN_START:     // nút start jet
        OnStartClicked();
        break;
    case IDC_BTN_PRINT:     // nút in
        OnPrintClicked();
        break;
    case IDC_BTN_STOP:      // nút dừng in
        OnStopClicked();
        break;
    case IDC_BTN_CLEAR:     // nút xóa log
        OnClearClicked();
        break;
    case IDC_BTN_SET:       // nút đặt số lượng in
        OnSetClicked();
        break;
    case IDC_BTN_DOWNLOAD:  // [2026-11-18 R36]
        OnDownloadClicked();
        break;
    case IDC_BTN_ADJUST:    // [2026-11-18 R37]
        OnAdjustClicked();
        break;
    case IDC_BTN_MSGREFRESH:  // [2026-11-22 R44] làm mới danh sách bản tin
        if (appController_) appController_->RefreshMessageList();
        break;
    case IDC_BTN_SETTINGS:  // [2026-11-22 R43] biểu tượng ⚙
        OnSettingsClicked();
        break;
    default:
        break;
    }
}

// WM_DRAWITEM: vẽ owner-draw button nếu UIManager dùng custom draw
void WindowManager::HandleDrawItem(LPDRAWITEMSTRUCT dis) {
    if (uiManager_ && dis) {    // kiểm tra UIManager và DRAWITEMSTRUCT không null
        uiManager_->HandleOwnerDraw(dis);   // chuyển yêu cầu vẽ đến UIManager
    }
}

// WM_DESTROY/WM_CLOSE: dọn dẹp, stop thread, save persist...
void WindowManager::HandleDestroy() {
    if (destroyHandled_) return;   // [2026-11-16 R25] member thay cho static
    destroyHandled_ = true;
    KillTimer(hwnd_, 1);
    KillTimer(hwnd_, 3);
    SaveWindowPlacement();            // [2026-11-22 R43]
    if (license_) license_->Save();   // [2026-11-22 R42] lưu số giờ đã dùng
    // ✅ DỪNG CONTROLLER TRƯỚC KHI POST QUIT
    if (appController_) {
        appController_->StopWorkerThread(1000);
    }
    // Worker đã dừng nên không còn ai PostMessage nữa -> dọn nốt phần còn trong hàng đợi.
    DrainPendingAppMessages();
    PostQuitMessage(0);
}

// [2026-11-16 R25] Các WM_APP_* mang con trỏ cấp phát bằng new. Khi cửa sổ bị huỷ, thông điệp chưa xử lý
// sẽ bị bỏ và không ai delete -> rò rỉ. Lấy hết ra và giải phóng đúng kiểu.
void WindowManager::DrainPendingAppMessages() {
    MSG m;
    while (PeekMessage(&m, hwnd_, WM_APP_LOG, WM_APP_BUTTON_STATE, PM_REMOVE)) {
        switch (m.message) {
        case WM_APP_LOG:               delete reinterpret_cast<LogMessage*>(m.wParam); break;
        case WM_APP_PRINTER_UPDATE:    delete reinterpret_cast<PrinterStateMessage*>(m.wParam); break;
        case WM_APP_CONNECTION_UPDATE: delete reinterpret_cast<ConnectionMessage*>(m.wParam); break;
        case WM_APP_BUTTON_STATE:      delete reinterpret_cast<ButtonStateMessage*>(m.wParam); break;
        default: break;   // WM_APP_PRINT_PROGRESS: hiện không có nơi nào gửi
        }
    }
}

// ================== AppController -> UI messages (WM_APP_*) ==================
// =============================================================================
// Nhận log từ worker thread và append vào log UI (MessageLogger/RichEdit)
void WindowManager::HandleAppLog(LogMessage* msg) {
    if (uiManager_ && msg) {    // kiểm tra UIManager và LogMessage không null
        uiManager_->AddMessage(msg->text, msg->level);  // thêm message vào log UI
    }
}

// Nhận snapshot trạng thái máy in (PrinterStateMessage) và cập nhật UI
void WindowManager::HandlePrinterUpdate(PrinterStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdatePrinterStatus(msg->statusText);   // Cập nhật text trạng thái máy in
        uiManager_->UpdateJobFields(msg->jobContent, msg->targetCount, msg->printedCount);
        uiManager_->UpdateWarnings(msg->state.warnings);                 // [2026-11-18 R35]
        uiManager_->UpdateRemoteFieldInfo(msg->state, msg->state.messageName);
        uiManager_->UpdateMessageList(msg->state);          // [2026-11-22 R44]   // [2026-11-18 R33/R36]
        uiManager_->UpdatePrinterUIState(msg->state);       // Cập nhật trạng thái UI dựa trên trạng thái máy in
        uiManager_->UpdateButtonStates(msg->state);
    }
}

// Nhận trạng thái connected/disconnected và cập nhật UI toggle/state
void WindowManager::HandleConnectionUpdate(ConnectionMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->SetToggleState(msg->connected);     // Cập nhật trạng thái toggle      
        if (msg->connected) { // Thêm message kết nối/ngắt kết nối vào log UI
            uiManager_->AddMessage(L"✅ Đã kết nối đến " + msg->ipAddress, 4);
        }
        else
            uiManager_->AddMessage(L"X Đã ngắt kết nối với " + msg->ipAddress, 2);
    }
}

// Nhận state enable/disable/running của các nút và áp vào UI
void WindowManager::HandleButtonState(ButtonStateMessage* msg) {
    if (uiManager_ && msg) {
        uiManager_->UpdateButtonStates(msg->state);     // Cập nhật trạng thái button dựa trên trạng thái máy in
    }
}

// Các handler giao tiếp WindowManager -> AppController (enqueue request)
void WindowManager::OnToggleClicked() {
    if (!appController_ || !uiManager_) return;
    bool isToggleCurrentlyOn = uiManager_->IsToggleOn();
    if (!isToggleCurrentlyOn) { // User muốn KẾT NỐI      
        std::wstring ip = uiManager_->GetIPAddress();
        if (!uiManager_->ValidateInput()) {
            uiManager_->AddMessage(L"❌ Địa chỉ IP không hợp lệ", 2);
            return;
        }
        appController_->SetLastIp(ip);
        {   // [2026-11-22 R43] nhớ IP đã dùng cho lần mở sau
            const std::string ipA = WideToUtf8(ip);
            if (ipA != settings_.ip) { settings_.ip = ipA; settings_.Save(AppSettings::DefaultPath()); }
        }
        appController_->DisableAutoReconnect();
        appController_->Connect(ip);
    }
    else {
        uiManager_->SetToggleState(false);
        appController_->Disconnect();
    }
}

void WindowManager::OnUploadClicked() {
    if (!appController_ || !uiManager_) return;  // kiểm tra AppController và UIManager tồn tại
    std::wstring content = uiManager_->GetInputText();  // lấy nội dung từ UI
    if (content.empty()) {
        uiManager_->AddMessage(L"⚠️ Chưa có nội dung để tải lên", 1);
    }
    std::wstring fieldName = appController_->GetCurrentState().remoteFieldName;   // [2026-11-18 R36] field đã đọc từ máy (mặc định RemoteField1)
    if (fieldName.empty()) fieldName = L"RemoteField1";
    appController_->UploadRemoteFieldData(fieldName, content);
    uiManager_->AddMessage(L"📤 Đang tải lên Remote Field: " + fieldName, 3);
}

void WindowManager::OnStartClicked() {
    if (!appController_) return;    // kiểm tra AppController tồn tại
    PrinterState cur = appController_->GetCurrentState();
    if (cur.jetOn) {
        uiManager_->AddMessage(L"⚠️ Jet đã ON — không gửi lệnh khởi động.", 3);
        return;
    }
    if (cur.status == PrinterStateType::StartingJet) {
        uiManager_->AddMessage(L"⚠️ Đang chờ Jet khởi động — vui lòng đợi.", 3);
        return;
    }
    appController_->StartJet(); //gọi StartJet trong AppController
    uiManager_->AddMessage(L"🚀 Khởi động jet...", 3);
}

void WindowManager::OnPrintClicked() {
    if (!appController_ || !uiManager_) return; // kiểm tra AppController tồn tại
    // Lấy trạng thái hiện tại từ AppController (hoặc PrinterModel)
    PrinterState cur = appController_->GetCurrentState();
    if (cur.status == PrinterStateType::Printing || cur.printing) {
        appController_->StopPrinting();
        uiManager_->AddMessage(L"⏸️ Yêu cầu tạm dừng in được gửi...", 3);
    }
    else {// Nếu không đang in -> bắt đầu in
        std::wstring content = uiManager_->GetInputText();
        int count = uiManager_->GetCountValue();
        if (!content.empty() && count > 0) {
            appController_->StartPrinting(content, count);
            uiManager_->AddMessage(L"🖨️ Đã gửi lệnh bắt đầu in...", 3);
        }
        else {
            uiManager_->AddMessage(L"⚠️ Chưa có nội dung hoặc số lượng không hợp lệ", 3);
        }
    }
}

void WindowManager::OnStopClicked() {
    if (!appController_) return;    // kiểm tra AppController tồn tại
    PrinterState cur = appController_->GetCurrentState();
    if (!cur.jetOn) {
        uiManager_->AddMessage(L"⚠️ Jet đã off — không gửi lệnh dừng", 3);
        return;
    }
    if (cur.status == PrinterStateType::StopingJet) {
        uiManager_->AddMessage(L"⚠️ Đang chờ dừng Jet  — vui lòng đợi.", 3);
        return;
    }
    appController_->StopJet(); //gọi StopPrinting trong AppController
    uiManager_->AddMessage(L"⏹️ Dừng jet...", 3);
}

void WindowManager::OnClearClicked() {
    if (!RequireSupervisor()) return;   // [2026-11-23 R45] chỉ Giám sát viên
    if (uiManager_) {   // kiểm tra UIManager tồn tại
        uiManager_->ClearMessages();    // xóa tất cả message trong log UI
        uiManager_->AddMessage(L"Đã xóa nhật ký", 3);
    }
}

// [2026-11-18 R36] Tải thông tin từ máy về; bỏ cờ "user đã sửa" để ô nhập được làm mới theo dữ liệu máy.
void WindowManager::OnDownloadClicked() {
    if (!appController_ || !uiManager_) return;
    uiManager_->MarkJobDirtyByUser(false);
    uiManager_->SetEditingJobFields(false);
    uiManager_->ResetJobFieldCache();
    appController_->DownloadFromPrinter();
    uiManager_->AddMessage(L"📥 Đang đọc remote field / message / count từ máy in...", 3);
}

// [2026-11-23 R45] ----- Phân quyền 2 cấp (R51: Vận hành không mật khẩu, Giám sát có mật khẩu) -----
// Phiên Giám sát tự hết sau 24 giờ kể từ lúc đăng nhập -> về Vận hành viên; chỉ ghi nhật ký, KHÔNG hiện ở màn hình chính.
void WindowManager::RefreshRoleUi() {
    const bool sup = session_.IsSupervisor();
    if (sup == lastSupervisor_) return;
    lastSupervisor_ = sup;
    if (!sup) Logger::GetInstance().Write(L"Phiên Giám sát viên đã hết - quay về Vận hành viên");
}

// [R51] Đổi người dùng: đang là Giám sát -> hạ về Vận hành ngay (không mật khẩu); đang là Vận hành -> hỏi mật khẩu Giám sát.
// Trả cấp mới (1 = Vận hành, 2 = Giám sát) hoặc 0 nếu hủy.
int WindowManager::LoginInteractive(HWND owner) {
    if (session_.IsSupervisor()) {
        session_.Logout();
        lastSupervisor_ = false;
        Logger::GetInstance().Write(L"Đăng xuất Giám sát viên - về Vận hành viên");
        return 1;
    }
    const bool ok = PinDialog::Run(owner, L"Đăng nhập Giám sát viên", 2,
        [&](const std::string& pin, int) { return session_.Login(auth::Role::Supervisor, pin); });
    if (!ok) return 0;
    Logger::GetInstance().Write(L"Đăng nhập: Giám sát viên");
    lastSupervisor_ = true;
    return 2;
}

// Trả true nếu đang là Giám sát viên; nếu chưa thì hỏi mật khẩu Giám sát (Hủy thì false).
bool WindowManager::RequireSupervisor() {
    if (session_.IsSupervisor()) return true;
    const bool ok = PinDialog::Run(hwnd_, L"Đăng nhập Giám sát viên", 2,
        [&](const std::string& pin, int) { return session_.Login(auth::Role::Supervisor, pin); });
    if (ok) {
        if (uiManager_) {
            Logger::GetInstance().Write(L"Đăng nhập: Giám sát viên");
        }
        lastSupervisor_ = true;
    }
    return ok;
}

// [2026-11-22 R44] Chọn bản tin: tải lên máy (30D) và nhớ lựa chọn cho lần mở sau
void WindowManager::OnMessageSelected() {
    if (!appController_ || !uiManager_) return;
    const std::wstring name = uiManager_->GetSelectedMessage();
    if (name.empty()) return;
    appController_->SelectMessage(name);
    const std::string a = WideToUtf8(name);
    if (a != settings_.printMessageName) { settings_.printMessageName = a; settings_.Sanitize(); settings_.Save(AppSettings::DefaultPath()); }
}

// [2026-11-22 R43] ⚙ Cài đặt: một cửa sổ có thẻ Kết nối / Nhật ký / Bản quyền (gia hạn) / Giới thiệu.
// Hết hạn vẫn mở được (để nhập mã gia hạn).
void WindowManager::OnSettingsClicked(int tab) {
    if (!license_ || !appController_ || !uiManager_) return;
    SettingsContext ctx;      // [R48] Vận hành viên vẫn mở được, ở chế độ chỉ xem
    ctx.readOnly = !session_.IsSupervisor();
    ctx.settings = settings_;
    ctx.license = license_.get();
    ctx.printerSerial = appController_->GetCurrentState().printerSerial;
    ctx.log = [this](const std::wstring& m, int lv) { uiManager_->AddMessage(m, lv); };
    ctx.apply = [this](const AppSettings& s) { ApplySettings(s, /*save=*/true); };
    ctx.roleText = session_.IsSupervisor() ? L"Giám sát viên" : L"Vận hành viên";
    ctx.switchUser = [this]() { return LoginInteractive(hwnd_); };
    SettingsDialog::Run(hwnd_, ctx, tab);
    lastWarnHours_ = 1 << 30;     // sau khi gia hạn: nhắc lại từ đầu
    UpdateTitle();
}

// Áp dụng cài đặt: tên bản tin khi in, tự kết nối lại, thời gian giữ nhật ký, IP mặc định.
void WindowManager::ApplySettings(const AppSettings& in, bool save) {
    AppSettings s = in;
    s.Sanitize();
    settings_ = s;
    if (appController_) {
        appController_->SetPrintMessageName(s.printMessageName);
        appController_->SetAutoReconnectAllowed(s.autoReconnect);
    }
    Logger::GetInstance().SetRetention(s.logKeepDays);
    if (uiManager_ && !appController_->IsConnected()) uiManager_->SetIPAddress(Utf8ToWide(s.ip));
    if (save) {
        const bool ok = settings_.Save(AppSettings::DefaultPath());
        if (uiManager_) uiManager_->AddMessage(ok ? L"💾 Đã lưu cài đặt" : L"⚠️ Không lưu được file cài đặt", ok ? 4 : 1);
    }
}

// Lưu vị trí + kích thước cửa sổ (trạng thái bình thường, kể cả khi đang phóng to) để lần sau mở lại đúng chỗ.
void WindowManager::SaveWindowPlacement() {
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!hwnd_ || !GetWindowPlacement(hwnd_, &wp)) return;
    settings_.window.x = wp.rcNormalPosition.left;
    settings_.window.y = wp.rcNormalPosition.top;
    settings_.window.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    settings_.window.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    settings_.window.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    settings_.Save(AppSettings::DefaultPath());
}

// [2026-11-22 R44] Tiêu đề cố định: thời gian sử dụng CHỈ hiển thị trong ⚙ Cài đặt › Bản quyền.
void WindowManager::UpdateTitle() {
    SetWindowTextW(hwnd_, L"Linx Controller");
}

// Mỗi phút: cộng giờ chạy, nhắc khi còn ≤100 / ≤24 / ≤1 giờ và khi hết hạn
void WindowManager::LicenseTick() {
    if (!license_ || !uiManager_) return;
    license_->Tick();
    UpdateTitle();
    const int64_t rem = license_->RemainingSeconds();
    const int hours = rem <= 0 ? 0 : (int)(rem / 3600);
    const int marks[] = { 100, 24, 1 };
    for (int mk : marks) {
        if (hours <= mk && lastWarnHours_ > mk && rem > 0) {
            uiManager_->AddMessage(L"⚠️ Sắp hết thời gian sử dụng - xem ⚙ Cài đặt › Bản quyền để biết chi tiết và gia hạn", 1);
            lastWarnHours_ = mk;
            break;
        }
    }
    if (rem <= 0 && lastWarnHours_ > 0) {
        lastWarnHours_ = 0;
        uiManager_->AddMessage(L"⛔ Đã hết thời gian sử dụng - các thao tác điều khiển in bị khoá - vào ⚙ Cài đặt › Bản quyền để gia hạn", 2);
    }
}

// [2026-11-18 R37] In bù (+N) / trừ (-N) khi máy vẫn đang hoạt động.
void WindowManager::OnAdjustClicked() {
    if (!appController_ || !uiManager_) return;
    bool ok = false;
    int delta = uiManager_->GetAdjustValue(ok);
    if (!ok || delta == 0) {
        uiManager_->AddMessage(L"⚠️ Nhập số nguyên khác 0 (vd +50 để in bù, -20 để trừ)", 1);
        return;
    }
    appController_->AdjustCount(delta);
}

void WindowManager::OnSetClicked() {
    if (!appController_ || !uiManager_) return; // kiểm tra AppController và UIManager tồn tại
    int count = uiManager_->GetCountValue();    // lấy số lượng in từ UI
    if (count > 0) {
        appController_->SetCount(count); // gọi SetCount trong AppController
        appController_->SavePrintDataOnExit();
    }
    else {
        uiManager_->AddMessage(L"❌ Số lượng phải lớn hơn 0", 1);
    }
}