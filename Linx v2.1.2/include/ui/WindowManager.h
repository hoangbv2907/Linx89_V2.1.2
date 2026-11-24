/*
Chức năng: Nhận message từ Windows/Nhận message custom từ worker/AppController
Chuyển các event UI (nhấn nút, nhập) → gọi AppController (enqueue request)
Gọi UIManager để cập nhật controls
*/

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <memory>

#include "ui/UIManager.h"      // Quản lý controls trên cửa sổ (edit, button, status, log...)
#include "controller/AppController.h"  // Logic nền: worker thread, socket, poll status, enqueue requests
#include "license/LicenseCore.h"     // [2026-11-22 R42]
#include "settings/AppSettings.h"     // [2026-11-22 R43]
#include "auth/AuthCore.h"        // [2026-11-23 R45]
#include "controller/MessageDef.h"     // WM_APP_* + struct message trao đổi giữa worker và UI thread

class WindowManager {
public:
    // ================== Lifecycle / Window creation ==================
    WindowManager();
    ~WindowManager();

    // Được gọi từ main.cpp: đăng ký window class, lưu hInstance, init common stuff
    bool Initialize(HINSTANCE hInstance);

    // Tạo cửa sổ chính (CreateWindowEx) với title/size
    bool CreateMainWindow(const std::wstring& title, int width, int height);

    // [2026-11-23 R45] Đăng nhập bắt buộc khi mở phần mềm. false = người dùng hủy -> thoát chương trình.

    // [2026-11-22 R43] true nếu lần chạy trước đóng chương trình khi đang phóng to
    bool StartMaximized() const { return settings_.window.maximized; }

    // Getter lấy HWND để main.cpp gọi ShowWindow/UpdateWindow
    HWND GetHwnd() const { return hwnd_; }

    // ================== Main message handling ==================
    // Entry point sau khi WndProc forward vào object instance
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // ================== Getters (access owned components) ==================
    UIManager* GetUIManager() { return uiManager_.get(); }
    AppController* GetAppController() { return appController_.get(); }

private:
    // ================== Win32 WndProc routing ==================
    // WndProc tĩnh đăng ký với Windows:
    // - WM_NCCREATE: gắn WindowManager* vào GWLP_USERDATA
    // - Forward các message về instance->HandleMessage(...)
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ================== Core WM_* handlers ==================
    // WM_CREATE: init UI controls (UIManager), bind state, load persisted data
    // (AppController thường đã được tạo ở WM_NCCREATE để có hwnd)
    void HandleCreate();

    // WM_COMMAND: dispatch theo control id (button/menu) sang OnXxxClicked()
    void HandleCommand(int id);

    // WM_DRAWITEM: vẽ owner-draw button nếu UIManager dùng custom draw
    void HandleDrawItem(LPDRAWITEMSTRUCT dis);

    // WM_DESTROY/WM_CLOSE: dọn dẹp, stop thread, save persist...
    void HandleDestroy();

    // ================== AppController -> UI messages (WM_APP_*) ==================
    // Nhận log từ worker thread và append vào log UI (MessageLogger/RichEdit)
    void HandleAppLog(LogMessage* msg);

    // Nhận snapshot trạng thái máy in (PrinterStateMessage) và cập nhật UI
    void HandlePrinterUpdate(PrinterStateMessage* msg);

    // Nhận trạng thái connected/disconnected và cập nhật UI toggle/state
    void HandleConnectionUpdate(ConnectionMessage* msg);

    // Nhận state enable/disable/running của các nút và áp vào UI
    void HandleButtonState(ButtonStateMessage* msg);

    // ================== UI interaction handlers (button events) ==================
    // Các handler giao tiếp WindowManager -> AppController (enqueue request)
    void OnToggleClicked();
    void OnUploadClicked();
    void OnStartClicked();
    void OnPrintClicked();
    void OnStopClicked();
    void OnClearClicked();
    void OnSetClicked();
    void OnDownloadClicked();   // [2026-11-18 R36] tải remote field/message/count từ máy về
    void OnAdjustClicked();
    int LoginInteractive(HWND owner);         // [2026-11-23 R45] hộp thoại đăng nhập (chọn cấp + mật khẩu); trả cấp (1/2) hoặc 0 nếu hủy
    bool RequireSupervisor();                 // [2026-11-23 R45] true nếu đang là Giám sát viên (hỏi mật khẩu Giám sát nếu chưa)
    void RefreshRoleUi();                     // [2026-11-23 R45] phát hiện phiên Giám sát tự hết -> ghi nhật ký
    void OnMessageSelected();                 // [2026-11-22 R44] người dùng chọn bản tin trong ô chọn
    void OnSettingsClicked(int tab = 0);   // [2026-11-22 R43] ⚙ Cài đặt (Kết nối / Nhật ký / Bản quyền / Giới thiệu)
    void ApplySettings(const AppSettings& s, bool save);   // áp dụng cài đặt vào chương trình (và lưu file)
    void SaveWindowPlacement();            // lưu vị trí / kích thước cửa sổ vào settings.json
    void LicenseTick();         // [2026-11-22 R42] cộng giờ chạy, cảnh báo sắp hết hạn
    void UpdateTitle();         // [2026-11-22 R42] hiện số giờ còn lại trên thanh tiêu đề

private:
    // ================== Handles / Owned objects ==================
    HINSTANCE hInstance_ = nullptr;                // Instance handle của ứng dụng
    HWND hwnd_ = nullptr;                          // HWND cửa sổ chính

    std::unique_ptr<UIManager> uiManager_;         // Quản lý UI controls theo vòng đời window
    std::unique_ptr<AppController> appController_; // Trung tâm logic: socket/poll/request -> post message về UI
    auth::Session session_;                        // [2026-11-23 R45] phiên đăng nhập (Vận hành / Giám sát)
    bool lastSupervisor_ = false;
    AppSettings settings_;                         // [2026-11-22 R43] cài đặt người dùng (settings.json)
    std::unique_ptr<lic::Service> license_;        // [2026-11-22 R42] bộ đếm giờ + mã gia hạn
    int lastWarnHours_ = 1 << 30;                  // [2026-11-22 R42] mốc cảnh báo đã nhắc (giờ còn lại)
    bool destroyHandled_ = false;                  // [2026-11-16 R25] thay cho biến static trong HandleDestroy (chỉ UI thread dùng)
    void DrainPendingAppMessages();                // [2026-11-16 R25] giải phóng các WM_APP_* còn nằm trong hàng đợi khi thoát
};
