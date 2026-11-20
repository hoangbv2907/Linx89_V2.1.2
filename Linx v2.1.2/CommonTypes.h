/*
là header chung định nghĩa các kiểu dữ liệu cơ bản dùng trong
AppController và RciClient. là data model + request model cho toàn hệ thống
*/
#pragma once
#include <string>
#include <vector>

// Cổng Ethernet RCI theo tài liệu Linx (MP65969-7, Chapter 1): 29043 cho 5900/7900/CJ400/8800/8900/Linx 10.
// [2026-11-18] Cho phép đổi cổng khi biên dịch bộ test (/DLINX_RCI_PORT=...) để không đụng simulator đang chạy ở 29043.
#ifndef LINX_RCI_PORT
#define LINX_RCI_PORT 29043
#endif
constexpr unsigned short kRciDefaultPort = LINX_RCI_PORT;

//trạng thái máy in tổng quát để cập nhật UI + quyết định có thể gửi lệnh in/jet hay không.
enum class PrinterStateType {
    Disconnected,   //chua kết nối  
    Connected,      //da ket noi
    Connecting,     //dang ket noi
    Reconnecting,   //dang ket noi lai
    StartingJet,    //dang bat jet
    StopingJet,     //dang tat jet
    Idle,           //jet off, máy vừa bật lên
    Ready,           // paused jet on, print off
    Printing,       //jet on, print on
    Error,          // có lỗi
    Unknown         // trạng thái không xác định
};

// Các loại request đến AppController để thực hiện các hành động tương ứng (status update, print control, connection management, content upload...)
enum class RequestType {
    RequestStatus,
    RequestPrintCount,
    RequestSetCount,
    RequestStartPrint,
    RequestStopPrint,
    RequestStartJet,
    RequestStopJet,
    RequestConnect,
    RequestDisconnect,
    RequestUploadContent,
    RequestUploadRemoteField,
    RequestAdjustCount,          // [2026-11-18 R37] in bù/trừ: count = độ lệch (+/-) cộng vào target khi máy đang chạy
    RequestDownloadFromPrinter   // [2026-11-18 R36] đọc remote field + message + count từ máy về phần mềm
};

// Trạng thái máy in tổng quát để cập nhật UI
struct PrinterState {
	PrinterStateType status = PrinterStateType::Disconnected; // trạng thái tổng quát (Disconnected, Connected, Printing, Error...)
	bool jetOn = false; // trạng thái jet (bật/tắt)
	bool printing = false;  // trạng thái in (đang in hay không)
	bool jetTransitioning = false; // trạng thái chuyển đổi jet (đang bật/tắt jet, dùng để hiển thị trạng thái "Starting/Stopping Jet..." trong UI)
	int printedCount = 0;   // số lượng đã in (đếm dựa trên số lần in thành công)
	int targetCount = 0;    // số lượng mục tiêu (được set trước khi in)

	std::wstring jobId; // ID công việc hiện tại 
	std::wstring errorMessage;  // lỗi (nếu có)
	std::wstring statusText;    // text hiển thị trạng thái chi tiết

	// [2026-11-18 R35] cảnh báo/lỗi đang có trên máy (hiển thị trên UI)
	std::vector<std::wstring> warnings;
	// [2026-11-18 R36] thông tin đọc từ máy: remote field (tên + số ký tự BẮT BUỘC) và tên message
	std::wstring remoteFieldName = L"RemoteField1";
	int remoteFieldMax = 0;     // 0 = chưa biết
	std::wstring messageName;

	// kiểm tra có đang ở trạng thái kết nối hay không 
    bool IsConnected() const {
        return status == PrinterStateType::Connected ||
            status == PrinterStateType::Idle ||
            status == PrinterStateType::Printing;
    }
	// kiểm tra có thể gửi lệnh hay không
    bool CanPrint() const {
        return IsConnected() && status != PrinterStateType::Error;
    }
};

// Request model để AppController xử lý các hành động tương ứng
struct Request {
	RequestType type;   // loại request (status update, print control, connection management, content upload...)
	std::vector<uint8_t> payload;   // payload thô (dùng cho các lệnh cần gửi data thẳng xuống RCI)
	std::wstring data;      // dữ liệu text (dùng cho các lệnh cần nội dung text như upload remote field)
	int count = 0;      // số lượng in (dùng cho lệnh set count)
	std::wstring ipAddress; // địa chỉ IP (dùng cho lệnh connect)
	std::wstring fieldName; // tên field (dùng cho lệnh upload remote field)
};

// Helper function để chuyển PrinterState thành text hiển thị cho UI
inline std::wstring StateToText(const PrinterState& st) {
    switch (st.status) {
    case PrinterStateType::StartingJet:
        return L"🟡 STARTING JET...";
    case PrinterStateType::StopingJet:
        return L"🟠 STOPPING JET...";
    case PrinterStateType::Printing:
        return L"🔵 PRINTING";
    case PrinterStateType::Ready:
        return L"🟢 READY";
    case PrinterStateType::Idle:
        return L"⚪ IDLE (Jet OFF)";
    case PrinterStateType::Error:
        return L"🔴 ERROR: " + st.errorMessage;
    case PrinterStateType::Connecting:
        return L"🔄 CONNECTING...";
    case PrinterStateType::Reconnecting:
        return L"🔁 RECONNECTING...";
    case PrinterStateType::Disconnected:
        return L"❌ DISCONNECTED";
    default:
        return L"❓ UNKNOWN";
    }
}