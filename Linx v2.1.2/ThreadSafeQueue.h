/*
- tạo hàng đợi thread-safe dùng chung cho nhiều loại dữ liệu trong chương trình đa luồng
- cho phép :
	- nhiều thread có thể push và pop dữ liệu
	- đảm bảo an toàn khi truy cập vào hàng đợi
*/
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>

//Template class
template<typename T>
class ThreadSafeQueue {
public:
	// Thêm phần tử vào hàng đợi
    void Push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        condition_.notify_one();
    }

	// Lấy phần tử từ hàng đợi, trả về false nếu hàng đợi rỗng
    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

	// Chờ dữ liệu xuất hiện trong queue, trả về false nếu hết timeout
    bool WaitPop(T& value, int timeoutMs = 100) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            condition_.wait_for(lock, std::chrono::milliseconds(timeoutMs));
            if (queue_.empty()) {
                return false;
            }
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

	// Kiểm tra nếu hàng đợi rỗng
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

	// Lấy kích thước của hàng đợi
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;  //khóa đọc ghi
    std::queue<T> queue_;   // hàng đợi FIFO
	std::condition_variable condition_; // đánh thức thread đang chờ khi có dữ liệu mới
};