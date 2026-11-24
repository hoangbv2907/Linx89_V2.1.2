/*
lưu, khôi phục trạng thái máy in cuối cùng của chương thình
bao gồm: nội dung in, số lượng mục tiêu cần đạt được, số lượng mục tiêu đã đạt được
*/
#include <windows.h>
#include <filesystem>
#include "Logger.h"
#include "PrintPersistStorage.h"
#include <fstream>
#include "json.hpp"
#include <ShlObj.h>   // SHGetFolderPathW
#pragma comment(lib, "Shell32.lib")

// Sử dụng nlohmann::json để lưu trữ dữ liệu dưới dạng JSON
namespace nlohmann {
    using json = basic_json<>;
}
// Định nghĩa alias để dễ sử dụng
using json = nlohmann::json;

// Lấy đường dẫn thư mục lưu trữ 
static std::wstring GetPersistDir(){
	wchar_t path[MAX_PATH] = { 0 }; // Đảm bảo buffer đủ lớn để chứa đường dẫn
	SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path);  // Lấy đường dẫn thư mục LocalAppData
	std::wstring dir = std::wstring(path) + L"\\LinxController";    // Tạo đường dẫn thư mục con cho Linx Controller
	CreateDirectoryW(dir.c_str(), nullptr);     // Tạo thư mục nếu chưa tồn tại (nếu đã tồn tại thì hàm này sẽ không làm gì)
	return dir; // Trả về đường dẫn thư mục lưu trữ
}

// Lấy đường dẫn đầy đủ đến file lưu trữ dữ liệu in ấn
static std::filesystem::path GetPersistPath(){
    return std::filesystem::path(GetPersistDir()) / L"last_print_data.json";
}

// Lưu dữ liệu in ấn vào file JSON trong thư mục LocalAppData
bool PrintPersistStorage::Save(const PrintPersistData& data){
	auto path = GetPersistPath();   // Lấy đường dẫn file lưu trữ
	json j; // Tạo đối tượng JSON và gán dữ liệu từ PrintPersistData
    j["message"] = data.message;
    j["target"] = data.targetCount;
    j["printed"] = data.printedCount;

	// Ghi dữ liệu JSON vào file
	std::ofstream ofs(path, std::ios::binary);  // Mở file để ghi, nếu file không tồn tại sẽ được tạo mới
	if (!ofs) return false; // Kiểm tra xem file đã mở thành công chưa
	ofs << j.dump(4);   // Ghi dữ liệu JSON vào file
	ofs.close();    // Đóng file sau khi ghi
	return true;    // Trả về true nếu lưu thành công
}

// Tải dữ liệu in ấn từ file JSON, trả về std::nullopt nếu không tồn tại hoặc lỗi
std::optional<PrintPersistData> PrintPersistStorage::Load(){
	auto path = GetPersistPath();   // Lấy đường dẫn file lưu trữ
	std::ifstream ifs(path, std::ios::binary);  // Mở file để đọc, nếu file không tồn tại sẽ trả về std::nullopt
    if (!ifs) return std::nullopt;

    try {
		json j; //Tao đối tượng JSON 
		ifs >> j;       // Đọc dữ liệu JSON từ file
		PrintPersistData data;	// Tạo đối tượng PrintPersistData và gán dữ liệu từ JSON
        data.message = j.value("message", "");
        data.targetCount = j.value("target", 0);
        data.printedCount = j.value("printed", 0);
		return data;// Trả về dữ liệu đã tải thành công
    }
	catch (const nlohmann::json::exception&) {   // [2026-11-13 R14] bỏ biến không dùng	// Nếu có lỗi khi phân tích JSON, ghi log lỗi và trả về std::nullopt
        Logger::GetInstance().Write(L"[Persist] Load FAILED (JSON parse error)", 2);
        return std::nullopt;
    }
}