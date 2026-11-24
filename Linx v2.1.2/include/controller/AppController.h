#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <string>
#include <set>
#include <chrono>
#include <vector>

#include "rci/RciClient.h"
#include "controller/PrinterModel.h"
#include "controller/MessageDef.h"
#include "common/ThreadSafeQueue.h"
#include "common/CommonTypes.h"
#include "controller/RequestQueue.h"
#include "common/ResourceTracker.h"

class RciClient;
class PrinterModel;

class AppController {
public:
    // ================== Lifecycle / Ownership ==================
    AppController(HWND mainWindow);   // Constructor
    ~AppController();                 // Destructor

    // ================== Worker thread management ==================
    void StartWorkerThread();               // Tạo và chạy thread nền (WorkerLoop)
    bool StopWorkerThread(int timeoutMs);   // Yêu cầu dừng và join thread trong timeout

    // ================== Cleanup routines ==================
    // Cleanup khẩn cấp: dừng worker càng nhanh càng tốt, force close socket, reset state
    // (StopJet chỉ best-effort nếu còn kết nối)
    void EmergencyCleanup();

    // Shutdown bình thường: stop worker tuần tự, disconnect, giải phóng tài nguyên
    void ComprehensiveCleanup();

    // ================== UI Facade (enqueue request only) ==================
    // Lưu ý: các hàm này KHÔNG làm socket trực tiếp, chỉ push request vào queue
    void Connect(const std::wstring& ipAddress);                 // Enqueue: kết nối
    void Disconnect();                                           // Enqueue: ngắt kết nối

    void UploadContent(const std::wstring& content, int count);  // Enqueue: cập nhật nội dung + số lượng
    void SetCount(int count);                                    // Enqueue: đặt target count

    void StartJet();                                             // Enqueue: bật jet
    void StopJet();                                              // Enqueue: tắt jet

    void StartPrinting(const std::wstring& content, int count);  // Enqueue: bắt đầu in (có thể gồm upload + start)
    void StopPrinting();                                         // Enqueue: dừng in

    // Enqueue: gửi Remote Field Data by Name (thường RCI 0x9E)
    void UploadRemoteFieldData(const std::wstring& fieldName, const std::wstring& value);

    // [2026-11-18 R36] Enqueue: đọc remote field (tên + số ký tự), tên message, print count từ máy về phần mềm.
    void DownloadFromPrinter();
    // [2026-11-18 R37] Enqueue: in bù (+) / trừ (-) sản lượng khi máy đang chạy. delta cộng vào target hiện tại.
    void AdjustCount(int delta);

    // [2026-11-22 R44] Danh sách bản tin trên máy (97D, chỉ khi máy KHÔNG đang in) và chọn bản tin (30D).
    void RefreshMessageList();
    void SelectMessage(const std::wstring& name);

    // ================== Validation / Policy ==================
    bool ValidatePrintContent(const std::wstring& content);      // Kiểm tra nội dung in hợp lệ
    bool ValidatePrintCount(int count);                          // Kiểm tra số lượng in hợp lệ

    // [2026-11-22 R42] Cổng giấy phép: trả false = hết hạn -> chặn các thao tác ĐIỀU KHIỂN in (jet, in, SET, gửi field, in bù/trừ).
    // Kết nối / ngắt / dừng jet / dừng in / tải thông tin vẫn được phép (cần để lấy serial máy in và dừng máy an toàn).
    // Gọi trước StartWorkerThread(); hàm được gọi từ UI thread nên phải thread-safe.
    void SetLicenseGate(std::function<bool()> allowed) { licenseGate_ = std::move(allowed); }

    void DisableAutoReconnect() { autoReconnect_ = false; }      // Tắt tự động reconnect
    void EnableAutoReconnect() { autoReconnect_ = autoReconnectAllowed_.load(); }   // [R43] theo cài đặt người dùng
    // [2026-11-22 R43] Cài đặt "tự kết nối lại": false = không bao giờ tự kết nối lại khi mất kết nối.
    void SetAutoReconnectAllowed(bool allowed) { autoReconnectAllowed_ = allowed; if (!allowed) autoReconnect_ = false; }        // Bật tự động reconnect

    // ================== Getters ==================
    PrinterState GetCurrentState() const;                        // Lấy trạng thái hiện tại trong PrinterModel
    bool IsConnected() const;                                    // Trạng thái kết nối (từ RciClient)

    void SetLastIp(const std::wstring& ip);                      // Lưu IP kết nối gần nhất (config/persist)

    // [2026-11-13 R07] Tên message ĐÃ CÓ sẵn trên máy để Load Print Message (30D) trước khi in.
    // Rỗng = dùng message đang được chọn trên máy (không gửi Load).
    // [2026-11-16 R28] có khoá: UI thread ghi, worker đọc.
    void SetPrintMessageName(const std::string& name) {
        std::lock_guard<std::mutex> lock(nameMutex_);
        printMessageName_ = name;
    }

    // ================== Data persistence ==================
    void SavePrintDataOnExit();                                  // Lưu last_print_data khi đóng ứng dụng
    void LoadPrintDataOnStart();                                 // Load last_print_data khi khởi động

    // ================== Messaging / UI updates ==================
    // Đóng gói state hiện tại từ PrinterModel và PostMessage lên UI (WM_APP_PRINTER_UPDATE)
    void SendStateUpdate();

private:
    // ================== Owned resources ==================
    ResourceTracker resourceTracker;               // Quản lý cleanup resources theo scope
    HWND mainWindow_;                              // Handle cửa sổ chính để PostMessage
    std::unique_ptr<RciClient> rciClient_;         // Client RCI Linx
    std::unique_ptr<PrinterModel> printerModel_;   // Model lưu trạng thái máy in / job

    // ================== Worker thread & request queue ==================
    std::thread workerThread_;                     // Thread xử lý nền
    std::mutex stopMutex_;                         // [2026-11-13 R10] serialize StopWorkerThread/EmergencyCleanup (thay cho biến static)
    std::atomic<bool> destructorCalled_{ false };  // [2026-11-13 R10] thay cho static trong ~AppController
    std::atomic<bool> emergencyInProgress_{ false };       // [2026-11-13 R10] thay cho static trong EmergencyCleanup
    std::atomic<bool> comprehensiveInProgress_{ false };   // [2026-11-13 R10] thay cho static trong ComprehensiveCleanup
    std::string printMessageName_;                 // [2026-11-13 R07] message trên máy cần load trước khi in
    std::function<bool()> licenseGate_;            // [2026-11-22 R42] null = không giới hạn
    bool LicenseBlocked();                         // [2026-11-22 R42] true (kèm log) nếu hết hạn
    std::mutex nameMutex_;                         // [2026-11-16 R28] bảo vệ printMessageName_

    // ---- các biến dưới đây CHỈ được dùng trong worker thread ----
    bool wasConnected_ = false;                    // [2026-11-16 R22] phát hiện cạnh "đang nối -> mất kết nối"
    int statusFailCount_ = 0;                      // [2026-11-16 R18] số lần status không hợp lệ liên tiếp
    std::chrono::steady_clock::time_point jetTransitionStart_{};   // [2026-11-16 R21] mốc bắt đầu Start/Stop Jet
    std::chrono::steady_clock::time_point nextReconnectTime_{};    // [2026-11-16 R23] thời điểm sớm nhất được reconnect
    std::atomic<bool> running_{ false };           // Cờ điều khiển vòng lặp worker
    RequestQueue requestQueue_;                    // Queue chứa các request từ UI

    // Timestamp lệnh RCI gần nhất (dùng throttle/giãn cách gửi lệnh để tránh spam)
    std::chrono::steady_clock::time_point lastCommandTime_;

    // True khi đang chờ jet chuyển trạng thái (starting/stopping) để tránh gửi lệnh chồng
    std::atomic<bool> jetTransitioning_{ false };

    // ================== Reconnect management ==================
    std::atomic<bool> autoReconnectAllowed_{ true };   // [2026-11-22 R43] cài đặt người dùng
    std::atomic<bool> autoReconnect_{ true };      // Tự động reconnect khi mất kết nối
    std::atomic<int> reconnectAttempts_{ 0 };      // Số lần đã thử reconnect
    const int MAX_RECONNECT_ATTEMPTS = 1;          // Giới hạn số lần reconnect

    // ================== State / Delta tracking ==================
    bool lastJetOn_ = false;                       // jetOn lần trước để phát hiện thay đổi (delta)
    bool hasInitialStatus_ = false;                // đã nhận status lần đầu sau connect chưa
    bool persistLoaded_ = false;                   // đã load dữ liệu persist trong phiên này chưa
	bool isTargetConfirmed_ = false;               // đã xác nhận target count với máy in

    uint32_t printBaseline_ = 0;                   // [2026-11-18 R38] print count của máy lúc bắt đầu in; số đã in = count - baseline
    // [2026-11-24 R54] Hỏi số đã in dày hơn (100 ms) khi GẦN ĐỦ sản lượng để giảm in thừa ở tốc độ cao.
    bool     nearTarget_ = false;                  // còn lại <= max(kNearMinRemaining, tốc độ * kNearLookaheadSec)
    double   printRate_ = 0;                       // tốc độ in ước lượng (bản / giây), làm mượt từ các lần đếm
    uint32_t prevCount_ = 0;
    bool     hasPrevCount_ = false;
    std::chrono::steady_clock::time_point prevCountTime_{}, lastStatusPoll_{};
    void     ResetRateTracking() { nearTarget_ = false; printRate_ = 0; hasPrevCount_ = false; }
    bool     stopAtTargetSent_ = false;            // [2026-11-18 R38] đã gửi Stop Print vì đủ sản lượng (chỉ gửi 1 lần)

    uint8_t  lastPStatus_ = 0;                     // P-STATUS lần trước (delta log)
    uint8_t  lastCStatus_ = 0;                     // C-STATUS lần trước (delta log)
    uint32_t lastErrorMask_ = 0;                   // errorMask lần trước (delta log)
    bool     hasLastStatus_ = false;               // đã có status trước đó để so sánh delta chưa
    std::vector<std::wstring> lastWarnings_;       // danh sách cảnh báo lần trước (decoded text)
    PrinterStatus lastStatus_{};                   // snapshot PrinterStatus lần trước

    // ================== Worker loop helpers ==================
    void WorkerLoop();                             // Vòng lặp chính của worker
    void HandleRequest(const Request& request);    // Dispatch xử lý từng request
    void DoPeriodicPoll();                         // Poll định kỳ (status/print count) khi connected
    void TryReconnect();                           // Thử reconnect nếu mất kết nối
    void OnConnectionLost();                       // [2026-11-16 R22] cập nhật model + UI ngay khi phát hiện mất kết nối

    // ================== Request handlers ==================
    // Poll/status
    void HandleStatusRequest();                    // RCI STATUS 0x14
    // Đồng bộ print count (thường: đọc/refresh số lượng đã in từ máy qua RCI)
    void HandlePrintCountRequest();

    // Print
    void HandleStartPrintRequest(const Request& request);    // Start printing
    void HandleStopPrintRequest();                           // Stop printing

    // Job/content/count
    void HandleUploadContentRequest(const Request& request); // Upload nội dung + target count
    void HandleSetCountRequest(const Request& request);      // Set target count
    void HandleUploadRemoteFieldRequest(const Request& req); // Upload nội dung Remote field data by name

    // Jet
    void HandleRefreshMessagesRequest();             // [2026-11-22 R44]
    void HandleSelectMessageRequest(const Request& request);   // [2026-11-22 R44]
    void HandleDownloadFromPrinterRequest();         // [2026-11-18 R36]
    void HandleAdjustCountRequest(const Request& request);   // [2026-11-18 R37]
    // [2026-11-18 R33] Đọc 157D, kiểm tra độ dài, đệm khoảng trắng cho ĐÚNG bằng kích thước remote field.
    // requireField=false: không có field nào trên máy thì để máy tự từ chối (NAK 63).
    bool PrepareRemoteValue(const std::string& fieldName, const std::wstring& text, std::string& outValue, bool requireField);
    void StoreRemoteFieldInfo(const std::string& name, int maxChars);
    bool HandleStartJetRequest();                    // Start jet (best-effort ACK)
    void HandleStopJetRequest();                     // Stop jet

    // Connection
    void HandleConnectRequest(const Request& request);// Connect
    void HandleDisconnectRequest();                  // Disconnect

    // ================== State machine / policy ==================
    void UpdatePrinterState();                       // Cập nhật state machine từ status/raw flags
    bool ShouldPollStatus() const;                   // Quyết định có poll status không
    bool ShouldGetPrintCount() const;                // Quyết định có poll số lượng đã in không
    bool ShouldAutoStartJet() const;                 // Quyết định có auto start jet không

    // ================== Messaging to UI ==================
    void SendLogMessage(const std::wstring& text, int level = 0); // Post log message lên UI
    void SendConnectionUpdate(bool connected);                    // Post trạng thái kết nối lên UI
};
