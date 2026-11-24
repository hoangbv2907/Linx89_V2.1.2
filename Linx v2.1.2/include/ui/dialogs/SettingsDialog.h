// [2026-11-22 R43] Cửa sổ Cài đặt (một biểu tượng ⚙ duy nhất) gồm các thẻ:
//   Kết nối | Nhật ký | Bản quyền (gia hạn) | Giới thiệu
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <string>
#include "AppSettings.h"
#include "LicenseCore.h"

struct SettingsContext {
    AppSettings settings;                                 // bản đang sửa (vào: cài đặt hiện tại; ra: giá trị sau khi Lưu)
    lic::Service* license = nullptr;                      // bộ đếm giờ + mã gia hạn
    std::wstring printerSerial;                           // serial máy in đang kết nối (rỗng nếu chưa kết nối)
    std::function<void(const std::wstring&, int)> log;    // ghi thông báo vào nhật ký
    std::function<void(const AppSettings&)> apply;        // gọi khi bấm Lưu để áp dụng + lưu file
};

class SettingsDialog {
public:
    enum Tab { Connection = 0, Logs = 1, License = 2, About = 3 };
    // Hiện hộp thoại modal. initialTab: thẻ mở sẵn (vd License khi đã hết hạn).
    static void Run(HWND parent, SettingsContext& ctx, int initialTab = Connection);
};
