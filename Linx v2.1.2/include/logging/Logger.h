/*
Logger dùng chung cho toàn bộ chương trình:
- Ghi log ra file và debug output
- Hỗ trợ nhiều mức độ log (info, warning, error, debug)
- Thread-safe
[2026-11-22 R41] Mặc định ghi vào %LOCALAPPDATA%\LinxController\logs\linx_YYYY-MM-DD.log (mỗi ngày một file,
tự sang file mới khi qua nửa đêm), tự xoá file quá 90 ngày hoặc khi tổng dung lượng quá 200 MB.
SetLogFile(đường dẫn) vẫn dùng được: chuyển sang chế độ một file cố định (dùng cho test).
*/
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <memory>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "AppPaths.h"

// Singleton Logger class
class Logger {
public:
    //Meyers Singleton,chỉ có 1 logger trong toàn bộ chương trình
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

	//hàm ghi log, có thể chỉ định mức độ log (mặc định là info)
    void Write(const std::wstring& message, int level = 0) {
		std::lock_guard<std::mutex> lock(logMutex_);    // Đảm bảo thread-safe
        if (autoDaily_) RollIfNeeded();                 // [R41] sang ngày mới -> file mới
		std::wstring levelStr;  // Xác định mức độ log

        switch (level) {
		case 1: levelStr = L"[WARNING] "; break;    // Mức độ cảnh báo
		case 2: levelStr = L"[ERROR] "; break;      // Mức độ lỗi
		case 3: levelStr = L"[DEBUG] "; break;      // Mức độ debug
		default: levelStr = L"[INFO] "; break;      // Mức độ thông tin (mặc định)
        }
		std::wstring logEntry = GetCurrentTime() + L" " + levelStr + message;   // Tạo chuỗi log hoàn chỉnh với timestamp và mức độ
		OutputDebugStringW((logEntry + L"\n").c_str()); // Ghi log ra debug output
		if (logFile_.is_open()) { // Ghi log ra file nếu file đã được mở
            // [2026-11-16 R27] Ghi UTF-8. Trước đây dùng wofstream với locale mặc định: gặp ký tự ngoài ASCII
            // (tiếng Việt, emoji) là stream lỗi và MỌI dòng log sau đó bị mất.
            std::string u8 = ToUtf8(logEntry);
            logFile_.write(u8.data(), (std::streamsize)u8.size());
            logFile_.put('\n');
            logFile_.flush();
        }
    }

	// dùng để chuyển sang một file cố định (tắt chế độ file theo ngày)
    void SetLogFile(const std::wstring& filename) {
        std::lock_guard<std::mutex> lock(logMutex_);
        autoDaily_ = false;
        if (logFile_.is_open()) {
            logFile_.close();
        }
        logFile_.clear();
        logFile_.open(filename, std::ios::app | std::ios::binary);
    }

	// Bật/tắt ghi log ra console (debug output)
    void EnableConsoleOutput(bool enable) {
        consoleOutput_ = enable;
    }

    // [R41] Xoá file linx_*.log quá keepDays ngày; nếu tổng dung lượng vẫn > maxBytes thì xoá tiếp file cũ nhất.
    // Không bao giờ xoá file đang ghi. Trả về số file đã xoá.
    // keepDays < 0 / maxBytes == 0: dùng giá trị đặt bằng SetRetention (mặc định 90 ngày, 200 MB).
    int CleanupOldLogs(int keepDays = -1, uintmax_t maxBytes = 0) {
        namespace fs = std::filesystem;
        if (keepDays < 0) keepDays = keepDays_;
        if (maxBytes == 0) maxBytes = (uintmax_t)maxMB_ * 1024 * 1024;
        std::error_code ec;
        int removed = 0;
        struct Item { fs::path p; fs::file_time_type t; uintmax_t size; };
        std::vector<Item> items;
        for (auto& e : fs::directory_iterator(LinxLogDir(), ec)) {
            if (!e.is_regular_file(ec)) continue;
            const std::wstring name = e.path().filename().wstring();
            if (name.rfind(L"linx_", 0) != 0 || e.path().extension() != L".log") continue;
            if (e.path() == currentPath_) continue;
            items.push_back({ e.path(), e.last_write_time(ec), e.file_size(ec) });
        }
        const auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24LL * keepDays);
        uintmax_t total = 0;
        std::vector<Item> keep;
        for (auto& it : items) {
            if (it.t < cutoff) { if (fs::remove(it.p, ec)) ++removed; }
            else { keep.push_back(it); total += it.size; }
        }
        std::sort(keep.begin(), keep.end(), [](const Item& a, const Item& b) { return a.t < b.t; });
        for (auto& it : keep) {
            if (total <= maxBytes) break;
            if (fs::remove(it.p, ec)) { ++removed; total -= it.size; }
        }
        return removed;
    }

    // [R43] Đặt thời gian giữ nhật ký (từ màn hình Cài đặt) và dọn ngay theo giá trị mới.
    void SetRetention(int keepDays, int maxMB) {
        keepDays_ = keepDays < 1 ? 1 : keepDays;
        maxMB_ = maxMB < 1 ? 1 : maxMB;
        CleanupOldLogs();
    }

    // [R41] Đường dẫn file log đang ghi (rỗng nếu đang ở chế độ file cố định)
    std::wstring CurrentLogPath() const { std::lock_guard<std::mutex> lock(logMutex_); return autoDaily_ ? currentPath_.wstring() : std::wstring(); }

private:
	// Constructor
    Logger() {
        autoDaily_ = true;
        OpenDaily();
        CleanupOldLogs();
        Write(L"Logger initialized");
    }

	// Destructor
    ~Logger() {
        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

    // [R41] Tên file theo ngày hiện tại: linx_YYYY-MM-DD.log
    static std::wstring TodayStamp(SYSTEMTIME* out = nullptr) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (out) *out = st;
        wchar_t b[32];
        swprintf_s(b, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        return b;
    }

    void OpenDaily() {   // gọi khi đã giữ lock hoặc trong constructor
        currentDay_ = TodayStamp();
        currentPath_ = LinxLogDir() / (L"linx_" + currentDay_ + L".log");
        if (logFile_.is_open()) logFile_.close();
        logFile_.clear();
        logFile_.open(currentPath_, std::ios::app | std::ios::binary);
    }

    void RollIfNeeded() {
        if (TodayStamp() != currentDay_) {
            OpenDaily();
            // dọn file cũ mỗi lần sang ngày (chương trình có thể chạy liên tục nhiều ngày)
            CleanupOldLogs();
        }
    }

	// Hàm lấy thời gian hiện tại dưới dạng chuỗi định dạng HH:MM:SS.mmm
    // [2026-11-16 R27] UTF-16 -> UTF-8
    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    std::wstring GetCurrentTime() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buffer[64];
        swprintf_s(buffer, L"%02d:%02d:%02d.%03d",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return std::wstring(buffer);
    }

	std::wstring logFilename_;  // Tên file log
	std::ofstream logFile_;     // [2026-11-16 R27] luồng byte, nội dung UTF-8
	mutable std::mutex logMutex_;   // Mutex để đảm bảo thread-safe khi ghi log
	bool consoleOutput_ = false;    // Cờ để bật/tắt ghi log ra console (debug output)
    int keepDays_ = 90;                     // [R43] giữ file nhật ký (ngày)
    int maxMB_ = 200;                       // [R43] tổng dung lượng tối đa (MB)
    bool autoDaily_ = false;                // [R41] true: file theo ngày trong thư mục logs
    std::wstring currentDay_;               // [R41] "YYYY-MM-DD" của file đang mở
    std::filesystem::path currentPath_;     // [R41] file đang ghi
};
