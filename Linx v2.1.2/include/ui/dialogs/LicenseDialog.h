// [2026-11-22 R42] Mô tả thời gian sử dụng. [R43] Cửa sổ nhập mã gia hạn đã được gộp vào thẻ "Bản quyền" của SettingsDialog.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "LicenseCore.h"

class LicenseDialog {
public:
    // Chuỗi mô tả thời gian còn lại, ví dụ "Đã dùng 12 / 6000 giờ. Còn 5987 giờ 20 phút." / "... ĐÃ HẾT HẠN."
    static std::wstring Describe(const lic::Service& s);
};
