#include "LicenseCore.h"
#include "AppPaths.h"
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace lic {

namespace {

// ---------- băm / HMAC ----------
bool Sha256(const uint8_t* d, size_t n, uint8_t out[32]) {
    BCRYPT_ALG_HANDLE h = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    const bool ok = BCRYPT_SUCCESS(BCryptHash(h, nullptr, 0, (PUCHAR)d, (ULONG)n, out, 32));
    BCryptCloseAlgorithmProvider(h, 0);
    return ok;
}
Bytes Sha256(const std::string& s) {
    Bytes o(32);
    if (!Sha256((const uint8_t*)s.data(), s.size(), o.data())) o.clear();
    return o;
}
bool HmacSha256(const Bytes& key, const std::string& data, uint8_t out[32]) {
    BCRYPT_ALG_HANDLE h = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) return false;
    const bool ok = BCRYPT_SUCCESS(BCryptHash(h, (PUCHAR)key.data(), (ULONG)key.size(), (PUCHAR)data.data(), (ULONG)data.size(), out, 32));
    BCryptCloseAlgorithmProvider(h, 0);
    return ok;
}

std::string ToHex(const uint8_t* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s.push_back(k[d[i] >> 4]); s.push_back(k[d[i] & 15]); }
    return s;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string o(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), o.data(), n, nullptr, nullptr);
    return o;
}

// ---------- base32 (RFC 4648, không padding) ----------
const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string B32Encode(const Bytes& in) {
    std::string out;
    uint32_t buf = 0; int bits = 0;
    for (uint8_t b : in) {
        buf = (buf << 8) | b; bits += 8;
        while (bits >= 5) { out.push_back(kB32[(buf >> (bits - 5)) & 31]); bits -= 5; }
    }
    if (bits > 0) out.push_back(kB32[(buf << (5 - bits)) & 31]);
    return out;
}
bool B32Decode(const std::string& in, Bytes& out) {
    out.clear();
    uint32_t buf = 0; int bits = 0;
    for (char c : in) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        c = (char)toupper((unsigned char)c);
        const char* p = strchr(kB32, c);
        if (!p || c == '\0') return false;
        buf = (buf << 5) | (uint32_t)(p - kB32); bits += 5;
        if (bits >= 8) { out.push_back((uint8_t)((buf >> (bits - 8)) & 0xFF)); bits -= 8; }
    }
    return true;
}
std::string Group5(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) { if (i && i % 5 == 0) o.push_back('-'); o.push_back(s[i]); }
    return o;
}

// ---------- payload ----------
constexpr size_t kPayload = 25, kSig = 64;

void Put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
uint32_t Get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

Bytes MachineHash8(const std::wstring& machineId) {
    std::wstring m;
    for (wchar_t c : machineId) m.push_back((wchar_t)towupper(c));
    Bytes h = Sha256("M|" + WideToUtf8(m));
    h.resize(8);
    return h;
}
Bytes SerialHash8(const std::string& serial) {
    Bytes h = Sha256("S|" + NormalizeSerial(serial));
    h.resize(8);
    return h;
}

Bytes BuildPayload(const std::wstring& machineId, const std::string& serial, uint32_t hours, uint32_t seq) {
    Bytes p{ 1 };
    Put32(p, hours);
    Put32(p, seq);
    Bytes m = MachineHash8(machineId), s = SerialHash8(serial);
    p.insert(p.end(), m.begin(), m.end());
    p.insert(p.end(), s.begin(), s.end());
    return p;
}

// ---------- ECDSA ----------
bool ImportKey(BCRYPT_ALG_HANDLE alg, const Bytes& blob, bool priv, BCRYPT_KEY_HANDLE& key) {
    return BCRYPT_SUCCESS(BCryptImportKeyPair(alg, nullptr, priv ? BCRYPT_ECCPRIVATE_BLOB : BCRYPT_ECCPUBLIC_BLOB,
        &key, (PUCHAR)blob.data(), (ULONG)blob.size(), 0));
}

} // namespace

// ============================================================
std::wstring MachineId() {
    std::wstring guid;
    wchar_t buf[128] = { 0 };
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &cb) == ERROR_SUCCESS)
        guid = buf;
    if (guid.empty()) {
        wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        GetComputerNameW(name, &n);
        guid = name;
    }
    Bytes h = Sha256("LinxMachine|" + WideToUtf8(guid));
    if (h.size() < 8) return L"0000-0000-0000-0000";
    const std::string hex = ToHex(h.data(), 8);
    std::wstring out;
    for (size_t i = 0; i < hex.size(); ++i) {
        if (i && i % 4 == 0) out.push_back(L'-');
        out.push_back((wchar_t)toupper((unsigned char)hex[i]));
    }
    return out;
}

std::string NormalizeSerial(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (c == '\0') break;
        if (isspace((unsigned char)c)) continue;
        s.push_back((char)toupper((unsigned char)c));
        if (s.size() == 8) break;
    }
    return s;
}

bool GenerateKeyPair(Bytes& priv, Bytes& pub) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) return false;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool ok = BCRYPT_SUCCESS(BCryptGenerateKeyPair(alg, &key, 256, 0)) && BCRYPT_SUCCESS(BCryptFinalizeKeyPair(key, 0));
    auto exp = [&](LPCWSTR type, Bytes& out) {
        ULONG n = 0;
        if (!BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, type, nullptr, 0, &n, 0))) return false;
        out.assign(n, 0);
        return BCRYPT_SUCCESS(BCryptExportKey(key, nullptr, type, out.data(), n, &n, 0));
    };
    if (ok) ok = exp(BCRYPT_ECCPRIVATE_BLOB, priv) && exp(BCRYPT_ECCPUBLIC_BLOB, pub);
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string MakeCode(const Bytes& priv, const std::wstring& machineId, const std::string& serial, uint32_t hours, uint32_t seq) {
    if (NormalizeSerial(serial).empty() || hours == 0) return {};
    Bytes payload = BuildPayload(machineId, serial, hours, seq);
    Bytes hash = Sha256(std::string(payload.begin(), payload.end()));
    if (hash.size() != 32) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) return {};
    BCRYPT_KEY_HANDLE key = nullptr;
    std::string code;
    if (ImportKey(alg, priv, true, key)) {
        Bytes sig(kSig);
        ULONG cb = 0;
        if (BCRYPT_SUCCESS(BCryptSignHash(key, nullptr, hash.data(), 32, sig.data(), (ULONG)sig.size(), &cb, 0)) && cb == kSig) {
            Bytes all = payload;
            all.insert(all.end(), sig.begin(), sig.end());
            code = Group5(B32Encode(all));
        }
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return code;
}

CodeResult VerifyCode(const Bytes& pub, const std::string& code, const std::wstring& machineId,
    const std::string& serial, CodeInfo& out)
{
    Bytes all;
    if (!B32Decode(code, all) || all.size() != kPayload + kSig || all[0] != 1) return CodeResult::BadFormat;
    Bytes payload(all.begin(), all.begin() + kPayload), sig(all.begin() + kPayload, all.end());
    Bytes hash = Sha256(std::string(payload.begin(), payload.end()));
    if (hash.size() != 32) return CodeResult::BadFormat;

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) return CodeResult::BadSignature;
    BCRYPT_KEY_HANDLE key = nullptr;
    bool good = false;
    if (ImportKey(alg, pub, false, key)) {
        good = BCRYPT_SUCCESS(BCryptVerifySignature(key, nullptr, hash.data(), 32, sig.data(), (ULONG)sig.size(), 0));
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!good) return CodeResult::BadSignature;

    // chữ ký đúng -> kiểm tra gắn kết
    if (Bytes(payload.begin() + 9, payload.begin() + 17) != MachineHash8(machineId)) return CodeResult::WrongMachine;
    if (NormalizeSerial(serial).empty()) return CodeResult::NoPrinter;
    if (Bytes(payload.begin() + 17, payload.begin() + 25) != SerialHash8(serial)) return CodeResult::WrongPrinter;
    out.hours = Get32(&payload[1]);
    out.seq = Get32(&payload[5]);
    return CodeResult::Ok;
}

// ============================================================
// Kho lưu + bộ đếm giờ
// ============================================================
StoreConfig DefaultStoreConfig() {
    return { LinxDataDir().wstring(), L"Software\\LinxController\\License" };
}

Service::Service(Bytes pubBlob, StoreConfig cfg, std::wstring machineId)
    : pub_(std::move(pubBlob)), cfg_(std::move(cfg)), machineId_(std::move(machineId)) {}

std::string Service::Serialize(const Data& d) const {
    std::ostringstream o;
    o << "LXL1|" << d.usedSec << "|" << d.limitHours << "|" << d.boundSerial << "|";
    bool first = true;
    for (uint32_t s : d.seqs) { if (!first) o << ","; o << s; first = false; }
    const std::string body = o.str();
    uint8_t mac[32] = { 0 };
    Bytes key = Sha256("LinxLic1|" + WideToUtf8(machineId_));
    HmacSha256(key, body, mac);
    return body + "|" + ToHex(mac, 32);
}

bool Service::Parse(const std::string& text, Data& d) const {
    const size_t cut = text.rfind('|');
    if (cut == std::string::npos) return false;
    const std::string body = text.substr(0, cut);
    std::string tail = text.substr(cut + 1);
    while (!tail.empty() && isspace((unsigned char)tail.back())) tail.pop_back();
    uint8_t mac[32] = { 0 };
    Bytes key = Sha256("LinxLic1|" + WideToUtf8(machineId_));
    if (!HmacSha256(key, body, mac) || ToHex(mac, 32) != tail) return false;

    std::vector<std::string> parts;
    std::string cur;
    for (char c : body) { if (c == '|') { parts.push_back(cur); cur.clear(); } else cur.push_back(c); }
    parts.push_back(cur);
    if (parts.size() != 5 || parts[0] != "LXL1") return false;
    try {
        Data r;
        r.usedSec = std::stoull(parts[1]);
        r.limitHours = (uint32_t)std::stoul(parts[2]);
        r.boundSerial = parts[3];
        std::stringstream ss(parts[4]);
        std::string item;
        while (std::getline(ss, item, ',')) if (!item.empty()) r.seqs.insert((uint32_t)std::stoul(item));
        d = std::move(r);
    }
    catch (...) { return false; }
    return true;
}

static bool ReadFileText(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}
static bool ReadReg(const std::wstring& key, std::string& out) {
    wchar_t buf[2048] = { 0 };
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"State", RRF_RT_REG_SZ, nullptr, buf, &cb) != ERROR_SUCCESS) return false;
    out = WideToUtf8(buf);
    return true;
}

void Service::Load() {
    std::lock_guard<std::mutex> lock(mu_);
    const std::wstring file = cfg_.dir + L"\\license.dat";
    std::string ft, rt;
    const bool hasF = ReadFileText(file, ft), hasR = ReadReg(cfg_.regKey, rt);
    Data df, dr;
    const bool okF = hasF && Parse(ft, df), okR = hasR && Parse(rt, dr);
    tampered_ = false;
    if (!hasF && !hasR) {
        data_ = Data();                                   // cài mới
    }
    else if (!okF && !okR) {
        data_ = Data();                                   // có dữ liệu nhưng sai chữ ký ở CẢ HAI nơi: bị sửa / chép từ máy khác
        data_.usedSec = (uint64_t)data_.limitHours * 3600ull;
        tampered_ = true;
    }
    else {
        // hợp nhất: lấy bản đã dùng nhiều nhất; hạn mức lớn nhất (chỉ tăng được bằng mã hợp lệ); hợp các mã đã dùng
        Data base = okF ? df : dr;
        if (okF && okR) {
            base = (dr.usedSec > df.usedSec) ? dr : df;
            base.limitHours = (std::max)(df.limitHours, dr.limitHours);
            base.seqs.insert(df.seqs.begin(), df.seqs.end());
            base.seqs.insert(dr.seqs.begin(), dr.seqs.end());
        }
        else if (hasF && hasR) {
            tampered_ = false;                            // một nơi sai chữ ký nhưng nơi kia hợp lệ: dùng nơi hợp lệ
        }
        data_ = base;
    }
    QueryUnbiasedInterruptTime(&lastTick100ns_);
    sinceSave100ns_ = 0;
    fraction100ns_ = 0;
    SaveLocked();
}

void Service::AddSeconds(uint64_t s) {
    std::lock_guard<std::mutex> lock(mu_);
    data_.usedSec += s;
}

void Service::Tick() {
    std::lock_guard<std::mutex> lock(mu_);
    ULONGLONG now = 0;
    QueryUnbiasedInterruptTime(&now);
    if (now > lastTick100ns_) {
        const uint64_t d = now - lastTick100ns_;
        lastTick100ns_ = now;
        fraction100ns_ += d;
        sinceSave100ns_ += d;
        data_.usedSec += fraction100ns_ / 10000000ull;
        fraction100ns_ %= 10000000ull;
    }
    if (sinceSave100ns_ >= 60ull * 10000000ull) { SaveLocked(); sinceSave100ns_ = 0; }
}

void Service::Save() { std::lock_guard<std::mutex> lock(mu_); SaveLocked(); }

void Service::SaveLocked() {
    const std::string text = Serialize(data_);
    std::error_code ec;
    std::filesystem::create_directories(cfg_.dir, ec);
    const std::wstring file = cfg_.dir + L"\\license.dat", tmp = file + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (f) { f.write(text.data(), (std::streamsize)text.size()); f.close(); MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH); }
    }
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, cfg_.regKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        std::wstring w(text.begin(), text.end());    // chuỗi chỉ gồm ASCII
        RegSetValueExW(k, L"State", 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

uint64_t Service::UsedSeconds() const { std::lock_guard<std::mutex> l(mu_); return data_.usedSec; }
uint32_t Service::LimitHours() const { std::lock_guard<std::mutex> l(mu_); return data_.limitHours; }
int64_t Service::RemainingSeconds() const {
    std::lock_guard<std::mutex> l(mu_);
    return (int64_t)((uint64_t)data_.limitHours * 3600ull) - (int64_t)data_.usedSec;
}
bool Service::Expired() const { return RemainingSeconds() <= 0; }
bool Service::Tampered() const { std::lock_guard<std::mutex> l(mu_); return tampered_; }
std::string Service::BoundSerial() const { std::lock_guard<std::mutex> l(mu_); return data_.boundSerial; }

CodeResult Service::Activate(const std::string& code, const std::string& printerSerial, uint32_t* added) {
    std::lock_guard<std::mutex> lock(mu_);
    CodeInfo info;
    const CodeResult r = VerifyCode(pub_, code, machineId_, printerSerial, info);
    if (r != CodeResult::Ok) return r;
    if (data_.seqs.count(info.seq)) return CodeResult::AlreadyUsed;
    // khi dữ liệu từng bị sửa: tính lại từ hạn mức gốc để mã mới thật sự mở khoá
    if (tampered_) { data_.usedSec = (uint64_t)data_.limitHours * 3600ull; tampered_ = false; }
    uint64_t lim = (uint64_t)data_.limitHours + info.hours;
    data_.limitHours = (uint32_t)(std::min)(lim, (uint64_t)4000000ull);
    data_.seqs.insert(info.seq);
    data_.boundSerial = NormalizeSerial(printerSerial);
    if (added) *added = info.hours;
    SaveLocked();
    return CodeResult::Ok;
}

} // namespace lic
