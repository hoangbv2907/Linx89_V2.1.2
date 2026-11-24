// [2026-11-16 R32] Test case theo tai lieu Linx RCI MP65969-7 (xem tests/TEST_CASES.md de biet can cu tung ca).
// AppController that + RciClient that + may in gia CO TRANG THAI (LinxSim) hanh xu theo manual:
//   jet state (Running 0 / Startup 1 / Shutdown 2 / Stopped 3 / Fault 4), print state (Idle 2 / Waiting 4),
//   message da load, remote field, Message Print Count (140D/141D), ma NAK theo Table 3-2.
// Cua so message-only dong vai UI: ghi lai moi WM_APP_* ma worker gui ve.
// Yeu cau: cong 127.0.0.1:29043 con trong. Phan luu file duoc stub (khong ghi de du lieu that).
#include "FakePrinter.h"
#include <cstdio>
#include <optional>
#include <map>
#include <algorithm>
#include "controller/AppController.h"
#include "controller/PrintPersistData.h"
#include "controller/PrintPersistStorage.h"

bool PrintPersistStorage::Save(const PrintPersistData&) { return true; }
std::optional<PrintPersistData> PrintPersistStorage::Load() { return std::nullopt; }

// ---------------- ket qua theo ma test case ----------------
static std::vector<std::string> g_order;
static std::map<std::string, int> g_failBy, g_runBy;
static void Check(const char* id, bool ok, const char* expr, int line) {
    if (!g_runBy.count(id)) g_order.push_back(id);
    ++g_runBy[id];
    if (!ok) { ++g_failBy[id]; std::printf("  FAIL [%s] %s  (line %d)\n", id, expr, line); }
}
#define TC(id, cond) Check(id, (cond), #cond, __LINE__)

// ---------------- "UI" ----------------
struct Seen {
    std::vector<PrinterState> states;
    std::vector<bool> conn;
    std::vector<std::wstring> logs;
} g;

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_APP_LOG: { auto* p = (LogMessage*)w; g.logs.push_back(p->text); delete p; return 0; }
    case WM_APP_PRINTER_UPDATE: {
        auto* p = (PrinterStateMessage*)w;
        PrinterState s = p->state; s.targetCount = p->targetCount; s.printedCount = p->printedCount;
        g.states.push_back(s); delete p; return 0;
    }
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
static bool SawConnSince(size_t mark, bool v) { for (size_t i = mark; i < g.conn.size(); ++i) if (g.conn[i] == v) return true; return false; }
static bool LastConn(bool v) { return !g.conn.empty() && g.conn.back() == v; }
static bool SawStatusSince(size_t mark, PrinterStateType t) {
    for (size_t i = mark; i < g.states.size(); ++i) if (g.states[i].status == t) return true; return false;
}
static int FirstStatusSince(size_t mark, PrinterStateType t) {
    for (size_t i = mark; i < g.states.size(); ++i) if (g.states[i].status == t) return (int)i; return -1;
}
static bool LastStatus(PrinterStateType t) { return !g.states.empty() && g.states.back().status == t; }
static int CountLogSince(size_t mark, const std::wstring& sub) {
    int n = 0;
    for (size_t i = mark; i < g.logs.size(); ++i) if (g.logs[i].find(sub) != std::wstring::npos) ++n;
    return n;
}
static bool HasLogSince(size_t mark, const std::wstring& sub) { return CountLogSince(mark, sub) > 0; }

// ---------------- may in gia co trang thai ----------------
struct LinxSim {
    std::mutex m;
    // trang thai may
    uint8_t jetState = 3, printState = 2, pStatus = 0;
    uint32_t errorMask = 0;
    int transitionPolls = 0;          // so lan poll status con lai truoc khi jet xong chuyen trang thai
    bool faultOnStart = false;        // Start Jet -> Fault (jet state 4, P-STATUS 3 Phase Failure)
    bool messageLoaded = true;
    std::string knownMessage = "MSG1";
    bool hasRemoteFields = true;
    std::vector<std::string> msgList{ "MSG1", "MSG2" };   // [R44] ban tin co tren may
    // [R47] Bang remote field; MOI BAN TIN CHI CO 1 FIELD (chi so trong bang, mac dinh 0). 157D tra field cua ban tin dang tai
    std::vector<std::pair<std::string, int>> fieldDefs{ { "RemoteField1", 5 } };
    std::map<std::string, int> msgField;
    int FieldIdxOf(const std::string& msg) const { auto it = msgField.find(msg); return it == msgField.end() ? 0 : it->second; }
    int TotalLen(const std::string& msg) const { int i = FieldIdxOf(msg); return i < (int)fieldDefs.size() ? fieldDefs[i].second : 0; }
    uint32_t counter = 0;             // Message Print Count
    // dieu khien loi
    int statusBadChecksum = 0, statusNak = 0, statusSilent = 0;   // N reply status ke tiep bi loi
    // ghi nhan
    int badFrames = 0;
    std::vector<uint8_t> cmds;
    std::string lastField29, lastField158, lastField158Name, lastLoadName;
    uint32_t lastCountSet = 0xFFFFFFFF; Bytes lastCountName;
    int prints = 0;                   // so ban in THUC TE ke tu lan Start Print gan nhat

    static bool Decode(const Bytes& f, Bytes& body) {
        if (f.size() < 5 || f[0] != 0x1B || (f[1] != 0x02 && f[1] != 0x01)) return false;
        size_t e = RciFindEscEtx(f, 2);
        if (e == std::string::npos) return false;
        body.clear();
        for (size_t i = 2; i < e; ++i) {
            if (f[i] == 0x1B) { ++i; if (i >= e) return false; }
            body.push_back(f[i]);
        }
        size_t a = e + 2;
        if (a >= f.size() || body.empty()) return false;
        uint8_t chk = (f[a] == 0x1B) ? (a + 1 < f.size() ? f[a + 1] : 0) : f[a];
        Bytes cs{ f[1] }; cs.insert(cs.end(), body.begin(), body.end()); cs.push_back(0x03);
        return RciClient::ComputeChecksum(cs) == chk;
    }

    Reply Handle(uint8_t cmd, const Bytes& frame) {
        std::lock_guard<std::mutex> l(m);
        Reply r;
        Bytes body;
        if (!Decode(frame, body)) {            // Table 3-2: 08 Invalid checksum
            ++badFrames;
            r.wire = MakeReply(false, pStatus, 0x08, cmd);
            return r;
        }
        cmds.push_back(cmd);
        Bytes pl(body.begin() + 1, body.end());
        auto ack = [&](const Bytes& d = {}) { r.wire = MakeReply(true, pStatus, 0, cmd, d); };
        auto nak = [&](uint8_t c) { r.wire = MakeReply(false, pStatus, c, cmd); };

        switch (cmd) {
        case 0x14: {   // Printer Status Request
            if (statusSilent > 0) { --statusSilent; r.none = true; break; }
            if (statusNak > 0) { --statusNak; nak(0x51); break; }           // 81 Printer busy
            ack({ jetState, printState, (uint8_t)errorMask, (uint8_t)(errorMask >> 8),
                  (uint8_t)(errorMask >> 16), (uint8_t)(errorMask >> 24) });
            if (statusBadChecksum > 0) { --statusBadChecksum; r.wire.back() ^= 0x01; }
            if (transitionPolls > 0 && --transitionPolls == 0) {
                if (jetState == 1) jetState = 0; else if (jetState == 2) jetState = 3;
            }
            break;
        }
        case 0x0F:     // Start Jet: jet dang chay -> NAK 19 'Jet not idle'
            if (jetState == 0 || jetState == 1) { nak(0x13); break; }
            if (faultOnStart) { jetState = 4; pStatus = 3; ack(); break; }
            jetState = 1; transitionPolls = 4; ack();
            break;
        case 0x10:     // Stop Jet: CJ400/8800/8900 thuc hien ca khi dang in
            printState = 2; jetState = 2; transitionPolls = 4; ack();
            break;
        case 0x11:     // Start Print
            if (!messageLoaded) { nak(0x2E); break; }                        // 46 Print command: No message
            if (pStatus != 0) { ack(); break; }                              // ACK nhung khong in (xem P-STATUS)
            if (jetState == 3) { jetState = 1; transitionPolls = 4; }        // jet idle -> tu khoi dong
            printState = 4; prints = 0; ack();
            break;
        case 0x12:     // Stop Print
            printState = 2; ack();
            break;
        case 0x1E: {   // Load Print Message: ten 16 byte + count 2 byte
            std::string nm((const char*)pl.data(), pl.size() < 16 ? pl.size() : 16);
            nm = nm.substr(0, nm.find('\0'));
            lastLoadName = nm;
            if (pl.size() != 18) { nak(0x16); break; }                       // 22 Number of bytes in command
            if (std::find(msgList.begin(), msgList.end(), nm) == msgList.end()) { nak(0x24); break; }   // 36 Unknown message
            knownMessage = nm; messageLoaded = true; ack();
            break;
        }
        case 0x1D: {   // Download Remote Field Data: len16 + data
            if (!messageLoaded) { nak(0x3B); break; }                        // 59 No print message loaded
            if (!hasRemoteFields) { nak(0x3F); break; }                      // 63 No remote fields in message
            if (pl.size() < 2) { nak(0x16); break; }
            size_t n = pl[0] | (pl[1] << 8);
            if (pl.size() != 2 + n) { nak(0x16); break; }
            if ((int)n != TotalLen(knownMessage)) { nak(0x40); break; }                         // 64 Number of remote characters
            lastField29.assign((const char*)pl.data() + 2, n); ack();
            break;
        }
        case 0x61: {   // [R44] Request Data Directory: 'M' -> [dem 1 byte] + header 41 byte; KHONG hop le khi dang in -> NAK 20
            if (printState != 2) { nak(0x14); break; }
            if (pl.size() != 1) { nak(0x16); break; }
            if (pl[0] != 'M') { ack({ 0 }); break; }
            Bytes d{ (uint8_t)msgList.size() };
            for (const auto& n : msgList) {
                Bytes h(41, 0);
                h[0] = 73; h[2] = 88; h[4] = 6;
                std::copy(n.begin(), n.end(), h.begin() + 9);
                const std::string rn = "16 QUALITY";
                std::copy(rn.begin(), rn.end(), h.begin() + 25);
                d.insert(d.end(), h.begin(), h.end());
            }
            ack(d);
            break;
        }
        case 0x98: {   // [R42] Request Serial Number: 9 byte (8 ky tu + NUL)
            Bytes d{ 'S', 'N', '1', '2', '3', '4', '5', '6', 0 };
            ack(d);
            break;
        }
        case 0x8C: {   // Set Message Print Count: count 4 byte + ten 16 byte
            if (pl.size() != 20) { nak(0x16); break; }
            counter = pl[0] | (pl[1] << 8) | (pl[2] << 16) | ((uint32_t)pl[3] << 24);
            lastCountSet = counter; lastCountName.assign(pl.begin() + 4, pl.end());
            ack();
            break;
        }
        case 0x8D: {   // Request Message Print Count
            Bytes d{ (uint8_t)counter, (uint8_t)(counter >> 8), (uint8_t)(counter >> 16), (uint8_t)(counter >> 24) };
            std::string nm = knownMessage; nm.resize(16, '\0');
            d.insert(d.end(), nm.begin(), nm.end());
            ack(d);
            break;
        }
        case 0x9D: {   // Request Remote Field Data
            if (!hasRemoteFields) { ack({ 0x00 }); break; }
            Bytes d{ 1 };
            { const auto& fd = fieldDefs[FieldIdxOf(knownMessage)];
                d.push_back(0x1C);
                std::string nm = fd.first; nm.resize(32, '\0');
                d.insert(d.end(), nm.begin(), nm.end());
                d.push_back((uint8_t)fd.second); d.push_back(1); d.push_back(0); d.push_back(0);
            }
            ack(d);
            break;
        }
        case 0x9E: {   // Send Remote Field Data by Name: n, name\0, data\0 ; sai ten/do dai -> NAK 23
            if (pl.size() < 3 || pl[0] != 1) { nak(0x17); break; }
            if (!messageLoaded) { nak(0x3B); break; }
            if (!hasRemoteFields) { nak(0x3F); break; }
            std::string rest((const char*)pl.data() + 1, pl.size() - 1);
            size_t z = rest.find('\0');
            if (z == std::string::npos) { nak(0x17); break; }
            std::string nm = rest.substr(0, z), val = rest.substr(z + 1);
            if (val.empty() || val.back() != '\0') { nak(0x17); break; }
            val.pop_back();
            bool okf = false;
            { const auto& fd = fieldDefs[FieldIdxOf(knownMessage)];
                if (fd.first == nm && (int)val.size() == fd.second) okf = true; }
            if (!okf) { nak(0x17); break; }
            lastField158Name = nm;
            lastField158 = val; ack();
            break;
        }
        default:
            nak(0x11);     // 17 Invalid command
        }
        return r;
    }

    // Mo phong n lan in (photocell). Chi in khi jet Running va print state Waiting.
    void Trigger(int n) {
        std::lock_guard<std::mutex> l(m);
        if (jetState == 0 && printState == 4) { counter += n; prints += n; }
    }
    template <class F> auto With(F f) { std::lock_guard<std::mutex> l(m); return f(*this); }
    size_t CmdCount() { std::lock_guard<std::mutex> l(m); return cmds.size(); }
    std::vector<uint8_t> CmdsSince(size_t mark) { std::lock_guard<std::mutex> l(m); return std::vector<uint8_t>(cmds.begin() + mark, cmds.end()); }
    int CountSince(size_t mark, uint8_t c) { int n = 0; for (uint8_t x : CmdsSince(mark)) if (x == c) ++n; return n; }
    // seq xuat hien dung thu tu (bo qua cac lenh xen giua nhu poll)
    bool InOrderSince(size_t mark, const std::vector<uint8_t>& seq) {
        auto v = CmdsSince(mark); size_t k = 0;
        for (uint8_t x : v) if (k < seq.size() && x == seq[k]) ++k;
        return k == seq.size();
    }
};

// "at so l" trong cau log "Da dat so luong in" (co dau) - chi cau bao hoan thanh san luong moi co
static const wchar_t* kDoneLog = L"\x1EA1t s\x1ED1 l";

int main() {
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = L"LinxManualTestWnd";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"LinxManualTestWnd", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { std::printf("khong tao duoc cua so test\n"); return 2; }

    LinxSim sim;
    FakePrinter fp;
    fp.handler = [&](uint8_t cmd, const Bytes& f) { return sim.Handle(cmd, f); };

    {
        AppController ctrl(hwnd);
        ctrl.SetLastIp(L"127.0.0.1");
        ctrl.StartWorkerThread();

        // ================= A. KET NOI =================
        std::printf("[A] Ket noi\n");
        // TC-CON-04: khong co may in (ket noi bi tu choi) -> thu lai co gioi han roi bao loi, khong treo
        {
            size_t ms = g.states.size(), mc = g.conn.size(), ml = g.logs.size();
            const long long t0 = FakePrinter::NowMs();
            ctrl.Connect(L"127.0.0.1");
            TC("TC-CON-04", PumpUntil([&] { return SawStatusSince(ms, PrinterStateType::Connecting); }, 3000));
            TC("TC-CON-04", PumpUntil([&] { return SawStatusSince(ms, PrinterStateType::Reconnecting); }, 8000));
            TC("TC-CON-04", PumpUntil([&] { return LastStatus(PrinterStateType::Error) && SawConnSince(mc, false); }, 60000));
            std::printf("  TC-CON-04: bao loi ket noi sau %lld ms\n", FakePrinter::NowMs() - t0);
            TC("TC-CON-04", !ctrl.IsConnected());
            TC("TC-CON-04", !SawConnSince(mc, true));
            TC("TC-CON-04", CountLogSince(ml, L"reconnect l") == 1);     // dung 1 lan thu tu dong (MAX_RECONNECT_ATTEMPTS = 1)
            size_t ml2 = g.logs.size();
            PumpFor(3000);                                                // het luot -> khong thu nua
            TC("TC-CON-04", CountLogSince(ml2, L"reconnect l") == 0);
        }

        if (!fp.Start(kRciDefaultPort)) {
            std::printf("KHONG CHAY DUOC: cong 127.0.0.1:%d dang bi chiem\n", (int)kRciDefaultPort);
            return 2;
        }

        // TC-CON-01: ket noi thanh cong toi cong 29043; UI: Connecting -> Connected; lenh dau tien la Status (14H)
        // TC-CON-02: UI phan anh trang thai may (jet Stopped -> Idle)
        {
            size_t ms = g.states.size(), mc = g.conn.size();
            ctrl.Connect(L"127.0.0.1");
            TC("TC-CON-01", PumpUntil([&] { return SawConnSince(mc, true); }, 6000));
            TC("TC-CON-01", ctrl.IsConnected());
            TC("TC-CON-01", SawStatusSince(ms, PrinterStateType::Connecting));
            TC("TC-CON-01", fp.accepts == 1);
            PumpUntil([&] { return sim.CmdCount() > 0; }, 2000);   // thông báo kết nối tới UI trước khi lệnh Status đầu tiên kịp được máy ghi nhận
            auto c = sim.CmdsSince(0);
            TC("TC-CON-01", !c.empty() && c[0] == 0x14);
            TC("TC-CON-02", PumpUntil([] { return LastStatus(PrinterStateType::Idle); }, 3000));
            TC("TC-CON-02", !g.states.back().jetOn && !g.states.back().printing);
        }

        // ================= C. TRINH TU TRANG THAI =================
        std::printf("[C] Trinh tu trang thai\n");
        // TC-SEQ-01: Start Jet (0FH) -> UI StartingJet trong luc jet Startup(1) -> Ready khi Running(0)
        {
            size_t ms = g.states.size(), mk = sim.CmdCount();
            ctrl.StartJet();
            TC("TC-SEQ-01", PumpUntil([&] { return sim.CountSince(mk, 0x0F) == 1; }, 2000));
            TC("TC-SEQ-01", PumpUntil([] { return LastStatus(PrinterStateType::Ready) && g.states.back().jetOn; }, 5000));
            int a = FirstStatusSince(ms, PrinterStateType::StartingJet), b = FirstStatusSince(ms, PrinterStateType::Ready);
            TC("TC-SEQ-01", a >= 0 && b > a);
            TC("TC-SEQ-01", sim.CountSince(mk, 0x0F) == 1);              // khong gui lap Start Jet
        }
        // TC-SEQ-03: Start Jet khi jet dang chay -> may NAK 19 'Jet not idle'; UI khong ket o StartingJet
        {
            size_t ml = g.logs.size(), mk = sim.CmdCount();
            ctrl.StartJet();
            TC("TC-SEQ-03", PumpUntil([&] { return HasLogSince(ml, L"Jet not idle"); }, 3000));
            TC("TC-SEQ-03", PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000));
            PumpFor(500);
            TC("TC-SEQ-03", LastStatus(PrinterStateType::Ready));
        }
        // TC-SEQ-06: 32-bit error mask: bit 3 Ink Low bat/tat -> log Warning ON / OFF, khong doi sang Error
        {
            size_t ml = g.logs.size(), ms = g.states.size();
            sim.With([](LinxSim& s) { s.errorMask = 0x08; return 0; });
            TC("TC-SEQ-06", PumpUntil([&] { return HasLogSince(ml, L"Warning ON") && HasLogSince(ml, L"Ink Low"); }, 3000));
            sim.With([](LinxSim& s) { s.errorMask = 0; return 0; });
            TC("TC-SEQ-06", PumpUntil([&] { return HasLogSince(ml, L"Warning OFF"); }, 3000));
            TC("TC-SEQ-06", !SawStatusSince(ms, PrinterStateType::Error));
        }
        // TC-CON-07: ngoai le khi nhan: reply sai checksum / NAK / khong tra loi (vai lan) -> giu nguyen trang thai, khong bao mat ket noi
        {
            size_t ms = g.states.size(), mc = g.conn.size();
            sim.With([](LinxSim& s) { s.statusBadChecksum = 2; return 0; });
            PumpFor(900);
            sim.With([](LinxSim& s) { s.statusNak = 2; return 0; });
            PumpFor(900);
            sim.With([](LinxSim& s) { s.statusSilent = 2; return 0; });
            PumpFor(1800);
            bool wrong = false;
            for (size_t i = ms; i < g.states.size(); ++i)
                if (!g.states[i].jetOn || g.states[i].status != PrinterStateType::Ready) wrong = true;
            TC("TC-CON-07", !wrong);
            TC("TC-CON-07", g.conn.size() == mc);
            TC("TC-CON-07", ctrl.IsConnected());
        }

        // ================= D. NOI DUNG / SO LUONG / SAN LUONG =================
        std::printf("[D] Noi dung, so luong, san luong\n");
        // TC-PRD-08: Remote field theo ten (158D): doc do dai bang 157D, dem cho du; qua dai / sai ten -> khong gui
        {
            size_t mk = sim.CmdCount(), ml = g.logs.size();
            ctrl.UploadRemoteFieldData(L"RemoteField1", L"AB");
            TC("TC-PRD-08", PumpUntil([&] { return sim.CountSince(mk, 0x9E) == 1; }, 3000));
            TC("TC-PRD-08", sim.InOrderSince(mk, { 0x9D, 0x9E }));
            TC("TC-PRD-08", sim.With([](LinxSim& s) { return s.lastField158 == "AB   "; }));
            PumpFor(300);
            TC("TC-PRD-08", !HasLogSince(ml, L"NAK"));
            mk = sim.CmdCount();
            ctrl.UploadRemoteFieldData(L"RemoteField1", L"ABCDEFG");     // 7 > 5
            ctrl.UploadRemoteFieldData(L"KhongCo", L"AB");               // field khong ton tai
            PumpFor(1200);
            TC("TC-PRD-08", sim.CountSince(mk, 0x9E) == 0);
            TC("TC-PRD-08", sim.With([](LinxSim& s) { return s.lastField158 == "AB   "; }));
        }
        // TC-PRD-02: chua SET so luong -> khong duoc gui Start Print
        {
            size_t mk = sim.CmdCount();
            ctrl.StartPrinting(L"HELLO", 3);
            PumpFor(1000);
            TC("TC-PRD-02", sim.CountSince(mk, 0x11) == 0);
            TC("TC-PRD-02", sim.With([](LinxSim& s) { return s.printState == 2; }));
        }
        // TC-PRD-01: SET so luong -> 140D (8CH): count 4 byte LSB truoc + ten 16 byte toan 0 (message hien hanh); UI: target = 3, printed = 0
        {
            size_t mk = sim.CmdCount();
            ctrl.SetCount(3);
            TC("TC-PRD-01", PumpUntil([&] { return sim.CountSince(mk, 0x8C) == 1; }, 3000));
            TC("TC-PRD-01", sim.With([](LinxSim& s) {
                bool zero = s.lastCountName.size() == 16;
                for (uint8_t b : s.lastCountName) if (b) zero = false;
                return zero; }));
            TC("TC-PRD-01", PumpUntil([] { return !g.states.empty() && g.states.back().targetCount == 3
                && g.states.back().printedCount == 0; }, 3000));
        }
        // TC-PRD-06: chua co message tren may -> 29D bi NAK 59 -> huy in, khong gui Start Print, UI khong sang Printing
        {
            size_t mk = sim.CmdCount(), ms = g.states.size(), ml = g.logs.size();
            sim.With([](LinxSim& s) { s.messageLoaded = false; return 0; });
            ctrl.StartPrinting(L"HELLO", 3);
            TC("TC-PRD-06", PumpUntil([&] { return HasLogSince(ml, L"No print message loaded"); }, 3000));
            PumpFor(600);
            TC("TC-PRD-06", sim.CountSince(mk, 0x11) == 0);
            TC("TC-PRD-06", !SawStatusSince(ms, PrinterStateType::Printing));
            sim.With([](LinxSim& s) { s.messageLoaded = true; return 0; });
        }
        // TC-PRD-07: message khong co remote field -> NAK 63 -> huy in
        {
            size_t mk = sim.CmdCount(), ml = g.logs.size();
            sim.With([](LinxSim& s) { s.hasRemoteFields = false; return 0; });
            ctrl.StartPrinting(L"HELLO", 3);
            TC("TC-PRD-07", PumpUntil([&] { return HasLogSince(ml, L"No remote fields in message"); }, 3000));
            PumpFor(600);
            TC("TC-PRD-07", sim.CountSince(mk, 0x11) == 0);
            sim.With([](LinxSim& s) { s.hasRemoteFields = true; return 0; });
        }
        // TC-PRD-09: may dang co loi (P-STATUS 3): Start Print duoc ACK nhung KHONG in -> UI khong duoc bao Printing
        {
            size_t ms = g.states.size(), ml = g.logs.size(), mk = sim.CmdCount();
            sim.With([](LinxSim& s) { s.pStatus = 3; return 0; });
            TC("TC-PRD-09", PumpUntil([] { return LastStatus(PrinterStateType::Error); }, 3000));
            ctrl.StartPrinting(L"HELLO", 3);
            PumpFor(1500);
            TC("TC-PRD-09", !SawStatusSince(ms, PrinterStateType::Printing));
            TC("TC-PRD-09", sim.CountSince(mk, 0x11) == 0 || HasLogSince(ml, L"Phase Failure"));
            sim.With([](LinxSim& s) { s.pStatus = 0; return 0; });
            TC("TC-PRD-09", PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000));
        }
        // TC-PRD-03: quy trinh in: Status -> Load Print Message (30D) -> Remote data (29D) -> Start Print (17D); UI sang Printing
        size_t doneMark = g.logs.size();
        {
            size_t mk = sim.CmdCount();
            ctrl.SetPrintMessageName("MSG1");
            ctrl.StartPrinting(L"HELLO", 3);
            TC("TC-PRD-03", PumpUntil([&] { return sim.CountSince(mk, 0x11) == 1; }, 4000));
            TC("TC-PRD-03", sim.InOrderSince(mk, { 0x14, 0x1E, 0x9D, 0x9E, 0x11 }));
            TC("TC-PRD-03", sim.CountSince(mk, 0x1D) == 0);              // [R47] khong dung 29D
            TC("TC-PRD-03", sim.CountSince(mk, 0x0F) == 0);              // jet dang chay -> khong gui Start Jet
            TC("TC-PRD-03", sim.With([](LinxSim& s) { return s.lastLoadName == "MSG1" && s.lastField158 == "HELLO" && s.lastField158Name == "RemoteField1"; }));
            TC("TC-PRD-03", PumpUntil([] { return LastStatus(PrinterStateType::Printing) && g.states.back().printing; }, 3000));
        }
        // TC-PRD-04: dang in: poll 141D; so da in tren UI = so ban in thuc te (0 -> 1 -> 2), chua bao hoan thanh
        {
            size_t mk = sim.CmdCount();
            PumpFor(1200);
            TC("TC-PRD-04", sim.CountSince(mk, 0x8D) >= 2);              // co poll Message Print Count khi dang in
            TC("TC-PRD-04", !HasLogSince(doneMark, kDoneLog));           // chua in ban nao -> chua duoc bao du san luong
            TC("TC-PRD-04", !g.states.empty() && g.states.back().printedCount == 0);
            for (int k = 1; k <= 2; ++k) {
                sim.Trigger(1);
                bool ok = PumpUntil([&] { return !g.states.empty() && g.states.back().printedCount == k; }, 2500);
                TC("TC-PRD-04", ok);
            }
            TC("TC-PRD-04", !HasLogSince(doneMark, kDoneLog));           // moi 2/3
            TC("TC-PRD-04", LastStatus(PrinterStateType::Printing));
        }
        // TC-PRD-05: du san luong (3/3): bao hoan thanh DUNG 1 lan, UI printed = target, host gui Stop Print (8900 khong tu dung theo count)
        {
            size_t mk = sim.CmdCount();
            sim.Trigger(1);
            TC("TC-PRD-05", PumpUntil([&] { return HasLogSince(doneMark, kDoneLog); }, 3000));
            TC("TC-PRD-05", PumpUntil([] { return !g.states.empty() && g.states.back().printedCount == 3
                && g.states.back().targetCount == 3; }, 3000));
            TC("TC-PRD-05", PumpUntil([&] { return sim.CountSince(mk, 0x12) >= 1; }, 3000));
            TC("TC-PRD-05", PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000));
            PumpFor(1000);
            TC("TC-PRD-05", CountLogSince(doneMark, kDoneLog) == 1);
            TC("TC-PRD-05", sim.With([](LinxSim& s) { return s.prints == 3; }));   // khong in thua
        }
        // ===== [R54] Gan du san luong: hoi print count day hon (100 ms), giam in thua o toc do cao =====
        {
            size_t mk = sim.CmdCount();
            ctrl.SetCount(60); PumpFor(400);                                 // sau khi du san luong phai SET lai
            mk = sim.CmdCount();
            ctrl.StartPrinting(L"FAST", 60);
            TC("TC-PRD-10", PumpUntil([&] { return LastStatus(PrinterStateType::Printing) && g.states.back().printing; }, 4000));
            // con xa muc tieu (in 10/40): chu ky ~300 ms -> trong 1 s chi ~3 lan hoi 8DH
            for (int i = 0; i < 10; ++i) { sim.Trigger(1); PumpFor(40); }
            PumpFor(300);
            size_t mf = sim.CmdCount();
            PumpFor(1000);
            const int farPolls = sim.CountSince(mf, 0x8D);
            TC("TC-PRD-10", farPolls >= 2 && farPolls <= 4);
            // gan muc tieu (con 3 ban): ~100 ms -> >= 7 lan hoi trong 1 s
            for (int i = 0; i < 47; ++i) { sim.Trigger(1); PumpFor(15); }
            PumpFor(350);                                                     // cho app nhan ra "gan du"
            size_t mn = sim.CmdCount();
            PumpFor(1000);
            const int nearPolls = sim.CountSince(mn, 0x8D);
            TC("TC-PRD-10", nearPolls >= 7);
            TC("TC-PRD-10", nearPolls > farPolls);
            // du san luong: tre tu ban in cuoi toi lenh Stop < 250 ms (truoc R54: toi da ~400 ms)
            const auto t0 = std::chrono::steady_clock::now();
            sim.Trigger(3);
            const bool stopped = PumpUntil([&] { return sim.CountSince(mn, 0x12) >= 1; }, 2000);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            TC("TC-PRD-10", stopped && ms < 250);
            std::printf("   [TC-PRD-10] far=%d near=%d polls/s, tre dung = %lld ms\n", farPolls, nearPolls, (long long)ms);
            TC("TC-PRD-10", sim.With([](LinxSim& s) { return s.prints == 60; }));     // khong in thua
            PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000);
        }
        // TC-SEQ-05: Stop Print (12H) -> print state Idle -> UI Ready (jet van chay)
        {
            size_t mk = sim.CmdCount();
            sim.With([](LinxSim& s) { s.printState = 4; return 0; });
            TC("TC-SEQ-05", PumpUntil([] { return LastStatus(PrinterStateType::Printing); }, 3000));
            ctrl.StopPrinting();
            TC("TC-SEQ-05", PumpUntil([&] { return sim.CountSince(mk, 0x12) >= 1; }, 2000));
            TC("TC-SEQ-05", PumpUntil([] { return LastStatus(PrinterStateType::Ready) && g.states.back().jetOn
                && !g.states.back().printing; }, 3000));
        }
        // TC-NEW-01 (R34): het muc (P-STATUS 7) / het dung moi (10) / loi chua xoa / warning nghiem trong -> KHONG gui Start Print
        {
            ctrl.SetCount(3);
            PumpFor(500);
            struct Case { uint8_t p; uint32_t mask; const wchar_t* log; } cases[] = {
                { 7, 0, L"Ink Tank Empty" }, { 10, 0, L"Solvent Tank Empty" }, { 0, 1u << 7, L"Printhead Cover Off" } };
            for (auto& c : cases) {
                size_t mk = sim.CmdCount(), ml = g.logs.size();
                sim.With([&](LinxSim& s) { s.pStatus = c.p; s.errorMask = c.mask; return 0; });
                PumpFor(700);
                ctrl.StartPrinting(L"HELLO", 3);
                TC("TC-NEW-01", PumpUntil([&] { return HasLogSince(ml, c.log); }, 3000));
                PumpFor(400);
                TC("TC-NEW-01", sim.CountSince(mk, 0x11) == 0);
            }
            sim.With([](LinxSim& s) { s.pStatus = 0; s.errorMask = 0; return 0; });
        }
        // TC-NEW-02 (R35): canh bao cua may (Ink Low, bit 3) hien trong state de UI hien thi; het canh bao thi xoa
        {
            sim.With([](LinxSim& s) { s.errorMask = 1u << 3; return 0; });
            TC("TC-NEW-02", PumpUntil([] { return !g.states.empty() && !g.states.back().warnings.empty(); }, 3000));
            sim.With([](LinxSim& s) { s.errorMask = 0; return 0; });
            TC("TC-NEW-02", PumpUntil([] { return !g.states.empty() && g.states.back().warnings.empty(); }, 3000));
        }
        // TC-NEW-03 (R36): Tai ve tu may: kich thuoc field, ten field, ten message
        {
            ctrl.DownloadFromPrinter();
            TC("TC-NEW-03", PumpUntil([] { return !g.states.empty() && g.states.back().remoteFieldMax == 5
                && g.states.back().remoteFieldName == L"RemoteField1" && g.states.back().messageName == L"MSG1"; }, 3000));
        }
        // TC-NEW-04 (R37): in bu/tru: target 3 -> +5 = 8; -20 bi tu choi (target khong doi)
        {
            ctrl.AdjustCount(5);
            TC("TC-NEW-04", PumpUntil([] { return !g.states.empty() && g.states.back().targetCount == 8; }, 3000));
            size_t ml = g.logs.size();
            ctrl.AdjustCount(-20);
            PumpFor(600);
            TC("TC-NEW-04", !g.states.empty() && g.states.back().targetCount == 8);
            ctrl.AdjustCount(-5);
            TC("TC-NEW-04", PumpUntil([] { return !g.states.empty() && g.states.back().targetCount == 3; }, 3000));
            (void)ml;
        }
        // ===== [R42] Serial may in (152D) + cong giay phep =====
        {
            // serial duoc doc sau khi ket noi
            TC("TC-LIC-01", !g.states.empty() && g.states.back().printerSerial == L"SN123456");
            // het han: chan cac thao tac DIEU KHIEN in, van cho dung in / dung jet / ket noi / tai thong tin
            std::atomic<bool> allowed{ false };
            ctrl.SetLicenseGate([&] { return allowed.load(); });
            size_t mk = sim.CmdCount(), ml = g.logs.size();
            ctrl.SetCount(9);
            ctrl.StartPrinting(L"HELLO", 9);
            ctrl.StartJet();
            ctrl.AdjustCount(+5);
            ctrl.UploadRemoteFieldData(L"RemoteField1", L"ABCDE");
            PumpFor(800);
            TC("TC-LIC-02", sim.CountSince(mk, 0x8C) == 0 && sim.CountSince(mk, 0x11) == 0 && sim.CountSince(mk, 0x0F) == 0);
            TC("TC-LIC-02", sim.CountSince(mk, 0x9E) == 0 && sim.CountSince(mk, 0x1D) == 0);
            TC("TC-LIC-02", CountLogSince(ml, L"gia h") >= 5);          // moi thao tac bi chan deu bao ly do
            size_t mk2 = sim.CmdCount();
            ctrl.StopPrinting();
            ctrl.DownloadFromPrinter();
            TC("TC-LIC-03", PumpUntil([&] { return sim.CountSince(mk2, 0x12) == 1 && sim.CountSince(mk2, 0x9D) >= 1; }, 3000));
            // gia han (cong mo lai) -> thao tac chay binh thuong
            allowed = true;
            size_t mk3 = sim.CmdCount();
            ctrl.SetCount(3);
            TC("TC-LIC-04", PumpUntil([&] { return sim.CountSince(mk3, 0x8C) == 1; }, 3000));
            ctrl.SetLicenseGate(nullptr);   // cong dang bat bien cuc bo `allowed`: go bo truoc khi het pham vi
        }
        // ===== [R44] Danh sach ban tin tren may (97D) + chon ban tin (30D) =====
        {
            TC("TC-MSG-01", PumpUntil([] { return !g.states.empty() && g.states.back().messages.size() == 2; }, 3000));
            TC("TC-MSG-01", g.states.back().messages[0] == L"MSG1" && g.states.back().messages[1] == L"MSG2");
            TC("TC-MSG-01", g.states.back().selectedMessage == L"MSG1");           // mac dinh = ban tin dang tai tren may

            // chon MSG2: 30D, doc lai kich thuoc field (157D) + ten ban tin hien hanh (141D)
            size_t mk = sim.CmdCount();
            ctrl.SelectMessage(L"MSG2");
            TC("TC-MSG-02", PumpUntil([&] { return sim.CountSince(mk, 0x1E) == 1; }, 3000));
            TC("TC-MSG-02", PumpUntil([] { return !g.states.empty() && g.states.back().selectedMessage == L"MSG2"
                && g.states.back().messageName == L"MSG2"; }, 3000));
            TC("TC-MSG-02", sim.InOrderSince(mk, { 0x14, 0x1E, 0x9D, 0x8D }));
            TC("TC-MSG-02", sim.With([](LinxSim& s) { return s.lastLoadName == "MSG2"; }));
            ctrl.SelectMessage(L"MSG1");
            TC("TC-MSG-02", PumpUntil([] { return !g.states.empty() && g.states.back().selectedMessage == L"MSG1"; }, 3000));

            // dang in: khong doc lai danh sach, khong doi ban tin, o chon giu nguyen
            sim.With([](LinxSim& s) { s.printState = 4; return 0; });
            TC("TC-MSG-03", PumpUntil([] { return LastStatus(PrinterStateType::Printing); }, 3000));
            size_t mk2 = sim.CmdCount();
            ctrl.RefreshMessageList();
            ctrl.SelectMessage(L"MSG2");
            PumpFor(1200);
            TC("TC-MSG-03", sim.CountSince(mk2, 0x61) == 0 && sim.CountSince(mk2, 0x1E) == 0);
            TC("TC-MSG-03", g.states.back().selectedMessage == L"MSG1" && g.states.back().messages.size() == 2);
            sim.With([](LinxSim& s) { s.printState = 2; return 0; });
            TC("TC-MSG-03", PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000));

            // ban tin bi xoa tren may sau khi da luu danh sach: NAK 36 -> tu lam moi danh sach
            sim.With([](LinxSim& s) { s.msgList = { "MSG1" }; return 0; });
            size_t mk3 = sim.CmdCount(), ml3 = g.logs.size();
            ctrl.SelectMessage(L"MSG2");
            TC("TC-MSG-04", PumpUntil([&] { return sim.CountSince(mk3, 0x61) == 1; }, 3000));
            TC("TC-MSG-04", PumpUntil([] { return !g.states.empty() && g.states.back().messages.size() == 1; }, 3000));
            TC("TC-MSG-04", g.states.back().selectedMessage == L"MSG1");
            TC("TC-MSG-04", HasLogSince(ml3, L"30D"));
            sim.With([](LinxSim& s) { s.msgList = { "MSG1", "MSG2" }; return 0; });
            ctrl.RefreshMessageList();
            TC("TC-MSG-04", PumpUntil([] { return !g.states.empty() && g.states.back().messages.size() == 2; }, 3000));

            // het han giay phep: khong chon duoc ban tin (la thao tac dieu khien), nhung van lam moi danh sach
            size_t mk4 = sim.CmdCount();
            ctrl.SetLicenseGate([] { return false; });
            ctrl.SelectMessage(L"MSG2");
            ctrl.RefreshMessageList();
            PumpFor(800);
            TC("TC-MSG-05", sim.CountSince(mk4, 0x1E) == 0 && sim.CountSince(mk4, 0x61) == 1);
            ctrl.SetLicenseGate(nullptr);
        }
        // ===== [R47] 4 ban tin mau, 4 remote field co do dai khac nhau (10, 5, 8, 3); MSGk chi co 1 field RemoteFieldk =====
        {
            sim.With([](LinxSim& s) {
                s.fieldDefs = { { "RemoteField1", 10 }, { "RemoteField2", 5 }, { "RemoteField3", 8 }, { "RemoteField4", 3 } };
                s.msgList = { "MSG1", "MSG2", "MSG3", "MSG4" };
                s.msgField = { { "MSG1", 0 }, { "MSG2", 1 }, { "MSG3", 2 }, { "MSG4", 3 } };
                return 0; });
            ctrl.RefreshMessageList();
            TC("TC-FLD-01", PumpUntil([] { return !g.states.empty() && g.states.back().messages.size() == 4; }, 3000));
            TC("TC-FLD-01", g.states.back().messages[0] == L"MSG1" && g.states.back().messages[3] == L"MSG4");

            const wchar_t* names[] = { L"MSG1", L"MSG2", L"MSG3", L"MSG4" };
            for (int k = 0; k < 4; ++k) {
                const std::wstring n = names[k];
                size_t mk = sim.CmdCount();
                ctrl.SelectMessage(n);
                // chon ban tin -> doc lai 157D: ten + kich thuoc field hien thi
                static const int kSizes[4] = { 10, 5, 8, 3 };
                const int want = kSizes[k];
                TC("TC-FLD-02", PumpUntil([&] { return !g.states.empty() && g.states.back().messageName == n
                    && g.states.back().remoteFieldMax == want; }, 3000));
                TC("TC-FLD-02", g.states.back().remoteFieldName == L"RemoteField" + std::to_wstring(k + 1));
                TC("TC-FLD-02", sim.InOrderSince(mk, { 0x1E, 0x9D, 0x8D }));

                // in tren ban tin nay: du lieu gui bang 158D theo ten field cua ban tin, dem dung kich thuoc, khong NAK
                size_t mp = sim.CmdCount(), ml = g.logs.size();
                ctrl.StartPrinting(L"ABC", 1);
                TC("TC-FLD-03", PumpUntil([&] { return sim.CountSince(mp, 0x11) == 1; }, 4000));
                TC("TC-FLD-03", sim.CountSince(mp, 0x1D) == 0 && sim.CountSince(mp, 0x9E) == 1);
                TC("TC-FLD-03", sim.With([&](LinxSim& s) { return s.lastField158 == std::string("ABC").append(want - 3, ' ')
                    && s.lastField158Name == "RemoteField" + std::to_string(k + 1); }));
                TC("TC-FLD-03", !HasLogSince(ml, L"NAK"));
                ctrl.StopPrinting();
                sim.With([](LinxSim& s) { s.printState = 2; return 0; });
                PumpUntil([] { return LastStatus(PrinterStateType::Ready); }, 3000);
            }
            TC("TC-FLD-04", sim.With([](LinxSim& s) { return s.TotalLen("MSG1") == 10 && s.TotalLen("MSG2") == 5
                && s.TotalLen("MSG3") == 8 && s.TotalLen("MSG4") == 3; }));

            // tra lai cau hinh 1 field / 2 ban tin cho cac test sau
            sim.With([](LinxSim& s) {
                s.fieldDefs = { { "RemoteField1", 5 } };
                s.msgList = { "MSG1", "MSG2" }; s.msgField.clear();
                return 0; });
            ctrl.SelectMessage(L"MSG1");
            ctrl.RefreshMessageList();
            PumpUntil([] { return !g.states.empty() && g.states.back().messages.size() == 2 && g.states.back().remoteFieldMax == 5; }, 3000);
        }
        // TC-SEQ-02: Stop Jet (10H) -> UI StopingJet trong luc Shutdown(2) -> Idle khi Stopped(3)
        {
            size_t ms = g.states.size(), mk = sim.CmdCount();
            ctrl.StopJet();
            TC("TC-SEQ-02", PumpUntil([&] { return sim.CountSince(mk, 0x10) == 1; }, 2000));
            TC("TC-SEQ-02", PumpUntil([] { return LastStatus(PrinterStateType::Idle) && !g.states.back().jetOn; }, 5000));
            int a = FirstStatusSince(ms, PrinterStateType::StopingJet), b = FirstStatusSince(ms, PrinterStateType::Idle);
            TC("TC-SEQ-02", a >= 0 && b > a);
        }
        // TC-SEQ-04: jet Fault khi khoi dong (jet state 4 + P-STATUS 3) -> UI Error co thong bao, khong ket o StartingJet
        {
            sim.With([](LinxSim& s) { s.faultOnStart = true; return 0; });
            ctrl.StartJet();
            TC("TC-SEQ-04", PumpUntil([] { return LastStatus(PrinterStateType::Error); }, 4000));
            TC("TC-SEQ-04", !g.states.back().errorMessage.empty());
            TC("TC-SEQ-04", !g.states.back().jetTransitioning);
            sim.With([](LinxSim& s) { s.faultOnStart = false; s.pStatus = 0; s.jetState = 3; return 0; });
            TC("TC-SEQ-04", PumpUntil([] { return LastStatus(PrinterStateType::Idle); }, 3000));
        }

        // ================= A (tiep). MAT KET NOI / NGAT =================
        std::printf("[A] Mat ket noi, ngat ket noi\n");
        // TC-CON-05: may in dong ket noi -> UI biet trong < 2 s (Reconnecting), tu ket noi lai va doc lai trang thai
        {
            size_t mc = g.conn.size(), ms = g.states.size();
            const long long t0 = FakePrinter::NowMs();
            fp.dropClient = true;
            TC("TC-CON-05", PumpUntil([&] { return SawConnSince(mc, false); }, 4000));
            TC("TC-CON-05", FakePrinter::NowMs() - t0 < 2000);
            TC("TC-CON-05", PumpUntil([&] { return SawStatusSince(ms, PrinterStateType::Reconnecting); }, 2000));
            TC("TC-CON-05", PumpUntil([] { return LastConn(true); }, 8000));
            TC("TC-CON-05", PumpUntil([] { return LastStatus(PrinterStateType::Idle); }, 3000));
        }
        // TC-SET-01 (R43): cai dat "tu ket noi lai" = tat -> mat ket noi thi KHONG tu noi lai; bat lai thi Connect binh thuong
        {
            size_t mc = g.conn.size();
            int acc = fp.accepts;
            ctrl.SetAutoReconnectAllowed(false);
            fp.dropClient = true;
            TC("TC-SET-01", PumpUntil([&] { return SawConnSince(mc, false); }, 4000));
            PumpFor(4500);                                               // du lau de lan thu noi lai dau tien (2 s) xay ra neu dang bat
            TC("TC-SET-01", fp.accepts == acc && !ctrl.IsConnected());
            TC("TC-SET-01", LastStatus(PrinterStateType::Disconnected));
            ctrl.SetAutoReconnectAllowed(true);
            ctrl.Connect(L"127.0.0.1");
            TC("TC-SET-01", PumpUntil([] { return LastConn(true); }, 6000));
            TC("TC-SET-01", PumpUntil([] { return LastStatus(PrinterStateType::Idle); }, 3000));
        }
        // TC-CON-03: nguoi dung ngat ket noi -> UI Disconnected; KHONG tu ket noi lai; khong gui them lenh nao
        {
            size_t mc = g.conn.size();
            int acc = fp.accepts;
            ctrl.Disconnect();
            TC("TC-CON-03", PumpUntil([&] { return SawConnSince(mc, false); }, 3000));
            TC("TC-CON-03", PumpUntil([] { return LastStatus(PrinterStateType::Disconnected); }, 2000));
            TC("TC-CON-03", !ctrl.IsConnected());
            PumpFor(500);
            size_t mk = sim.CmdCount();
            PumpFor(3000);
            TC("TC-CON-03", fp.accepts == acc);
            TC("TC-CON-03", sim.CmdCount() == mk);
            TC("TC-CON-03", !SawConnSince(mc, true));
        }

        // ================= B. KHUNG TRUYEN / REQUEST =================
        std::printf("[B] Khung truyen\n");
        // TC-FRM-01: MOI khung host gui trong ca phien deu dung dang ESC STX ... ESC ETX + checksum dung
        TC("TC-FRM-01", sim.With([](LinxSim& s) { return s.badFrames == 0 && s.cmds.size() > 50; }));
        // TC-FRM-02: host khong gui lenh moi khi chua nhan reply cua lenh truoc
        TC("TC-FRM-02", fp.pipelined == 0);
        // TC-FRM-03: chi dung cac ma lenh co trong Table 2-1 ma ung dung ho tro
        TC("TC-FRM-03", sim.With([](LinxSim& s) {
            for (uint8_t c : s.cmds) {
                switch (c) { case 0x0F: case 0x10: case 0x11: case 0x12: case 0x14: case 0x1D: case 0x1E:
                             case 0x8C: case 0x8D: case 0x9D: case 0x9E: case 0x98: case 0x61: break; default: return false; }
            }
            return true; }));

        ctrl.StopWorkerThread(0);
        Pump();
    }
    Pump();
    fp.Stop();
    DestroyWindow(hwnd);

    int failed = 0, total = 0;
    std::printf("\n%-12s %s\n", "TEST CASE", "KET QUA");
    for (auto& id : g_order) {
        bool ok = g_failBy[id] == 0;
        std::printf("%-12s %s (%d/%d)\n", id.c_str(), ok ? "PASS" : "FAIL", g_runBy[id] - g_failBy[id], g_runBy[id]);
        ++total; if (!ok) ++failed;
    }
    std::printf("\n%d test case, %d FAIL\n", total, failed);
    return failed ? 1 : 0;
}
