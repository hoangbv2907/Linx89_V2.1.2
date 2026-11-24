#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#include "common/CommonTypes.h"

// ================== Parsed printer status snapshot ==================
// PrinterStatus là snapshot sau khi parse phản hồi Status (thường cmd 0x14).
// Dùng trong AppController để cập nhật PrinterModel + log delta (P/C status, warnings...).
struct PrinterStatus {
    // [2026-11-16 R18] true CHỈ khi reply status hợp lệ (ACK + đúng checksum + đủ dữ liệu).
    // Khi timeout/NAK/sai khung, các trường bên dưới là giá trị mặc định và KHÔNG được dùng.
    bool valid = false;

    // --- from reply header (RCI ACK/NAK + status bytes) ---
    uint8_t pStatus = 0;              // P-STATUS raw
    uint8_t cStatus = 0;              // C-STATUS raw
    std::wstring pStatusText;         // decoded text (DecodePStatus)
    std::wstring cStatusText;         // decoded text (DecodeCStatus)

    // --- from 0x14 body (status data) ---
    uint8_t jetState = 0;             // jet state raw
    uint8_t printState = 0;           // print state raw

    uint32_t errorMask = 0;           // bitmask lỗi (nếu manual có định nghĩa)
    std::vector<std::wstring> warnings; // cảnh báo đã decode (nếu có)

    // --- convenience flags (derived) ---
    bool jetOn = false;               // suy ra từ jetState / status bits
    bool printing = false;            // suy ra từ printState
    bool idle = false;                // suy ra từ printState / jet state
};

// Kết quả parse một reply RCI: ESC ACK/NAK P-STATUS C-STATUS CMDID [data] ESC ETX chk
struct ParsedReply {
    bool ok = false;   // khung hợp lệ + checksum đúng
    bool ack = false;  // true=ACK, false=NAK
    uint8_t p_status = 0, c_status = 0, cmdid = 0;
    std::vector<uint8_t> data;  // dữ liệu sau CMDID (đã unescape)
};

// [2026-11-13 R06] Tìm vị trí "ESC ETX" kết thúc khung, coi mọi cặp "ESC x" (kể cả ESC ESC = byte dữ liệu 0x1B)
// là một đơn vị. Tránh nhận nhầm "ESC ESC 03" (dữ liệu 1B 03) là kết thúc khung.
// Trả std::string::npos nếu chưa thấy (hoặc còn thiếu byte).
size_t RciFindEscEtx(const std::vector<uint8_t>& buf, size_t from = 0);

// Mô tả một Remote Field trên máy (lệnh 157D / 0x9D).
struct RemoteFieldInfo {
    std::string name;       // tên field (<= 31 ký tự)
    uint8_t maxChars = 0;   // số ký tự tối đa/bắt buộc của field
    bool autoLoad = false;
};

// Parse + kiểm tra checksum một reply thô (dùng cả trong unit test).
ParsedReply ParseReplyAndVerify(const std::vector<uint8_t>& reply,
    uint8_t(*ComputeChecksumFn)(const std::vector<uint8_t>&));

// ================== RCI client (TCP transport + protocol helpers) ==================
class RciClient {
public:
    // Callback để đẩy log/message ra ngoài (thường AppController nối vào UI logger)
    using MessageCallback = std::function<void(const std::wstring&, int)>;

    // ================== Lifecycle ==================
    RciClient();
    ~RciClient();

    // ================== Connection management ==================
    // Kết nối TCP tới printer (mặc định kRciDefaultPort = 29043). timeoutMs dùng cho connect/send/recv.
    bool Connect(const std::wstring& ip, unsigned short port = kRciDefaultPort, int timeoutMs = 3000);

    // Ngắt kết nối và đóng socket. Trả false nếu đã disconnected hoặc có lỗi close.
    bool Disconnect();

    // Trạng thái kết nối hiện tại (cờ logic, không phải ping thực tế).
    bool IsConnected() const;

    // Mark disconnected nhanh, không throw (dùng khi detect socket error trong send/recv).
    void MarkDisconnected_NoThrow();

    // ================== Low-level send/receive ==================
    // Gửi một frame đã build sẵn và nhận reply thô.
    // Hàm này phải thread-safe (serialize bằng mtx_) vì socket chỉ nên send/recv tuần tự.
    bool SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs = 3000);

    // Gửi cmd + payload và chờ ACK/NAK (không cần parse body).
    // Trả true nếu ACK, false nếu NAK/timeout/socket error.
    bool SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs = 3000);

    // [2026-11-13 R03] Như SendAndWaitAck nhưng trả thêm reply đã parse (để kiểm tra P-STATUS/C-STATUS/data).
    bool SendAndWaitAckEx(uint8_t cmdid, const std::vector<uint8_t>& payload, ParsedReply& out, int timeoutMs = 3000);

    // Gửi cmd + payload và lấy BODY phản hồi (dùng cho lệnh có data trả về).
    // outBody là phần body đã tách khỏi framing (ACK/status/header... tuỳ implement).
    bool SendAndGetBody(uint8_t cmdid, const std::vector<uint8_t>& payload,
        std::vector<uint8_t>& outBody, int timeoutMs = 3000);

    // ================== High-level RCI commands ==================
    // Status: phiên bản đơn giản (chỉ gửi request). Nếu không dùng, có thể bỏ.
    bool RequestStatus();

    // Status: phiên bản đầy đủ (gửi 0x14 và parse về PrinterStatus).
    PrinterStatus RequestStatusEx();

    // Jet control
    bool StartJet();
    bool StopJet();

    // Print control
    bool StartPrint();
    bool StopPrint();

    // Message/job operations (tuỳ manual)
    bool LoadMessage(const std::string& name, uint16_t printCount = 1);
    bool DownloadRemoteField(const std::vector<uint8_t>& data);
    bool DownloadMessageData(const std::vector<uint8_t>& messageData);

    // ================== Specialized helpers used by AppController ==================
    // Send remote field data by field NAME (RCI 0x9E): fieldName + valueUtf8 (null-terminated theo manual).
    bool SendRemoteFieldDataByName(const std::string& fieldName,
        const std::string& valueUtf8,
        int timeoutMs = 3000);

    // [2026-11-13 R08] Request Remote Field Data (157D / 0x9D): liệt kê field + số ký tự tối đa.
    // Layout theo bảng trong tài liệu: [số field][ {marker, name[32], maxChars, autoLoad, reserved[2]} x n ].
    bool RequestRemoteFields(std::vector<RemoteFieldInfo>& out, int timeoutMs = 3000);

    // [2026-11-22 R42] Request Serial Number (152D / 0x98): 9 byte, kết thúc NUL. false nếu máy NAK / không hỗ trợ.
    bool RequestPrinterSerial(std::string& outSerial, int timeoutMs = 2000);

    // [2026-11-22 R44] Request Data Directory (97D / 0x61) loại 'M': danh sách bản tin trên máy (header 41 byte, tên ở byte 10..25).
    // Manual: lệnh KHÔNG hợp lệ khi đang in -> máy NAK 20 (Print not idle): khi đó *printingBusy = true và trả false.
    bool RequestMessageList(std::vector<std::string>& outNames, int timeoutMs = 3000, bool* printingBusy = nullptr);

    // Request message print count (RCI 0x8D):
    // outCount: số lượng đã in (hoặc target/printed tuỳ manual)
    // outMsgName: tên message trả về nếu printer trả kèm
    // msgNameOpt: nếu truyền rỗng thì lấy message hiện hành
    bool RequestMessagePrintCount(uint32_t& outCount,
        std::string& outMsgName,
        const std::string& msgNameOpt = "",
        int timeoutMs = 3000);

    // Set message print count: đặt số lượng in cho message (RCI cmd tuỳ implement).
    // Lưu ý: comment hiện tại của bạn nói "không chờ ACK"; hãy đảm bảo .cpp đúng như vậy.
    bool SetMessagePrintCount(uint32_t count, const std::string& msgNameOpt, int timeoutMs);

    // ================== Frame builders / utilities (static) ==================
    // Build frame theo chuẩn RCI:
    // - commandId: mã lệnh
    // - payload: data
    // - useSOH/includeChecksum: tuỳ lệnh / tuỳ phiên bản framing bạn đang áp dụng
    static std::vector<uint8_t> BuildFrame(uint8_t commandId,
        const std::vector<uint8_t>& payload = {},
        bool useSOH = false,
        bool includeChecksum = true);

    // Compute checksum theo rule của manual (thường là 8-bit two's complement).
    static uint8_t ComputeChecksum(const std::vector<uint8_t>& bytes);

    // Debug helper: convert reply bytes thành chuỗi hex để log.
    static std::wstring ReplyToString(const std::vector<uint8_t>& reply);

    // Set callback để đẩy log ra ngoài.
    void SetMessageCallback(MessageCallback cb) { callback_ = cb; }

private:
    // ================== Socket state ==================
    SOCKET sock_ = INVALID_SOCKET;         // TCP socket tới printer
    std::atomic_bool connected_{ false };  // cờ logic connected
    std::wstring host_;                    // IP/host đang kết nối
    unsigned short port_ = kRciDefaultPort;           // port đang dùng

    // Serialize send/recv để tránh nhiều thread dùng chung socket cùng lúc
    std::mutex mtx_;

    // [2026-11-16 R30] Disconnect() đặt cờ này để huỷ một lần Connect() đang chờ (dừng chương trình nhanh).
    std::atomic_bool abortConnect_{ false };

    // Cache/debug flag: ACK kết quả lệnh StartPrint lần gần nhất (nếu bạn dùng để gate logic)
    bool lastStartPrintAck = false;

    // Buffer giữ bytes dư khi recv không đọc đủ frame/reply trong 1 lần (stream TCP)
    std::vector<uint8_t> rxPending_;

    // Callback log ra ngoài
    MessageCallback callback_;

    // Log nội bộ qua callback_ (type/level: info/warn/error)
    void Log(const std::wstring& msg, int type = 0);
};
