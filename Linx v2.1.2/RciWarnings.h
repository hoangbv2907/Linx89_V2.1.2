#pragma once
#include <string>
#include <vector>
#include <cstdint>

std::wstring JoinList(const std::vector<std::wstring>& v, const wchar_t* sep = L", ");
std::wstring DecodePStatus(uint8_t p);
std::wstring DecodeCStatus(uint8_t c);
std::vector<std::wstring> DecodeWarningMask(uint32_t mask);


// [2026-11-18 R35] Danh sách MỌI cảnh báo/lỗi đang có trên máy (P-STATUS, jet fault, warning mask kể cả bit chưa đặt tên)
// để hiển thị trên phần mềm.
std::vector<std::wstring> BuildActiveWarnings(uint8_t pStatus, uint8_t jetState, uint32_t errorMask);

// [2026-11-18 R34] Điều kiện chặn Start Print: trả về lý do (rỗng = được phép in).
// Chặn khi: bình mực/dung môi hết (P-STATUS 7/10), còn lỗi P-STATUS khác chưa xoá, jet fault,
// hoặc warning nghiêm trọng (cover off, dữ liệu hỏng, chưa có message...).
std::wstring CheckPrintBlockers(uint8_t pStatus, uint8_t jetState, uint32_t errorMask);
