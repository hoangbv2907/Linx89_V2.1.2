/*
dùng để khai báo:
- ID cho các control (button, textbox…)
- Custom message để thread khác cập nhật UI
- Forward declaration cho các struct/enum liên quan đến trạng thái máy in
*/
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
// Control IDs
#define IDC_TOGGLE     1001
#define IDC_BTN_UPLOAD 1002
#define IDC_BTN_START  1003
#define IDC_BTN_PRINT  1004
#define IDC_BTN_STOP   1005
#define IDC_BTN_CLEAR  1006
#define IDC_BTN_SET    1007
#define IDC_CURRENT_COUNT 1008
#define IDC_BTN_DOWNLOAD 1009   // [2026-11-18 R36] tải thông tin từ máy in về
#define IDC_BTN_ADJUST   1010
#define IDC_BTN_SETTINGS 1013   // [2026-11-22 R43] biểu tượng Cài đặt (gộp Kết nối / Nhật ký / Bản quyền / Giới thiệu)
#define IDC_MSG_COMBO    1014   // [2026-11-22 R44] ô chọn bản tin
#define IDC_BTN_MSGREFRESH 1015 // [2026-11-22 R44] làm mới danh sách bản tin
// Custom messages for thread-safe UI updates
#define WM_APP_LOG             (WM_APP + 1)
#define WM_APP_PRINTER_UPDATE  (WM_APP + 2) 
#define WM_APP_CONNECTION_UPDATE (WM_APP + 3)
#define WM_APP_PRINT_PROGRESS  (WM_APP + 4)
#define WM_APP_BUTTON_STATE	  (WM_APP + 5)
// Forward declarations
struct PrinterState;
enum class PrinterStatusType;