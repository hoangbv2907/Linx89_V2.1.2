// [2026-11-22 R43] Test bố cục co giãn (UILayout.h) và cài đặt (AppSettings).
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include "../Linx v2.1.2/UILayout.h"
#include "../Linx v2.1.2/AppSettings.h"

namespace fs = std::filesystem;
static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int Wd(const RECT& r) { return r.right - r.left; }
static int Ht(const RECT& r) { return r.bottom - r.top; }
static bool Overlap(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

static std::vector<std::pair<const char*, RECT>> All(const UiRects& u) {
    return { {"lblIp",u.lblIp},{"ip",u.ip},{"toggle",u.toggle},{"settings",u.settings},{"lblContent",u.lblContent},{"lblMessage",u.lblMessage},{"message",u.message},{"msgRefresh",u.msgRefresh},
        {"content",u.content},{"upload",u.upload},{"download",u.download},{"fieldInfo",u.fieldInfo},{"lblCount",u.lblCount},
        {"count",u.count},{"lblReached",u.lblReached},{"reached",u.reached},{"set",u.set},{"lblAdjust",u.lblAdjust},
        {"adjust",u.adjust},{"adjustBtn",u.adjustBtn},{"start",u.start},{"print",u.print},{"stop",u.stop},{"status",u.status},
        {"lblWarn",u.lblWarn},{"warn",u.warn},{"lblLog",u.lblLog},{"clear",u.clear},{"log",u.log} };
}

// Mọi control nằm trong client, có kích thước dương, và không chồng lên nhau
static void CheckLayout(int cw96, int ch96, int dpi) {
    const int W = MulDiv(cw96, dpi, 96), H = MulDiv(ch96, dpi, 96);
    const UiRects u = ComputeLayout(W, H, dpi);
    auto v = All(u);
    for (auto& p : v) {
        const RECT& r = p.second;
        const bool inside = r.left >= 0 && r.top >= 0 && r.right <= W && r.bottom <= H;
        const bool positive = Wd(r) > 0 && Ht(r) > 0;
        if (!inside || !positive) std::printf("  [%dx%d@%d] %s ngoai client / rong: (%ld,%ld,%ld,%ld)\n", cw96, ch96, dpi, p.first, r.left, r.top, r.right, r.bottom);
        CHECK(inside && positive);
    }
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j) {
            const bool o = Overlap(v[i].second, v[j].second);
            if (o) std::printf("  [%dx%d@%d] %s chong %s\n", cw96, ch96, dpi, v[i].first, v[j].first);
            CHECK(!o);
        }
}

static void TestLayoutAllSizes() {
    const int dpis[] = { 96, 120, 144, 192 };
    const int sizes[][2] = { {520, 590}, {520, 640}, {640, 700}, {800, 900}, {1280, 720}, {1920, 1040} };
    for (int d : dpis) for (auto& s : sizes) CheckLayout(s[0], s[1], d);
}

static void TestLayoutStretches() {
    const UiRects a = ComputeLayout(520, 640, 96), b = ComputeLayout(1000, 640, 96), c = ComputeLayout(520, 900, 96);
    CHECK(Wd(b.content) > Wd(a.content) && Wd(b.content) - Wd(a.content) == 480);   // giãn đúng bằng phần tăng thêm
    CHECK(Wd(b.warn) > Wd(a.warn) && Wd(b.status) > Wd(a.status) && Wd(b.log) > Wd(a.log) && Wd(b.fieldInfo) > Wd(a.fieldInfo));
    CHECK(Wd(b.message) - Wd(a.message) == 480);                                      // ô chọn bản tin giãn theo chiều ngang
    CHECK(Wd(b.start) > Wd(a.start) && Wd(b.print) > Wd(a.print) && Wd(b.stop) > Wd(a.stop));
    CHECK(Ht(c.log) - Ht(a.log) == 260);                                              // ô nhật ký nhận hết chiều cao tăng thêm
    CHECK(Ht(c.content) == Ht(a.content) && Ht(c.warn) == Ht(a.warn));              // các ô khác không đổi chiều cao
    // control sát phải giữ nguyên khoảng cách tới mép phải
    CHECK(520 - a.settings.right == 1000 - b.settings.right && 520 - a.upload.right == 1000 - b.upload.right && 520 - a.set.right == 1000 - b.set.right);
    // 3 nút lớn: bằng nhau (±1px) và phủ kín khoảng giữa hai lề
    CHECK(std::abs(Wd(b.start) - Wd(b.print)) <= 1 && std::abs(Wd(b.print) - Wd(b.stop)) <= 3);
    CHECK(b.start.left == 20 && b.stop.right == 1000 - 20);
}

static void TestLayoutDpiScale() {
    const UiRects a = ComputeLayout(520, 640, 96), b = ComputeLayout(1040, 1280, 192);
    CHECK(Wd(b.ip) == 2 * Wd(a.ip) && Ht(b.ip) == 2 * Ht(a.ip));                     // 200% DPI = gấp đôi
    CHECK(b.start.top == 2 * a.start.top && Ht(b.start) == 2 * Ht(a.start));
    CHECK(Ht(b.log) == 2 * Ht(a.log));
}

static void TestSettings() {
    CHECK(IsValidIpv4("192.168.1.10") && IsValidIpv4("0.0.0.0") && IsValidIpv4("255.255.255.255"));
    CHECK(!IsValidIpv4("") && !IsValidIpv4("1.2.3") && !IsValidIpv4("1.2.3.4.5") && !IsValidIpv4("256.1.1.1") && !IsValidIpv4("a.b.c.d") && !IsValidIpv4("1..2.3") && !IsValidIpv4("1.2.3.4 "));

    fs::path dir = fs::temp_directory_path() / (L"linx_uitest_" + std::to_wstring(GetTickCount64()));
    fs::create_directories(dir);
    const fs::path p = dir / L"settings.json";

    AppSettings d = AppSettings::Load(p);                                           // file chưa có -> mặc định
    CHECK(d.ip == "127.0.0.1" && d.autoReconnect && d.printMessageName.empty() && d.logKeepDays == 90 && d.logMaxMB == 200 && d.window.w == 0);

    AppSettings s;
    s.ip = "192.168.0.50"; s.autoReconnect = false; s.printMessageName = "MSG1"; s.logKeepDays = 30; s.logMaxMB = 100;
    s.window = { 120, 80, 700, 900, true };
    CHECK(s.Save(p));
    AppSettings r = AppSettings::Load(p);                                           // lưu rồi đọc lại giữ nguyên
    CHECK(r.ip == "192.168.0.50" && !r.autoReconnect && r.printMessageName == "MSG1" && r.logKeepDays == 30 && r.logMaxMB == 100);
    CHECK(r.window.x == 120 && r.window.y == 80 && r.window.w == 700 && r.window.h == 900 && r.window.maximized);
    CHECK(!fs::exists(fs::path(p.wstring() + L".tmp")));                            // không để lại file tạm

    // giá trị ngoài phạm vi / sai được làm sạch
    { std::ofstream f(p, std::ios::trunc); f << R"({"ip":"999.1.1.1","logKeepDays":1,"logMaxMB":999999,"printMessageName":"TENQUADAIVOCUNGDAI123","window":{"w":-5,"h":50000}})"; }
    r = AppSettings::Load(p);
    CHECK(r.ip == "127.0.0.1" && r.logKeepDays == 7 && r.logMaxMB == 5000 && r.printMessageName.empty() && r.window.w == 0 && r.window.h == 0);
    // tên bản tin có ký tự không phải ASCII
    { std::ofstream f(p, std::ios::trunc); f << "{\"printMessageName\":\"B\xE1\xBA\xA3n\"}"; }
    CHECK(AppSettings::Load(p).printMessageName.empty());
    // file hỏng / sai kiểu -> mặc định, không ném lỗi
    { std::ofstream f(p, std::ios::trunc); f << "{ khong phai json"; }
    CHECK(AppSettings::Load(p).ip == "127.0.0.1");
    { std::ofstream f(p, std::ios::trunc); f << R"({"logKeepDays":"abc","autoReconnect":false})"; }
    r = AppSettings::Load(p);
    CHECK(r.logKeepDays == 90 && r.autoReconnect);                                  // sai kiểu -> bỏ cả file
    { std::ofstream f(p, std::ios::trunc); f << "[1,2,3]"; }
    CHECK(AppSettings::Load(p).logMaxMB == 200);

    // Save cũng làm sạch giá trị trước khi ghi
    AppSettings bad; bad.ip = "x"; bad.logKeepDays = 99999;
    CHECK(bad.Save(p));
    r = AppSettings::Load(p);
    CHECK(r.ip == "127.0.0.1" && r.logKeepDays == 3650);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

static void TestClampWindow() {
    const RECT work{ 0, 0, 1920, 1040 };
    RECT r = ClampWindowToWorkArea(100, 50, 700, 800, work, 540, 600);
    CHECK(r.left == 100 && r.top == 50 && Wd(r) == 700 && Ht(r) == 800);            // hợp lệ -> giữ nguyên
    r = ClampWindowToWorkArea(5000, 3000, 700, 800, work, 540, 600);                // nằm ngoài màn hình (đổi màn hình)
    CHECK(r.right <= 1920 && r.bottom <= 1040 && r.left >= 0 && r.top >= 0 && Wd(r) == 700);
    r = ClampWindowToWorkArea(-3000, -2000, 700, 800, work, 540, 600);
    CHECK(r.left == 0 && r.top == 0);
    r = ClampWindowToWorkArea(0, 0, 9000, 9000, work, 540, 600);                    // lớn hơn màn hình
    CHECK(Wd(r) == 1920 && Ht(r) == 1040);
    r = ClampWindowToWorkArea(0, 0, 100, 100, work, 540, 600);                      // nhỏ hơn tối thiểu
    CHECK(Wd(r) == 540 && Ht(r) == 600);
    const RECT small{ 0, 0, 1366, 728 };                                            // màn hình nhỏ (laptop)
    r = ClampWindowToWorkArea(300, 300, 800, 900, small, 540, 600);
    CHECK(Ht(r) == 728 && r.bottom <= 728 && r.top == 0);
    const RECT second{ 1920, 0, 3840, 1040 };                                       // màn hình phụ bên phải
    r = ClampWindowToWorkArea(2000, 100, 700, 800, second, 540, 600);
    CHECK(r.left == 2000 && r.right <= 3840);
}

int main() {
    TestLayoutAllSizes();
    TestLayoutStretches();
    TestLayoutDpiScale();
    TestSettings();
    TestClampWindow();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
