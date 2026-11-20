/*
Logger dùng chung cho toàn bộ chương trình:
- Ghi log ra file và debug output
- Hỗ trợ nhiều mức độ log (info, warning, error, debug)
- Thread-safe
*/
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <memory>

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

	// dùng để thay đổi file log nếu cần
    void SetLogFile(const std::wstring& filename) {
        std::lock_guard<std::mutex> lock(logMutex_);
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

private:
	// Constructor 
    Logger() {
        SetLogFile(L"linx_controller.log");
        Write(L"Logger initialized");
    }

	// Destructor
    ~Logger() {
        if (logFile_.is_open()) {
            logFile_.close();
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
};