// Unit test cho khung RCI / checksum / parse reply.
// Các vector hex lấy từ tài liệu Linx RCI MP65969-7 (Chapter 1 và Appendix D examples).
// Build: xem tests/build_tests.bat. Không cần framework, trả exit code != 0 nếu có test fail.
#include <cstdio>
#include <vector>
#include <cstdint>
#include "rci/RciClient.h"
#include "rci/RciWarnings.h"
#include "rci/MessageBuilder.h"
#include "controller/RequestQueue.h"
#include "logging/Logger.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <iterator>

using Bytes = std::vector<uint8_t>;
static int g_fail = 0, g_run = 0;

#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static ParsedReply Parse(const Bytes& r) {
    return ParseReplyAndVerify(r, [](const Bytes& v) { return RciClient::ComputeChecksum(v); });
}

// --- Checksum ---
static void TestChecksum() {
    // Doc ch.1: STX GS LF NUL "1234567890" ETX -> tổng 0x239 -> checksum C7
    Bytes d = { 0x02,0x1D,0x0A,0x00,'1','2','3','4','5','6','7','8','9','0',0x03 };
    CHECK(RciClient::ComputeChecksum(d) == 0xC7);
    // Tổng mod 256 == 0 -> checksum phải là 00 (không phải 0x100)
    CHECK(RciClient::ComputeChecksum({ 0x00 }) == 0x00);
    CHECK(RciClient::ComputeChecksum({ 0x80, 0x80 }) == 0x00);
}

// --- BuildFrame so với ví dụ của tài liệu ---
static void TestBuildFrame() {
    // Start Jet: 1B 02 0F 1B 03 EC
    CHECK((RciClient::BuildFrame(0x0F) == Bytes{ 0x1B,0x02,0x0F,0x1B,0x03,0xEC }));
    // Start Print: ... EA
    CHECK((RciClient::BuildFrame(0x11) == Bytes{ 0x1B,0x02,0x11,0x1B,0x03,0xEA }));
    // Stop Print: ... E9
    CHECK((RciClient::BuildFrame(0x12) == Bytes{ 0x1B,0x02,0x12,0x1B,0x03,0xE9 }));
    // Status: ... E7
    CHECK((RciClient::BuildFrame(0x14) == Bytes{ 0x1B,0x02,0x14,0x1B,0x03,0xE7 }));
    // Delete Message (cmd 0x1B bị nhân đôi ESC), tên "LINX TEST": checksum 44
    Bytes payload = { 0x01,'L','I','N','X',' ','T','E','S','T',0,0,0,0,0,0,0 };
    Bytes exp = { 0x1B,0x02,0x1B,0x1B,0x01,'L','I','N','X',' ','T','E','S','T',0,0,0,0,0,0,0,0x1B,0x03,0x44 };
    CHECK(RciClient::BuildFrame(0x1B, payload) == exp);
    // Không checksum
    CHECK((RciClient::BuildFrame(0x14, {}, false, false) == Bytes{ 0x1B,0x02,0x14,0x1B,0x03 }));
    // SOH thay STX
    Bytes soh = RciClient::BuildFrame(0x14, {}, true);
    CHECK(soh.size() == 6 && soh[1] == 0x01);
    // STX (02) trong dữ liệu KHÔNG được escape (tài liệu, Example 2 mục Data transfer)
    Bytes f = RciClient::BuildFrame(0x02);
    CHECK((f[2] == 0x02 && f.size() == 6));
}

// --- ParseReply với reply trong tài liệu ---
static void TestParseReply() {
    // Start Jet reply: 1B 06 00 00 0F 1B 03 E8
    auto r = Parse({ 0x1B,0x06,0x00,0x00,0x0F,0x1B,0x03,0xE8 });
    CHECK(r.ok && r.ack && r.p_status == 0 && r.c_status == 0 && r.cmdid == 0x0F && r.data.empty());
    // Status reply: jet running, print waiting, mask 0, checksum DF
    r = Parse({ 0x1B,0x06,0x00,0x00,0x14,0x00,0x04,0,0,0,0,0x1B,0x03,0xDF });
    CHECK(r.ok && r.ack && r.cmdid == 0x14 && r.data.size() == 6 && r.data[1] == 0x04);
    // Status reply ví dụ error mask 90 00 00 00, checksum 51
    r = Parse({ 0x1B,0x06,0x00,0x00,0x14,0x00,0x02,0x90,0,0,0,0x1B,0x03,0x51 });
    CHECK(r.ok && r.data.size() == 6 && r.data[2] == 0x90);
    // Load message reply: checksum D9
    r = Parse({ 0x1B,0x06,0x00,0x00,0x1E,0x1B,0x03,0xD9 });
    CHECK(r.ok && r.ack && r.cmdid == 0x1E);
    // Checksum sai -> không ok
    r = Parse({ 0x1B,0x06,0x00,0x00,0x0F,0x1B,0x03,0xE9 });
    CHECK(!r.ok);
    // NAK, C-STATUS 0x2E: ACK/NAK byte 0x15; checksum = -(15+00+2E+11+03) = 0xA9
    r = Parse({ 0x1B,0x15,0x00,0x2E,0x11,0x1B,0x03,0xA9 });
    CHECK(r.ok && !r.ack && r.c_status == 0x2E && r.cmdid == 0x11);
    // Reply thiếu byte / rác
    CHECK(!Parse({}).ok);
    CHECK(!Parse({ 0x1B,0x06,0x00 }).ok);
    CHECK(!Parse({ 0x00,0x06,0x00,0x00,0x0F,0x1B,0x03,0xE8 }).ok);
}

// --- [2026-11-13 R06] ESC ETX: dữ liệu 1B 03 được gửi là ESC ESC 03, không phải kết thúc khung ---
static void TestEscEtx() {
    // Status reply, error mask = 1B 03 00 00 -> trên dây: 1B 1B 03 00 00. Checksum tính trên dữ liệu đã unescape = C3.
    Bytes wire = { 0x1B,0x06,0x00,0x00,0x14,0x00,0x02,0x1B,0x1B,0x03,0x00,0x00,0x1B,0x03,0xC3 };
    CHECK(RciFindEscEtx(wire) == 12);
    auto r = Parse(wire);
    CHECK(r.ok && r.data.size() == 6 && r.data[2] == 0x1B && r.data[3] == 0x03);
    // Chưa đủ dữ liệu: ESC lẻ ở cuối -> chưa có kết thúc
    CHECK(RciFindEscEtx({ 0x1B,0x06,0x00,0x1B }) == std::string::npos);
    CHECK(RciFindEscEtx({ 0x1B,0x06,0x00,0x00,0x0F,0x1B,0x03 }) == 5);
}

// --- [2026-11-13 R04/R05] Bảng mã trạng thái ---
static void TestStatusTables() {
    CHECK(DecodePStatus(0) == L"No fault");
    CHECK(DecodePStatus(10) == L"Solvent Tank Empty");        // 10 thập phân, không phải 0x10
    CHECK(DecodePStatus(13) == L"Viscosity");
    CHECK(DecodePStatus(20) == L"Printer Over Temperature");
    CHECK(DecodePStatus(36) == L"Printhead Fault");
    CHECK(DecodePStatus(255) == L"Other Fault");
    CHECK(DecodePStatus(0x10).rfind(L"Unknown", 0) == 0);     // 16 không có trong bảng
    CHECK(DecodeCStatus(0x2E) == L"Print command: No message");
    CHECK(DecodeCStatus(0x61) == L"Invalid jet state");
    CHECK(DecodeCStatus(0x66) == L"Unknown line");
    CHECK(DecodeCStatus(0x5D).rfind(L"Unknown", 0) == 0);     // "Not used"
}

// --- [2026-11-13 R09] Mã hoá UTF-8 cho 29D ---
static void TestUtf8Builder() {
    MessageBuilder mb;
    CHECK((mb.BuildTextUtf8(L"AB1") == Bytes{ 'A','B','1' }));
    CHECK((mb.BuildTextUtf8(L"\xE9") == Bytes{ 0xC3,0xA9 }));    // e-acute
    CHECK((mb.BuildTextUtf8(L"A\nB") == Bytes{ 'A','?','B' }));     // control char -> '?'
    CHECK(mb.BuildTextUtf8(L"").empty());
}

static long long ElapsedMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

// --- [2026-11-16 R23/R24] RequestQueue: request khan chen len dau, cho co the ngat ---
static void TestRequestQueue() {
    RequestQueue q;
    Request a{ RequestType::RequestSetCount }, b{ RequestType::RequestStartPrint }, stop{ RequestType::RequestStopPrint };
    q.Push(a); q.Push(b); q.PushFront(stop);
    Request out;
    CHECK(q.Size() == 3);
    CHECK(q.Pop(out, 10) && out.type == RequestType::RequestStopPrint);   // khan di truoc
    CHECK(q.Pop(out, 10) && out.type == RequestType::RequestSetCount);
    CHECK(q.Pop(out, 10) && out.type == RequestType::RequestStartPrint);
    CHECK(q.Empty());

    // het gio khi khong co request
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!q.WaitForRequest(150));
    long long ms = ElapsedMs(t0);
    CHECK(ms >= 120 && ms < 1000);

    // Push tu thread khac danh thuc ngay
    t0 = std::chrono::steady_clock::now();
    std::thread t1([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); q.Push(a); });
    CHECK(q.WaitForRequest(3000));
    CHECK(ElapsedMs(t0) < 1000);
    t1.join();
    CHECK(q.Pop(out, 10));

    // Interrupt danh thuc ngay ca khi khong co request; sau do Pop/Wait tra ve lien
    t0 = std::chrono::steady_clock::now();
    std::thread t2([&] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); q.Interrupt(); });
    CHECK(!q.WaitForRequest(3000));
    CHECK(ElapsedMs(t0) < 1000);
    t2.join();
    t0 = std::chrono::steady_clock::now();
    CHECK(!q.Pop(out, 2000));
    CHECK(ElapsedMs(t0) < 500);
    q.ResetInterrupt();
    t0 = std::chrono::steady_clock::now();
    CHECK(!q.WaitForRequest(120));
    CHECK(ElapsedMs(t0) >= 100);

    q.Push(a); q.Push(b); q.Clear();
    CHECK(q.Empty());
}

// --- [2026-11-16 R27] Logger: dong co ky tu ngoai ASCII khong lam mat cac dong sau ---
static void TestLoggerUtf8() {
    const wchar_t* path = L"logger_test_output.log";
    DeleteFileW(path);
    Logger::GetInstance().SetLogFile(path);
    Logger::GetInstance().Write(L"Kh\x1EDFi t\x1EA1o UI", 1);       // "Khoi tao" co dau (canh bao: file chi ghi canh bao / loi)
    Logger::GetInstance().Write(L"second line after non-ascii", 2);
    Logger::GetInstance().SetLogFile(L"linx_controller.log");
    std::ifstream f("logger_test_output.log", std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(s.find("Kh\xE1\xBB\x9F" "i t\xE1\xBA\xA1o UI") != std::string::npos);   // UTF-8 dung
    CHECK(s.find("second line after non-ascii") != std::string::npos);            // dong sau van duoc ghi
    f.close();
    DeleteFileW(path);
}

int main() {
    TestRequestQueue();
    TestLoggerUtf8();
    TestChecksum();
    TestBuildFrame();
    TestParseReply();
    TestEscEtx();
    TestStatusTables();
    TestUtf8Builder();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
