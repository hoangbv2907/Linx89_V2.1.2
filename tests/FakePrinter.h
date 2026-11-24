// [2026-11-16 R29] Máy in giả (TCP loopback) dùng chung cho RciSimTests và AppControllerTests.
// Chỉ mô phỏng theo tài liệu MP65969-7, KHÔNG thay thế việc thử trên máy thật.
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <chrono>
#include "rci/RciClient.h"

using Bytes = std::vector<uint8_t>;

// Dựng reply theo tài liệu: ESC ACK/NAK P C CMD [data] ESC ETX chk
inline Bytes MakeReply(bool ack, uint8_t p, uint8_t c, uint8_t cmd, const Bytes& data = {}) {
    Bytes cs{ (uint8_t)(ack ? 0x06 : 0x15), p, c, cmd };
    cs.insert(cs.end(), data.begin(), data.end());
    cs.push_back(0x03);
    const uint8_t chk = RciClient::ComputeChecksum(cs);
    Bytes w{ 0x1B, cs[0] };
    auto esc = [&](uint8_t b) { w.push_back(b); if (b == 0x1B) w.push_back(b); };
    esc(p); esc(c); esc(cmd);
    for (uint8_t d : data) esc(d);
    w.push_back(0x1B); w.push_back(0x03);
    esc(chk);
    return w;
}

struct Reply {
    Bytes wire;
    bool none = false;       // không trả lời (mô phỏng timeout)
    bool byteByByte = false; // gửi từng byte một (mô phỏng TCP phân mảnh)
    int delayMs = 0;         // trễ trước khi trả lời
};

class FakePrinter {
public:
    using Handler = std::function<Reply(uint8_t cmd, const Bytes& frame)>;
    Handler handler;
    unsigned short port = 0;
    std::atomic<int> accepts{ 0 };            // so ket noi TCP da nhan
    std::atomic<int> pipelined{ 0 };          // so lan host gui lenh moi khi CHUA nhan reply lenh truoc (manual cam)
    std::atomic<bool> dropClient{ false };   // true -> đóng kết nối hiện tại (mô phỏng đứt mạng)

    bool Start(unsigned short wantPort = 0) {
        WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
        lsock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(wantPort);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        if (bind(lsock_, (sockaddr*)&a, sizeof(a)) != 0) return false;
        int len = sizeof(a);
        getsockname(lsock_, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        listen(lsock_, 4);
        th_ = std::thread([this] { Loop(); });
        return true;
    }
    void Stop() {
        stop_ = true;
        if (th_.joinable()) th_.join();
        if (lsock_ != INVALID_SOCKET) { closesocket(lsock_); lsock_ = INVALID_SOCKET; }
    }
    std::vector<Bytes> Received() { std::lock_guard<std::mutex> l(m_); return rx_; }
    void ClearReceived() { std::lock_guard<std::mutex> l(m_); rx_.clear(); rxTime_.clear(); }
    // Thời điểm (ms, steady clock) nhận được khung đầu tiên có command id = cmd; -1 nếu chưa có.
    long long FirstSeenMs(uint8_t cmd) {
        std::lock_guard<std::mutex> l(m_);
        for (size_t i = 0; i < rx_.size(); ++i) if (rx_[i].size() > 2 && rx_[i][2] == cmd) return rxTime_[i];
        return -1;
    }
    static long long NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    SOCKET lsock_ = INVALID_SOCKET;
    std::thread th_;
    std::atomic<bool> stop_{ false };
    std::mutex m_;
    std::vector<Bytes> rx_;
    std::vector<long long> rxTime_;

    static bool Readable(SOCKET s, int ms) {
        fd_set f; FD_ZERO(&f); FD_SET(s, &f);
        timeval tv{ 0, ms * 1000 };
        return select(0, &f, nullptr, nullptr, &tv) > 0;
    }
    void SendReply(SOCKET c, const Reply& r) {
        if (r.none) return;
        if (r.delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(r.delayMs));
        if (r.byteByByte) {
            for (uint8_t b : r.wire) { send(c, (const char*)&b, 1, 0); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        }
        else {
            send(c, (const char*)r.wire.data(), (int)r.wire.size(), 0);
        }
    }
    void Loop() {
        while (!stop_) {
            if (!Readable(lsock_, 20)) continue;
            SOCKET c = accept(lsock_, nullptr, nullptr);
            if (c == INVALID_SOCKET) continue;
            ++accepts;
            Bytes buf;
            while (!stop_) {
                if (dropClient.exchange(false)) break;
                if (!Readable(c, 20)) continue;
                char tmp[512];
                int n = recv(c, tmp, sizeof(tmp), 0);
                if (n <= 0) break;
                buf.insert(buf.end(), tmp, tmp + n);
                for (;;) {
                    if (buf.size() < 2) break;
                    if (!(buf[0] == 0x1B && (buf[1] == 0x02 || buf[1] == 0x01))) { buf.erase(buf.begin()); continue; }
                    size_t e = RciFindEscEtx(buf, 2);
                    if (e == std::string::npos) break;
                    size_t after = e + 2;
                    if (buf.size() <= after) break;
                    size_t end = (buf[after] == 0x1B) ? after + 2 : after + 1;
                    if (buf.size() < end) break;
                    Bytes frame(buf.begin(), buf.begin() + end);
                    buf.erase(buf.begin(), buf.begin() + end);
                    if (!buf.empty()) ++pipelined;
                    { std::lock_guard<std::mutex> l(m_); rx_.push_back(frame); rxTime_.push_back(NowMs()); }
                    SendReply(c, handler(frame[2], frame));
                }
            }
            closesocket(c);
        }
    }
};
