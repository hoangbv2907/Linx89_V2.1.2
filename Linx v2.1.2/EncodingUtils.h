#pragma once
#include <string>
#include <windows.h>

// Convert wide string (UTF-16) to UTF-8 string 
static std::string WideToUtf8(const std::wstring& w)
{
	if (w.empty()) return {}; // Nếu chuỗi đầu vào rỗng, trả về chuỗi UTF-8 rỗng
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
		nullptr, 0, nullptr, nullptr); // Tính kích thước buffer cần thiết để chứa chuỗi UTF-8 sau khi chuyển đổi từ wide string (UTF-16). trả về số byte cần thiết, không bao gồm null terminator.
	std::string out(size, '\0');    // Tạo chuỗi UTF-8 với kích thước đã tính
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),   
		out.data(), size, nullptr, nullptr);    // Thực hiện chuyển đổi từ wide string (UTF-16) sang UTF-8, lưu kết quả vào chuỗi out.
    return out;
}

// Convert UTF-8 string to wide string (UTF-16)
static std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};   // Nếu chuỗi đầu vào rỗng, trả về chuỗi wide string rỗng
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
		nullptr, 0);    // Tính kích thước buffer cần thiết để chứa wide string (UTF-16) sau khi chuyển đổi từ UTF-8. trả về số wchar_t cần thiết, không bao gồm null terminator.
	std::wstring out(size, L'\0');  // Tạo chuỗi wide string với kích thước đã tính
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),	
		out.data(), size); // Thực hiện chuyển đổi từ UTF-8 sang wide string (UTF-16), lưu kết quả vào chuỗi out.
    return out;
}
