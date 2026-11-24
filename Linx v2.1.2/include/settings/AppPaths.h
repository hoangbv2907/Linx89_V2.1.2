// [2026-11-22 R41] Đường dẫn dữ liệu của chương trình: %LOCALAPPDATA%\LinxController\{logs, ...}
// Dùng biến môi trường LOCALAPPDATA (bộ test đổi sang thư mục tạm để không đụng dữ liệu thật).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <string>

inline std::filesystem::path LinxDataDir() {
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::filesystem::path base;
    if (n > 0 && n < MAX_PATH) base = buf;
    else {
        std::error_code ec;
        base = std::filesystem::temp_directory_path(ec);
    }
    std::filesystem::path dir = base / L"LinxController";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::filesystem::path LinxLogDir() {
    std::filesystem::path dir = LinxDataDir() / L"logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
