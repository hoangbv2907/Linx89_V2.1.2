#define WIN32_LEAN_AND_MEAN         //loại bỏ nhiều header không cần thiết, tránh xung đột WinSock
#include <winsock2.h>               // thư viện TCP/UDP
#include <ws2tcpip.h>
#include <windows.h>                // thư viện Windows API
#include <commctrl.h>               // thư viện control chuẩn
#include "WindowManager.h"          // quản lý UI, tạo cửa sổ, xử lý message…
#include "Logger.h"                 // ghi log

// [2026-11-16 R26] Tạo Logger TRƯỚC g_windowManager để nó bị huỷ SAU: ~WindowManager -> ~AppController
// vẫn ghi log khi chương trình thoát (trước đây Logger đã bị huỷ lúc đó -> dùng đối tượng đã huỷ).
static Logger& g_loggerKeepAlive = Logger::GetInstance();
static WindowManager g_windowManager;  //instance toàn cục để quản lý cả vòng đời UI

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) { //điểm bắt đầu chương trình Win32
    // [2026-11-22 R43] Hỗ trợ DPI cao / nhiều màn hình khác tỉ lệ (Windows 10 1703+)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Initialize logger
    Logger::GetInstance().Write(L"Linx Controller starting...");
    // Khởi tạo WindowManager
    if (!g_windowManager.Initialize(hInstance)) {
        Logger::GetInstance().Write(L"Failed to initialize WindowManager", 2);
        return -1;
    }
    // Tạo cửa sổ chính
    if (!g_windowManager.CreateMainWindow(L"Linx Controller - BY BUI_VAN_HOANG", 520, 640)) {  //Gọi hàm trong WindowManager để tạo HWND.
        Logger::GetInstance().Write(L"Failed to create main window", 2);
        return -1;
    }
    // Show và refresh cửa sổ
    ShowWindow(g_windowManager.GetHwnd(), g_windowManager.StartMaximized() ? SW_SHOWMAXIMIZED : nCmdShow);    //hiển thị window theo style (normal, minimized…).
    UpdateWindow(g_windowManager.GetHwnd());            //yêu cầu Windows vẽ ngay nội dung.
    Logger::GetInstance().Write(L"Application started successfully");
    // vòng lặp chính
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {   //nhận message trong hàng đợi (chuột, bàn phím, redraw, timer, socket notify…)
        TranslateMessage(&msg);                 //xử lý phím (WM_KEYDOWN → WM_CHAR)
        DispatchMessage(&msg);                  //chuyển message đến WndProc của WindowManager
    }
    //thoát ứng dụng
    Logger::GetInstance().Write(L"Application shutting down");
    return (int)msg.wParam;
}