/*
Class : MessageBuilder dùng để chuyển nội dung text thành dữ liệu dạng Byte gửi tới máy in
. dùng trong appController để chuẩn bị playload text trước khi gửi qua TCP.
*/
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "EncodingUtils.h"

class MessageBuilder {
public: 
	// Chuyển nội dung text thành dữ liệu dạng Byte, chỉ giữ lại các ký tự in được (32-126), thay thế các ký tự không in được bằng '?'
    std::vector<uint8_t> BuildSimpleTextField(const std::wstring& text)
    {
        std::vector<uint8_t> out; // tạo Vector chứa byte ASCII của text
		// Duyệt qua từng ký tự trong text, nếu là ký tự in được thì thêm vào vector, nếu không thì thêm '?'
        for (wchar_t ch : text) { 
			if (ch >= 32 && ch <= 126) {    // Kiểm tra nếu ký tự là ký tự in được (ASCII 32-126)
				out.push_back(static_cast<uint8_t>(ch));    // Thêm ký tự vào vector dưới dạng byte
            }
            else {
				out.push_back('?'); // Thay thế ký tự không in được bằng '?'
            }
        }
        return out;
    }

    // [2026-11-13 R09] Bản UTF-8 cho Command 29D (8800/8900 v5.1+ hỗ trợ UTF-8 khi bật "UTF-8 Message Text"
    // trên máy). Ký tự điều khiển (<32, 127) đổi thành '?'; ký tự khác (kể cả tiếng Việt) giữ nguyên.
    // Số BYTE (không phải số ký tự) là giá trị gửi trong trường độ dài của 29D.
    std::vector<uint8_t> BuildTextUtf8(const std::wstring& text)
    {
        std::wstring clean;
        clean.reserve(text.size());
        for (wchar_t ch : text) {
            clean.push_back((ch < 32 || ch == 127) ? L'?' : ch);
        }
        std::string u8 = WideToUtf8(clean);
        return std::vector<uint8_t>(u8.begin(), u8.end());
    }
};  