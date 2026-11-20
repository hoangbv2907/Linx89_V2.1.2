// STRUCT LƯU DỮ LIỆU
#pragma once
#include <string>
#include <cstdint>

// Lưu dữ liệu để in ra sau khi hoàn thành một số tác vụ nhất định
struct PrintPersistData {
	std::string message;    // nội dung in
	uint32_t targetCount = 0;   // số lượng mục tiêu cần đạt được 
	uint32_t printedCount = 0;	// số lượng mục tiêu đã đạt được
};