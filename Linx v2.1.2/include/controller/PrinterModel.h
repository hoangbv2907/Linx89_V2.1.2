/*
model trung tâm lưu trạng thái máy in tương tự datalayer
các module khác sẽ đọc/ghi dữ liệu của máy in thông qua class này.
*/
#pragma once
#include <string>
#include "common/CommonTypes.h"
#include <mutex>

class PrinterModel {
public:
    PrinterModel() {
        // Khởi tạo currentState_ với giá trị mặc định
        currentState_.status = PrinterStateType::Disconnected; // trạng thái kết nối mặc định
        currentState_.statusText = L"Chưa kết nối";
        currentState_.jetOn = false;    // trạng thái jet
        currentState_.printing = false; // trạng thái in
        currentState_.printedCount = 0; // số lượng đã in
        currentState_.targetCount = 0;  // số lượng mục tiêu
    }

    // nội dung job đang in
    // [2026-11-16 R19] Trả BẢN SAO dưới mutex (trước đây trả tham chiếu không khoá -> data race UI/worker).
    std::wstring GetCurrentJobContent() const{
        std::lock_guard<std::mutex> lock(mutex_);
        return currentJobContent_;
    }

	// Đặt trạng thái máy in
    void SetState(PrinterState state) {
		std::lock_guard<std::mutex> lock(mutex_);   // đảm bảo thread-safe khi cập nhật trạng thái
        currentState_ = state;
    }

	// Lấy trạng thái máy in hiện tại
    PrinterState GetState() const {
		std::lock_guard<std::mutex> lock(mutex_); // đảm bảo thread-safe khi đọc trạng thái
        return currentState_;
    }

	// Đặt văn bản trạng thái máy in
    void SetStatusText(const std::wstring& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        statusText_ = status;
        currentState_.statusText = status; 
    }
	// Lấy văn bản trạng thái máy in
    std::wstring GetStatusText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return statusText_;
    }

	// Đặt công việc in hiện tại
    void SetCurrentJob(const std::wstring& jobId, int totalCount) {
        std::lock_guard<std::mutex> lock(mutex_);
		currentJobContent_ = jobId; // tên công việc in hiện tại ID job
		jobTotal_ = totalCount; // tổng số lượng mục tiêu in
		jobCurrent_ = 0;    // reset số lượng đã in
		currentState_.jobId = jobId;    // cập nhật ID công việc 
		currentState_.targetCount = totalCount; // cập nhật số lượng mục tiêu 
		currentState_.printedCount = 0; // reset số lượng đã in 
    }
	
    // [2026-11-18 R37] Đổi số lượng mục tiêu mà KHÔNG reset số đã in (in bù/trừ khi máy đang chạy)
    void SetTargetCount(int totalCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        jobTotal_ = totalCount;
        currentState_.targetCount = totalCount;
    }

    // Cập nhật tiến trình công việc in
    void UpdateJobProgress(int currentCount) {
        std::lock_guard<std::mutex> lock(mutex_);
		jobCurrent_ = currentCount ; // cập nhật số lượng đã in
        currentState_.printedCount = currentCount; 
    }

	// Xóa công việc in hiện tại
    void ClearCurrentJob() {
        std::lock_guard<std::mutex> lock(mutex_);
        currentJobContent_.clear();      // ✅ đúng biến đang dùng
        jobTotal_ = 0;
        jobCurrent_ = 0;
        currentState_.jobId.clear();
        currentState_.targetCount = 0;
        currentState_.printedCount = 0;
    }
	
    // Đặt lỗi cuối cùng
    void SetLastError(const std::wstring& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_ = error;
        currentState_.errorMessage = error;
        currentState_.status = PrinterStateType::Error;
    }
	
    // Lấy lỗi cuối cùng
    std::wstring GetLastError() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastError_;
    }
	
    // Đặt thông tin kết nối máy in
    void SetConnectionInfo(const std::wstring& ip, int port) {
        std::lock_guard<std::mutex> lock(mutex_);
        ipAddress_ = ip;
        port_ = port;
    }
	
    // Lấy địa chỉ IP máy in
    std::wstring GetIpAddress() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ipAddress_;
    }
	
    // Lấy cổng máy in
    int GetPort() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return port_;
    }
	
    // Đặt công việc in đang chờ
    bool HasPendingPrintJob() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return hasPendingJob_;
    }
	
    // Đặt công việc in đang chờ
    void ClearPendingJob() {
        std::lock_guard<std::mutex> lock(mutex_);
        hasPendingJob_ = false;
        printContent_.clear();
    }
	
    // Lấy số lượng mục tiêu đã in hiện tại
    int GetCurrentCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobCurrent_;
    }
	
    // Lấy tổng số lượng mục tiêu in
    int GetTargetCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return jobTotal_;
    }
   
    // Thêm method để cập nhật trạng thái in
    void SetPrintingState(bool isPrinting) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_.printing = isPrinting;
        currentState_.status = isPrinting ? PrinterStateType::Printing : PrinterStateType::Idle;
    }
   
    // Thêm method để cập nhật trạng thái jet
    void SetJetState(bool jetOn) {
        std::lock_guard<std::mutex> lock(mutex_);
        currentState_.jetOn = jetOn;
    }

private:
    mutable std::mutex mutex_;
	PrinterState currentState_;  // trạng thái máy in hiện tại
	std::wstring statusText_ = L"Chưa kết nối"; // văn bản trạng thái máy in
	std::wstring lastError_;       // lỗi cuối cùng (nếu có)
	std::wstring printContent_;     // nội dung công việc in đang chờ (nếu có)
	bool hasPendingJob_ = false;    // trạng thái có công việc in đang chờ hay không

	int jobTotal_ = 0;  // tổng số lượng mục tiêu in
	int jobCurrent_ = 0;    // số lượng mục tiêu đã in hiện tại
	std::wstring currentJobContent_;    // ID công việc in hiện tại 

	std::wstring ipAddress_;    // địa chỉ IP máy in
	int port_ = kRciDefaultPort;   // cổng máy in (RCI mặc định 29043)
};