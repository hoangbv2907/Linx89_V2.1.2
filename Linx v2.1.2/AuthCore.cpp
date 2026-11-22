#include "AuthCore.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace auth {

namespace {

std::string ToHex(const std::vector<uint8_t>& d) {
    static const char* k = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) { s.push_back(k[b >> 4]); s.push_back(k[b & 15]); }
    return s;
}

bool FromHex(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    if (s.size() % 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); i += 2) {
        int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back((uint8_t)(a * 16 + b));
    }
    return true;
}

// PBKDF2-HMAC-SHA256
bool Pbkdf2(const std::string& pin, const std::vector<uint8_t>& salt, int iterations, std::vector<uint8_t>& out) {
    BCRYPT_ALG_HANDLE h = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG))) return false;
    out.assign(32, 0);
    const bool ok = BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(h, (PUCHAR)pin.data(), (ULONG)pin.size(),
        (PUCHAR)salt.data(), (ULONG)salt.size(), (ULONGLONG)iterations, out.data(), (ULONG)out.size(), 0));
    BCryptCloseAlgorithmProvider(h, 0);
    return ok;
}

bool ConstantTimeEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace

bool IsValidPinFormat(const std::string& pin) {
    if (pin.size() < 4 || pin.size() > 8) return false;
    for (char c : pin) if (c < '0' || c > '9') return false;
    return true;
}

PinRecord MakePin(const std::string& pin) {
    PinRecord r;
    if (!IsValidPinFormat(pin)) return r;
    std::vector<uint8_t> salt(16), hash;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, salt.data(), (ULONG)salt.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return r;
    if (!Pbkdf2(pin, salt, kPinIterations, hash)) return r;
    r.saltHex = ToHex(salt);
    r.hashHex = ToHex(hash);
    r.iterations = kPinIterations;
    return r;
}

bool VerifyPin(const PinRecord& rec, const std::string& pin) {
    if (!rec.IsSet()) {   // chưa đặt PIN riêng -> PIN mặc định
        const std::string d = kDefaultPin;
        uint8_t diff = (uint8_t)(pin.size() ^ d.size());
        for (size_t i = 0; i < d.size() && i < pin.size(); ++i) diff |= (uint8_t)(pin[i] ^ d[i]);
        return diff == 0;
    }
    std::vector<uint8_t> salt, expected, got;
    if (!FromHex(rec.saltHex, salt) || !FromHex(rec.hashHex, expected) || salt.empty() || expected.size() != 32) return false;
    if (rec.iterations < 1000 || rec.iterations > 5000000) return false;
    if (!Pbkdf2(pin, salt, rec.iterations, got)) return false;
    return ConstantTimeEqual(expected, got);
}

Session::Session(Clock clock) : clock_(std::move(clock)) {
    if (!clock_) clock_ = []() { return (uint64_t)GetTickCount64(); };
    lastActive_ = clock_();
}

bool Session::Login(const PinRecord& rec, const std::string& pin) {
    if (!VerifyPin(rec, pin)) return false;
    role_ = Role::Supervisor;
    lastActive_ = clock_();
    return true;
}

void Session::Logout() { role_ = Role::Operator; }

Role Session::CurrentRole() {
    if (role_ == Role::Supervisor && timeoutMin_ > 0) {
        const uint64_t idle = clock_() - lastActive_;
        if (idle >= (uint64_t)timeoutMin_ * 60ull * 1000ull) role_ = Role::Operator;
    }
    return role_;
}

void Session::Touch() {
    if (role_ == Role::Supervisor) {
        if (CurrentRole() == Role::Supervisor) lastActive_ = clock_();
    }
}

void Session::SetTimeoutMinutes(int m) {
    timeoutMin_ = m < 0 ? 0 : (m > 240 ? 240 : m);
    if (role_ == Role::Supervisor) lastActive_ = clock_();
}

} // namespace auth
