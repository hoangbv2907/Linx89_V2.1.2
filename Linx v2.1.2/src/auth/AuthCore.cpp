#include "auth/AuthCore.h"
#include <windows.h>

namespace auth {

namespace {
// So sánh thời gian không đổi (không thoát sớm khi gặp ký tự sai)
bool ConstantTimeEquals(const std::string& a, const std::string& b) {
    uint8_t diff = (uint8_t)(a.size() ^ b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}
} // namespace

bool VerifyPin(const std::string& pin, Role role) {
    if (role == Role::Operator) return true;                      // cấp thấp nhất: không mật khẩu
    if (role == Role::Supervisor) return ConstantTimeEquals(pin, kSupervisorPin);
    return false;
}

Session::Session(Clock clock) : clock_(std::move(clock)) {
    if (!clock_) clock_ = []() { return (uint64_t)GetTickCount64(); };
}

bool Session::Login(Role role, const std::string& pin) {
    if (!VerifyPin(pin, role)) return false;
    role_ = role;
    if (role == Role::Supervisor) supervisorSince_ = clock_();    // mốc 24 giờ tính từ lúc đăng nhập
    return true;
}

void Session::Logout() { role_ = Role::Operator; }

Role Session::CurrentRole() {
    if (role_ == Role::Supervisor) {
        const uint64_t elapsed = clock_() - supervisorSince_;
        if (elapsed >= (uint64_t)kSupervisorSessionHours * 3600ull * 1000ull) role_ = Role::Operator;   // hết 24 giờ -> Vận hành
    }
    return role_;                       // Vận hành viên: không bao giờ tự hết
}

} // namespace auth
