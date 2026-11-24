
#include "controller/AppController.h"
#include "logging/Logger.h"
#include "ui/FontManager.h"
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>
#include <future>
#include "controller/PrintPersistData.h"
#include "controller/PrintPersistStorage.h"
#include "rci/MessageBuilder.h"
#include "common/EncodingUtils.h"
#include "rci/RciWarnings.h"
// ================== Helper functions (namespace ẩn) ==================
static std::wstring ToHex2(uint8_t v) {
    wchar_t buf[8];
    swprintf(buf, 8, L"%02X", v);
    return buf;
}

static std::wstring ToHex8(uint32_t v) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%08X", v);
    return buf;
}

// [2026-11-16 R18/R21/R23/R24] Hằng số điều khiển worker.
static const int kMaxStatusFailures = 5;                              // số lần status lỗi liên tiếp -> coi như mất kết nối
static const auto kJetTransitionTimeout = std::chrono::minutes(5);    // tài liệu: jet shutdown có thể tới 4 phút
static const auto kReconnectInterval = std::chrono::seconds(2);       // giãn cách giữa các lần reconnect
static const int kPollIntervalMs = 300;                               // chu kỳ poll bình thường (status + print count)
static const int kNearPollIntervalMs = 100;                           // [R54] chu kỳ hỏi print count khi gần đủ sản lượng
static const int kNearMinRemaining = 5;                               // [R54] gần đủ: còn <= 5 bản ...
static const double kNearLookaheadSec = 1.5;                          // [R54] ... hoặc còn <= tốc độ in * 1.5 giây
static const int kPollTimeoutMs = 500;                                // timeout cho lệnh poll (status / print count)

// [2026-11-13 R13] Chuyển what() (ANSI code page) sang wstring đúng, thay cho cách copy từng byte (sai với non-ASCII).
static std::wstring AnsiToWide(const char* s) {
    if (!s || !*s) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 1) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

// [2026-11-13 R13] Đếm số ký tự (code point) của chuỗi UTF-8.
static size_t Utf8CharCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

static std::wstring JoinWithComma(const std::vector<std::wstring>& items) {
    std::wstring out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += L", ";
        out += items[i];
    }
    return out;
}

namespace {
    template<typename T>  // T: kiểu status trả về từ RciClient::RequestStatusEx()
    PrinterStatus ConvertFromRciStatus(const T& raw) {
        if (raw.errorMask != 0) {   // Có lỗi → Error
            return PrinterStatus::Error;
        }
        if (raw.printing) {        // Đang in
            return PrinterStatus::Printing;
        }
        if (raw.paused) {        // Paused (đang tạm dừng)
            return PrinterStatus::Idle;
        }
        if (!raw.jetOn) {        // Chưa bật jet
            return PrinterStatus::Idle;
        }
        return PrinterStatus::Connected;        // Jet đang chạy, không in
    }
    template<typename T>
    std::wstring BuildStatusText(const T& raw) {
        std::wstring text = L"Jet=" + std::to_wstring(raw.jetState) +
            L", Print=" + std::to_wstring(raw.printState) +
            L", ErrMask=0x" + ToHex8(raw.errorMask);   // [2026-11-13 R13] trước đây in số thập phân sau "0x"
        return text;
    }
}
// ================== Constructor / Destructor ==================
AppController::AppController(HWND mainWindow) : mainWindow_(mainWindow), resourceTracker("AppController"), lastJetOn_(false) {
    printerModel_ = std::make_unique<PrinterModel>();   // Model lưu trạng thái máy in
    rciClient_ = std::make_unique<RciClient>();      // Client RCI Linx 8900

    //== ĐĂNG KÝ CALLBACK LOG TỪ RCI CLIENT ==
    rciClient_->SetMessageCallback([this](const std::wstring& msg, int level) {
        this->SendLogMessage(L"[Máy in] " + msg, level);
        });
    // ========== ĐĂNG KÝ CLEANUP TASKS ==========
    // 1. RCI Client cleanup
    resourceTracker.addCleanup("RciClient_Disconnect", [this]() {
        if (rciClient_) {
            Logger::GetInstance().Write(L"Disconnecting RCI client...");
            rciClient_->Disconnect();
            Logger::GetInstance().Write(L"RCI client disconnected");
        }
        });
    // 2. Worker thread cleanup
    resourceTracker.addCleanup("WorkerThread_Stop", [this]() {
        Logger::GetInstance().Write(L"Stopping worker thread...");
        StopWorkerThread(3000);
        Logger::GetInstance().Write(L"Worker thread stopped");
        });
    // 3. Request queue cleanup
    resourceTracker.addCleanup("RequestQueue_Clear", [this]() {
        Logger::GetInstance().Write(L"Clearing request queue...");
        requestQueue_.Clear();   // [2026-11-13 R15] trước đây chỉ ghi log, không xoá gì
        Logger::GetInstance().Write(L"Request queue cleared");
        });
    // 4. Model cleanup
    resourceTracker.addCleanup("PrinterModel_Cleanup", [this]() {
        if (printerModel_) {
            Logger::GetInstance().Write(L"Cleaning printer model...");
            printerModel_.reset();
            Logger::GetInstance().Write(L"Printer model cleaned");
        }
        });
    // 5. Message callback cleanup
    resourceTracker.addCleanup("MessageCallback_Clear", [this]() {
        if (rciClient_) {
            Logger::GetInstance().Write(L"Clearing message callbacks...");
            rciClient_->SetMessageCallback(nullptr);
            Logger::GetInstance().Write(L"Message callbacks cleared");
        }
        });
    Logger::GetInstance().Write(
        L"AppController initialized with " +
        std::to_wstring(resourceTracker.getPendingCleanupCount()) + L" cleanup tasks"
    );
}

AppController::~AppController() {
    // [2026-11-13 R10] Cờ gắn với từng object (trước đây là static toàn process).
    if (destructorCalled_.exchange(true)) {
        return;
    }
    try {
        // [2026-11-13 R11] Dừng worker TRƯỚC khi lưu để model không bị đổi giữa chừng.
        StopWorkerThread(2000);
        if (printerModel_) {
            SavePrintDataOnExit();
        }
        resourceTracker.cleanupAll();
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(L"Exception in destructor: " + AnsiToWide(e.what()), 2);
    }
}
// ================== Worker Thread Control ==================
void AppController::StartWorkerThread() {
    if (running_) return;
    requestQueue_.ResetInterrupt();   // [2026-11-16 R23]
    running_ = true;
    workerThread_ = std::thread(&AppController::WorkerLoop, this);
}

// [2026-11-13 R12] Viết lại: không còn std::async + detach (nguy cơ use-after-free và race trên join).
// Cách làm: tắt cờ running_, đóng socket để mọi send/recv đang chặn thoát ngay, rồi join.
// Tham số timeoutMs được giữ lại cho tương thích chữ ký cũ nhưng không còn dùng (join luôn an toàn
// vì mọi thao tác socket đã có timeout và bị ngắt bởi Disconnect()).
bool AppController::StopWorkerThread(int /*timeoutMs*/) {
    std::lock_guard<std::mutex> lock(stopMutex_);
    running_ = false;
    requestQueue_.Interrupt();        // [2026-11-16 R23] đánh thức worker đang chờ trên queue

    if (!workerThread_.joinable()) {
        Logger::GetInstance().Write(L"Worker thread already stopped");
        return true;
    }
    if (std::this_thread::get_id() == workerThread_.get_id()) {
        Logger::GetInstance().Write(L"StopWorkerThread gọi từ chính worker - bỏ qua join", 2);
        return false;
    }
    try {
        if (rciClient_) rciClient_->Disconnect();   // mở khoá recv/send đang chặn
        workerThread_.join();
        Logger::GetInstance().Write(L"Worker thread stopped gracefully");
        return true;
    }
    catch (const std::exception& e) {
        Logger::GetInstance().Write(L"StopWorkerThread join failed: " + AnsiToWide(e.what()), 2);
        return false;
    }
}
// Cleanup khẩn cấp
// [2026-11-13 R12] Không còn detach thread; dùng cùng cơ chế đóng socket + join như StopWorkerThread.
void AppController::EmergencyCleanup() {
    if (emergencyInProgress_.exchange(true)) {
        Logger::GetInstance().Write(L"EmergencyCleanup already in progress");
        return;
    }
    StopWorkerThread(0);
    if (rciClient_) {
        rciClient_->Disconnect();
    }
    resourceTracker.clearWithoutCleanup();
    Logger::GetInstance().Write(L"⚠️ EMERGENCY CLEANUP COMPLETED");
    emergencyInProgress_ = false;
}

void AppController::ComprehensiveCleanup() {
    Logger::GetInstance().Write(L"=== STARTING COMPREHENSIVE CLEANUP ===");
    if (comprehensiveInProgress_.exchange(true)) {   // [2026-11-13 R10] member thay cho static
        Logger::GetInstance().Write(L"ComprehensiveCleanup already in progress");
        return;
    }
    struct ScopeGuard {
        std::atomic<bool>& flag;
        ~ScopeGuard() { flag = false; }
    } guard{ comprehensiveInProgress_ };

    Logger::GetInstance().Write(L"1. Stopping worker threads...");
    if (!StopWorkerThread(5000)) {
        Logger::GetInstance().Write(L"⚠️ Worker thread không dừng sạch", 1);
    }
    // [2026-11-13 R11] Lưu dữ liệu SAU khi worker đã dừng (trước đây lưu khi worker còn chạy).
    if (printerModel_) {
        Logger::GetInstance().Write(L"[Persist] ComprehensiveCleanup -> SavePrintDataOnExit()");
        SavePrintDataOnExit();
    }

    Logger::GetInstance().Write(L"2. Closing network connections...");
    if (rciClient_) {
        rciClient_->Disconnect();
    }

    Logger::GetInstance().Write(L"3. Releasing GDI resources...");
    FontManager::GetInstance().Cleanup();

    Logger::GetInstance().Write(L"4. Clearing containers...");

    Logger::GetInstance().Write(L"5. Additional resource cleanup...");

    printerModel_.reset();
    rciClient_.reset();
    mainWindow_ = nullptr;

    resourceTracker.cleanupAll();

    Logger::GetInstance().Write(L"=== COMPREHENSIVE CLEANUP COMPLETED ===");
}
// ================== UI Facade (enqueue request only) ==================
// [2026-11-16 R20] UI thread KHÔNG còn ghi PrinterState (trước đây GetState -> sửa -> SetState ở đây
// chạy song song với worker cũng đọc-sửa-ghi, bên ghi sau đè mất cập nhật của bên kia).
// Quy ước: PrinterState chỉ được GHI trong worker thread; UI thread chỉ đọc.
// Trạng thái "Đang kết nối..." do HandleConnectRequest (worker) đặt.
void AppController::Connect(const std::wstring& ipAddress) {
    PrinterState st = printerModel_->GetState();   // chỉ đọc
    if (st.status == PrinterStateType::Connecting ||
        st.status == PrinterStateType::Connected)
    {
        Logger::GetInstance().Write(L"Connect ignored: already connecting/connected");
        return;
    }
    Request req{ RequestType::RequestConnect };
    req.data = ipAddress;
    requestQueue_.Push(req);
}

// [2026-11-16 R24] Disconnect / Stop Jet / Stop Print là request KHẨN: chen lên đầu hàng đợi.
void AppController::Disconnect() {
    Request req{ RequestType::RequestDisconnect };
    requestQueue_.PushFront(req);
}

void AppController::UploadContent(const std::wstring& content, int count) {
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    if (!ValidatePrintContent(content)) {
        SendLogMessage(L"Nội dung in không hợp lệ", 2);
        return;
    }
    if (count <= 0) count = printerModel_->GetTargetCount();
    Request req{ RequestType::RequestUploadContent };
    req.data = content;
    req.count = count;
    requestQueue_.Push(req);
}

void AppController::SetCount(int count) {
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    Request req{ RequestType::RequestSetCount };
    req.count = count;
    requestQueue_.Push(req);
}

void AppController::StartJet() {
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    Request req{ RequestType::RequestStartJet };
    requestQueue_.Push(req);
}

void AppController::StopJet() {
    Request req{ RequestType::RequestStopJet };
    requestQueue_.PushFront(req);   // [2026-11-16 R24]
}

void AppController::StartPrinting(const std::wstring& content, int count) {
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    if (!ValidatePrintContent(content)) {
        SendLogMessage(L"Nội dung in không hợp lệ", 2);
        return;
    }
    if (!ValidatePrintCount(count)) {
        SendLogMessage(L"Số lượng in phải từ 1 đến 999.999.999", 2);
        return;
    }

    Request req{ RequestType::RequestStartPrint };
    req.data = content;
    req.count = count;
    requestQueue_.Push(req);
}

void AppController::StopPrinting() {
    Request req{ RequestType::RequestStopPrint };
    requestQueue_.PushFront(req);   // [2026-11-16 R24]
}

void AppController::UploadRemoteFieldData(const std::wstring& fieldName, const std::wstring& value)
{
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    Request req{ RequestType::RequestUploadRemoteField };
    req.fieldName = fieldName;
    req.data = value;              // tận dụng sẵn field data (wstring)
    requestQueue_.Push(req);
}
void AppController::DownloadFromPrinter() {
    Request req{ RequestType::RequestDownloadFromPrinter };
    requestQueue_.Push(req);
}

void AppController::AdjustCount(int delta) {
    if (LicenseBlocked()) return;   // [2026-11-22 R42]
    Request req{ RequestType::RequestAdjustCount };
    req.count = delta;
    requestQueue_.Push(req);
}
// [2026-11-22 R44] Danh sách bản tin / chọn bản tin
void AppController::RefreshMessageList() {
    Request req{ RequestType::RequestRefreshMessages };
    requestQueue_.Push(req);
}

void AppController::SelectMessage(const std::wstring& name) {
    if (LicenseBlocked()) return;
    if (name.empty()) return;
    Request req{ RequestType::RequestSelectMessage };
    req.data = name;
    requestQueue_.Push(req);
}

// [2026-11-22 R42] Hết thời gian sử dụng -> không cho điều khiển in.
bool AppController::LicenseBlocked() {
    if (!licenseGate_ || licenseGate_()) return false;
    SendLogMessage(L"⛔ Đã hết thời gian sử dụng - vào ⚙ Cài đặt › Bản quyền để nhập mã gia hạn và tiếp tục điều khiển máy in", 2);
    return true;
}

// ================== Validation / Policy ==================
bool AppController::ValidatePrintContent(const std::wstring& content) {
    return !content.empty() && content.length() <= 1000;
}

bool AppController::ValidatePrintCount(int count) {
    // [2026-11-13 R16] Lệnh 140D nhận 0..999,999,999 (tài liệu: Message Print Count). UI vẫn yêu cầu >= 1.
    return count > 0 && count <= 999999999;
}
// ================== Getters ==================
PrinterState AppController::GetCurrentState() const {
    return printerModel_->GetState();
}

bool AppController::IsConnected() const {
    return rciClient_ && rciClient_->IsConnected();
}

void AppController::SetLastIp(const std::wstring& ip) {
    if (printerModel_) {
        printerModel_->SetConnectionInfo(ip, kRciDefaultPort);
    }
}
// ================== Data persistence ==================
void AppController::SavePrintDataOnExit() {
    PrintPersistData data;
    data.message = WideToUtf8(printerModel_->GetCurrentJobContent());
    data.targetCount = printerModel_->GetTargetCount();
    data.printedCount = printerModel_->GetCurrentCount();
    PrintPersistStorage::Save(data);
}

void AppController::LoadPrintDataOnStart() {
    if (persistLoaded_) return;
    persistLoaded_ = true;

    auto loaded = PrintPersistStorage::Load();
    if (!loaded) return;

    const auto& d = *loaded;

    printerModel_->SetCurrentJob(Utf8ToWide(d.message), d.targetCount);
    printerModel_->UpdateJobProgress(d.printedCount);
    SendStateUpdate();
}

void AppController::SendStateUpdate() {
    if (!mainWindow_) return;

    auto state = printerModel_->GetState();
    auto statusText = printerModel_->GetStatusText();
    // [2026-11-13 R14] bỏ SetStatusText(statusText): ghi lại chính giá trị vừa đọc, không có tác dụng.

    // ✅ Lấy dữ liệu job/count từ model
    const auto& job = printerModel_->GetCurrentJobContent();
    int target = (int)state.targetCount;     // hoặc printerModel_->GetTargetCount()
    int printed = (int)state.printedCount;   // hoặc printerModel_->GetCurrentCount()

    auto* msg = new PrinterStateMessage{};
    msg->state = state;
    msg->statusText = statusText;
    msg->jobContent = job;
    msg->targetCount = target;
    msg->printedCount = printed;

    if (!PostMessage(mainWindow_, WM_APP_PRINTER_UPDATE, (WPARAM)msg, 0)) delete msg;
}
// ================== Worker loop helpers ==================
void AppController::WorkerLoop() {
    // [2026-11-16 R23] Mọi lần chờ đều qua requestQueue_.WaitForRequest(): bị đánh thức ngay khi UI gửi request
    // hoặc khi StopWorkerThread() gọi Interrupt(). Không còn sleep_for không ngắt được.
    const int POLL_INTERVAL_MS = 100;

    try {
        while (running_) {
            try {
                // 0) ƯU TIÊN TUYỆT ĐỐI: XỬ LÝ REQUEST TỪ UI TRƯỚC
                Request req;
                if (requestQueue_.Pop(req, 0)) {   // [R54] không chặn thêm 50 ms mỗi vòng (chu kỳ poll gần đủ sản lượng = 100 ms)
                    HandleRequest(req);
                    continue;
                }
                if (!running_) break;

                // 1) KIỂM TRA KẾT NỐI
                bool connected = (rciClient_ && rciClient_->IsConnected());
                // [2026-11-16 R22] Vừa mất kết nối -> cập nhật model + UI NGAY (trước đây UI giữ trạng thái cũ).
                if (!connected && wasConnected_) {
                    OnConnectionLost();
                }
                wasConnected_ = connected;

                if (!connected) {
                    PrinterState currentState = printerModel_->GetState();
                    const bool noReconnect =
                        currentState.status == PrinterStateType::Connecting   // 1.1) đang connecting
                        || !autoReconnect_                                     // 1.2) autoReconnect tắt
                        || printerModel_->GetIpAddress().empty()               // 1.3) không có IP
                        || reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS;       // 1.4) hết lượt
                    if (!autoReconnect_) reconnectAttempts_ = 0;
                    if (noReconnect) {
                        requestQueue_.WaitForRequest(POLL_INTERVAL_MS);
                        continue;
                    }
                    // 1.5) Reconnect, giãn cách kRECONNECT_INTERVAL giữa các lần thử
                    const auto now = std::chrono::steady_clock::now();
                    if (now < nextReconnectTime_) {
                        requestQueue_.WaitForRequest(POLL_INTERVAL_MS);
                        continue;
                    }
                    nextReconnectTime_ = now + kReconnectInterval;
                    TryReconnect();
                    continue;
                }
                // 2) Nếu đã kết nối → poll hoặc idle
                DoPeriodicPoll();
                requestQueue_.WaitForRequest(nearTarget_ ? 20 : POLL_INTERVAL_MS);   // [R54] gần đủ: thức dày để poll mỗi 100 ms
            }
            // Catch lỗi trong vòng lặp nội bộ
            catch (const std::exception& e) {
                SendLogMessage(L"Lỗi trong WorkerLoop: " +
                    AnsiToWide(e.what()), 2);
                requestQueue_.WaitForRequest(POLL_INTERVAL_MS);
            }
            catch (...) {
                SendLogMessage(L"Lỗi không xác định trong WorkerLoop", 2);
                requestQueue_.WaitForRequest(POLL_INTERVAL_MS);
            }
        }
    }
    // Catch lỗi crash toàn bộ loop
    catch (...) {
        Logger::GetInstance().Write(L"CRITICAL: WorkerLoop bị crash", 2);
    }
    Logger::GetInstance().Write(L"WorkerLoop exited normally");
}

// [2026-11-16 R22] Gọi trong worker khi phát hiện socket vừa đứt (không phải do người dùng bấm ngắt).
void AppController::OnConnectionLost() {
    jetTransitioning_ = false;      // trạng thái jet không còn biết chắc; sẽ đọc lại sau khi nối lại
    hasInitialStatus_ = false;
    statusFailCount_ = 0;

    const bool willRetry = autoReconnect_
        && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS
        && !printerModel_->GetIpAddress().empty();

    PrinterState st = printerModel_->GetState();
    st.status = willRetry ? PrinterStateType::Reconnecting : PrinterStateType::Disconnected;
    st.statusText = willRetry ? L"Đang kết nối lại..." : L"Mất kết nối";
    st.jetOn = false;
    st.printing = false;
    st.jetTransitioning = false;
    st.warnings.clear();
    st.printerSerial.clear();   // [2026-11-22 R42]
    printerModel_->SetState(st);
    printerModel_->SetStatusText(st.statusText);

    SendLogMessage(L"🔌 Mất kết nối với máy in", 2);
    SendStateUpdate();
    SendConnectionUpdate(false);
    nextReconnectTime_ = std::chrono::steady_clock::now();   // thử lại ngay lần đầu
}

void AppController::HandleRequest(const Request& request) {
    try {
        switch (request.type) {
        case RequestType::RequestConnect:
            HandleConnectRequest(request);
            break;
        case RequestType::RequestDisconnect:
            HandleDisconnectRequest();
            break;
        case RequestType::RequestStartPrint:
            HandleStartPrintRequest(request);
            break;
        case RequestType::RequestStopPrint:
            HandleStopPrintRequest();
            break;
        case RequestType::RequestSetCount:
            HandleSetCountRequest(request);
            break;
        case RequestType::RequestStartJet:
            HandleStartJetRequest();
            break;
        case RequestType::RequestStopJet:
            HandleStopJetRequest();
            break;
        case RequestType::RequestUploadContent:
            HandleUploadContentRequest(request);
            break;
        case RequestType::RequestUploadRemoteField:
            HandleUploadRemoteFieldRequest(request);
            break;
        case RequestType::RequestDownloadFromPrinter:
            HandleDownloadFromPrinterRequest();
            break;
        case RequestType::RequestAdjustCount:
            HandleAdjustCountRequest(request);
            break;
        case RequestType::RequestRefreshMessages:
            HandleRefreshMessagesRequest();
            break;
        case RequestType::RequestSelectMessage:
            HandleSelectMessageRequest(request);
            break;

        default:
            break;
        }

        SendStateUpdate();
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SendLogMessage(L"Lỗi xử lý request: " +
            AnsiToWide(errorMsg.c_str()), 2);
    }
}

void AppController::DoPeriodicPoll() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCommandTime_).count();

    const bool fastPoll = nearTarget_ && printerModel_->GetState().status == PrinterStateType::Printing;
    if (elapsed < (fastPoll ? kNearPollIntervalMs : kPollIntervalMs)) return;
    // [R54] Gần đủ sản lượng: hỏi print count mỗi 100 ms, còn status vẫn giữ chu kỳ 300 ms (không tăng tải lên máy in).
    const auto statusAge = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStatusPoll_).count();
    if (!fastPoll || statusAge >= kPollIntervalMs) {
        lastStatusPoll_ = now;
        HandleStatusRequest();
        // [2026-11-16 R24] Có request của UI đang chờ (vd Stop Print) -> dừng poll để xử lý ngay.
        if (!requestQueue_.Empty()) return;
    }
    if (ShouldGetPrintCount()) {
        HandlePrintCountRequest();
    }
    if (ShouldAutoStartJet()) {
        HandleStartJetRequest();
    }
    // [2026-11-16 R22] bỏ UpdatePrinterState(): việc báo mất kết nối do OnConnectionLost() đảm nhiệm
    // (gọi ở đây sẽ gửi thông báo trùng).
}

void AppController::TryReconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) return; // không log nữa
    auto lastIp = printerModel_->GetIpAddress();
    if (lastIp.empty()) return;

    reconnectAttempts_++;
    SendLogMessage(
        L"Tự động reconnect lần " + std::to_wstring(reconnectAttempts_) +
        L" tới " + lastIp + L"...", 1);
    Request req{ RequestType::RequestConnect };
    req.data = lastIp;
    requestQueue_.Push(req);
}
// ================== Request handlers ==================
// Poll/status
void AppController::HandleStatusRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;

    PrinterStatus raw = rciClient_->RequestStatusEx();
    if (!rciClient_->IsConnected()) return;

    // [2026-11-16 R18] Reply không hợp lệ (timeout/NAK/sai checksum): KHÔNG dùng dữ liệu mặc định
    // (trước đây bị hiểu là jet tắt/không in/hết cảnh báo). Lỗi liên tiếp nhiều lần -> coi như mất kết nối.
    if (!raw.valid) {
        if (++statusFailCount_ >= kMaxStatusFailures) {
            SendLogMessage(L"❌ Máy in không trả lời status " + std::to_wstring(kMaxStatusFailures) +
                L" lần liên tiếp - coi như mất kết nối", 2);
            statusFailCount_ = 0;
            rciClient_->Disconnect();   // vòng lặp worker sẽ gọi OnConnectionLost()
        }
        return;
    }
    statusFailCount_ = 0;

    // =========================
    // A) LOG P-STATUS / C-STATUS (chỉ khi đổi)
    // =========================

    // 1) P-STATUS
    if (!hasLastStatus_ || raw.pStatus != lastPStatus_) {
        if (raw.pStatus == 0 && hasLastStatus_ && lastPStatus_ != 0) {
            SendLogMessage(L"✅ P-STATUS cleared", 4);
        }
        else if (raw.pStatus != 0) {
            SendLogMessage(L"🔴 P-STATUS=0x" + ToHex2(raw.pStatus) + L" " + raw.pStatusText, 2);
        }
        lastPStatus_ = raw.pStatus;
    }

    // 2) C-STATUS (thêm log cleared + sửa level)
    if (!hasLastStatus_ || raw.cStatus != lastCStatus_) {
        if (raw.cStatus == 0 && hasLastStatus_ && lastCStatus_ != 0) {
            SendLogMessage(L"✅ C-STATUS cleared", 4);
        }
        else if (raw.cStatus != 0) {
            // 0x42/0x43 là warning; còn lại coi như error
            int level = (raw.cStatus == 0x42 || raw.cStatus == 0x43) ? 1 : 2;
            SendLogMessage(L"🟠 C-STATUS=0x" + ToHex2(raw.cStatus) + L" " + raw.cStatusText, level);
        }
        lastCStatus_ = raw.cStatus;
    }

    // =========================
    // B) WARNING MASK (0x14) - DIFF BIT (ON/OFF)
    // =========================
    if (!hasLastStatus_ || raw.errorMask != lastErrorMask_) {
        const uint32_t prev = hasLastStatus_ ? lastErrorMask_ : 0;
        const uint32_t now = raw.errorMask;

        const uint32_t added = now & ~prev;
        const uint32_t cleared = prev & ~now;

        if (added) {
            auto addedList = DecodeWarningMask(added);
            if (!addedList.empty()) {
                SendLogMessage(L"⚠️ Warning ON: " + JoinWithComma(addedList), 1);
            }
            else {
                SendLogMessage(L"⚠️ Warning ON (mask=0x" + ToHex8(added) + L")", 1);
            }
        }

        if (cleared) {
            auto clearedList = DecodeWarningMask(cleared);
            if (!clearedList.empty()) {
                SendLogMessage(L"✅ Warning OFF: " + JoinWithComma(clearedList), 4);
            }
            else {
                SendLogMessage(L"✅ Warning OFF (mask=0x" + ToHex8(cleared) + L")", 4);
            }
        }

        if (hasLastStatus_ && prev != 0 && now == 0) {
            SendLogMessage(L"✅ All warnings cleared (mask=0)", 4);
        }

        lastErrorMask_ = now;
    }

    hasLastStatus_ = true;

    // =========================
    // C) UPDATE STATE MACHINE
    // =========================
    PrinterState st = printerModel_->GetState();

    st.jetOn = raw.jetOn;
    st.printing = raw.printing;
    st.jetTransitioning = jetTransitioning_;
    st.warnings = BuildActiveWarnings(raw.pStatus, raw.jetState, raw.errorMask);   // [2026-11-18 R35] hiển thị cảnh báo của máy trên UI

    const bool jetFault = (raw.jetState == 0x04);
    const bool hasFault = (raw.pStatus != 0) || jetFault;
    const bool hasWarning = (raw.errorMask != 0);

    const bool jetOnNow = raw.jetOn;

    // =========================
    // D) HANDLE TRANSITION (Start/Stop Jet)
    // =========================
    if (jetTransitioning_) {
        if (!hasInitialStatus_) {
            lastJetOn_ = jetOnNow;
            hasInitialStatus_ = true;
        }

        // ✅ Nếu có FAULT trong lúc transition → kết thúc transition ngay để khỏi kẹt
        bool transitionDone = hasFault;

        // [2026-11-16 R21] Watchdog: quá thời gian chờ thì thôi chờ, lấy trạng thái thật của máy (tránh kẹt vĩnh viễn).
        if (!transitionDone &&
            std::chrono::steady_clock::now() - jetTransitionStart_ > kJetTransitionTimeout) {
            SendLogMessage(L"⚠️ Quá thời gian chờ jet chuyển trạng thái - lấy trạng thái hiện tại của máy", 1);
            transitionDone = true;
        }

        if (!transitionDone) {
            if (st.status == PrinterStateType::StartingJet) {
                transitionDone = (raw.jetState == 0x00 /*RUNNING*/);
            }
            else if (st.status == PrinterStateType::StopingJet) {
                transitionDone = (raw.jetState == 0x03 /*STOPPED*/);
            }
            else {
                transitionDone = (jetOnNow != lastJetOn_);
            }
        }

        if (transitionDone) {
            jetTransitioning_ = false;
            st.jetTransitioning = false;

            if (hasFault) {
                st.status = PrinterStateType::Error;
                st.errorMessage = (raw.pStatus != 0)
                    ? (L"P-STATUS=0x" + ToHex2(raw.pStatus) + L" " + raw.pStatusText)
                    : L"JET FAULT (jetState=0x04)";
            }
            else if (raw.printing) {
                st.status = PrinterStateType::Printing;
                st.errorMessage.clear();
            }
            else if (raw.jetOn) {
                st.status = PrinterStateType::Ready;
                st.errorMessage.clear();
            }
            else {
                st.status = PrinterStateType::Idle;
                st.errorMessage.clear();
            }

            // (tuỳ chọn) warning chỉ hiển thị nhẹ
            // if (!hasFault && hasWarning) st.statusText += L" ⚠️";

            st.statusText = StateToText(st);

            lastJetOn_ = jetOnNow;
            printerModel_->SetState(st);
            SendStateUpdate();
            return;
        }

        // [2026-11-16 R21] bỏ dòng log lặp lại mỗi chu kỳ poll ("Transition chưa xong").

        st.statusText = StateToText(st);
        printerModel_->SetStatusText(st.statusText);
        printerModel_->SetState(st);
        SendStateUpdate();
        return;
    }

    // =========================
    // E) NORMAL STATE
    // =========================
    if (hasFault) {
        st.status = PrinterStateType::Error;
        st.errorMessage = (raw.pStatus != 0)
            ? (L"P-STATUS=0x" + ToHex2(raw.pStatus) + L" " + raw.pStatusText)
            : L"JET FAULT (jetState=0x04)";
    }
    else if (raw.printing) {
        st.status = PrinterStateType::Printing;
        st.errorMessage.clear();
    }
    else if (raw.jetOn) {
        st.status = PrinterStateType::Ready;
        st.errorMessage.clear();
    }
    else {
        st.status = PrinterStateType::Idle;
        st.errorMessage.clear();
    }

    st.statusText = StateToText(st);

    lastJetOn_ = jetOnNow;
    hasInitialStatus_ = true;
    printerModel_->SetStatusText(st.statusText);
    printerModel_->SetState(st);
    SendStateUpdate();
}
// Đồng bộ print count
void AppController::HandlePrintCountRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;

    uint32_t count = 0;
    std::string msgName;

    if (!rciClient_->RequestMessagePrintCount(count, msgName, "", kPollTimeoutMs)   /* [2026-11-16 R24] 3000 -> 500 ms */) return;
    if (msgName.empty()) return;

    // [2026-11-18 R38] Số đã in = count hiện tại - count lúc bắt đầu in (máy có thể đã giữ count cũ / count do 140D đặt).
    const int targetCount = printerModel_->GetTargetCount();
    const int printed = (count >= printBaseline_) ? (int)(count - printBaseline_) : 0;
    {   // [R54] ước lượng tốc độ in và xác định "gần đủ sản lượng"
        const auto tnow = std::chrono::steady_clock::now();
        if (hasPrevCount_ && count >= prevCount_) {
            const double dt = std::chrono::duration<double>(tnow - prevCountTime_).count();
            if (dt > 0.05) {
                const double inst = (count - prevCount_) / dt;
                printRate_ = printRate_ <= 0 ? inst : 0.5 * printRate_ + 0.5 * inst;
            }
        }
        prevCount_ = count; prevCountTime_ = tnow; hasPrevCount_ = true;
        const int remaining = targetCount - printed;
        nearTarget_ = targetCount > 0 && remaining > 0 &&
            remaining <= (std::max)(kNearMinRemaining, (int)std::ceil(printRate_ * kNearLookaheadSec));
    }

    if (targetCount > 0 && printed >= targetCount) {
        printerModel_->UpdateJobProgress(targetCount);
        // [2026-11-18 R38] ĐỦ SẢN LƯỢNG -> tạm dừng in để tránh in thừa (chỉ gửi 1 lần; lỗi thì thử lại ở chu kỳ sau).
        if (!stopAtTargetSent_) {
            stopAtTargetSent_ = true;
            isTargetConfirmed_ = false;
            SendLogMessage(L"✅ Đã đạt số lượng in", 4);
            if (rciClient_->StopPrint()) {
                SendLogMessage(L"⏹️ Đủ sản lượng - đã gửi lệnh tạm dừng in", 0);
            }
            else {
                stopAtTargetSent_ = false;
                isTargetConfirmed_ = true;
                SendLogMessage(L"❌ Đủ sản lượng nhưng gửi lệnh dừng in thất bại - sẽ thử lại", 2);
            }
        }
    }
    else {
        printerModel_->UpdateJobProgress(printed);
    }

    lastCommandTime_ = std::chrono::steady_clock::now();
}
// Print
// [2026-11-13 R07] Quy trình in theo tài liệu RCI (MP65969-7):
//   (1) bật jet nếu cần -> (2) Load Print Message (30D) một message ĐÃ CÓ trên máy
//   -> (3) Download Remote Field Data (29D, UTF-8) -> (4) Start Print (17D).
// Trước đây code tải remote field TRƯỚC khi có message rồi Load một tên "JOBn" chưa từng tồn tại.
// Lưu ý: 8800/8900/CJ400 bỏ qua print count của 30D; số lượng đích dùng 140D (nút SET).
void AppController::HandleStartPrintRequest(const Request& req) {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"Chưa kết nối máy in", 2);
        return;
    }
    if (!isTargetConfirmed_) {
        SendLogMessage(L"⚠️ Vui lòng SET lại số lượng trước khi in", 1);
        return;
    }
    PrinterStatus raw = rciClient_->RequestStatusEx();
    if (!raw.valid) {   // [2026-11-16 R18]
        SendLogMessage(L"Không đọc được trạng thái máy in - huỷ in", 2);
        return;
    }
    // [2026-11-18 R34] Điều kiện bắt đầu in: hết mực/dung môi, lỗi nghiêm trọng chưa xoá -> KHÔNG in.
    {
        const std::wstring why = CheckPrintBlockers(raw.pStatus, raw.jetState, raw.errorMask);
        if (!why.empty()) {
            SendLogMessage(L"⛔ Không cho phép in: " + why, 2);
            return;
        }
    }
    std::string messageName;
    {   // [2026-11-16 R28]
        std::lock_guard<std::mutex> lock(nameMutex_);
        messageName = printMessageName_;
    }
    if (!raw.jetOn && !HandleStartJetRequest()) {
        SendLogMessage(L"Không bật được jet - huỷ in", 2);
        return;
    }
    // (2) Load message
    if (!messageName.empty()) {
        const uint16_t cnt = (uint16_t)(std::min)(req.count, 65535);
        if (!rciClient_->LoadMessage(messageName, cnt)) {
            SendLogMessage(L"Lỗi LoadMessage", 2);
            return;
        }
    }
    else {
        SendLogMessage(L"Dùng message đang chọn trên máy (chưa đặt tên message)", 1);
    }
    // (3) Remote field data (UTF-8). [2026-11-18 R33] kích thước dữ liệu phải BẰNG đúng kích thước remote field:
    //     đọc 157D, từ chối nếu dài hơn, đệm khoảng trắng nếu ngắn hơn.
    std::string padded;
    if (!PrepareRemoteValue(WideToUtf8(printerModel_->GetState().remoteFieldName), req.data, padded, false)) {
        return;
    }
    // [2026-11-23 R47] Gửi theo TÊN (158D) thay vì 29D: 29D đòi tổng ký tự của TẤT CẢ remote field trong bản tin
    // (bản tin nhiều field -> NAK 64). 158D chỉ ghi đúng field đã chọn, độ dài đã đệm đúng kích thước.
    if (!padded.empty()) {
        const std::string fname = WideToUtf8(printerModel_->GetState().remoteFieldName);
        if (!rciClient_->SendRemoteFieldDataByName(fname, padded, 3000)) {
            SendLogMessage(L"Lỗi gửi remote field (158D)", 2);
            return;
        }
    }
    // [2026-11-18 R38] Mốc count của máy trước khi in; số đã in = count - mốc.
    {
        uint32_t c0 = 0; std::string n0;
        printBaseline_ = rciClient_->RequestMessagePrintCount(c0, n0, "", kPollTimeoutMs) ? c0 : 0;
    }
    // (4) Start print (đã kiểm tra ACK + P-STATUS)
    if (!rciClient_->StartPrint()) {
        SendLogMessage(L"Lỗi StartPrint", 2);
        return;
    }
    stopAtTargetSent_ = false;
    ResetRateTracking();
    printerModel_->SetCurrentJob(req.data, req.count);
    SendLogMessage(L"Đã gửi job in mới (RCI chuẩn)", 4);
    lastCommandTime_ = std::chrono::steady_clock::now();
}

void AppController::HandleStopPrintRequest() {
    if (rciClient_ && rciClient_->IsConnected()) {
        if (!rciClient_->StopPrint()) {
            SendLogMessage(L"Lỗi gửi lệnh StopPrint", 2);
            return;
        }
    }
    SendLogMessage(L"Đã gửi lệnh dừng in, chờ máy in phản hồi...", 4);
    lastCommandTime_ = std::chrono::steady_clock::now();
}
// Job/content/count
void AppController::HandleUploadContentRequest(const Request& request) {
    int target = request.count;
    if (target <= 0) target = 1;
    printerModel_->SetCurrentJob(request.data, target);
    SendLogMessage(L"Đã cập nhật nội dung in và số lượng: " + std::to_wstring(target), 4);
    SavePrintDataOnExit();
    lastCommandTime_ = std::chrono::steady_clock::now();
}

void AppController::HandleSetCountRequest(const Request& req) {
    int newTarget = req.count;
    if (!ValidatePrintCount(newTarget)) {
        SendLogMessage(L"Số lượng in phải từ 1 đến 999.999.999", 2);
        return;
    }
    if (!rciClient_ || !rciClient_->IsConnected()) {   // [2026-11-13 R14] thiếu kiểm tra kết nối
        SendLogMessage(L"Chưa kết nối máy in", 2);
        return;
    }
    // ✅ đảm bảo chọn đúng message trước khi set count
    // ví dụ: dùng lastLoadedMsgName_ (bạn cần lưu nó khi LoadMessage OK)
    bool ok = rciClient_->SetMessagePrintCount((uint32_t)newTarget, "", 3000);
    if (!ok) {
        SendLogMessage(L"❌ Set Message Print Count (0x8C) thất bại", 2);
        return;
    }
    // ✅ cập nhật model sau khi printer OK
    stopAtTargetSent_ = false;   // [2026-11-18 R38]
    isTargetConfirmed_ = true;
    int curPrinted = printerModel_->GetCurrentCount();
    printerModel_->SetCurrentJob(printerModel_->GetCurrentJobContent(), newTarget);
    printerModel_->UpdateJobProgress(0);

    SendStateUpdate();
    SendLogMessage(L"✅ Đã gửi số lượng in lên máy (0x8C)", 4);
    lastCommandTime_ = std::chrono::steady_clock::now();
}

// [2026-11-13 R08] Lệnh 158D yêu cầu số ký tự của giá trị ĐÚNG bằng số ký tự tối đa của field
// (ngược lại máy trả NAK 23 "Parameter rejected"). Đọc độ dài bằng 157D rồi đệm khoảng trắng.
void AppController::HandleUploadRemoteFieldRequest(const Request& req)
{
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"❌ Chưa kết nối máy in", 2);
        return;
    }
    std::string field = WideToUtf8(req.fieldName);
    std::string value;
    // [2026-11-18 R33] Dùng chung PrepareRemoteValue (đọc 157D, kiểm tra, đệm đúng kích thước). Không đọc được 157D -> huỷ.
    if (!PrepareRemoteValue(field, req.data, value, true)) return;

    if (!rciClient_->SendRemoteFieldDataByName(field, value, 3000))   return;
    int target = printerModel_->GetTargetCount();
    if (target <= 0) target = 1;

    printerModel_->SetCurrentJob(req.data, target);
    SavePrintDataOnExit();
    SendLogMessage(L"✅ Đã gửi nội dung vào Remote Field (0x9E) và lưu nội dung", 4);
    lastCommandTime_ = std::chrono::steady_clock::now();
}
// [2026-11-18 R33] Chuẩn bị giá trị remote field: UTF-8, đúng BẰNG số ký tự của field (đệm khoảng trắng).
// requireField=true  : field phải tồn tại (upload theo tên).
// requireField=false : (đường 29D) nếu máy không báo field nào thì không đệm, để máy tự từ chối (NAK 63);
//                      field tên không khớp thì dùng field đầu tiên.
bool AppController::PrepareRemoteValue(const std::string& fieldName, const std::wstring& text,
    std::string& outValue, bool requireField)
{
    {
        const auto bytes = MessageBuilder().BuildTextUtf8(text);
        outValue.assign(bytes.begin(), bytes.end());
    }

    std::vector<RemoteFieldInfo> fields;
    if (!rciClient_->RequestRemoteFields(fields)) {
        SendLogMessage(L"❌ Không đọc được kích thước remote field (157D) - huỷ để tránh gửi sai độ dài", 2);
        return false;
    }
    if (fields.empty()) {
        if (requireField) {
            SendLogMessage(L"❌ Remote field không tồn tại trên máy: " + Utf8ToWide(fieldName), 2);
            return false;
        }
        return true;
    }
    const RemoteFieldInfo* info = nullptr;
    for (const auto& f : fields) if (f.name == fieldName) { info = &f; break; }
    if (!info) {
        if (requireField) {
            SendLogMessage(L"❌ Remote field không tồn tại trên máy: " + Utf8ToWide(fieldName), 2);
            return false;
        }
        info = &fields.front();
    }
    const size_t chars = Utf8CharCount(outValue);
    if (chars > info->maxChars) {
        SendLogMessage(L"❌ Giá trị dài " + std::to_wstring(chars) + L" ký tự, field chỉ nhận " +
            std::to_wstring((int)info->maxChars), 2);
        return false;
    }
    outValue.append(info->maxChars - chars, ' ');
    StoreRemoteFieldInfo(info->name, info->maxChars);
    return true;
}

// Lưu tên + kích thước field vào state để UI hiển thị / giới hạn ô nhập.
void AppController::StoreRemoteFieldInfo(const std::string& name, int maxChars) {
    PrinterState st = printerModel_->GetState();
    st.remoteFieldName = Utf8ToWide(name);
    st.remoteFieldMax = maxChars;
    printerModel_->SetState(st);
}

// [2026-11-22 R44] ----- Danh sách bản tin trên máy (97D) + chọn bản tin (30D) -----
// Danh sách chỉ đọc được khi máy KHÔNG in (manual: 97D không hợp lệ khi đang in) -> đang in thì dùng danh sách đã lưu.
void AppController::HandleRefreshMessagesRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    PrinterStatus s = rciClient_->RequestStatusEx();
    if (s.valid && s.printing) {
        SendLogMessage(L"⏸️ Máy đang in - không đọc lại được danh sách bản tin (dùng danh sách đã lưu)", 1);
        return;
    }
    std::vector<std::string> names;
    bool busy = false;
    if (!rciClient_->RequestMessageList(names, 3000, &busy)) {
        if (busy) SendLogMessage(L"⏸️ Máy đang in - không đọc lại được danh sách bản tin", 1);
        else SendLogMessage(L"❌ Không đọc được danh sách bản tin từ máy (97D) - có thể nhập tên bản tin ở ⚙ Cài đặt", 2);
        return;
    }
    std::vector<std::wstring> list;
    for (const auto& n : names) list.push_back(Utf8ToWide(n));
    std::sort(list.begin(), list.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    auto has = [&](const std::wstring& n) {
        for (const auto& x : list) if (_wcsicmp(x.c_str(), n.c_str()) == 0) return true;
        return false;
    };
    PrinterState st = printerModel_->GetState();
    if (st.messageName.empty()) {   // chưa biết bản tin đang tải trên máy: hỏi bằng 141D (tên toàn 0 = hiện hành)
        uint32_t c = 0; std::string cur;
        if (rciClient_->RequestMessagePrintCount(c, cur, "", kPollTimeoutMs) && !cur.empty()) st.messageName = Utf8ToWide(cur);
    }
    std::string sel;
    { std::lock_guard<std::mutex> lock(nameMutex_); sel = printMessageName_; }
    std::wstring selW = Utf8ToWide(sel);
    if (!list.empty() && !selW.empty() && !has(selW)) {
        SendLogMessage(L"⚠️ Bản tin đã chọn trước đó '" + selW + L"' không còn trên máy", 1);
        selW.clear();
        SetPrintMessageName("");
    }
    if (selW.empty() && has(st.messageName)) selW = st.messageName;   // mặc định: bản tin đang tải trên máy
    st.messages = list;
    st.selectedMessage = selW;
    printerModel_->SetState(st);
    SendLogMessage(L"📋 Có " + std::to_wstring(list.size()) + L" bản tin trên máy", 3);
}

void AppController::HandleSelectMessageRequest(const Request& req) {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"❌ Chưa kết nối máy in", 2);
        return;
    }
    const std::string name = WideToUtf8(req.data);
    PrinterStatus s = rciClient_->RequestStatusEx();
    if (!s.valid) { SendLogMessage(L"❌ Không đọc được trạng thái máy in - huỷ đổi bản tin", 2); return; }
    if (s.printing) {
        SendLogMessage(L"⛔ Máy đang in - không đổi được bản tin", 2);
        PrinterState st = printerModel_->GetState();       // trả ô chọn về bản tin cũ
        std::string cur;
        { std::lock_guard<std::mutex> lock(nameMutex_); cur = printMessageName_; }
        st.selectedMessage = Utf8ToWide(cur);
        printerModel_->SetState(st);
        return;
    }
    if (!rciClient_->LoadMessage(name, 0)) {
        SendLogMessage(L"❌ Không tải được bản tin '" + req.data + L"' (30D) - làm mới danh sách bản tin", 2);
        HandleRefreshMessagesRequest();
        return;
    }
    SetPrintMessageName(name);
    {
        PrinterState st = printerModel_->GetState();
        st.selectedMessage = req.data;
        printerModel_->SetState(st);
    }
    HandleDownloadFromPrinterRequest();                    // đọc lại kích thước field (157D) + tên bản tin hiện hành (141D)
    PrinterState st = printerModel_->GetState();
    if (_wcsicmp(st.messageName.c_str(), req.data.c_str()) != 0 && !st.messageName.empty()) {
        SendLogMessage(L"⚠️ Máy báo bản tin hiện hành là '" + st.messageName + L"', khác bản tin đã chọn '" + req.data + L"'", 1);
    }
    else {
        SendLogMessage(L"✅ Đã chọn bản tin '" + req.data + L"'" +
            (st.remoteFieldMax > 0 ? L" - field " + st.remoteFieldName + L": " + std::to_wstring(st.remoteFieldMax) + L" ký tự" : L""), 4);
    }
    lastCommandTime_ = std::chrono::steady_clock::now();
}

// [2026-11-18 R36] Tải thông tin từ máy in về phần mềm: field (tên + số ký tự), message, count.
// RCI KHÔNG có lệnh đọc giá trị hiện tại của field -> nội dung lấy từ bản lưu cục bộ (model), bỏ khoảng trắng đệm.
void AppController::HandleDownloadFromPrinterRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"❌ Chưa kết nối máy in", 2);
        return;
    }
    std::vector<RemoteFieldInfo> fields;
    if (!rciClient_->RequestRemoteFields(fields)) {
        SendLogMessage(L"❌ Không đọc được remote field từ máy (157D)", 2);
        return;
    }
    PrinterState st = printerModel_->GetState();
    if (fields.empty()) {
        st.remoteFieldMax = 0;
        SendLogMessage(L"⚠️ Message hiện tại trên máy không có remote field", 1);
    }
    else {
        const std::wstring cur = st.remoteFieldName;
        const RemoteFieldInfo* info = &fields.front();
        for (const auto& f : fields) if (Utf8ToWide(f.name) == cur) { info = &f; break; }
        st.remoteFieldName = Utf8ToWide(info->name);
        st.remoteFieldMax = info->maxChars;
        SendLogMessage(L"📥 Remote field: " + st.remoteFieldName + L" - " + std::to_wstring((int)info->maxChars) +
            L" ký tự (bắt buộc đúng bằng kích thước này)", 3);
    }
    uint32_t count = 0; std::string msgName;
    if (rciClient_->RequestMessagePrintCount(count, msgName, "", 3000)) {
        st.messageName = Utf8ToWide(msgName);
        SendLogMessage(L"📥 Message: " + st.messageName + L" - print count trên máy: " + std::to_wstring(count), 3);
    }
    else {
        SendLogMessage(L"⚠️ Không đọc được message/print count (141D)", 1);
    }
    printerModel_->SetState(st);

    std::wstring content = printerModel_->GetCurrentJobContent();
    while (!content.empty() && content.back() == L' ') content.pop_back();
    printerModel_->SetCurrentJob(content, printerModel_->GetTargetCount());
    printerModel_->UpdateJobProgress(printerModel_->GetCurrentCount());
    lastCommandTime_ = std::chrono::steady_clock::now();
}

// [2026-11-18 R37] In bù/trừ khi máy vẫn đang chạy: chỉ đổi mục tiêu phía phần mềm (không động tới count trên máy,
// không làm gián đoạn lệnh in). Đủ sản lượng mới (R38) sẽ được so với mục tiêu mới.
void AppController::HandleAdjustCountRequest(const Request& req) {
    if (!rciClient_ || !rciClient_->IsConnected()) {
        SendLogMessage(L"❌ Chưa kết nối máy in", 2);
        return;
    }
    const int target = printerModel_->GetTargetCount();
    const int printed = printerModel_->GetCurrentCount();
    if (target <= 0) {
        SendLogMessage(L"❌ Chưa có số lượng mục tiêu - hãy SET số lượng trước", 2);
        return;
    }
    const long long nt = (long long)target + req.count;
    if (nt < 1 || nt > 999999999) {
        SendLogMessage(L"❌ Số lượng sau điều chỉnh (" + std::to_wstring(nt) + L") ngoài phạm vi 1..999.999.999", 2);
        return;
    }
    if (nt < printed) {
        SendLogMessage(L"❌ Số lượng sau điều chỉnh (" + std::to_wstring(nt) + L") nhỏ hơn số đã in (" +
            std::to_wstring(printed) + L")", 2);
        return;
    }
    printerModel_->SetTargetCount((int)nt);
    if (nt > printed) stopAtTargetSent_ = false;   // nâng mục tiêu: cho phép cơ chế dừng khi đủ chạy lại
    SavePrintDataOnExit();
    SendLogMessage(std::wstring(req.count >= 0 ? L"➕ In bù " : L"➖ In trừ ") + std::to_wstring(req.count >= 0 ? req.count : -req.count) +
        L": mục tiêu " + std::to_wstring(target) + L" → " + std::to_wstring(nt), 4);
}

// Jet
bool AppController::HandleStartJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return false;
    if (jetTransitioning_)
    {
        auto st0 = printerModel_->GetState();
        if (st0.jetOn) {
            return true;
        }
    }

    jetTransitioning_ = true; jetTransitionStart_ = std::chrono::steady_clock::now();
    hasInitialStatus_ = false; // QUAN TRỌNG: reset để theo dõi

    PrinterState st = printerModel_->GetState();
    st.status = PrinterStateType::StartingJet;
    st.statusText = L"Đang khởi động Jet...";
    st.jetTransitioning = true;

    printerModel_->SetState(st);
    SendStateUpdate();

    bool result = rciClient_->StartJet();

    if (!result) {
        jetTransitioning_ = false;
        return false;
    }
    lastCommandTime_ = std::chrono::steady_clock::now();
    return true;
}

void AppController::HandleStopJetRequest() {
    if (!rciClient_ || !rciClient_->IsConnected()) return;
    if (jetTransitioning_) return;

    jetTransitioning_ = true; jetTransitionStart_ = std::chrono::steady_clock::now(); // 🔒 LOCK
    hasInitialStatus_ = false; // QUAN TRỌNG: reset;

    PrinterState st = printerModel_->GetState();
    st.status = PrinterStateType::StopingJet;
    st.statusText = L"Đang dừng Jet...";
    st.jetTransitioning = true;

    printerModel_->SetState(st);
    SendStateUpdate();

    if (!rciClient_->StopJet()) {
        jetTransitioning_ = false;
        return;
    }
    lastCommandTime_ = std::chrono::steady_clock::now();
    SendStateUpdate();
}
// Connection
void AppController::HandleConnectRequest(const Request& req) {
    // [2026-11-16 R20] Bỏ qua yêu cầu trùng (vd bấm 2 lần, hoặc reconnect tự động trùng với bấm tay).
    if (rciClient_->IsConnected()) {
        Logger::GetInstance().Write(L"Connect request ignored: already connected");
        return;
    }
    SendLogMessage(L"Đang kết nối...", 3);
    printerModel_->SetConnectionInfo(req.data, kRciDefaultPort);
    {   // [2026-11-16 R20] Trạng thái "Connecting" do worker đặt (trước đây UI thread ghi).
        PrinterState c = printerModel_->GetState();
        if (c.status != PrinterStateType::Reconnecting) {
            c.status = PrinterStateType::Connecting;
            c.statusText = L"Đang kết nối...";
            printerModel_->SetState(c);
            printerModel_->SetStatusText(c.statusText);
            SendStateUpdate();
        }
    }
    bool ok = rciClient_->Connect(req.data, kRciDefaultPort, 3000);
    PrinterState st = printerModel_->GetState();

    // ❌ CONNECT FAIL
    if (!ok)
    {
        EnableAutoReconnect();//BẬT LẠI AUTORECONNECT
        nextReconnectTime_ = std::chrono::steady_clock::now() + kReconnectInterval;   // [2026-11-16 R23]
        // 🔁 Nếu vẫn còn lượt thử lại → giữ nguyên trạng thái hiện tại (Connecting = vàng)
        if (autoReconnect_ && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS)
        {
            PrinterState st2 = st;
            st2.status = PrinterStateType::Reconnecting;
            st2.statusText = L"Đang kết nối lại...";
            printerModel_->SetState(st2);

            SendStateUpdate();
            return;
        }
        st.status = PrinterStateType::Error;
        st.statusText = L"Lỗi kết nối";
        printerModel_->SetState(st);
        printerModel_->SetStatusText(st.statusText);
        SendLogMessage(L"❌ Không thể kết nối", 2);
        SendStateUpdate();
        SendConnectionUpdate(false);
        return;
    }
    // ✅ CONNECT SUCCESS
    reconnectAttempts_ = 0; // reset lại bộ đếm nếu kết nối thành công
    // [2026-11-16 R21/R22] Kết nối mới: bỏ mọi trạng thái chờ cũ, sẽ đọc lại từ máy.
    jetTransitioning_ = false;
    hasInitialStatus_ = false;
    statusFailCount_ = 0;
    wasConnected_ = true;
    st.status = PrinterStateType::Connected;
    st.statusText = StateToText(st);
    st.jetOn = false;
    st.printing = false;
    st.errorMessage.clear();

    printerModel_->SetState(st);

    SendConnectionUpdate(true);
    SendStateUpdate();

    if (rciClient_ && rciClient_->IsConnected()) {
        HandleStatusRequest();   // sẽ cập nhật PrinterModel + SendStateUpdate()
    }
    else {
        SendStateUpdate();
    }
    // [2026-11-22 R42] Serial máy in (152D) - dùng để gắn mã gia hạn với máy in
    if (rciClient_ && rciClient_->IsConnected()) {
        std::string sn;
        PrinterState ps = printerModel_->GetState();
        if (rciClient_->RequestPrinterSerial(sn)) {
            ps.printerSerial = Utf8ToWide(sn);
            SendLogMessage(L"🏷️ Serial máy in: " + ps.printerSerial, 3);
        }
        else {
            ps.printerSerial.clear();
            SendLogMessage(L"⚠️ Không đọc được serial máy in (152D) - chưa thể nhập mã gia hạn", 1);
        }
        printerModel_->SetState(ps);
        SendStateUpdate();
        HandleRefreshMessagesRequest();   // [2026-11-22 R44] danh sách bản tin để người dùng chọn
        HandleDownloadFromPrinterRequest();   // [R47] đọc luôn kích thước field + bản tin hiện hành, không cần bấm Tải Về
    }
    EnableAutoReconnect();
    lastCommandTime_ = std::chrono::steady_clock::now();
}

void AppController::HandleDisconnectRequest() {
    DisableAutoReconnect();

    jetTransitioning_ = false;   // ⬅️ RESET
    hasInitialStatus_ = false;   // ⬅️ RESET
    lastJetOn_ = false;          // ⬅️ RESET
    wasConnected_ = false;       // [2026-11-16 R22] ngắt chủ động, không phải mất kết nối
    statusFailCount_ = 0;

    if (rciClient_) {
        rciClient_->Disconnect();
    }
    PrinterState st = printerModel_->GetState();

    st.status = PrinterStateType::Disconnected;
    st.statusText = L"Ngắt kết nối";
    st.warnings.clear();
    st.printerSerial.clear();   // [2026-11-22 R42]
    st.jetOn = false;
    st.printing = false;

    printerModel_->SetState(st);

    SendStateUpdate();            // bắt buộc
    SendConnectionUpdate(false);  // UI chỉ là phụ
    lastCommandTime_ = std::chrono::steady_clock::now();
}
// ================== State machine / policy ==================
void AppController::UpdatePrinterState() {
    auto currentState = printerModel_->GetState();
    if (!rciClient_ || !rciClient_->IsConnected())// Nếu socket KHÔNG KẾT NỐI
    {
        // 1. Nếu đang autoReconnect và chưa quá số lần → báo Reconnecting
        if (autoReconnect_ && reconnectAttempts_ < MAX_RECONNECT_ATTEMPTS)
        {
            SendLogMessage(L"🔄 Đang kết nối lại...", 1);
            // KHÔNG đổi PrinterState (để không phá state Idle/Printing/Ready)
            SendConnectionUpdate(false);
            return;
        }
        // 2. Hết lượt hoặc autoReconnect tắt → báo Disconnected
        SendLogMessage(L"🔌 Mất kết nối với máy in", 2);
        return;
    }
}

bool AppController::ShouldPollStatus() const {
    return rciClient_ && rciClient_->IsConnected();
}

bool AppController::ShouldGetPrintCount() const {
    auto state = printerModel_->GetState();
    return state.status == PrinterStateType::Printing;
}

bool AppController::ShouldAutoStartJet() const {
    auto state = printerModel_->GetState();
    return (!state.jetOn)
        && printerModel_->HasPendingPrintJob()
        && !jetTransitioning_;
}
// ================== Messaging to UI ==================
void AppController::SendLogMessage(const std::wstring& text, int level) {
    if (!mainWindow_) return;
    auto* msg = new LogMessage{ text, level };
    if (!PostMessage(mainWindow_, WM_APP_LOG, (WPARAM)msg, 0)) delete msg;
}

void AppController::SendConnectionUpdate(bool connected) {
    if (!mainWindow_) return;
    auto ip = printerModel_->GetIpAddress();
    auto* msg = new ConnectionMessage{ connected, ip, kRciDefaultPort };
    if (!PostMessage(mainWindow_, WM_APP_CONNECTION_UPDATE, (WPARAM)msg, 0)) delete msg;
}