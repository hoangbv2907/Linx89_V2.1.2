// [2026-11-13 R17] Test tích hợp: máy in giả (TCP loopback) + RciClient thật qua socket.
// Kiểm tra: gửi/nhận khung, reply bị chia nhỏ từng byte, dữ liệu chứa ESC (1B), rác trước reply,
// NAK, ACK kèm P-STATUS lỗi, sai Command ID, sai checksum, timeout, parse 157D / 141D.
// Máy in giả chỉ mô phỏng theo tài liệu MP65969-7, KHÔNG thay thế việc thử trên máy thật.
#include "FakePrinter.h"
#include <cstdio>

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool Has(const std::vector<std::wstring>& logs, const std::wstring& s) {
    for (auto& l : logs) if (l.find(s) != std::wstring::npos) return true;
    return false;
}

int main() {
    FakePrinter fp;
    // cấu hình hành vi theo từng kịch bản
    std::atomic<int> startPrintMode{ 0 };   // 0=ACK, 1=NAK 0x2E, 2=ACK nhưng P-STATUS=10
    std::atomic<int> stopPrintMode{ 0 };    // 0=ACK, 1=sai cmd id, 2=sai checksum, 3=không trả lời, 4=rác trước reply
    std::atomic<bool> statusSilent{ false }; // true = không trả lời status (timeout)
    fp.handler = [&](uint8_t cmd, const Bytes&) -> Reply {
        Reply r;
        switch (cmd) {
        case 0x14: {   // Status: jet running(0), print waiting(4), error mask 0x0000031B (byte 1B 03 00 00 -> ESC ESC 03)
            if (statusSilent) { r.none = true; break; }
            r.wire = MakeReply(true, 0, 0, 0x14, { 0x00, 0x04, 0x1B, 0x03, 0x00, 0x00 });
            r.byteByByte = true;   // kiểm tra reply bị chia nhỏ
            break;
        }
        case 0x11:
            if (startPrintMode == 1) r.wire = MakeReply(false, 0, 0x2E, 0x11);
            else if (startPrintMode == 2) r.wire = MakeReply(true, 10, 0, 0x11);
            else r.wire = MakeReply(true, 0, 0, 0x11);
            break;
        case 0x12: {
            int m = stopPrintMode;
            if (m == 1) r.wire = MakeReply(true, 0, 0, 0x13);
            else if (m == 2) { r.wire = MakeReply(true, 0, 0, 0x12); r.wire.back() ^= 0x01; }
            else if (m == 3) r.none = true;
            else if (m == 4) { r.wire = { 0x00, 0xFF, 0x55 }; auto g = MakeReply(true, 0, 0, 0x12); r.wire.insert(r.wire.end(), g.begin(), g.end()); }
            else r.wire = MakeReply(true, 0, 0, 0x12);
            break;
        }
        case 0x9D: {   // 157D: 1 field "RemoteField1", max 5, autoload 1
            Bytes d{ 0x01, 0x1C };
            std::string nm = "RemoteField1"; nm.resize(32, '\0');
            d.insert(d.end(), nm.begin(), nm.end());
            d.push_back(5); d.push_back(1); d.push_back(0); d.push_back(0);
            r.wire = MakeReply(true, 0, 0, 0x9D, d);
            break;
        }
        case 0x8D: {   // 141D: count 0x0001021B (chứa byte 1B) + tên message
            Bytes d{ 0x1B, 0x02, 0x01, 0x00 };
            std::string nm = "JOB00001"; nm.resize(16, '\0');
            d.insert(d.end(), nm.begin(), nm.end());
            r.wire = MakeReply(true, 0, 0, 0x8D, d);
            break;
        }
        default:
            r.wire = MakeReply(true, 0, 0, cmd);
        }
        return r;
    };
    if (!fp.Start()) { std::printf("không khởi động được máy in giả\n"); return 2; }

    std::vector<std::wstring> logs;
    RciClient c;
    c.SetMessageCallback([&](const std::wstring& m, int) { logs.push_back(m); });
    CHECK(c.Connect(L"127.0.0.1", fp.port, 2000));
    CHECK(c.IsConnected());

    // --- Status: reply chia nhỏ từng byte + dữ liệu chứa ESC ---
    {
        PrinterStatus s = c.RequestStatusEx();
        CHECK(s.valid);            // [2026-11-16 R18]
        CHECK(s.jetOn);
        CHECK(s.printing);
        CHECK(s.errorMask == 0x0000031B);
        CHECK(s.pStatus == 0 && s.cStatus == 0);
    }
    // --- khung gửi đi đúng như tài liệu ---
    {
        fp.ClearReceived();
        CHECK(c.StartJet());
        CHECK(c.StopJet());
        CHECK(c.StartPrint());
        auto rx = fp.Received();
        CHECK(rx.size() == 3);
        if (rx.size() == 3) {
            CHECK((rx[0] == Bytes{ 0x1B,0x02,0x0F,0x1B,0x03,0xEC }));   // Start Jet (tài liệu)
            CHECK((rx[1] == Bytes{ 0x1B,0x02,0x10,0x1B,0x03,0xEB }));   // Stop Jet
            CHECK((rx[2] == Bytes{ 0x1B,0x02,0x11,0x1B,0x03,0xEA }));   // Start Print (tài liệu)
        }
    }
    // --- LoadMessage / Remote data / Set count: payload đúng ---
    {
        fp.ClearReceived();
        CHECK(c.LoadMessage("JOB00001", 5));
        std::string val = "ABC  ";
        CHECK(c.SendRemoteFieldDataByName("RemoteField1", val, 1000));
        CHECK(c.SetMessagePrintCount(100, "", 1000));
        auto rx = fp.Received();
        CHECK(rx.size() == 3);
        if (rx.size() == 3) {
            Bytes p1; std::string n1 = "JOB00001"; n1.resize(16, '\0'); p1.assign(n1.begin(), n1.end()); p1.push_back(5); p1.push_back(0);
            CHECK(rx[0] == RciClient::BuildFrame(0x1E, p1));
            Bytes p2{ 0x01 }; for (char ch : std::string("RemoteField1")) p2.push_back((uint8_t)ch);
            p2.push_back(0); for (char ch : val) p2.push_back((uint8_t)ch); p2.push_back(0);
            CHECK(rx[1] == RciClient::BuildFrame(0x9E, p2));
            Bytes p3{ 100, 0, 0, 0 }; p3.insert(p3.end(), 16, 0);
            CHECK(rx[2] == RciClient::BuildFrame(0x8C, p3));
        }
    }
    // --- NAK: không còn bị coi là thành công; log có C-STATUS ---
    {
        logs.clear(); startPrintMode = 1;
        CHECK(!c.StartPrint());
        CHECK(Has(logs, L"NAK"));
        CHECK(Has(logs, L"No message"));
    }
    // --- ACK nhưng P-STATUS lỗi -> không coi là in ---
    {
        logs.clear(); startPrintMode = 2;
        CHECK(!c.StartPrint());
        CHECK(Has(logs, L"Solvent Tank Empty"));    // P-STATUS 10 (thập phân)
        startPrintMode = 0;
        CHECK(c.StartPrint());
    }
    // --- sai Command ID / sai checksum / timeout / rác trước reply ---
    {
        logs.clear(); stopPrintMode = 1;
        CHECK(!c.StopPrint());
        CHECK(Has(logs, L"Command ID"));

        logs.clear(); stopPrintMode = 2;
        CHECK(!c.StopPrint());
        CHECK(Has(logs, L"checksum"));

        stopPrintMode = 3;
        auto t0 = std::chrono::steady_clock::now();
        CHECK(!c.SendAndWaitAck(0x12, {}, 300));
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms >= 250 && ms < 2000);
        CHECK(c.IsConnected());                     // timeout không làm mất kết nối

        // [2026-11-16 R18] status timeout -> valid = false (không được dùng làm dữ liệu thật)
        statusSilent = true;
        CHECK(!c.RequestStatusEx().valid);
        CHECK(c.IsConnected());
        statusSilent = false;
        CHECK(c.RequestStatusEx().valid);

        stopPrintMode = 4;                          // rác 00 FF 55 trước reply hợp lệ
        CHECK(c.StopPrint());

        stopPrintMode = 0;
        CHECK(c.StopPrint());                       // phục hồi bình thường sau lỗi
    }
    // --- 157D: danh sách remote field ---
    {
        std::vector<RemoteFieldInfo> f;
        CHECK(c.RequestRemoteFields(f, 1000));
        CHECK(f.size() == 1);
        if (f.size() == 1) {
            CHECK(f[0].name == "RemoteField1");
            CHECK(f[0].maxChars == 5);
            CHECK(f[0].autoLoad);
        }
    }
    // --- 141D: print count chứa byte 1B ---
    {
        uint32_t cnt = 0; std::string name;
        CHECK(c.RequestMessagePrintCount(cnt, name, "", 1000));
        CHECK(cnt == 0x0001021B);
        CHECK(name == "JOB00001");
    }
    // --- ngắt kết nối ---
    c.Disconnect();
    CHECK(!c.IsConnected());
    CHECK(!c.RequestStatusEx().valid);   // [2026-11-16 R18]
    CHECK(!c.StartJet());   // không crash khi gọi lúc đã ngắt

    fp.Stop();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
