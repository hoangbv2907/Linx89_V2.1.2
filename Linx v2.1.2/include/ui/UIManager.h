#pragma once
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include <map>
#include <memory>
#include <vector>

#include "common/CommonDefs.h"
#include "ui/ModernButton.h"
#include "ui/ToggleSwitch.h"
#include "ui/MessageLogger.h"
#include "common/CommonTypes.h"

class UIManager {
public:
    // ================== Lifecycle ==================
    UIManager();
    ~UIManager();

    // ================== Init / Create UI ==================
    // Lưu HWND cửa sổ chính (hParent_) + khởi tạo các custom component (logger, toggle...)
    // Chưa tạo control Win32 ở đây; CreateControls() sẽ tạo.
    bool Initialize(HWND hParent);

    // Tạo toàn bộ controls Win32: static label, edit IP, edit content, edit count,
    // status text, current count text, các button (start/print/stop/upload/clear/set),
    // và gắn font/owner-draw theo thiết kế.
    void CreateControls();

    // ================== Log / Messaging ==================
    // Append 1 dòng log vào MessageLogger. level dùng để tô màu/biểu tượng (info/warn/error...)
    void AddMessage(const std::wstring& text, int level);

    // Xóa toàn bộ log trong MessageLogger.
    void ClearMessages();

    // ================== UI updates from App state ==================
    // Cập nhật text trạng thái (ví dụ: "Connected", "Printing", "Error: ...") lên label status.
    // Thường gọi từ WindowManager khi nhận WM_APP_PRINTER_UPDATE.
    void UpdatePrinterStatus(const std::wstring& text);

    // Cập nhật "UI skin/state" theo trạng thái máy: đổi label, icon, màu nền, highlight...
    // (Ví dụ: Printing thì highlight xanh, Error thì đỏ, Disconnected thì xám...)
    void UpdatePrinterUIState(PrinterState state);

    // Wrapper để cập nhật enable/disable của các nút theo trạng thái máy.
    // Thường gọi UpdateButtonStateForPrinterState() bên trong.
    void UpdateButtonStates(PrinterState state);

    // Cập nhật các trường job: nội dung in, target count, printed count.
    // Lưu ý: thường phải tôn trọng 2 cờ:
    // - editingJobFields_: user đang focus vào ô input (không overwrite)
    // - jobDirtyByUser_: user đã sửa nhưng chưa Upload/Set (không overwrite)
    void UpdateJobFields(const std::wstring& content, int target, int printed);

    // [2026-11-18 R35] Hiển thị cảnh báo/lỗi đang có trên máy in (rỗng = không có).
    void UpdateWarnings(const std::vector<std::wstring>& warnings);

    // [2026-11-18 R33/R36] Hiển thị tên + kích thước remote field, tên message; giới hạn ô nhập theo kích thước field.
    void UpdateRemoteFieldInfo(const PrinterState& state, const std::wstring& messageName);

    // [2026-11-18 R37] Độ lệch in bù (+) / trừ (-) người dùng nhập. ok=false nếu không phải số nguyên.
    int GetAdjustValue(bool& ok) const;

    // ================== Edit-protection flags (chống UI update spam) ==================
    // Bật khi user đang nhập (focus) ở ô content/count để tránh poll status overwrite UI.
    void SetEditingJobFields(bool editing) { editingJobFields_ = editing; }

    // Bật khi user đã gõ/sửa nội dung hoặc số lượng nhưng CHƯA bấm Upload/Set.
    // Khi true, UpdateJobFields() nên tránh ghi đè input của user.
    void MarkJobDirtyByUser(bool dirty) { jobDirtyByUser_ = dirty; }

    // [2026-11-18 R36] Buộc lần cập nhật kế tiếp ghi đè ô nội dung/số lượng bằng dữ liệu từ máy (sau khi bấm Tải Về).
    void ResetJobFieldCache() { lastJobContent_ = L" sentinel"; lastTarget_ = -1; lastPrinted_ = -1; }

    // ================== Toggle switch ==================
    // Set ON/OFF cho toggle trên UI (ví dụ: Connected = ON).
    void SetToggleState(bool state);

    // Đọc trạng thái toggle hiện tại (UI state) - không phải trạng thái socket thật.
    bool IsToggleOn() const;

    // ================== Data access (read user inputs) ==================
    // Lấy IP từ edit box (hIpAddress_).
    std::wstring GetIPAddress() const;

    // Lấy nội dung từ edit box nội dung in (hInputValue_).
    std::wstring GetInputText() const;

    // Lấy số lượng in từ edit box count (hCount_).
    int GetCountValue() const;

    // Kiểm tra dữ liệu nhập hợp lệ (IP đúng định dạng, count > 0...).
    // Thường dùng trước khi gọi AppController::Connect/Upload/Set.
    bool ValidateInput() const;

    // [2026-11-22 R43] Co giãn: đặt lại vị trí/kích thước mọi control theo client (WM_SIZE); đổi DPI; IP từ cài đặt.
    void Layout(int clientW, int clientH);
    void SetDpi(int dpi);
    int Dpi() const { return dpi_; }
    void SetIPAddress(const std::wstring& ip);
    // [2026-11-22 R44] Cập nhật ô chọn bản tin từ trạng thái (danh sách trên máy + bản tin đã chọn).
    void UpdateMessageList(const PrinterState& state);
    // Bản tin người dùng vừa chọn trong ô (rỗng nếu chưa chọn). Gọi từ xử lý CBN_SELCHANGE.
    std::wstring GetSelectedMessage();

    // ================== Owner-draw handling ==================
    // Xử lý WM_DRAWITEM: vẽ các controls kiểu owner-draw (ModernButton/ToggleSwitch).
    // WindowManager sẽ gọi hàm này từ HandleDrawItem().
    void HandleOwnerDraw(LPDRAWITEMSTRUCT dis);

private:
    // ================== Parent / raw Win32 handles ==================
    HWND hParent_ = nullptr;          // HWND cửa sổ chính (owner của mọi control)

    // Input controls
    HWND hIpAddress_ = nullptr;       // Edit IP address
    HWND hInputValue_ = nullptr;      // Edit nội dung in
    HWND hCount_ = nullptr;           // Edit số lượng in

    // Status / display controls
    HWND hStatusDisplay_ = nullptr;   // Label/status text chung (nếu bạn dùng)
    HWND hCurrentCount_ = nullptr;    // Label hiển thị số lượng đã in (printed count)
    HWND hPrinterState_ = nullptr;    // Label hiển thị trạng thái máy (Printing/Error/Idle...)

    // Buttons (nếu có button thường)
    HWND hBtnUpload_ = nullptr;       // Handle button Upload (nếu Upload không dùng ModernButton)
    HWND hBtnDownload_ = nullptr;     // [2026-11-18 R36] nút TẢI VỀ (đọc từ máy)
    HWND lblMessage_ = nullptr, hMessageCombo_ = nullptr, hBtnMsgRefresh_ = nullptr;   // [2026-11-22 R44] chọn bản tin trên máy
    std::vector<std::wstring> lastMessages_;     // danh sách đang hiển thị trong ô chọn
    ULONGLONG lastPickTick_ = 0;                 // lúc người dùng vừa chọn: tạm không đồng bộ ngược từ trạng thái máy
    HWND hBtnSettings_ = nullptr;     // [2026-11-22 R43] biểu tượng ⚙ Cài đặt
    HWND hTip_ = nullptr;             // tooltip của nút Cài đặt
    HWND lblIp_ = nullptr, lblContent_ = nullptr, lblCount_ = nullptr, lblReached_ = nullptr,
         lblAdjust_ = nullptr, lblWarn_ = nullptr, lblLog_ = nullptr;   // nhãn (để Layout di chuyển)
    int dpi_ = 96;                    // DPI hiện tại của cửa sổ
    HWND hBtnAdjust_ = nullptr;       // [2026-11-18 R37] nút ĐIỀU CHỈNH (in bù/trừ)
    HWND hAdjust_ = nullptr;          // ô nhập độ lệch +/-
    HWND hFieldInfo_ = nullptr;       // nhãn: remote field + kích thước + message
    HWND hWarnings_ = nullptr;        // ô cảnh báo của máy in (read-only)
    std::wstring lastWarningsText_;   // tránh SetWindowText thừa
    std::wstring lastFieldInfoText_;
    int lastFieldMax_ = -1;

    // ================== Custom controls (C++ wrapper) ==================
    std::unique_ptr<ToggleSwitch> toggleSwitch_;    // Toggle custom (owner-draw)
    std::unique_ptr<MessageLogger> messageLogger_;  // Logger (RichEdit wrapper)

    // Modern buttons (owner-draw)
    ModernButton btnStart_, btnPrint_, btnStop_, btnClear_, btnSet_;

    // ================== Cached UI state (để tránh update thừa) ==================
    int currentCount_ = 0;                           // cached printed count đang hiển thị
    bool isToggleOn_ = false;                        // cached toggle state

    // Lưu state lần trước để tránh repaint/update quá nhiều
    PrinterStateType lastState_ = PrinterStateType::Unknown;

    // Cache job fields lần trước để chỉ update khi thật sự đổi
    std::wstring lastJobContent_;
    int lastTarget_ = -1;
    int lastPrinted_ = -1;

    // ================== User editing protection ==================
    bool editingJobFields_ = false;  // user đang focus trong ô content/count?
    bool jobDirtyByUser_ = false;    // user đã sửa nhưng chưa Upload/Set?

    // ================== Helper methods (control creation / layout) ==================
    // Tạo static label đơn giản (WS_CHILD | WS_VISIBLE).
    HWND CreateStatic(int x, int y, int w, int h, const wchar_t* text);

    // Tạo button chuẩn Win32 (nếu không dùng ModernButton cho nút đó).
    HWND CreateButton(int x, int y, int w, int h, const wchar_t* text, int id);

    // Tạo edit box (cho IP/content/count). text mặc định trống.
    HWND CreateEdit(int x, int y, int w, int h, const wchar_t* text = L"");

    // Apply font thống nhất lên tất cả controls để UI đồng bộ.
    void ApplyFontToAllControls();

    // ================== Button state management ==================
    // Core logic enable/disable từng nút theo PrinterState:
    // - Disconnected: chỉ cho Connect
    // - Connected/Idle: cho Upload/Set/StartJet/Print...
    // - Printing: disable Upload/Set, enable Stop...
    // - Error: có thể chỉ cho Stop/Clear...
    void UpdateButtonStateForPrinterState(PrinterState state);
};
