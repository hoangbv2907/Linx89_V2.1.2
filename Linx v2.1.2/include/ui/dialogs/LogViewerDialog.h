// [2026-11-22 R41] Cửa sổ xem lại nhật ký sự kiện (modal): chọn tháng, lọc theo ngày / mức độ / từ khoá,
// xuất CSV, mở thư mục nhật ký.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class LogViewerDialog {
public:
    static void Run(HWND parent);
};
