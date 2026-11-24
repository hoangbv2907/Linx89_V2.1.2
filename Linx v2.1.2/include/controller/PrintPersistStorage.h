#pragma once
#include "PrintPersistData.h"
#include <optional>

// Lưu trữ dữ liệu in ấn giữa các phiên làm việc của Linx Controller
class PrintPersistStorage {
public:
	// Lưu dữ liệu in ấn vào file JSON trong thư mục LocalAppData
    static bool Save(const PrintPersistData& data); 
	// Tải dữ liệu in ấn từ file JSON, trả về std::nullopt nếu không tồn tại hoặc lỗi
    static std::optional<PrintPersistData> Load();
};