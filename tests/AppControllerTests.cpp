// [2026-11-16 R29] Test luồng xử lý / quản lý thread của AppController với máy in giả.
// AppController thật + RciClient thật + cửa sổ message-only nhận WM_APP_* (đóng vai UI thread).
// Phần lưu file (PrintPersistStorage) được thay bằng stub để KHÔNG ghi đè dữ liệu thật của người dùng.
// Yêu cầu: cổng 127.0.0.1:29043 còn trống.
#include "FakePrinter.h"
#include <cstdio>
#include <optional>
#include "../Linx v2.1.2/AppController.h"
#include "../Linx v2.1.2/PrintPersistData.h"
#include "../Linx v2.1.2/PrintPersistStorage.h"

// ---- stub: không đụng tới %LOCALAPPDATA%\LinxController ----
bool PrintPersistStorage::Save(const PrintPersistData&) { return true; }
std::optional<PrintPersistData> PrintPersistStorage::Load() { return std::nullopt; }

static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { ++g_run; if (!(cond)) { ++g_fail; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---- "UI": ghi lại mọi thông điệp worker gửi về ----
struct Seen {
    std::vector<PrinterState> states;
    std::vector<bool> conn;
    std::vector<std::wstring> logs;
} g;

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_APP_LOG: { auto* p = (LogMessage*)w; g.logs.push_back(p->text); delete p; return 0; }
    case WM_APP_PRINTER_UPDATE: { auto* p = (PrinterStateMessage*)w; g.states.push_back(p->state); delete p; return 0; }
    case WM_APP_CONNECTION_UPDATE: { auto* p = (ConnectionMessage*)w; g.conn.push_back(p->connected); delete p; return 0; }
    case WM_APP_BUTTON_STATE: { delete (ButtonStateMessage*)w; return 0; }
    }
    return DefWindowProc(h, m, w, l);
}

static void Pump() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
}
template <class P> static bool PumpUntil(P pred, int timeoutMs) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        Pump();
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= end) return false;
        Sleep(5);
    }
}
static void PumpFor(int ms) { PumpUntil([] { return false; }, ms); }
static bool LastConn(bool v) { return !g.conn.empty() && g.conn.back() == v; }
static bool SawConn(bool v) { for (bool c : g.conn) if (c == v) return true; return false; }

int main() {
    // ---- máy in giả trên đúng cổng RCI ----
    FakePrinter fp;
    std::atomic<int> statusMode{ 0 };     // 0 = trả lời, 1 = im lặng (timeout)
    std::atomic<int> printState{ 2 };     // 2 = idle, 4 = waiting (đang in)
    std::atomic<int> countDelayMs{ 0 };   // trễ khi trả lời 141D
    fp.handler = [&](uint8_t cmd, const Bytes&) -> Reply {
        Reply r;
        switch (cmd) {
        case 0x14:
            if (statusMode == 1) { r.none = true; break; }
            r.wire = MakeReply(true, 0, 0, 0x14, { 0x00, (uint8_t)printState.load(), 0, 0, 0, 0 });
            break;
        case 0x8D: {
            Bytes d{ 7, 0, 0, 0 };
            std::string nm = "MSG1"; nm.resize(16, '\0');
            d.insert(d.end(), nm.begin(), nm.end());
            r.wire = MakeReply(true, 0, 0, 0x8D, d);
            r.delayMs = countDelayMs;
            break;
        }
        default:
            r.wire = MakeReply(true, 0, 0, cmd);
        }
        return r;
    };
    if (!fp.Start(kRciDefaultPort)) {
        std::printf("KHONG CHAY DUOC: cong 127.0.0.1:%d dang bi chiem\n", (int)kRciDefaultPort);
        return 2;
    }

    // ---- cửa sổ message-only đóng vai UI ----
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"LinxTestWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"LinxTestWnd", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { std::printf("khong tao duoc cua so test\n"); return 2; }

    {
        AppController ctrl(hwnd);
        ctrl.SetLastIp(L"127.0.0.1");
        ctrl.StartWorkerThread();

        // === S1: kết nối -> UI nhận Connected, trạng thái đọc từ máy (jet running) ===
        ctrl.Connect(L"127.0.0.1");
        CHECK(PumpUntil([] { return LastConn(true); }, 5000));
        CHECK(PumpUntil([] { return !g.states.empty() && g.states.back().jetOn
            && g.states.back().status == PrinterStateType::Ready; }, 3000));
        // R20: trạng thái Connecting do worker đặt và UI có nhận được
        {
            bool sawConnecting = false;
            for (auto& s : g.states) if (s.status == PrinterStateType::Connecting) sawConnecting = true;
            CHECK(sawConnecting);
        }
        // R20: yêu cầu Connect trùng không làm kết nối lại
        {
            size_t connBefore = g.conn.size();
            ctrl.Connect(L"127.0.0.1");
            PumpFor(400);
            CHECK(g.conn.size() == connBefore);
            CHECK(ctrl.IsConnected());
        }

        // === S2 (R18): status timeout vài lần KHÔNG được làm UI nhảy sang "jet tắt/Idle" ===
        {
            g.states.clear(); g.conn.clear();
            statusMode = 1;
            PumpFor(1300);              // ~2 lần poll bị timeout (500 ms mỗi lần)
            statusMode = 0;
            PumpFor(700);
            bool wrong = false;
            for (auto& s : g.states) if (!s.jetOn || s.status == PrinterStateType::Idle) wrong = true;
            CHECK(!wrong);
            CHECK(g.conn.empty());      // không báo mất kết nối
            CHECK(ctrl.IsConnected());
            CHECK(ctrl.GetCurrentState().jetOn);
        }

        // === S3 (R18/R22): máy im lặng kéo dài -> báo mất kết nối ngay trên UI, rồi tự kết nối lại ===
        {
            g.states.clear(); g.conn.clear();
            statusMode = 1;
            CHECK(PumpUntil([] { return SawConn(false); }, 8000));
            bool sawReconnecting = false;
            for (auto& s : g.states) if (s.status == PrinterStateType::Reconnecting) sawReconnecting = true;
            CHECK(sawReconnecting);
            statusMode = 0;
            CHECK(PumpUntil([] { return LastConn(true); }, 8000));
            CHECK(PumpUntil([] { return !g.states.empty() && g.states.back().jetOn; }, 3000));
        }

        // === S4 (R22): máy in đóng kết nối -> UI biết trong < 2 giây, sau đó tự nối lại ===
        {
            g.states.clear(); g.conn.clear();
            const long long t0 = FakePrinter::NowMs();
            fp.dropClient = true;
            CHECK(PumpUntil([] { return SawConn(false); }, 4000));
            const long long dt = FakePrinter::NowMs() - t0;
            std::printf("  S4: bao mat ket noi sau %lld ms\n", dt);
            CHECK(dt < 2000);
            CHECK(PumpUntil([] { return LastConn(true); }, 8000));
        }

        // === S5 (R24): Stop Print không bị kẹt sau poll chậm ===
        {
            printState = 4;             // máy đang in -> worker poll cả status lẫn print count
            countDelayMs = 300;         // print count trả lời chậm
            CHECK(PumpUntil([] { return !g.states.empty() && g.states.back().status == PrinterStateType::Printing; }, 3000));
            PumpFor(600);
            fp.ClearReceived();
            const long long t0 = FakePrinter::NowMs();
            ctrl.StopPrinting();
            CHECK(PumpUntil([&] { return fp.FirstSeenMs(0x12) >= 0; }, 3000));
            const long long dt = fp.FirstSeenMs(0x12) - t0;
            std::printf("  S5: Stop Print toi may sau %lld ms\n", dt);
            CHECK(dt >= 0 && dt < 1000);
            printState = 2; countDelayMs = 0;
        }

        // === S6 (R12/R23): dừng worker nhanh kể cả khi máy in đang không trả lời ===
        {
            statusMode = 1;
            PumpFor(200);
            const long long t0 = FakePrinter::NowMs();
            CHECK(ctrl.StopWorkerThread(0));
            const long long dt = FakePrinter::NowMs() - t0;
            std::printf("  S6: StopWorkerThread mat %lld ms\n", dt);
            CHECK(dt < 1500);
            statusMode = 0;
        }

        // === S7 (R23): khởi động lại worker, mất máy in hẳn, dừng trong lúc đang chờ reconnect ===
        {
            g.conn.clear();
            ctrl.StartWorkerThread();
            ctrl.Connect(L"127.0.0.1");
            CHECK(PumpUntil([] { return LastConn(true); }, 6000));
            fp.Stop();                  // máy in biến mất
            CHECK(PumpUntil([] { return SawConn(false); }, 8000));
            PumpFor(500);               // worker đang ở chế độ chờ giữa các lần reconnect
            const long long t0 = FakePrinter::NowMs();
            ctrl.StopWorkerThread(0);
            const long long dt = FakePrinter::NowMs() - t0;
            std::printf("  S7: dung worker khi dang cho reconnect mat %lld ms\n", dt);
            CHECK(dt < 3500);           // tệ nhất: đang giữa một lần connect() (timeout 3 giây)
        }
        Pump();
    }   // ~AppController: không được treo / crash

    Pump();
    DestroyWindow(hwnd);
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
