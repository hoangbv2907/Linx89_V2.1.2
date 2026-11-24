/*
giải mã các mã trạng thái (status code) và cảnh báo (warning mask)
từ máy in khi nhận được qua RCI (Remote Command Interface)
chức năng chính:
- format số hex thành chuỗi dễ đọc
- gép danh sách chuỗi thành 1 chuỗi
- giải mã P-STATUS (mã lỗi máy in)
- giải mã C-STATUS (mã lỗi lệnh)
- giải mã Warning Mask (mã cảnh báo dạng bitmask)
*/
#include "rci/RciWarnings.h"
#include <sstream>
#include <iomanip>

// chuyển 1 byte thành chuỗi hex 2 ký tự, ví dụ 0x1A -> "1A"
static std::wstring Hex2(uint8_t v) {
	std::wstringstream ws;  // sử dụng wstringstream để format hex với các manipulators
	ws << std::hex << std::uppercase << std::setfill(L'0') << std::setw(2) << (int)v; // dạng hex - chữ hoa - nếu thiếu số thì fill bằng 0 - độ rộng 2 ký tự - cast v sang int để in ra số thay vì ký tự
    return ws.str();
}

// chuyển 32-bit thành chuỗi hex 8 ký tự, ví dụ 0x1234ABCD -> "1234ABCD"
static std::wstring Hex8(uint32_t v) {
    std::wstringstream ws;  
    ws << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8) << v;
    return ws.str();
}

// gép các chuỗi trong vector thành 1 chuỗi duy nhất, ngăn cách bởi ( ", ")
std::wstring JoinList(const std::vector<std::wstring>& v, const wchar_t* sep) {
    std::wstringstream ws;
	for (size_t i = 0; i < v.size(); ++i) { // duyệt qua từng chuỗi trong vector
		if (i) ws << sep;   // nếu không phải chuỗi đầu tiên thì thêm dấu phân cách
		ws << v[i]; // thêm chuỗi hiện tại vào output
    }
    return ws.str();
}

/*
P - STATUS là mã lỗi liên quan đến trạng thái máy in
Manual Linx 8800/8900 RCI
[2026-11-13 R04] Bảng P-STATUS trong tài liệu (MP65969-7, "Printer fault codes (P-STATUS)")
ghi mã theo SỐ THẬP PHÂN (01-08, 10-13, 20-36, 255). Trước đây code dùng các số đó như hex
(0x10 = 16...) nên mã 10-13 và 20-36 bị giải mã sai. Đã đổi sang literal thập phân.
*/
std::wstring DecodePStatus(uint8_t p) {
    switch (p) {
    case 0: return L"No fault";

    case 1: return L"Print Head Temperature";
    case 2: return L"EHT Trip";
    case 3: return L"Phase Failure";
    case 4: return L"Time of Flight";
    case 5: return L"300V Power Supply";
    case 6: return L"Hardware Safety Trip";
    case 7: return L"Ink Tank Empty";
    case 8: return L"Internal Spillage";

    case 10: return L"Solvent Tank Empty";
    case 11: return L"Jet Misaligned";
    case 12: return L"Pressure Limit";
    case 13: return L"Viscosity";

    case 20: return L"Printer Over Temperature";
    case 21: return L"Service Module Removed";
    case 22: return L"Unable to fill Service Module";
    case 23: return L"Charge Amplifier Trip";
    case 24: return L"RAM Low Fault";
    case 25: return L"Flash Memory Low Fault";
    case 26: return L"Service Module Requires Replacement";
    case 27: return L"Valid UNIC Chip Not Found";
    case 28: return L"Pump Drive Failure";
    case 29: return L"Pressure Reading At Maximum";
    case 30: return L"Pressure Reading Suspect";
    case 31: return L"Pressure Reading At Minimum";
    case 32: return L"Valve Drive 1 Failure";
    case 33: return L"Valve Drive 2 Failure";
    case 34: return L"Pump Stalled";
    case 35: return L"Unable to Fill Solvent Buffer Tank";
    case 36: return L"Printhead Fault";

    case 255: return L"Other Fault";

    default:
        return L"Unknown P-STATUS (" + std::to_wstring((int)p) + L" / 0x" + Hex2(p) + L")";
    }
}

// C-STATUS là mã lỗi liên quan đến lệnh gửi đến máy in
std::wstring DecodeCStatus(uint8_t c) {
    switch (c) {
    case 0x00: return L"No command errors";

        // Link/serial layer
    case 0x01: return L"Parity error";
    case 0x02: return L"Framing error";
    case 0x03: return L"Data overrun";
    case 0x04: return L"Serial break";
    case 0x05: return L"Receive buffer overflow";
    case 0x06: return L"Command start";
    case 0x07: return L"Command end";
    case 0x08: return L"Invalid checksum";

        // Command / state / parameter validation
    case 0x11: return L"Invalid command";
    case 0x12: return L"Jet not running";
    case 0x13: return L"Jet not idle";
    case 0x14: return L"Print not idle";
    case 0x15: return L"Message edit in progress";
    case 0x16: return L"Number of bytes in command";
    case 0x17: return L"Parameter rejected";
    case 0x18: return L"Minimum string length";
    case 0x19: return L"Maximum string length";
    case 0x1A: return L"Minimum value";
    case 0x1B: return L"Maximum value";
    case 0x1C: return L"Memory full";
    case 0x1D: return L"No character sets";
    case 0x1E: return L"No barcodes";
    case 0x1F: return L"No logos";
    case 0x20: return L"No date formats";
    case 0x21: return L"PROM-based data set specified";
    case 0x22: return L"Unknown data set";
    case 0x23: return L"No messages";
    case 0x24: return L"Unknown message";
    case 0x25: return L"Field too large";
    case 0x26: return L"Additional message overwrite";
    case 0x27: return L"Non-alphanumeric character";
    case 0x28: return L"Positive value";

        // Trigger/print specific
    case 0x29: return L"Trigger print: Photocell mode";
    case 0x2A: return L"Trigger print: Print idle";
    case 0x2B: return L"Trigger print: Already printing";
    case 0x2C: return L"Trigger print: Cover off";
    case 0x2D: return L"Print command: Jet not running";
    case 0x2E: return L"Print command: No message";

        // Jet command warnings/errors
    case 0x2F: return L"Jet command: Ink low";
    case 0x30: return L"Jet command: Solvent low";
    case 0x31: return L"Jet command: Print fail";
    case 0x32: return L"Jet command: Print in progress";
    case 0x33: return L"Jet command: Phase";
    case 0x34: return L"Jet command: Time of flight";
    case 0x35: return L"Calibrate printhead: Try later";
    case 0x36: return L"Calibrate printhead: Failed";

        // Message / raster / remote field
    case 0x37: return L"Message too large";
    case 0x38: return L"Pixel RAM overflow";
    case 0x39: return L"Invalid message format";
    case 0x3A: return L"Invalid field type";
    case 0x3B: return L"No print message loaded";
    case 0x3C: return L"Invalid print mode";
    case 0x3D: return L"Invalid failure condition";
    case 0x3E: return L"Invalid buffer divisor";
    case 0x3F: return L"No remote fields in message";
    case 0x40: return L"Number of remote characters";
    case 0x41: return L"Remote data too large";

        // NOTE: may be returned with ACK as warning
    case 0x42: return L"Remote buffer now full";
    case 0x43: return L"Remote buffer still full";

    case 0x44: return L"Field data exceeds message end";
    case 0x45: return L"Invalid remote field type";
    case 0x46: return L"Invalid while display enabled";

        // Other
    case 0x4F: return L"Too many messages specified";
    case 0x51: return L"Printer busy";
    case 0x52: return L"Unknown raster";
    case 0x53: return L"Invalid field length";
    case 0x54: return L"Duplicate name";
    case 0x55: return L"Invalid barcode linkage";
    case 0x56: return L"Data set in ROM";
    case 0x57: return L"Data set in use";
    case 0x58: return L"Invalid field height";

        // Production schedule
    case 0x59: return L"Production schedule: No message schedules";
    case 0x5A: return L"Production schedule: Too many message schedules";
    // [2026-11-13 R05] Bổ sung mã 0x5B-0x66 theo Table 3-2 (các mã 0x5D/0x5E "Not used")
    case 0x5B: return L"Production schedule: Unknown message schedule";
    case 0x5C: return L"Production schedule: Duplicate message schedule";
    case 0x5F: return L"Incorrect trigger mapping";
    case 0x60: return L"Not valid RCI data";
    case 0x61: return L"Invalid jet state";
    case 0x62: return L"Execution of command failed";
    case 0x63: return L"Printer is in IDLE state";
    case 0x64: return L"Printer is in invalid UI state";
    case 0x65: return L"Incompatible format";
    case 0x66: return L"Unknown line";

    default:
        return L"Unknown C-STATUS (0x" + Hex2(c) + L")";
    }
}

// Warning Mask là một bitmask 32-bit, mỗi bit đại diện cho một cảnh báo cụ thể
// chức năng của hàm này là giải mã 32-bit warning mask: kiểm tra từng bit trong mask và nếu bit đó được set (bằng 1), 
// thì thêm cảnh báo tương ứng vào danh sách output
// ví dụ: nếu bit 0 và bit 3 được set, thì sẽ có cảnh báo "No Time of Flight" và "Ink Low"
std::vector<std::wstring> DecodeWarningMask(uint32_t mask) {
	std::vector<std::wstring> out; // tạo một vector để chứa các cảnh báo đã giải mã
	if (mask == 0) return out;      // nếu mask là 0 thì không có cảnh báo nào, trả về vector rỗng

	// helper lambda function để kiểm tra từng bit và thêm cảnh báo vào output nếu bit đó được set
    auto add_if = [&](int bit, const wchar_t* text) {
		if (mask & (1u << bit)) out.emplace_back(text); // nếu bit tại vị trí 'bit' được set trong mask, thì thêm 'text' vào vector output
        };

    add_if(0, L"No Time of Flight");   
    add_if(1, L"Shutdown Incomplete");
    add_if(2, L"Over Speed (Print Trigger)");
    add_if(3, L"Ink Low");
    add_if(4, L"Solvent Low");
    add_if(5, L"Over Speed (No Remote Data)");
    add_if(6, L"Printer Requires Scheduled Maintenance");
    add_if(7, L"Printhead Cover Off");
    add_if(8, L"Over Speed (Synchronous Data)");
    add_if(9, L"Over Speed (Line Speed)");
    add_if(10, L"Over Speed (Compensation)");
    add_if(11, L"Safety Override Active");
    add_if(12, L"Low Pressure");
    add_if(13, L"Under Speed (Line Speed)");
    add_if(14, L"Over Speed (Asynchronous Data)");

    add_if(16, L"User Data Corrupt");
    add_if(17, L"Memory Corrupt");
    add_if(18, L"No Message in Memory");

    add_if(20, L"Remote Error");
    add_if(22, L"Corrupt Program Data");
    add_if(27, L"Print Go After Schedule End");

    add_if(31, L"Extended Errors Present");

    // optional: show unknown bits for debug
    // (nếu bạn muốn debug, có thể add thêm đoạn scan bits chưa mapping)

    return out;
}

// ============================================================
// [2026-11-18 R34/R35] Cảnh báo hiển thị trên UI + điều kiện chặn in
// ============================================================
// Các bit warning làm máy không thể in đúng: Printhead Cover Off(7), User Data Corrupt(16), Memory Corrupt(17),
// No Message in Memory(18), Corrupt Program Data(22).
static const uint32_t kBlockingWarningMask =
    (1u << 7) | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 22);

std::vector<std::wstring> BuildActiveWarnings(uint8_t pStatus, uint8_t jetState, uint32_t errorMask) {
    std::vector<std::wstring> out;
    if (pStatus != 0) out.push_back(L"LỖI: " + DecodePStatus(pStatus) + L" (P-STATUS " + std::to_wstring((int)pStatus) + L")");
    if (jetState == 0x04) out.push_back(L"LỖI: Jet Fault");
    uint32_t known = 0;
    for (const auto& w : DecodeWarningMask(errorMask)) out.push_back(L"Cảnh báo: " + w);
    // bit chưa có tên trong bảng giải mã: vẫn báo để người dùng không bỏ sót
    static const int kNamed[] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,16,17,18,20,22,27,31 };
    for (int b : kNamed) known |= (1u << b);
    for (int b = 0; b < 32; ++b)
        if ((errorMask & (1u << b)) && !(known & (1u << b)))
            out.push_back(L"Cảnh báo: bit " + std::to_wstring(b) + L" (chưa đặt tên)");
    return out;
}

std::wstring CheckPrintBlockers(uint8_t pStatus, uint8_t jetState, uint32_t errorMask) {
    if (pStatus == 7)  return L"Bình mực đã hết (Ink Tank Empty) - không thể in";
    if (pStatus == 10) return L"Bình dung môi đã hết (Solvent Tank Empty) - không thể in";
    if (pStatus != 0)  return L"Máy còn lỗi chưa xoá: " + DecodePStatus(pStatus) + L" (P-STATUS " + std::to_wstring((int)pStatus) + L")";
    if (jetState == 0x04) return L"Jet đang ở trạng thái lỗi (Fault) - chưa xoá lỗi";
    const uint32_t blk = errorMask & kBlockingWarningMask;
    if (blk) return L"Cảnh báo nghiêm trọng: " + JoinList(DecodeWarningMask(blk), L", ");
    return L"";
}
