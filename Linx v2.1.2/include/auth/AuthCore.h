/*
[2026-11-24 R51] Phân quyền 2 cấp:
  - Vận hành viên: cấp THẤP NHẤT, KHÔNG dùng mật khẩu - là cấp mặc định khi mở phần mềm, không giới hạn thời gian.
    Quyền: kết nối, KHỞI ĐỘNG / DỪNG jet, IN / DỪNG in, chọn bản tin, Upload / Download, SET số lượng, Điều chỉnh in bù/trừ;
    mở ⚙ Cài đặt ở chế độ chỉ xem.
  - Giám sát viên (mật khẩu kSupervisorPin viết cứng): thêm chỉnh sửa trong ⚙ Cài đặt + XÓA NHẬT KÝ.
    Phiên Giám sát viên TỰ HẠ về Vận hành viên sau đúng kSupervisorSessionHours (24 giờ) kể từ lúc đăng nhập
    (thao tác không kéo dài được); muốn tiếp tục phải nhập lại mật khẩu.
Không giới hạn số lần nhập sai; không có chức năng đổi / quên mật khẩu. Không phụ thuộc giao diện nên test được.
*/
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace auth {

constexpr const char* kSupervisorPin = "1234";
constexpr int kSupervisorSessionHours = 24;

enum class Role { None, Operator, Supervisor };   // None chỉ dùng cho giá trị không hợp lệ; phiên luôn là Operator hoặc Supervisor

// Vận hành: không cần mật khẩu (luôn đúng). Giám sát: so sánh thời gian không đổi. Role::None luôn sai.
bool VerifyPin(const std::string& pin, Role role);

class Session {
public:
    using Clock = std::function<uint64_t()>;       // đồng hồ đơn điệu (mili giây); mặc định GetTickCount64
    explicit Session(Clock clock = nullptr);

    bool Login(Role role, const std::string& pin);   // Vận hành: luôn được; Giám sát: đúng mật khẩu -> vai trò hiện tại = role
    void Logout();                                   // về Vận hành viên (cấp thấp nhất)
    Role CurrentRole();                              // tự hạ Giám sát -> Vận hành khi đủ 24 giờ kể từ lúc đăng nhập
    bool IsLoggedIn() { return CurrentRole() != Role::None; }   // luôn true (mặc định là Vận hành)
    bool IsSupervisor() { return CurrentRole() == Role::Supervisor; }

private:
    Clock clock_;
    Role role_ = Role::Operator;
    uint64_t supervisorSince_ = 0;
};

} // namespace auth
