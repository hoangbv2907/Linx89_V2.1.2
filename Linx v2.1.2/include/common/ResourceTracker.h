/*
- tự động giải phóng các resoure của chương trình theo cơ chế clearup tasks
*/
#pragma once
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include "logging/Logger.h"

// class ResourceTracker 
class ResourceTracker {
private:
	std::vector<std::pair<std::string, std::function<void()>>> cleanupTasks;    // dạng vector - pair tên task - text , kiểu function<void() 
	std::string name_;  // Tên của tracker 

public:

	// Constructor cho phép đặt tên tracker để dễ dàng phân biệt trong log
    ResourceTracker(const std::string& name = "ResourceTracker") : name_(name) {}
    
	// Thêm một cleanup task mới vào tracker
    template<typename T>
    void addCleanup(const std::string& taskName, T&& task) {
		cleanupTasks.emplace_back(taskName, std::forward<T>(task));     // thêm task vào vector
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
            L"] Added cleanup task: " + std::wstring(taskName.begin(), taskName.end()));
    }
    // Thực thi tất cả cleanup task
    void cleanupAll() {

        //Log bắt đầu cleanup
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
            L"] Starting cleanup of " + std::to_wstring(cleanupTasks.size()) + L" resources");
        
		// Thực thi các task theo thứ tự ngược lại (LIFO)
        for (auto it = cleanupTasks.rbegin(); it != cleanupTasks.rend(); ++it) {
			const auto& taskName = it->first;   // Lấy tên task để log
			const auto& task = it->second;  // Lấy function task để thực thi
            try {
				// Log trước khi thực thi task
                Logger::GetInstance().Write(L"Cleaning up: " + std::wstring(taskName.begin(), taskName.end()));
				task(); // Thực thi task
				// Log sau khi thực thi thành công
                Logger::GetInstance().Write(L"✓ Success: " + std::wstring(taskName.begin(), taskName.end()));
            }
			catch (const std::exception& e) {  //exception chuẩn nếu có
				std::string error = e.what();   // Log lỗi nếu có exception
                Logger::GetInstance().Write(L"✗ Failed cleanup [" +
                    std::wstring(taskName.begin(), taskName.end()) +
                    L"]: " + std::wstring(error.begin(), error.end()), 2);
            }
			catch (...) { // không xác định 
                Logger::GetInstance().Write(L"✗ Unknown error in cleanup [" +
                    std::wstring(taskName.begin(), taskName.end()) + L"]", 2);
            }
        }
		cleanupTasks.clear(); // Xóa tất cả tasks sau khi đã thực thi
		// Log kết thúc cleanup
        Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) + L"] Cleanup completed");
    }
    // Manual cleanup của một task cụ thể
    bool cleanupTask(const std::string& taskName) {
		// Tìm task theo tên và thực thi nếu tìm thấy
        for (auto it = cleanupTasks.begin(); it != cleanupTasks.end(); ++it) {
			if (it->first == taskName) {    // Nếu tìm thấy task có tên trùng với taskName
                try {
					it->second();   // Thực thi task
					cleanupTasks.erase(it); // Xóa task khỏi danh sách sau khi thực thi
                    Logger::GetInstance().Write(L"✓ Manually cleaned: " +
                        std::wstring(taskName.begin(), taskName.end()));
                    return true;
                }
                catch (...) {
                    Logger::GetInstance().Write(L"✗ Manual cleanup failed: " +
                        std::wstring(taskName.begin(), taskName.end()), 2);
                    return false;
                }
            }
        }
        return false;
    }

	// Destructor tự động gọi cleanupAll nếu vẫn còn tasks chưa được thực thi
    ~ResourceTracker() {
        if (!cleanupTasks.empty()) {
            Logger::GetInstance().Write(L"[" + std::wstring(name_.begin(), name_.end()) +
                L"] Auto-cleaning in destructor");
            cleanupAll();
        }
    }
    // Kiểm tra số lượng tasks còn lại
    size_t getPendingCleanupCount() const { return cleanupTasks.size(); }
    // Xóa tất cả tasks mà không thực thi (khi transfer ownership)
    void clearWithoutCleanup() {
        cleanupTasks.clear();
    }
};