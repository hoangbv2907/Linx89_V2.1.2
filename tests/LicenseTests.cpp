// [2026-11-22 R42] Test giới hạn thời gian sử dụng + mã gia hạn (LicenseCore).
// Dữ liệu ghi vào thư mục tạm và khoá registry thử riêng; xoá sạch khi xong.
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include <windows.h>
#include "../Linx v2.1.2/LicenseCore.h"

namespace fs = std::filesystem;
using namespace lic;
static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const wchar_t* kRegTest = L"Software\\LinxControllerTest\\License";
static Bytes g_priv, g_pub, g_otherPub;
static const std::wstring kMachine = L"AAAA-BBBB-CCCC-DDDD";

static fs::path NewDir() {
    static int n = 0;
    fs::path p = fs::temp_directory_path() / (L"linx_lictest_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(++n));
    fs::create_directories(p);
    return p;
}
static void WipeReg() { RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\LinxControllerTest"); }
static StoreConfig Cfg(const fs::path& d) { return { d.wstring(), kRegTest }; }
static std::string Slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void Spit(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << s; }

static void TestBasics() {
    const std::wstring m = MachineId();
    CHECK(m.size() == 19 && m[4] == L'-' && m[9] == L'-' && m[14] == L'-');
    CHECK(MachineId() == m);                                          // ổn định
    CHECK(NormalizeSerial(std::string(" sn12 345\0xx", 12)) == "SN12345");
    CHECK(NormalizeSerial("abcdefghijk") == "ABCDEFGH");              // tối đa 8 ký tự
    CHECK(NormalizeSerial("").empty());
}

static void TestCodes() {
    const std::string code = MakeCode(g_priv, kMachine, "sn123456", 6000, 1);
    CHECK(!code.empty());
    CHECK(code.size() > 100 && code.find('-') != std::string::npos);
    CodeInfo ci;
    CHECK(VerifyCode(g_pub, code, kMachine, "SN123456", ci) == CodeResult::Ok);
    CHECK(ci.hours == 6000 && ci.seq == 1);
    // không phân biệt hoa thường / dấu gạch / khoảng trắng khi nhập
    std::string messy = code;
    for (auto& c : messy) c = (char)tolower((unsigned char)c);
    messy.insert(7, " \n");
    CHECK(VerifyCode(g_pub, messy, kMachine, "sn123456", ci) == CodeResult::Ok);
    CHECK(VerifyCode(g_pub, code, L"AAAA-BBBB-CCCC-DDDE", "SN123456", ci) == CodeResult::WrongMachine);
    CHECK(VerifyCode(g_pub, code, kMachine, "SN123457", ci) == CodeResult::WrongPrinter);
    CHECK(VerifyCode(g_pub, code, kMachine, "", ci) == CodeResult::NoPrinter);
    CHECK(VerifyCode(g_otherPub, code, kMachine, "SN123456", ci) == CodeResult::BadSignature);   // khoá khác
    std::string bad = code;
    bad[10] = (bad[10] == 'A') ? 'B' : 'A';                                                       // sửa 1 ký tự
    CHECK(VerifyCode(g_pub, bad, kMachine, "SN123456", ci) != CodeResult::Ok);
    CHECK(VerifyCode(g_pub, "ABCDE-FGHIJ", kMachine, "SN123456", ci) == CodeResult::BadFormat);
    CHECK(VerifyCode(g_pub, "!!!!", kMachine, "SN123456", ci) == CodeResult::BadFormat);
    CHECK(MakeCode(g_priv, kMachine, "", 100, 1).empty());                                         // thiếu serial
    CHECK(MakeCode(g_priv, kMachine, "SN1", 0, 1).empty());                                        // 0 giờ
    CHECK(MakeCode(g_priv, kMachine, "SN123456", 6000, 2) != code);                                // seq khác -> mã khác
}

static void TestCounterAndExpiry() {
    WipeReg();
    fs::path d = NewDir();
    Service s(g_pub, Cfg(d), kMachine);
    s.Load();
    CHECK(s.LimitHours() == 6000 && s.UsedSeconds() == 0 && !s.Expired() && !s.Tampered());
    CHECK(s.RemainingSeconds() == 6000ll * 3600);

    s.AddSeconds(5999ull * 3600);
    CHECK(!s.Expired() && s.RemainingSeconds() == 3600);
    s.AddSeconds(3600);
    CHECK(s.Expired() && s.RemainingSeconds() == 0);                  // đủ 6000 giờ là hết

    // Tick cộng thời gian thật (chạy ngắn: chỉ kiểm tra không lùi và không nhảy vọt)
    Service t(g_pub, Cfg(NewDir()), kMachine);
    t.Load();
    std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    t.Tick();
    CHECK(t.UsedSeconds() >= 2 && t.UsedSeconds() <= 3);
    t.Tick();
    CHECK(t.UsedSeconds() <= 3);

    // lưu rồi nạp lại: giữ nguyên số đã dùng
    s.Save();
    Service s2(g_pub, Cfg(d), kMachine);
    s2.Load();
    CHECK(s2.UsedSeconds() == 6000ull * 3600 && s2.Expired());
}

static void TestActivate() {
    WipeReg();
    fs::path d = NewDir();
    Service s(g_pub, Cfg(d), kMachine);
    s.Load();
    s.AddSeconds(6000ull * 3600);
    CHECK(s.Expired());
    const std::string code = MakeCode(g_priv, kMachine, "SN123456", 3000, 7);
    uint32_t added = 0;
    CHECK(s.Activate(code, "", &added) == CodeResult::NoPrinter);     // phải đang kết nối máy in
    CHECK(s.Activate(code, "SN999999", &added) == CodeResult::WrongPrinter);
    CHECK(s.Expired());                                               // thất bại thì không đổi gì
    CHECK(s.Activate(code, "sn123456", &added) == CodeResult::Ok && added == 3000);
    CHECK(!s.Expired() && s.LimitHours() == 9000 && s.RemainingSeconds() == 3000ll * 3600);
    CHECK(s.BoundSerial() == "SN123456");
    CHECK(s.Activate(code, "SN123456") == CodeResult::AlreadyUsed);   // dùng lại cùng mã
    CHECK(s.LimitHours() == 9000);

    // mã thứ hai (seq khác) cộng dồn
    CHECK(s.Activate(MakeCode(g_priv, kMachine, "SN123456", 100, 8), "SN123456") == CodeResult::Ok);
    CHECK(s.LimitHours() == 9100);

    // nạp lại vẫn nhớ mã đã dùng (không dùng lại được)
    Service s2(g_pub, Cfg(d), kMachine);
    s2.Load();
    CHECK(s2.LimitHours() == 9100 && s2.Activate(code, "SN123456") == CodeResult::AlreadyUsed);

    // mã của máy tính khác không dùng được
    Service other(g_pub, Cfg(NewDir()), L"1111-2222-3333-4444");
    WipeReg();
    other.Load();
    CHECK(other.Activate(code, "SN123456") == CodeResult::WrongMachine);
}

static void TestTamper() {
    // 1) sửa file nhưng registry còn nguyên -> dùng registry (không mất dữ liệu, không bị giảm số đã dùng)
    WipeReg();
    fs::path d = NewDir();
    {
        Service s(g_pub, Cfg(d), kMachine);
        s.Load();
        s.AddSeconds(4000ull * 3600);
        s.Save();
    }
    std::string orig = Slurp(d / L"license.dat");
    CHECK(!orig.empty());
    Spit(d / L"license.dat", "LXL1|0|6000||" "|deadbeef");           // giả mạo: đặt số đã dùng = 0
    {
        Service s(g_pub, Cfg(d), kMachine);
        s.Load();
        CHECK(!s.Tampered() && s.UsedSeconds() == 4000ull * 3600);    // lấy từ registry
    }
    // 2) khôi phục file cũ (ít giờ hơn) trong khi registry mới hơn -> lấy số lớn hơn
    {
        Service s(g_pub, Cfg(d), kMachine);
        s.Load();
        s.AddSeconds(100ull * 3600);
        s.Save();
    }
    Spit(d / L"license.dat", orig);                                    // file cũ hợp lệ (4000 giờ), registry = 4100
    {
        Service s(g_pub, Cfg(d), kMachine);
        s.Load();
        CHECK(s.UsedSeconds() == 4100ull * 3600);
    }
    // 3) cả hai nơi đều sai -> coi là bị sửa: hết hạn cho tới khi nhập mã hợp lệ
    Spit(d / L"license.dat", "garbage");
    {
        HKEY k; RegOpenKeyExW(HKEY_CURRENT_USER, kRegTest, 0, KEY_SET_VALUE, &k);
        const wchar_t* bad = L"LXL1|0|6000||x|00";
        RegSetValueExW(k, L"State", 0, REG_SZ, (const BYTE*)bad, (DWORD)((wcslen(bad) + 1) * 2));
        RegCloseKey(k);
    }
    {
        Service s(g_pub, Cfg(d), kMachine);
        s.Load();
        CHECK(s.Tampered() && s.Expired());
        CHECK(s.Activate(MakeCode(g_priv, kMachine, "SN123456", 500, 21), "SN123456") == CodeResult::Ok);
        CHECK(!s.Tampered() && !s.Expired() && s.RemainingSeconds() == 500ll * 3600);
    }
    // 4) sao chép license.dat + registry sang MÁY KHÁC (mã máy khác) -> chữ ký không khớp -> hết hạn
    {
        Service moved(g_pub, Cfg(d), L"9999-8888-7777-6666");
        moved.Load();
        CHECK(moved.Tampered() && moved.Expired());
    }
    WipeReg();
}

int main() {
    if (!GenerateKeyPair(g_priv, g_pub)) { std::printf("KHONG TAO DUOC KHOA\n"); return 2; }
    Bytes p2, pu2;
    GenerateKeyPair(p2, g_otherPub);
    TestBasics();
    TestCodes();
    TestCounterAndExpiry();
    TestActivate();
    TestTamper();
    WipeReg();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
