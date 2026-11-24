#include "ui/dialogs/LicenseDialog.h"
#include <string>

std::wstring LicenseDialog::Describe(const lic::Service& s) {
    const int64_t rem = s.RemainingSeconds();
    const uint64_t used = s.UsedSeconds();
    std::wstring t = L"Đã dùng " + std::to_wstring(used / 3600) + L" / " + std::to_wstring(s.LimitHours()) + L" giờ. ";
    if (s.Tampered()) return t + L"DỮ LIỆU THỜI GIAN BỊ SỬA HOẶC SAO CHÉP TỪ MÁY KHÁC - cần nhập mã gia hạn.";
    if (rem <= 0) return t + L"ĐÃ HẾT HẠN.";
    return t + L"Còn " + std::to_wstring(rem / 3600) + L" giờ " + std::to_wstring((rem % 3600) / 60) + L" phút.";
}
