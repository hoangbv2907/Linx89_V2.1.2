// [R51] Test phân quyền 2 cấp: Vận hành KHÔNG mật khẩu (cấp thấp nhất, mặc định, không hết hạn);
//   Giám sát có mật khẩu viết cứng, tự hạ về Vận hành sau đúng 24 giờ kể từ lúc đăng nhập (không kéo dài được).
#include <cstdio>
#include <string>
#include "auth/AuthCore.h"

using namespace auth;
static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const uint64_t kHour = 3600ull * 1000;

static void TestVerify() {
    CHECK(std::string(kSupervisorPin) == "1234");
    CHECK(VerifyPin("", Role::Operator) && VerifyPin("bat-ky", Role::Operator));          // Vận hành: không cần mật khẩu
    CHECK(VerifyPin("1234", Role::Supervisor));
    CHECK(!VerifyPin("", Role::Supervisor) && !VerifyPin("1111", Role::Supervisor) && !VerifyPin("123", Role::Supervisor));
    CHECK(!VerifyPin("12345", Role::Supervisor) && !VerifyPin(" 1234", Role::Supervisor) && !VerifyPin("1234 ", Role::Supervisor));
    CHECK(!VerifyPin("1234", Role::None) && !VerifyPin("", Role::None));
}

static void TestOperatorDefault() {
    uint64_t now = 5000;
    Session s([&] { return now; });
    CHECK(s.CurrentRole() == Role::Operator && s.IsLoggedIn() && !s.IsSupervisor());    // mở phần mềm: mặc định Vận hành, không cần đăng nhập
    now += 24 * kHour;
    CHECK(s.CurrentRole() == Role::Operator);
    now += 1000 * kHour;                                                               // ~42 ngày: không bao giờ hết
    CHECK(s.CurrentRole() == Role::Operator && s.IsLoggedIn());
    CHECK(s.Login(Role::Operator, "") && s.CurrentRole() == Role::Operator);
    s.Logout();
    CHECK(s.CurrentRole() == Role::Operator && s.IsLoggedIn() && !s.IsSupervisor());    // đăng xuất = về Vận hành
}

static void TestSupervisorSession() {
    uint64_t now = 1000;
    Session s([&] { return now; });
    CHECK(s.Login(Role::Supervisor, "1234") && s.IsSupervisor());
    now += 23 * kHour + 59 * 60 * 1000;                                       // 23 giờ 59 phút
    CHECK(s.IsSupervisor());
    now += 60 * 1000;                                                         // đủ 24 giờ
    CHECK(!s.IsSupervisor() && s.CurrentRole() == Role::Operator && s.IsLoggedIn());   // tự đăng xuất về Vận hành
    now += 100 * kHour;
    CHECK(s.CurrentRole() == Role::Operator);                                 // không tự lên lại Giám sát

    // đúng mốc: 24 giờ trừ 1 ms vẫn còn, đủ 24 giờ thì hết
    Session t([&] { return now; });
    t.Login(Role::Supervisor, "1234");
    now += 24 * kHour - 1;
    CHECK(t.IsSupervisor());
    now += 1;
    CHECK(!t.IsSupervisor());

    // thao tác của người dùng KHÔNG kéo dài phiên: các lần gọi CurrentRole() / IsSupervisor() giữa chừng không đổi mốc
    Session u([&] { return now; });
    u.Login(Role::Supervisor, "1234");
    for (int i = 0; i < 23; ++i) { now += kHour; CHECK(u.IsSupervisor()); }
    now += kHour;
    CHECK(!u.IsSupervisor());

    // đăng nhập lại -> mốc 24 giờ mới
    CHECK(u.Login(Role::Supervisor, "1234") && u.IsSupervisor());
    now += 23 * kHour;
    CHECK(u.IsSupervisor());
    now += kHour;
    CHECK(!u.IsSupervisor());
}

static void TestSwitching() {
    uint64_t now = 0;
    Session s([&] { return now; });
    CHECK(!s.Login(Role::Supervisor, "1111") && s.CurrentRole() == Role::Operator);   // nâng quyền sai: giữ nguyên
    CHECK(s.Login(Role::Supervisor, "1234") && s.IsSupervisor());                     // nâng quyền đúng
    CHECK(s.Login(Role::Operator, "") && s.CurrentRole() == Role::Operator);          // hạ xuống Vận hành không cần mật khẩu
    CHECK(s.Login(Role::Supervisor, "1234") && s.IsSupervisor());
    s.Logout();
    CHECK(!s.IsSupervisor() && s.CurrentRole() == Role::Operator);                     // đăng xuất Giám sát -> Vận hành
    // không giới hạn số lần nhập sai
    for (int i = 0; i < 500; ++i) CHECK(!s.Login(Role::Supervisor, "9999"));
    CHECK(s.Login(Role::Supervisor, "1234"));
}

int main() {
    TestVerify();
    TestOperatorDefault();
    TestSupervisorSession();
    TestSwitching();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
