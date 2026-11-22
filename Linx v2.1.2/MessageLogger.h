#pragma once
#include <windows.h>
#include <string>
#include <richedit.h>
//Quản lý một RichEdit control
//Hiển thị log từ AppController sang UI
//Có thể áp dụng font tùy chỉnh
class MessageLogger {
public:
    MessageLogger();
    ~MessageLogger();
	// Tạo RichEdit control
    HWND Create(HWND hParent, int x, int y, int width, int height);
	// Thêm tin nhắn vào RichEdit control
    void AddMessage(const std::wstring& text, int level);
	// Xóa tất cả tin nhắn trong RichEdit control
    void Clear();
	// Đặt font tùy chỉnh cho RichEdit control
    void SetFont(HFONT hFont);
    // [2026-11-22 R43] handle để UIManager::Layout đổi kích thước theo cửa sổ
    HWND GetHandle() const { return hwnd_; }

private:
	HWND hwnd_ = nullptr;   // Handle của RichEdit control
};