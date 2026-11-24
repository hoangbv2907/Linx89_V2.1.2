/*
   - cơ chế giao tiếp giữa các thread 
   - tổ chức hàng đợi thread-safe để truyền request từ UI -> worker thread
*/
#pragma once
#include "CommonTypes.h"
#include <deque>    // hàng đợi hai đầu (cho phép chen request khẩn lên đầu)
#include <chrono>    //cấu trức hàng đợi FIFO
#include <mutex>    // khóa mutex để bảo vệ truy cập đa luồng
#include <condition_variable>   // đánh thức thread khi có request mới

//class RequestQueue
// [2026-11-16 R23/R24] Viết lại:
//  - PushFront(): request khẩn (Stop Print/Stop Jet/Disconnect) chen lên đầu hàng đợi.
//  - WaitForRequest(): worker chờ "có request HOẶC bị Interrupt HOẶC hết giờ" thay cho sleep_for
//    (sleep không ngắt được làm UI và việc tắt chương trình phải đợi).
//  - Interrupt(): đánh thức worker ngay khi cần dừng thread.
class RequestQueue {
public:
	// Thêm request vào cuối queue
    void Push(const Request& request) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(request);
        }
        condition_.notify_all();
    }

	// Thêm request KHẨN vào đầu queue (được xử lý trước các request đang chờ)
    void PushFront(const Request& request) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_front(request);
        }
        condition_.notify_all();
    }

	// Lấy request từ queue; chờ tối đa timeoutMs. Trả false nếu hết giờ/bị Interrupt mà không có request.
    bool Pop(Request& request, int timeoutMs = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [this] { return !queue_.empty() || interrupted_; });
        if (queue_.empty()) return false;
        request = queue_.front();
        queue_.pop_front();
        return true;
    }

	// Chờ tới khi có request / bị Interrupt / hết timeoutMs. Trả true nếu đang có request chờ xử lý.
    bool WaitForRequest(int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [this] { return !queue_.empty() || interrupted_; });
        return !queue_.empty();
    }

	// Đánh thức mọi thread đang chờ (dùng khi dừng worker). Sau đó Pop/WaitForRequest trả về ngay.
    void Interrupt() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            interrupted_ = true;
        }
        condition_.notify_all();
    }

	// Cho phép chờ lại bình thường (gọi khi khởi động worker).
    void ResetInterrupt() {
        std::lock_guard<std::mutex> lock(mutex_);
        interrupted_ = false;
    }

	// [2026-11-16 R15] Xoá toàn bộ request đang chờ (dùng khi shutdown).
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

	// Kiểm tra queue có rỗng không
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

	// Lấy số lượng request trong queue
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
	mutable std::mutex mutex_;  // mutex để bảo vệ truy cập đa luồng vào queue
	std::deque<Request> queue_; // hàng đợi chứa các request
	std::condition_variable condition_; // điều kiện để đánh thức thread khi có request mới
	bool interrupted_ = false;  // true khi worker cần được đánh thức để dừng
};
