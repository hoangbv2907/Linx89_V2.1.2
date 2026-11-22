/*
[2026-11-22 R42] Giới hạn thời gian sử dụng + mã gia hạn.

- Hạn mức ban đầu kBaseHours (6000) giờ CHƯƠNG TRÌNH CHẠY (cộng dồn, đồng hồ đơn điệu - chỉnh đồng hồ hệ thống không ảnh hưởng).
- Hết hạn: khoá các thao tác điều khiển in (vẫn kết nối được để đọc mã máy in, vẫn xem nhật ký, vẫn nhập mã).
- Mã gia hạn: ECDSA P-256 ký trên {hours, seq, hash(mã máy tính), hash(serial máy in)}. Phần mềm chỉ chứa KHOÁ CÔNG KHAI
  nên không thể tự sinh mã; khoá riêng nằm ở công cụ LicenseKeygen do nhà cung cấp giữ.
  Mã chỉ dùng được trên đúng máy tính (mã máy) VÀ khi đang kết nối đúng máy in (serial, lệnh 152D); mỗi mã (seq) dùng 1 lần.
- Dữ liệu đếm giờ lưu 2 nơi (file license.dat + registry HKCU), có HMAC khoá theo mã máy: sao chép sang máy khác
  hoặc sửa tay đều bị phát hiện (coi như hết hạn cho tới khi nhập mã hợp lệ).
Không phụ thuộc giao diện; dùng được trong test.
*/
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <mutex>

namespace lic {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kBaseHours = 6000;

// Mã máy tính dạng "XXXX-XXXX-XXXX-XXXX" (từ MachineGuid của Windows).
std::wstring MachineId();

// Serial máy in (lệnh 152D): bỏ khoảng trắng/NUL, in hoa, tối đa 8 ký tự đầu (manual: 8 ký tự đầu là duy nhất).
std::string NormalizeSerial(const std::string& raw);

// ---- khoá (dùng cho công cụ tạo mã và test) ----
bool GenerateKeyPair(Bytes& privBlob, Bytes& pubBlob);

// ---- mã gia hạn ----
// Tạo mã (chỉ công cụ của nhà cung cấp có khoá riêng). Trả chuỗi rỗng nếu lỗi.
std::string MakeCode(const Bytes& privBlob, const std::wstring& machineId, const std::string& printerSerial,
    uint32_t hours, uint32_t seq);

struct CodeInfo { uint32_t hours = 0; uint32_t seq = 0; };
enum class CodeResult { Ok, BadFormat, BadSignature, WrongMachine, WrongPrinter, AlreadyUsed, NoPrinter };

CodeResult VerifyCode(const Bytes& pubBlob, const std::string& code, const std::wstring& machineId,
    const std::string& printerSerial, CodeInfo& out);

// ---- bộ đếm giờ + kho lưu ----
struct StoreConfig {
    std::wstring dir;      // thư mục chứa license.dat
    std::wstring regKey;   // khoá HKCU, ví dụ L"Software\\LinxController\\License"
};
StoreConfig DefaultStoreConfig();

class Service {
public:
    Service(Bytes pubBlob, StoreConfig cfg = DefaultStoreConfig(), std::wstring machineId = MachineId());

    void Load();                       // đọc kho; không có = cài mới; sai chữ ký = bị sửa -> hết hạn
    void Tick();                       // cộng thời gian chạy kể từ lần gọi trước; tự lưu mỗi ~60 giây
    void AddSeconds(uint64_t s);       // (test) cộng thẳng số giây đã dùng
    void Save();

    uint64_t UsedSeconds() const;
    uint32_t LimitHours() const;
    int64_t RemainingSeconds() const;  // có thể âm khi hết hạn
    bool Expired() const;
    bool Tampered() const;             // dữ liệu bị sửa / sao chép từ máy khác
    std::string BoundSerial() const;   // serial máy in của mã gia hạn gần nhất

    CodeResult Activate(const std::string& code, const std::string& printerSerial, uint32_t* addedHours = nullptr);
    const std::wstring& Machine() const { return machineId_; }

private:
    struct Data {
        uint64_t usedSec = 0;
        uint32_t limitHours = kBaseHours;
        std::set<uint32_t> seqs;       // các mã đã dùng
        std::string boundSerial;
    };
    std::string Serialize(const Data& d) const;
    bool Parse(const std::string& text, Data& d) const;
    void SaveLocked();

    Bytes pub_;
    StoreConfig cfg_;
    std::wstring machineId_;
    mutable std::mutex mu_;
    Data data_;
    bool tampered_ = false;
    uint64_t lastTick100ns_ = 0;
    uint64_t sinceSave100ns_ = 0;
    uint64_t fraction100ns_ = 0;
};

} // namespace lic
