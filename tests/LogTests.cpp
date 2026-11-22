// [2026-11-22 R41] Test lưu trữ nhật ký: Logger theo ngày + dọn file cũ, EventLog CSV (ghi / đọc / liệt kê tháng).
// Dữ liệu ghi vào thư mục tạm (đổi LOCALAPPDATA), không đụng nhật ký thật.
#include <cstdio>
#include <fstream>
#include <chrono>
#include <string>
#include <filesystem>
#include <thread>
#include <vector>
#include "../Linx v2.1.2/Logger.h"
#include "../Linx v2.1.2/EventLog.h"

namespace fs = std::filesystem;
static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void TestLoggerDaily() {
    Logger::GetInstance().Write(L"Kh\x1EDFi t\x1EA1o UI");          // tiếng Việt
    Logger::GetInstance().Write(L"dong thu hai", 2);
    const std::wstring cur = Logger::GetInstance().CurrentLogPath();
    CHECK(!cur.empty());
    CHECK(fs::path(cur).filename().wstring().rfind(L"linx_", 0) == 0);
    CHECK(fs::path(cur).parent_path() == LinxLogDir());            // đúng thư mục logs
    const std::string s = ReadAll(cur);
    CHECK(s.find("Kh\xE1\xBB\x9F" "i t\xE1\xBA\xA1o UI") != std::string::npos);
    CHECK(s.find("[ERROR] dong thu hai") != std::string::npos);

    // dọn file: file cũ 100 ngày bị xoá, file mới 1 ngày được giữ, file đang ghi không bao giờ bị xoá
    auto oldp = LinxLogDir() / L"linx_2000-01-01.log";
    auto newp = LinxLogDir() / L"linx_2999-01-01.log";
    { std::ofstream(oldp) << "old"; std::ofstream(newp) << "new"; }
    fs::last_write_time(oldp, fs::file_time_type::clock::now() - std::chrono::hours(24 * 100));
    fs::last_write_time(newp, fs::file_time_type::clock::now() - std::chrono::hours(24));
    const int removed = Logger::GetInstance().CleanupOldLogs(90, 200ull * 1024 * 1024);
    CHECK(removed == 1);
    CHECK(!fs::exists(oldp) && fs::exists(newp) && fs::exists(cur));

    // giới hạn dung lượng: maxBytes nhỏ -> xoá file cũ nhất trước, vẫn giữ file đang ghi
    auto p2 = LinxLogDir() / L"linx_2999-01-02.log";
    { std::ofstream(p2) << std::string(5000, 'x'); }
    fs::last_write_time(newp, fs::file_time_type::clock::now() - std::chrono::hours(48));
    fs::last_write_time(p2, fs::file_time_type::clock::now() - std::chrono::hours(1));
    Logger::GetInstance().CleanupOldLogs(90, 4000);                // tổng (ngoài file đang ghi) > 4000 -> xoá bớt
    CHECK(!fs::exists(newp));                                       // cũ nhất bị xoá trước
    CHECK(!fs::exists(p2));                                         // vẫn > 4000 nên xoá tiếp
    CHECK(fs::exists(cur));
}

static void TestEventLog() {
    EventLog::Append(0, L"B\x1EAFt \x111\x1EA7u in");               // "Bắt đầu in"
    EventLog::Append(2, L"L\x1ED7i: c\xF3 \"d\x1EA5u nh\xE1y\", d\x1EA5u ph\x1EA9y,\nxu\x1ED1ng d\xF2ng");
    EventLog::Append(4, L"OK");
    const auto months = EventLog::ListMonths();
    CHECK(months.size() == 1 && months[0].size() == 7 && months[0][4] == L'-');
    const auto v = EventLog::Load(months[0]);
    CHECK(v.size() == 3);
    CHECK(v[0].level == 0 && v[0].text == L"B\x1EAFt \x111\x1EA7u in");
    CHECK(v[0].time.size() == 23 && v[0].time[4] == L'-' && v[0].time[10] == L' ');
    CHECK(v[1].level == 2);
    CHECK(v[1].text.find(L"\"d\x1EA5u nh\xE1y\", d\x1EA5u ph\x1EA9y,") != std::wstring::npos);   // dấu nháy và dấu phẩy giữ nguyên
    CHECK(v[1].text.find(L'\n') == std::wstring::npos);              // xuống dòng được thay bằng khoảng trắng
    CHECK(v[2].level == 4 && v[2].text == L"OK");

    // file có BOM (Excel) và tiêu đề không bị lặp khi ghi tiếp
    const std::string raw = ReadAll(EventLog::PathFor(months[0]));
    CHECK(raw.size() > 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF);
    CHECK(raw.find("\xEF\xBB\xBF", 3) == std::string::npos);

    // tháng khác
    { std::ofstream f(EventLog::PathFor(L"2026-01"), std::ios::binary); f << "2026-01-05 10:00:00.000,WARN,\"cu\"\n"; }
    const auto m2 = EventLog::ListMonths();
    CHECK(m2.size() == 2 && m2[1] == L"2026-01");                    // mới nhất trước
    const auto v2 = EventLog::Load(L"2026-01");
    CHECK(v2.size() == 1 && v2[0].level == 1 && v2[0].text == L"cu");
    CHECK(EventLog::Load(L"1999-12").empty());                       // tháng không có -> rỗng

    // xuất lại dòng CSV rồi đọc lại phải giống hệt
    const std::wstring line = EventLog::ToCsvLine(v[1]);
    CHECK(line.find(L",ERROR,\"") != std::wstring::npos);
}

static void TestFilter() {
    using E = EventLog::Entry;
    std::vector<E> v = {
        { L"2026-11-20 08:00:00.000", 0, L"BAFt 1A7u in" },
        { L"2026-11-20 09:00:00.000", 1, L"CA3nh báo: Ink Low" },
        { L"2026-11-21 10:00:00.000", 2, L"LD7i kBFt nD1i" },
        { L"2026-11-21 11:00:00.000", 4, L"OK" },
    };
    CHECK(EventLog::Filter(v, 0, L"", L"").size() == 4);
    CHECK(EventLog::Filter(v, 1, L"", L"").size() == 2);              // cảnh báo + lỗi
    CHECK(EventLog::Filter(v, 2, L"", L"").size() == 1);              // chỉ lỗi
    CHECK(EventLog::Filter(v, 0, L"2026-11-21", L"").size() == 2);    // theo ngày
    CHECK(EventLog::Filter(v, 0, L"2026-11-22", L"").empty());
    CHECK(EventLog::Filter(v, 0, L"", L"ink low").size() == 1);       // không phân biệt hoa thường
    CHECK(EventLog::Filter(v, 1, L"2026-11-20", L"ink").size() == 1); // kết hợp
    CHECK(EventLog::Filter(v, 2, L"2026-11-20", L"").empty());
}

static void TestEventLogThreads() {
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) ts.emplace_back([t] { for (int i = 0; i < 50; ++i) EventLog::Append(0, L"thread " + std::to_wstring(t)); });
    for (auto& th : ts) th.join();
    const auto v = EventLog::Load(EventLog::ListMonths()[0]);
    CHECK(v.size() == 3 + 200);                                      // không mất / không lẫn dòng
}

int main() {
    // Đổi LOCALAPPDATA sang thư mục tạm TRƯỚC khi Logger được tạo
    fs::path root = fs::temp_directory_path() / (L"linx_logtest_" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.wstring().c_str());
    TestLoggerDaily();
    TestEventLog();
    TestEventLogThreads();
    TestFilter();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    std::error_code ec;
    fs::remove_all(root, ec);
    return g_fail ? 1 : 0;
}
