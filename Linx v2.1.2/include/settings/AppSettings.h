// [2026-11-22 R43] Cài đặt người dùng, lưu ở %LOCALAPPDATA%\LinxController\settings.json.
// Gồm: IP mặc định, tự kết nối lại, tên bản tin dùng khi in, thời gian giữ nhật ký, vị trí/kích thước cửa sổ.
// Dữ liệu đọc từ file luôn được "làm sạch" (Sanitize) nên file hỏng / sửa tay không làm chương trình lỗi.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <string>

struct AppSettings {
    std::string ip = "127.0.0.1";          // IP máy in mặc định
    bool autoReconnect = true;             // tự kết nối lại khi mất kết nối
    std::string printMessageName;          // tên bản tin trên máy dùng khi in; rỗng = bản tin đang chọn trên máy
    int logKeepDays = 90;                  // giữ file nhật ký (ngày)

    struct Window {
        int x = 0, y = 0;                  // vị trí cửa sổ ở trạng thái bình thường (toạ độ màn hình)
        int w = 0, h = 0;                  // kích thước ngoài (pixel); 0 = chưa lưu -> dùng mặc định
        bool maximized = false;
    } window;

    // Đưa mọi giá trị về phạm vi hợp lệ (IP sai -> 127.0.0.1, ngày giữ 7..3650, MB 20..5000, tên bản tin ASCII <=15 ký tự).
    void Sanitize();

    static std::filesystem::path DefaultPath();
    static AppSettings Load(const std::filesystem::path& path);   // không có / hỏng -> mặc định
    bool Save(const std::filesystem::path& path) const;           // ghi an toàn (file tạm rồi thay thế)
};

// Kiểm tra IP dạng a.b.c.d (mỗi số 0..255).
bool IsValidIpv4(const std::string& s);

// Cửa sổ lưu từ lần trước có thể nằm ngoài màn hình (đổi màn hình / độ phân giải): đưa về trong vùng làm việc.
// Trả về RECT (left, top, right, bottom). Đảm bảo kích thước không nhỏ hơn min và không lớn hơn vùng làm việc,
// và thanh tiêu đề luôn nằm trong vùng làm việc.
RECT ClampWindowToWorkArea(int x, int y, int w, int h, const RECT& work, int minW, int minH);
