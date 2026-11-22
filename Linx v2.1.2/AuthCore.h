/*
[2026-11-22 R45] Phân quyền 2 mức dùng chung PIN:
  - Vận hành viên (mặc định, không cần PIN): kết nối, KHỞI ĐỘNG / DỪNG jet, IN / DỪNG in, chọn bản tin, Upload / Download,
    SET số lượng, Điều chỉnh in bù/trừ.
  - Giám sát viên (cần PIN): mở ⚙ Cài đặt (đổi cài đặt, xem nhật ký, nhập mã gia hạn, đổi PIN), XÓA NHẬT KÝ trên màn hình.
PIN 4..8 chữ số, lưu dạng băm PBKDF2-SHA256 có muối. Chưa đặt PIN riêng thì dùng PIN mặc định kDefaultPin.
Không giới hạn số lần nhập sai; không có chức năng "quên PIN" (theo yêu cầu). Không phụ thuộc giao diện nên test được.
*/
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace auth {

constexpr const char* kDefaultPin = "1234";
constexpr int kPinIterations = 100000;

struct PinRecord {
    std::string saltHex;     // 16 byte muối (32 ký tự hex)
    std::string hashHex;     // 32 byte băm (64 ký tự hex)
    int iterations = 0;
    bool IsSet() const { return !saltHex.empty() && !hashHex.empty() && iterations > 0; }
};

// PIN hợp lệ: 4..8 chữ số.
bool IsValidPinFormat(const std::string& pin);

// Tạo bản ghi PIN mới (muối ngẫu nhiên). pin phải hợp lệ; trả bản ghi rỗng nếu lỗi.
PinRecord MakePin(const std::string& pin);

// Kiểm tra PIN. rec chưa đặt (IsSet() == false) -> so với PIN mặc định. So sánh thời gian không đổi.
bool VerifyPin(const PinRecord& rec, const std::string& pin);

enum class Role { Operator, Supervisor };

// Phiên làm việc: mặc định Vận hành; đăng nhập đúng PIN -> Giám sát; không thao tác quá timeout -> tự về Vận hành.
class Session {
public:
    using Clock = std::function<uint64_t()>;       // đồng hồ đơn điệu (mili giây); mặc định GetTickCount64
    explicit Session(Clock clock = nullptr);

    bool Login(const PinRecord& rec, const std::string& pin);   // đúng PIN -> Giám sát viên
    void Logout();
    Role CurrentRole();                // tự hạ về Vận hành nếu quá hạn không thao tác
    bool IsSupervisor() { return CurrentRole() == Role::Supervisor; }
    void Touch();                      // có thao tác của người dùng -> kéo dài phiên giám sát
    void SetTimeoutMinutes(int m);     // 0 = không tự đăng xuất
    int TimeoutMinutes() const { return timeoutMin_; }

private:
    Clock clock_;
    Role role_ = Role::Operator;
    uint64_t lastActive_ = 0;
    int timeoutMin_ = 15;
};

} // namespace auth
