#include "RciClient.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>

#include "RciWarnings.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

// ============================================================
// Helpers (file-scope)
// ============================================================

// Append string dạng null-terminated với giới hạn byte (không tính null).
static void AppendNullTerminated(std::vector<uint8_t>& out, const std::string& s, size_t maxBytesNoNull) {
    size_t n = min(s.size(), maxBytesNoNull);
    out.insert(out.end(), s.begin(), s.begin() + n);
    out.push_back(0x00);
}

// Build buffer 16 bytes message name (null-terminated), nếu rỗng => toàn 0 (current message).
static std::vector<uint8_t> BuildMsgName16(const std::string& nameOpt) {
    std::vector<uint8_t> buf(16, 0x00);
    if (!nameOpt.empty()) {
        size_t n = std::min<size_t>(15, nameOpt.size());
        memcpy(buf.data(), nameOpt.data(), n);
        buf[n] = 0x00;
    }
    return buf;
}

// Hex dump giới hạn để log debug.
static std::wstring HexDump(const std::vector<uint8_t>& v, size_t max = 128) {
    std::wstringstream ws;
    ws << std::hex << std::setfill(L'0');
    size_t n = min(v.size(), max);
    for (size_t i = 0; i < n; ++i) ws << std::setw(2) << (int)v[i] << L' ';
    if (v.size() > n) ws << L"...";
    return ws.str();
}

// Unescape body: chỉ unescape các byte hợp lệ theo rule (b < 0x20 hoặc ESC).
static void UnescapeBodyBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == 0x1B) {
            if (i + 1 >= in.size()) break;
            uint8_t b = in[i + 1];

            if (b < 0x20 || b == 0x1B) {
                out.push_back(b);
                ++i;
                continue;
            }

            // ESC + byte không hợp lệ theo rule: coi ESC là data thô
            out.push_back(0x1B);
            continue;
        }
        out.push_back(in[i]);
    }
}

// Read little-endian uint32.
static inline uint32_t ReadU32_LSB_First(const uint8_t* p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

// Trích cmdid từ frame TX để phục vụ log/throttle (best-effort).
static bool TryGetCmdIdFromFrame(const std::vector<uint8_t>& frame, uint8_t& outCmd) {
    if (frame.size() < 3) return false;
    if (frame[0] != 0x1B) return false;
    if (frame[1] != 0x01 && frame[1] != 0x02) return false; // SOH/STX
    if (frame[2] == 0x1B) { // double ESC -> cmdid = 0x1B
        outCmd = 0x1B;
        return true;
    }
    outCmd = frame[2];
    return true;
}

// Byte -> "1A" (log).
static std::wstring Hex2Local(uint8_t v) {
    std::wstringstream ws;
    ws << std::hex << std::uppercase << std::setfill(L'0') << std::setw(2) << (int)v;
    return ws.str();
}

// Convert WSA error code to readable string (for logs).
static std::wstring WsaErrorToString(int err) {
    wchar_t* msg = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&msg, 0, NULL);

    std::wstring out;
    if (msg) {
        out = msg;
        LocalFree(msg);
    }
    else {
        std::wstringstream ss; ss << L"WSA error " << err;
        out = ss.str();
    }
    return out;
}

// [2026-11-13 R06] Xem khai báo trong RciClient.h.
size_t RciFindEscEtx(const std::vector<uint8_t>& buf, size_t from) {
    for (size_t i = from; i + 1 < buf.size(); ) {
        if (buf[i] == 0x1B) {
            if (buf[i + 1] == 0x03) return i;  // ESC ETX thật sự
            i += 2;                             // ESC + byte khác (ESC ESC, ESC ACK...) là một cặp
        }
        else {
            ++i;
        }
    }
    return std::string::npos;
}

// Parse + verify checksum theo rule: checksum over (ACK/NAK) + body(unescaped) + ETX(0x03).
ParsedReply ParseReplyAndVerify(const std::vector<uint8_t>& reply,uint8_t(*ComputeChecksumFn)(const std::vector<uint8_t>&))
{
	ParsedReply pr; //khởi tạo mặc định là không hợp lệ (ok=false). Nếu parse + checksum thành công sẽ set ok=true và điền các trường khác.
    
    //kiểm tra định dạng cơ bản 
    if (reply.size() < 6) return pr;
    if (reply[0] != 0x1B) return pr;
    if (reply[1] != 0x06 && reply[1] != 0x15) return pr;

    // tìm vị trí ESC ETX
    size_t esc_etx = RciFindEscEtx(reply);   // [2026-11-13 R06] không còn quét byte thô
    if (esc_etx == std::string::npos) return pr;

	// lấy checksum byte sau ESC ETX (có thể có escape)
    size_t after = esc_etx + 2;
    if (after >= reply.size()) return pr;
	uint8_t recvChk = 0; // byte checksum thực tế nhận được (có thể bị escape)
	if (reply[after] == 0x1B) { // byte checksum bị escape, phải đọc thêm 1 byte nữa
		if (after + 1 >= reply.size()) return pr;   //  nếu thiếu byte sau escape thì frame không hợp lệ
		recvChk = reply[after + 1]; // byte checksum thực tế là byte sau escape
    }
    else {
		recvChk = reply[after]; // byte checksum không bị escape, đọc trực tiếp
    }

	// tách body đã escape (phần giữa ACK/NAK và ESC ETX) 
    std::vector<uint8_t> escBody(reply.begin() + 2, reply.begin() + esc_etx);
    std::vector<uint8_t> body;
    UnescapeBodyBytes(escBody, body);
	if (body.size() < 3) return pr; //kiểm tra body tối thiểu phải có 3 byte (p_status, c_status, cmdid)

	// chuẩn bị vùng tính checksum: [ACK/NAK][body...][ETX]
	std::vector<uint8_t> csArea;    // vùng dữ liệu để tính checksum theo rule của manual
	csArea.push_back(reply[1]);     // ACK hoặc NAK
	csArea.insert(csArea.end(), body.begin(), body.end());  // phần body đã unescape (p_status, c_status, cmdid, data...)
	csArea.push_back(0x03);     // byte ETX

	// tính checksum và so sánh với byte checksum nhận được
	uint8_t expected = ComputeChecksumFn(csArea);   // tính checksum theo rule của manual
	if (expected != recvChk) return pr; // nếu checksum không khớp thì frame không hợp lệ

	//so sánh checksum thành công, parse các trường và đánh dấu ok
    pr.ok = true;
    pr.ack = (reply[1] == 0x06); //ack true nếu byte thứ 2 là ACK
	pr.p_status = body[0];      // byte đầu tiên của body là p_status
	pr.c_status = body[1];      // byte thứ hai của body là c_status
	pr.cmdid = body[2];         // byte thứ ba của body là cmdid (giúp xác định lệnh nào đang trả về)
	pr.data.assign(body.begin() + 3, body.end());   // phần còn lại của body là data (nếu có)
    return pr;
}

// ============================================================
// RciClient: Lifecycle
// ============================================================

/* Function: RciClient()
 * Mục đích: Khởi tạo WinSock API (WSAStartup) và init state mặc định.
 */
RciClient::RciClient() : sock_(INVALID_SOCKET), connected_(false), port_(0) {
	WSADATA wsa;    // khởi tạo WinSock 2.2
	int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);   // nếu WSAStartup trả về khác 0 thì có lỗi, log ra để debug
    if (wsaResult != 0) {   //kiểm tra lỗi 
        std::wcerr << L"WSAStartup failed with error: " << wsaResult << std::endl;
    }
}

/* Function: ~RciClient()
 * Mục đích: Đảm bảo disconnect socket và cleanup WinSock (WSACleanup).
 */
RciClient::~RciClient() {
    Disconnect();
    WSACleanup();
}

// ============================================================
// RciClient: Connection management
// ============================================================

/* Function: MarkDisconnected_NoThrow()
 * Mục đích: Đánh dấu disconnect ngay lập tức, đóng socket, clear rxPending_.
 * Dùng khi send/recv gặp lỗi để tránh throw và tránh dùng tiếp socket hỏng.
 */
void RciClient::MarkDisconnected_NoThrow() {
	SOCKET s = INVALID_SOCKET; //tạo biến tạm socket để lưu socket hiện tại trước khi đóng, tránh giữ lock quá lâu trong lúc đóng socket
    {
		std::lock_guard<std::mutex> lock(mtx_);     // lock để đồng bộ trạng thái socket và connected flag
		connected_.store(false, std::memory_order_release); // đánh dấu đã disconnect (store false với memory_order_release để đảm bảo các thread khác thấy được cập nhật trước khi socket bị đóng)
		s = sock_;  // lưu socket hiện tại vào biến tạm 
		sock_ = INVALID_SOCKET; // đặt sock_ về INVALID_SOCKET để các thread khác biết socket không còn socket hợp lệ
		rxPending_.clear(); // clear buffer pending để tránh các thread khác xử lý dữ liệu cũ sau khi đã disconnect
    }
	if (s != INVALID_SOCKET) {  // nếu có socket hợp lệ trước khi đóng thì mới thực hiện shutdown + close
        ::shutdown(s, SD_BOTH); 
        ::closesocket(s);
    }
}

/* Function: Connect(ip, port, timeoutMs)
 * Mục đích: Tạo kết nối TCP tới printer (non-blocking connect + select timeout),
 *          sau đó publish sock_ và set socket options (timeouts, nodelay, keepalive).
 */
bool RciClient::Connect(const std::wstring& ip, unsigned short port, int timeoutMs) {
    // 1) Close old socket (outside mutex)
    SOCKET oldSock = INVALID_SOCKET;    // dùng biến tạm lưu socket cũ
    {
		std::lock_guard<std::mutex> lock(mtx_); // lock để đồng bộ truy cập sock_ và connected_
		if (connected_ || sock_ != INVALID_SOCKET) { // nếu đang connected hoặc có socket cũ thì mới cần đóng
            connected_.store(false, std::memory_order_release); 
            oldSock = sock_;    
            sock_ = INVALID_SOCKET;
        }
    }
	if (oldSock != INVALID_SOCKET) { //đóng socket cũ nếu có, thực hiện shutdown + close ngoài mutex để tránh giữ lock quá lâu
        ::shutdown(oldSock, SD_BOTH);
        ::closesocket(oldSock);
    }

    abortConnect_.store(false);   // [2026-11-16 R30]

    // 2) Create new socket
    Log(L"🔌 [Connect] Bắt đầu kết nối đến " + ip + L":" + std::to_wstring(port), 1);
	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);   // tạo socket TCP IPv4
	if (s == INVALID_SOCKET) {  // nếu tạo socket thất bại, log lỗi và return false
        Log(L"❌ Không thể tạo socket", 2);
        return false;
    }

	sockaddr_in addr{}; // khởi tạo cấu trúc địa chỉ cho kết nối TCP
	addr.sin_family = AF_INET;  // sử dụng IPv4
	addr.sin_port = htons(port);//  chuyển đổi port sang network byte order (big-endian)

	char ipUtf8[64] = {};   // buffer tạm để chứa địa chỉ IP sau khi convert sang UTF-8
	WideCharToMultiByte(CP_ACP, 0, ip.c_str(), -1, ipUtf8, sizeof(ipUtf8), NULL, NULL); // convert IP từ wide string sang UTF-8
	if (inet_pton(AF_INET, ipUtf8, &addr.sin_addr) <= 0) {  // nếu địa chỉ IP không hợp lệ, log lỗi, đóng socket và return false
        Log(L"❌ Địa chỉ IP không hợp lệ", 2);
        closesocket(s);
        return false;
    }

    // 3) Set non-blocking for connect + select
	u_long mode = 1;    // đặt socket ở chế độ non-blocking để connect không block thread hiện tại
	ioctlsocket(s, FIONBIO, &mode); // nếu ioctlsocket trả về lỗi, log lỗi, đóng socket và return false

	int res = connect(s, (sockaddr*)&addr, sizeof(addr)); // thực hiện kết nối TCP đến địa chỉ đã chuẩn bị, kết quả có thể là 0 hoặc SOCKET_ERROR với lỗi WSAEWOULDBLOCK (đang tiến hành kết nối)
	if (res == SOCKET_ERROR) {  // nếu connect trả về lỗi, kiểm tra lỗi có phải là WSAEWOULDBLOCK (đang tiến hành kết nối) hoặc WSAEINPROGRESS/WSAEALREADY (đang tiến hành kết nối) hay không, nếu không phải thì log lỗi, đóng socket và return false
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
            std::wstringstream ss;
            ss << L"❌ connect() thất bại, WSAError=" << err << L" (" << WsaErrorToString(err) << L")";
            Log(ss.str(), 2);
            closesocket(s);
            return false;
        }
    }

    // 4) Wait until writable (connected)
    // [2026-11-16 R30] Chờ theo từng lát 100 ms thay vì một lần select() dài:
    //  - theo dõi cả exceptfds: trên Windows, kết nối bị TỪ CHỐI được báo qua exceptfds (trước đây phải đợi hết timeout);
    //  - kiểm tra abortConnect_ để Disconnect()/dừng chương trình huỷ được lần connect đang chờ.
    bool writable = false, failed = false;
    for (int waited = 0; waited < timeoutMs; waited += 100) {
        if (abortConnect_.load()) {
            Log(L"Huỷ kết nối đang chờ", 1);
            closesocket(s);
            return false;
        }
        fd_set wset, eset;
        FD_ZERO(&wset); FD_SET(s, &wset);
        FD_ZERO(&eset); FD_SET(s, &eset);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        res = select(0, NULL, &wset, &eset, &tv);
        if (res == SOCKET_ERROR) { failed = true; break; }
        if (res > 0) {
            if (FD_ISSET(s, &eset)) { failed = true; break; }
            if (FD_ISSET(s, &wset)) { writable = true; break; }
        }
    }
    if (!writable) {
        Log(failed ? L"❌ Kết nối bị từ chối/không tới được máy in" : L"⏰ Timeout kết nối", 2);
        closesocket(s);
        return false;
    }

    // 5) Check SO_ERROR
	int so_err = 0; // kiểm tra lỗi kết nối thực sự bằng cách lấy giá trị SO_ERROR của socket, nếu có lỗi thì log lỗi, đóng socket và return false
	int optlen = sizeof(so_err);    // chuẩn bị biến và độ dài cho getsockopt
	if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so_err, &optlen) == 0) {    // nếu getsockopt thành công, kiểm tra giá trị lỗi trong so_err, nếu khác 0 thì có lỗi kết nối thực sự, log lỗi, đóng socket và return false
		if (so_err != 0) {  // có lỗi kết nối thực sự, log lỗi chi tiết và đóng socket
            std::wstringstream ss;
            ss << L"❌ Kết nối thất bại (SO_ERROR=" << so_err << L")";
            Log(ss.str(), 2);
            closesocket(s);
            return false;
        }
    }

    // 6) Back to blocking
	mode = 0;   // đặt lại socket về blocking mode để sử dụng cho send/recv bình thường sau khi đã kết nối thành công
	ioctlsocket(s, FIONBIO, &mode); // nếu ioctlsocket trả về lỗi, log lỗi, đóng socket và return false

    // 6.1) socket timeouts
	DWORD sndTo = 2000; // đặt timeout cho send và recv qua setsockopt với SO_SNDTIMEO và SO_RCVTIMEO, giá trị timeout là 2000 milliseconds (2 giây) để tránh block quá lâu trong send/recv nếu có vấn đề về mạng hoặc printer không phản hồi
    DWORD rcvTo = 2000; 
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndTo, sizeof(sndTo));   
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTo, sizeof(rcvTo));

    // 6.2) TCP_NODELAY
	BOOL nodelay = TRUE;    // đặt TCP_NODELAY để tắt Nagle's algorithm, giúp giảm latency cho các lệnh nhỏ thường dùng trong RCI, tránh bị delay do chờ gom gói
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));  // nếu setsockopt trả về lỗi, log lỗi, đóng socket và return false

    // 6.3) KEEPALIVE (optional)
	BOOL ka = TRUE; // bật TCP keepalive để phát hiện nhanh khi kết nối bị rớt 
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ka, sizeof(ka)); 

    // publish
    {
		std::lock_guard<std::mutex> lock(mtx_); // lock để đồng bộ cập nhật trạng thái kết nối và thông tin socket
		sock_ = s;  // cập nhật sock_ với socket mới đã kết nối thành công
		host_ = ip; // lưu lại thông tin host và port 
		port_ = port; // lưu port đang kết nối (mặc định kRciDefaultPort)
		connected_.store(true, std::memory_order_release);  // đánh dấu đã connected 
		rxPending_.clear(); // clear buffer pending khi mới kết nối
    }

    return true;
}

/* Function: Disconnect()
 * Mục đích: Đóng socket hiện tại, clear rxPending_, chuyển connected_ = false.
 */
bool RciClient::Disconnect() {
    abortConnect_.store(true);   // [2026-11-16 R30] huỷ Connect() đang chờ (nếu có)
	SOCKET localSock = INVALID_SOCKET; //tạo biến tạm để lưu socket hiện tại trước khi đóng
    {
		std::lock_guard<std::mutex> lock(mtx_); // lock để đồng bộ truy cập sock_ và connected_
		if (!connected_.load(std::memory_order_acquire) && sock_ == INVALID_SOCKET) { // nếu đã disconnected (connected_ = false) và không có socket hợp lệ (sock_ = INVALID_SOCKET) thì chỉ cần return true, không cần làm gì thêm
            return true;
        }
		connected_.store(false, std::memory_order_release); // đánh dấu đã disconnected 
		localSock = sock_;  // lưu socket hiện tại vào biến tạm trước khi đặt sock_ về INVALID_SOCKET 
		sock_ = INVALID_SOCKET; // đặt sock_ về INVALID_SOCKET để các thread khác biết socket không còn hợp lệ
		rxPending_.clear(); // clear buffer pending 
    }
	if (localSock != INVALID_SOCKET) {  // nếu có socket hợp lệ trước khi đóng thì thực hiện shutdown + close ngoài mutex để tránh giữ lock quá lâu
        ::shutdown(localSock, SD_BOTH);
        ::closesocket(localSock);
    }
    return true;
}

/* Function: IsConnected()
 * Mục đích: Trả về cờ logic connected_ (không phải ping thực tế).
 */
bool RciClient::IsConnected() const {
	return connected_.load(std::memory_order_acquire); // đọc cờ connected_ với memory_order_acquire để đảm bảo thấy được cập nhật từ thread khác khi đã connect hoặc disconnect
}

// ============================================================
// RciClient: Low-level send/receive
// ============================================================

/* Function: SendFrame(frame, reply, timeoutMs)
 * Mục đích: Gửi raw frame (đã BuildFrame) và đọc 1 reply frame hợp lệ (ESC ACK/NAK ... ESC ETX chk).
 *          - Resync nếu có rác trong stream
 *          - Lưu phần dư vào rxPending_
 *          - timeout qua SO_RCVTIMEO/WSAETIMEDOUT
 */
bool RciClient::SendFrame(const std::vector<uint8_t>& frame, std::vector<uint8_t>& reply, int timeoutMs) {
	SOCKET s = INVALID_SOCKET;  // biến tạm để lưu socket hiện tại

    // 1) Snapshot socket + take pending
	std::vector<uint8_t> acc;   // buffer tạm để accumulate dữ liệu nhận được (bao gồm cả rxPending_ và dữ liệu mới từ recv)
    {
        std::lock_guard<std::mutex> lock(mtx_); //Đảm bảo thread-safe khi truy cập
		if (!connected_.load(std::memory_order_acquire) || sock_ == INVALID_SOCKET) return false; // kiểm tra trạng thái kết nối và socket hợp lệ, nếu không thì return false ngay
        s = sock_;
        acc = rxPending_;
        rxPending_.clear();
    }
	acc.reserve(std::max<size_t>(256, acc.size() + 256));   // reserve dung lượng cho buffer tạm để tránh phải realloc nhiều lần khi nhận dữ liệu mới từ recv, tối thiểu là 256 bytes hoặc lớn hơn nếu rxPending_ đã có dữ liệu
	reply.clear();  // clear buffer reply để chuẩn bị nhận dữ liệu mới từ frame trả về

    // 2) Apply timeouts (blocking)
	if (timeoutMs > 0) {    // nếu timeoutMs > 0 thì áp dụng timeout cho send/recv bằng setsockopt với SO_RCVTIMEO và SO_SNDTIMEO, giá trị timeout được chuyển từ milliseconds sang DWORD (milliseconds), đặt recv timeout bằng timeoutMs và send timeout tối đa 2000ms để tránh block quá lâu trong send)
        DWORD rcvTo = (DWORD)timeoutMs;
        DWORD sndTo = (DWORD)min(timeoutMs, 2000);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTo, sizeof(rcvTo));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&sndTo, sizeof(sndTo));
    }

	uint8_t cmdid = 0xFF;   // trích cmdid từ frame để phục vụ log và throttle (best-effort), nếu không trích được thì cmdid = 0xFF
	TryGetCmdIdFromFrame(frame, cmdid);     //trích cmdid từ frame để biết lệnh nào đang gửi
	const bool isStatusCmd = (cmdid == 0x14);   // kiểm tra có phải là lệnh status (0x14) hay không để áp dụng throttle và log phù hợp

    // Chặn log hexdump cho status để đỡ loạn
	if (!isStatusCmd) { // nếu không phải lệnh status thì mới log hexdump frame để tránh log quá nhiều cho lệnh status thường xuyên
        //Log(L"➡️ TX: " + HexDump(frame), 1);
    }

    // 3) Send all
    size_t total = 0;   
	while (total < frame.size()) { // gửi dữ liệu trong frame cho đến khi gửi hết, mỗi lần gọi send có thể gửi một phần dữ liệu, nếu send trả về SOCKET_ERROR thì log lỗi chi tiết và mark disconnected, nếu send trả về số byte đã gửi thì cộng dồn vào total để tiếp tục gửi phần còn lại của frame
        int n = send(s, (const char*)frame.data() + total, (int)(frame.size() - total), 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            Log(L"❌ send() failed Err=" + std::to_wstring(err), 2);
            MarkDisconnected_NoThrow();
            return false;
        }
        total += (size_t)n;
    }

    // 4) Receive one valid reply frame: must start with ESC ACK/NAK
	auto find_header = [&](const std::vector<uint8_t>& buf) -> size_t { // tìm vị trí header hợp lệ (ESC ACK/NAK) trong buffer, nếu không tìm thấy thì trả về std::string::npos, nếu tìm thấy thì trả về index của byte ESC
        for (size_t i = 0; i + 1 < buf.size(); ++i) {
            if (buf[i] == 0x1B && (buf[i + 1] == 0x06 || buf[i + 1] == 0x15)) return i;
        }
        return std::string::npos;
        };

	while (true) {  // vòng lặp để nhận dữ liệu cho đến khi tìm được frame trả lời hợp lệ hoặc gặp lỗi/timeout
        // resync: find header
		size_t h = find_header(acc);    // tìm vị trí header hợp lệ trong buffer tạm, nếu có rác ở đầu buffer thì sẽ bị loại bỏ dần qua mỗi vòng lặp cho đến khi tìm được header hợp lệ hoặc buffer hết dữ liệu, nếu tìm thấy header ở vị trí h > 0 thì xóa phần rác ở đầu buffer trước khi tiếp tục xử lý
        if (h != std::string::npos && h > 0) {
            acc.erase(acc.begin(), acc.begin() + h);
        }

        // parse if buffer begins with ESC ACK/NAK
		if (acc.size() >= 2 && acc[0] == 0x1B && (acc[1] == 0x06 || acc[1] == 0x15)) {  // nếu buffer bắt đầu với ESC ACK/NAK thì cố gắng parse frame trả lời
            // find ESC ETX
            // [2026-11-13 R06] tìm ESC ETX theo cặp (ESC ESC là dữ liệu), thay cho quét byte thô
            size_t escEtxPos = RciFindEscEtx(acc);

            if (escEtxPos != std::string::npos) {
				size_t after = escEtxPos + 2;   // sau ESC ETX có thể có 1 byte checksum hoặc 2 byte nếu checksum bị escape, nên cần kiểm tra byte ngay sau ESC ETX để xác định vị trí kết thúc thực sự của frame trả lời, nếu byte ngay sau ESC ETX là ESC thì checksum bị escape và phải đọc thêm 1 byte nữa, nếu không phải thì byte đó là checksum và vị trí kết thúc của frame trả lời là ngay sau byte đó
				if (acc.size() > after) {   // nếu có dữ liệu sau ESC ETX thì kiểm tra byte đó để xác định vị trí kết thúc của frame trả lời
                    size_t frameEnd = 0;    
					if (acc[after] == 0x1B) {  // byte checksum bị escape, nên vị trí kết thúc của frame trả lời là sau 2 byte (ESC + byte checksum thực tế), cần kiểm tra có đủ dữ liệu cho 2 byte này hay không
                        if (acc.size() > after + 1) frameEnd = after + 2;   
                    }
                    else {
                        frameEnd = after + 1;
                    }

					if (frameEnd > 0) { // nếu xác định được vị trí kết thúc của frame trả lời thì sẽ cắt buffer tạm từ đầu đến vị trí đó để lấy frame trả lời, sau đó lưu phần dư còn lại vào rxPending_ để sử dụng cho lần nhận tiếp theo, nếu parse frame trả lời thành công và checksum hợp lệ thì sẽ trả về true, nếu không hợp lệ thì sẽ tiếp tục vòng lặp để nhận thêm dữ liệu
                        reply.assign(acc.begin(), acc.begin() + frameEnd);

                        // save leftover
                        {
                            std::lock_guard<std::mutex> lock(mtx_);
                            rxPending_.assign(acc.begin() + frameEnd, acc.end());
                        }

						if (!isStatusCmd) { // nếu không phải lệnh status thì mới log hexdump frame trả lời để tránh log quá nhiều cho lệnh status thường xuyên, log frame trả lời đã nhận được (chưa parse) để debug
                            //Log(L"⬅️ RX: " + HexDump(reply), 1);
                        }
                        return true;
                    }
                }
            }
        }

        // need more data
		uint8_t tmp[1024];  // buffer tạm để nhận dữ liệu mới từ recv, mỗi lần recv có thể nhận một phần dữ liệu và sẽ được cộng dồn vào buffer tạm acc để tiếp tục tìm kiếm frame trả lời hợp lệ, nếu recv trả về 0 thì có nghĩa là peer đã đóng kết nối, nếu trả về SOCKET_ERROR thì kiểm tra lỗi có phải là WSAETIMEDOUT (timeout) hay không, nếu timeout thì log timeout và clear rxPending_ để tránh xử lý dữ liệu cũ trong lần nhận tiếp theo, nếu lỗi khác thì log lỗi chi tiết và mark disconnected
		int n = recv(s, (char*)tmp, sizeof(tmp), 0);    // gọi recv để nhận dữ liệu mới từ socket, nếu recv trả về số byte đã nhận thì sẽ cộng dồn vào buffer tạm acc, nếu recv trả về 0 thì có nghĩa là peer đã đóng kết nối, nếu trả về SOCKET_ERROR thì kiểm tra lỗi có phải là WSAETIMEDOUT (timeout) hay không, nếu timeout thì log timeout và clear rxPending_ để tránh xử lý dữ liệu cũ trong lần nhận tiếp theo, nếu lỗi khác thì log lỗi chi tiết và mark disconnected

		if (n == 0) {   // recv trả về 0 có nghĩa là peer đã đóng kết nối, log thông tin này và mark disconnected để cập nhật trạng thái kết nối
            Log(L"❌ recv(): peer closed", 2);
            MarkDisconnected_NoThrow();
            return false;
        }
		if (n == SOCKET_ERROR) {    // nếu recv trả về SOCKET_ERROR thì kiểm tra lỗi có phải là WSAETIMEDOUT (timeout) hay không, nếu timeout thì log timeout và clear rxPending_ để tránh xử lý dữ liệu cũ trong lần nhận tiếp theo, nếu lỗi khác thì log lỗi chi tiết và mark disconnected
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                Log(L"⬅️ RX timeout", 1);
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    rxPending_.clear();
                }
                return false;
            }
            Log(L"❌ recv() failed Err=" + std::to_wstring(err), 2);
            MarkDisconnected_NoThrow();
            return false;
        }

		acc.insert(acc.end(), tmp, tmp + n);    // cộng dồn dữ liệu mới nhận được vào buffer tạm acc để tiếp tục tìm kiếm frame trả lời hợp lệ trong vòng lặp, nếu buffer tạm quá lớn (ví dụ > 8192 bytes) thì có thể có rác không hợp lệ trong stream, nên xóa bớt phần đầu của buffer để tránh unbounded growth, chỉ giữ lại phần cuối của buffer (ví dụ 2048 bytes) để tiếp tục tìm kiếm frame trả lời hợp lệ

        // prevent unbounded growth if garbage stream
		// [2026-11-18 R40] Chỉ cắt khi KHÔNG thấy header ESC ACK/NAK: reply hợp lệ có thể lớn (Upload Message Data tới ~65 KB).
		if (acc.size() > 8192 && find_header(acc) == std::string::npos) {
            acc.erase(acc.begin(), acc.end() - 2048);
        }
    }
}

// ============================================================
// RciClient: Frame builders / utilities (static)
// ============================================================

/* Function: ComputeChecksum(bytes)
 * Mục đích: Tính checksum 8-bit kiểu two's complement theo manual: chk = (0x100 - (sum & 0xFF)) & 0xFF.
 */
uint8_t RciClient::ComputeChecksum(const vector<uint8_t>& bytes) {
    unsigned int sum = 0;
    for (auto b : bytes) sum += b;
    return (uint8_t)((0x100 - (sum & 0xFF)) & 0xFF);
}

/* Function: BuildFrame(commandId, payload, useSOH, includeChecksum)
 * Mục đích: Build frame theo format bạn đang áp dụng:
 *          ESC + STX/SOH + [cmdid/payload (escape ESC bằng double-ESC)] + ESC ETX + [checksum (escape ESC)].
 * NOTE: Hiện implementation chỉ escape ESC (không escape <0x20). Đảm bảo khớp simulator/manual bạn đang dùng.
 */
std::vector<uint8_t> RciClient::BuildFrame(uint8_t commandId,const std::vector<uint8_t>& payload,bool useSOH,bool includeChecksum) 
{
	// Định nghĩa các byte đặc biệt theo manual: ESC=0x1B, STX=0x02, SOH=0x01, ETX=0x03. Sử dụng useSOH để quyết định header là STX hay SOH, và includeChecksum để quyết định có tính và thêm checksum vào cuối frame hay không.
    const uint8_t ESC = 0x1B;
    const uint8_t STX = 0x02;
    const uint8_t SOH = 0x01;
    const uint8_t ETX = 0x03;

    //Tạo buffer chứa frame
    std::vector<uint8_t> frame;
    frame.reserve(2 + 2 + payload.size() * 2 + 4);


    // Tạo Header của frame
    frame.push_back(ESC);
    frame.push_back(useSOH ? SOH : STX);

    // Chuẩn bị dữ liệu để tính checksum
    std::vector<uint8_t> csData;
    csData.reserve(1 + 1 + payload.size() + 1);

    //Thêm dữ liệu vào checksum area  
    csData.push_back(useSOH ? SOH : STX);
    csData.push_back(commandId);
    csData.insert(csData.end(), payload.begin(), payload.end());
    csData.push_back(ETX);

    //Lambda escape byte ESC
    auto push_escaped_esc_only = [&](uint8_t b) {
        if (b == ESC) {
            frame.push_back(ESC);
            frame.push_back(ESC);
        }
        else {
            frame.push_back(b);
        }
        };

    // Thêm Command ID vào frame
    push_escaped_esc_only(commandId);

    //Payload chứa data của command.
    for (uint8_t b : payload) {
        push_escaped_esc_only(b);
    }

    // ký hiệu kết thúc frame.
    frame.push_back(ESC);
    frame.push_back(ETX);

    // Thêm checksum
    if (includeChecksum) {
        uint8_t chk = ComputeChecksum(csData);
		push_escaped_esc_only(chk); // nếu checksum bằng ESC thì sẽ được escape thành ESC ESC, nếu không thì thêm trực tiếp
    }

    return frame;
}

/* Function: ReplyToString(reply)
 * Mục đích: Debug helper — in size + một phần bytes đầu.
 * hàm hỗ trợ debug (debug helper) dùng để chuyển dữ liệu reply dạng vector<uint8_t> thành chuỗi wstring
 */
std::wstring RciClient::ReplyToString(const vector<uint8_t>& reply) {
    wstringstream ws;
    ws << L"[" << reply.size() << L" bytes] ";
    for (size_t i = 0; i < min(reply.size(), size_t(12)); ++i)
        ws << hex << setw(2) << setfill(L'0') << (int)reply[i] << L" ";
    return ws.str();
}

// ============================================================
// RciClient: High-level commands (simple wrappers)
// ============================================================

/* Function: RequestStatus()
 * Mục đích: Gửi lệnh STATUS (0x14) dạng đơn giản (không parse body).
 * Khuyến nghị AppController dùng RequestStatusEx() nếu cần dữ liệu.
 */
bool RciClient::RequestStatus() {
    return SendAndWaitAck(0x14, {});
}

/* Function: StartPrint()
 * Mục đích: Gửi lệnh Start Print (0x11) (simple wrapper).
 */
bool RciClient::StartPrint() {
    // [2026-11-13 R03] Tài liệu: Start Print vẫn được ACK khi máy đang có lỗi (print fail) nhưng KHÔNG in;
    // phải kiểm tra P-STATUS của reply.
    ParsedReply pr;
    if (!SendAndWaitAckEx(0x11, {}, pr)) return false;
    if (pr.p_status != 0) {
        Log(L"❌ Start Print được ACK nhưng máy báo lỗi P-STATUS=" + std::to_wstring((int)pr.p_status) +
            L" (" + DecodePStatus(pr.p_status) + L") - chưa in", 2);
        return false;
    }
    return true;
}

/* Function: StopPrint()
 * Mục đích: Gửi lệnh Stop Print (0x12) (simple wrapper).
 */
bool RciClient::StopPrint() {
    return SendAndWaitAck(0x12, {});
}

/* Function: StartJet()
 * Mục đích: Gửi lệnh Start Jet (0x0F) và chờ ACK.
 */
bool RciClient::StartJet() {
    return SendAndWaitAck(0x0F, {}, 3000);
}

/* Function: StopJet()
 * Mục đích: Gửi lệnh Stop Jet (0x10) và chờ ACK.
 */
bool RciClient::StopJet() {
    return SendAndWaitAck(0x10, {}, 3000);
}

/* Function: LoadMessage(name, printCount)
 * Mục đích: Load message theo name + set printCount 
 */
bool RciClient::LoadMessage(const string& name, uint16_t printCount) {
    vector<uint8_t> payload; //Tạo buffer payload
    std::vector<uint8_t> name16 = BuildMsgName16(name); //Chuyển tên message thành 16 byte
    payload.insert(payload.end(), name16.begin(), name16.end()); //Thêm message name vào payload

    payload.push_back(printCount & 0xFF);   //Thêm print count (byte thấp)
    payload.push_back((printCount >> 8) & 0xFF);    //Thêm print count (byte cao)

    return SendAndWaitAck(0x1E, payload); //Gửi và kiểm tra ACK
}

/* Function: DownloadRemoteField(data)
 * Mục đích: Download Remote Field payload kiểu (len16 + data...) dùng cmd 0x1D 
 */
bool RciClient::DownloadRemoteField(const vector<uint8_t>& data) {
    vector<uint8_t> payload;
    size_t size = data.size();
    if (size > UINT16_MAX) {
        Log(L"Payload quá lớn", 2);
        return false;
    }
    uint16_t len = static_cast<uint16_t>(size);

    payload.push_back(len & 0xFF);
    payload.push_back((len >> 8) & 0xFF);
    payload.insert(payload.end(), data.begin(), data.end());

    return SendAndWaitAck(0x1D, payload);
}

/* Function: DownloadMessageData(data)
 * Mục đích: Download Message Data (cmd 0x19 
 */
bool RciClient::DownloadMessageData(const vector<uint8_t>& data) {
    return SendAndWaitAck(0x19, data);
}

// ============================================================
// RciClient: Extended utilities used by AppController
// ============================================================

/* Function: SendAndWaitAck(cmdid, payload, timeoutMs)
 * Mục đích: Gửi cmd + payload và verify reply:
 *          - parse reply frame
 *          - verify checksum
 *          - yêu cầu ACK và cmdid khớp
 */
bool RciClient::SendAndWaitAck(uint8_t cmdid, const std::vector<uint8_t>& payload, int timeoutMs) {
    ParsedReply pr;
    return SendAndWaitAckEx(cmdid, payload, pr, timeoutMs);
}

/* Function: SendAndWaitAckEx(cmdid, payload, out, timeoutMs)
 * [2026-11-13 R03] Giống SendAndWaitAck nhưng trả reply đã parse. Log lý do khi thất bại.
 */
bool RciClient::SendAndWaitAckEx(uint8_t cmdid, const std::vector<uint8_t>& payload, ParsedReply& pr, int timeoutMs) {
    pr = ParsedReply{};
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;

    pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok) {
        Log(L"❌ Reply không hợp lệ (sai khung/checksum) cho lệnh 0x" + Hex2Local(cmdid), 2);
        return false;
    }
    if (!pr.ack) {
        Log(L"❌ NAK cho lệnh 0x" + Hex2Local(cmdid) + L": C-STATUS=0x" + Hex2Local(pr.c_status) +
            L" " + DecodeCStatus(pr.c_status), 2);
        return false;
    }
    if (pr.cmdid != cmdid) {
        Log(L"❌ Reply sai Command ID: gửi 0x" + Hex2Local(cmdid) + L", nhận 0x" + Hex2Local(pr.cmdid), 2);
        return false;
    }
    return true;
}

/* Function: SendAndGetBody(cmdid, payload, outBody, timeoutMs)
 * Mục đích: Gửi cmd + payload và lấy body đã parse/unescape/verify checksum.
 * outBody format theo code hiện tại: [p_status, c_status, cmdid, data...]
 */
bool RciClient::SendAndGetBody(uint8_t cmdid,const std::vector<uint8_t>& payload,std::vector<uint8_t>& outBody,int timeoutMs) 
{
    outBody.clear();
    std::vector<uint8_t> reply;
	if (!SendFrame(BuildFrame(cmdid, payload), reply, timeoutMs)) return false;  // gửi frame và nhận reply, nếu không thành công thì return false

	auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });  // parse reply và verify checksum, nếu không hợp lệ thì return false
	if (!pr.ok || !pr.ack) return false;    // nếu reply không hợp lệ hoặc không phải ACK thì return false
	if (pr.cmdid != cmdid) return false;    // nếu cmdid trong reply không khớp với cmdid đã gửi thì return false

	outBody.push_back(pr.p_status);     // thêm p_status vào outBody
	outBody.push_back(pr.c_status);     // thêm c_status vào outBody
	outBody.push_back(pr.cmdid);        // thêm cmdid vào outBody để tiện cho việc kiểm tra sau này nếu cần
	outBody.insert(outBody.end(), pr.data.begin(), pr.data.end());  // thêm phần data đã parse vào outBody để trả về cho caller, format của outBody sẽ là [p_status, c_status, cmdid, data...]
    return true;
}

/* Function: RequestStatusEx()
 * Mục đích: Gửi STATUS (0x14) và parse đầy đủ thành PrinterStatus:
 *          - p/c status + text
 *          - jetState/printState + errorMask
 *          - warnings (decode từ errorMask)
 *          - derived flags jetOn/printing/idle
 */
PrinterStatus RciClient::RequestStatusEx() {
    PrinterStatus s;
    if (!IsConnected()) return s; // Kiểm tra kết nối

    std::vector<uint8_t> reply; //Tạo buffer reply
    if (!SendFrame(BuildFrame(0x14), reply, 500)) return s;//Gửi lệnh STATUS

    auto pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { //parse frame, unescape data, verify checksum
        return RciClient::ComputeChecksum(v);
        });
    if (!pr.ok || !pr.ack || pr.cmdid != 0x14) return s; //Kiểm tra frame hợp lệ

    if (pr.data.size() < 2 + 4) return s; //Kiểm tra kích thước data 

    s.pStatus = pr.p_status; //Lưu pStatus và cStatus
    s.cStatus = pr.c_status;
	s.pStatusText = DecodePStatus(s.pStatus); //Decode status text từ pStatus và cStatus để hiển thị 
    s.cStatusText = DecodeCStatus(s.cStatus);

	s.jetState = pr.data[0];    //Lưu jetState và printState
    s.printState = pr.data[1];
    s.errorMask = ReadU32_LSB_First(&pr.data[2]);   //Parse errorMask

    s.warnings = DecodeWarningMask(s.errorMask); //Decode warnings

    const bool jetStopped = (s.jetState == 0x03);   
    const bool jetFault = (s.jetState == 0x04);
    s.jetOn = (!jetStopped && !jetFault); //Tính jetOn

    s.idle = (s.printState == 0x02); //Tính idle
    s.printing = (s.printState == 0x00 || s.printState == 0x04 || s.printState == 0x06); //Tính printing
    s.valid = true;   // [2026-11-16 R18]

    return s;
}

/* Function: RequestRemoteFields(out, timeoutMs)
 * [2026-11-13 R08] Gửi 157D (0x9D), parse danh sách remote field (tên + số ký tự tối đa).
 * Nếu máy không có remote field: ACK với 1 byte 0 -> out rỗng, trả true.
 * LƯU Ý: layout descriptor (37 byte/field) suy ra từ bảng trong tài liệu, chưa kiểm chứng trên máy thật.
 */
bool RciClient::RequestRemoteFields(std::vector<RemoteFieldInfo>& out, int timeoutMs) {
    out.clear();
    ParsedReply pr;
    if (!SendAndWaitAckEx(0x9D, {}, pr, timeoutMs)) return false;
    const auto& d = pr.data;
    if (d.empty()) { Log(L"❌ 157D: reply không có dữ liệu", 2); return false; }
    const size_t n = d[0];
    if (n == 0) return true;
    const size_t kDesc = 1 + 32 + 1 + 1 + 2;   // marker + name + maxChars + autoLoad + reserved
    if (d.size() < 1 + n * kDesc) {
        Log(L"❌ 157D: độ dài dữ liệu không khớp layout (" + std::to_wstring(d.size()) + L" byte, " +
            std::to_wstring(n) + L" field)", 2);
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const size_t off = 1 + i * kDesc;
        RemoteFieldInfo f;
        const char* nm = (const char*)&d[off + 1];
        size_t len = 0;
        while (len < 32 && nm[len] != '\0') ++len;
        f.name.assign(nm, len);
        f.maxChars = d[off + 33];
        f.autoLoad = d[off + 34] != 0;
        out.push_back(std::move(f));
    }
    return true;
}

/* Function: SendRemoteFieldDataByName(fieldName, valueUtf8, timeoutMs)
 * Mục đích: Build payload theo format 0x9E:
 *          [numFields=1][fieldName(<=31)+0][value(<=255)+0] và chờ ACK.
 */
bool RciClient::SendRemoteFieldDataByName(const std::string& fieldName,const std::string& value,int timeoutMs) 
{
    std::vector<uint8_t> payload;
	payload.push_back(0x01);    // Số lượng field
    AppendNullTerminated(payload, fieldName, 31);   //Thêm fieldName 
	AppendNullTerminated(payload, value, 255);  //Thêm value
    return SendAndWaitAck(0x9E, payload, timeoutMs);
}

/* Function: RequestMessagePrintCount(outCount, outMsgName, msgNameOpt, timeoutMs)
 * Mục đích: Gửi 0x8D để đọc print count + message name:
 *          payload = msgName 16 bytes (all 0 => current)
 *          response data = count(4 bytes LSB) + msgName(16 bytes)
 */
bool RciClient::RequestMessagePrintCount(uint32_t& outCount,std::string& outMsgName,const std::string& msgNameOpt,int timeoutMs)
{
    outCount = 0;   // Reset output 
    outMsgName.clear();

    std::vector<uint8_t> payload = BuildMsgName16(msgNameOpt); //Build payload
    std::vector<uint8_t> body;
    if (!SendAndGetBody(0x8D, payload, body, timeoutMs)) return false;   // Gửi command

    const size_t dataOffset = 3; //Xác định offset data
    if (body.size() < dataOffset + 4 + 16) return false;    //Kiểm tra kích thước

	const uint8_t* p = body.data() + dataOffset; //Trích xuất count (4 bytes LSB-first)
	outCount = (uint32_t)p[0] | // Lưu count vào outCount bằng cách kết hợp 4 byte theo thứ tự LSB-first
        ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16)|
        ((uint32_t)p[3] << 24);

	outMsgName.assign((const char*)(p + 4), 16); //Trích xuất msgName (16 bytes) và lưu vào outMsgName, sau đó cắt bỏ phần sau null nếu có để lấy tên gốc
    size_t z = outMsgName.find('\0'); //
	if (z != std::string::npos) outMsgName.resize(z); // nếu tìm thấy null terminator trong msgName thì cắt chuỗi tại vị trí đó để loại bỏ phần padding null và lấy tên gốc

    return true;
}

/* Function: SetMessagePrintCount(count, msgNameOpt, timeoutMs)
 * Mục đích: Gửi cmd 0x8C để set print count:
 *          payload = count(4 bytes LSB-first) + msgName16 (all 0 => current)
 * Hiện implementation chờ ACK (SendAndWaitAck).
 */
bool RciClient::SetMessagePrintCount(uint32_t count, const std::string& msgNameOpt, int timeoutMs) 
{
	std::vector<uint8_t> payload; // Tạo buffer payload và reserve dung lượng
    payload.reserve(4 + 16);

	payload.push_back((uint8_t)(count & 0xFF)); // Thêm count vào payload theo thứ tự LSB-first
	payload.push_back((uint8_t)((count >> 8) & 0xFF));  // Thêm byte thứ 2 của count
	payload.push_back((uint8_t)((count >> 16) & 0xFF));     // Thêm byte thứ 3 của count
	payload.push_back((uint8_t)((count >> 24) & 0xFF));     // Thêm byte thứ 4 của count

	std::vector<uint8_t> name16 = BuildMsgName16(msgNameOpt);   // Chuyển tên message thành 16 byte và thêm vào payload
	payload.insert(payload.end(), name16.begin(), name16.end());    // Gửi command 0x8C với payload đã xây dựng và chờ ACK

    return SendAndWaitAck(0x8C, payload, timeoutMs);
}


/* Function: RequestPrinterSerial(outSerial, timeoutMs)
 * [2026-11-22 R42] Lệnh 152D (0x98): reply data = serial 9 byte (8 ký tự + NUL; máy serial dài hơn bị cắt còn 8 ký tự đầu).
 */
bool RciClient::RequestPrinterSerial(std::string& outSerial, int timeoutMs) {
    outSerial.clear();
    ParsedReply pr;
    if (!SendAndWaitAckEx(0x98, {}, pr, timeoutMs)) return false;
    size_t n = 0;
    while (n < pr.data.size() && pr.data[n] != 0x00) ++n;
    outSerial.assign((const char*)pr.data.data(), n);
    while (!outSerial.empty() && isspace((unsigned char)outSerial.back())) outSerial.pop_back();
    return !outSerial.empty();
}

/* Function: RequestMessageList(outNames, timeoutMs, printingBusy)
 * [2026-11-22 R44] 97D (0x61) + 'M'. Reply data = [số bản tin] + header 41 byte mỗi bản tin
 * (size 2, rasters 2, EHT 1, width 2, delay 2, TÊN 16, raster name 16). Manual không ghi rõ độ dài trường đếm
 * nên đọc linh hoạt: đếm 1 byte, hoặc 2 byte, hoặc không có trường đếm (suy ra từ độ dài dữ liệu).
 */
bool RciClient::RequestMessageList(std::vector<std::string>& outNames, int timeoutMs, bool* printingBusy) {
    outNames.clear();
    if (printingBusy) *printingBusy = false;
    std::vector<uint8_t> reply;
    if (!SendFrame(BuildFrame(0x61, std::vector<uint8_t>{ 'M' }), reply, timeoutMs)) return false;
    ParsedReply pr = ParseReplyAndVerify(reply, [](const std::vector<uint8_t>& v) { return RciClient::ComputeChecksum(v); });
    if (!pr.ok) { Log(L"❌ Reply 97D không hợp lệ (sai khung/checksum)", 2); return false; }
    if (!pr.ack) {
        if (pr.c_status == 0x14 && printingBusy) *printingBusy = true;
        Log(L"❌ NAK cho lệnh 0x61: C-STATUS=0x" + Hex2Local(pr.c_status) + L" " + DecodeCStatus(pr.c_status), 2);
        return false;
    }
    const auto& d = pr.data;
    const size_t kHdr = 41;
    size_t off = 0;
    if (d.empty()) return true;                                           // không có bản tin
    if (d.size() == 1 + (size_t)d[0] * kHdr) off = 1;                     // đếm 1 byte
    else if (d.size() >= 2 && d.size() == 2 + (size_t)(d[0] | (d[1] << 8)) * kHdr) off = 2;   // đếm 2 byte
    else if (d.size() % kHdr == 0) off = 0;                               // không có trường đếm
    else { Log(L"❌ 97D: độ dài dữ liệu " + std::to_wstring(d.size()) + L" byte không khớp bố cục header 41 byte", 2); return false; }
    for (size_t p = off; p + kHdr <= d.size(); p += kHdr) {
        size_t n = 0;
        while (n < 16 && d[p + 9 + n] != 0) ++n;
        bool ascii = n > 0;
        for (size_t k = 0; k < n; ++k) if (d[p + 9 + k] < 0x20 || d[p + 9 + k] > 0x7E) ascii = false;
        if (ascii) outNames.emplace_back((const char*)&d[p + 9], n);
    }
    return true;
}

// ============================================================
// RciClient: Logging
// ============================================================

/* Function: Log(msg, type)
 * Mục đích: Đẩy log ra callback_ (nếu set) để UI/AppController hiển thị.
 */
void RciClient::Log(const std::wstring& msg, int type) {
    if (callback_) callback_(msg, type);    
}
