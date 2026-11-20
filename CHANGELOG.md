# Nhật ký sửa đổi (CHANGELOG)

Mỗi thay đổi có mã `Rnn` và ngày sửa. Trong code, tìm theo chuỗi `[2026-11-13 Rnn]` để truy vết.
Tài liệu tham chiếu: *Linx RCI Reference Manual MP65969-7* (CJ400 / 8800 / 8900 / Linx 10).

## 2026-11-18 — lô sửa 3 (kết quả thử máy in thật)

| Mã | Nội dung | File chính |
|---|---|---|
| R33 | Dữ liệu gửi xuống LUÔN đúng bằng kích thước remote field: đọc 157D, từ chối nếu dài hơn, đệm khoảng trắng nếu ngắn hơn (cả đường 29D khi in lẫn 158D khi tải lên). Không đọc được 157D thì huỷ thay vì gửi sai độ dài. UI giới hạn ô nhập theo kích thước field | `AppController.cpp`, `UIManager.cpp` |
| R34 | Điều kiện bắt đầu in: chặn khi hết mực/dung môi (P-STATUS 7/10), còn lỗi P-STATUS chưa xoá, jet fault, hoặc warning nghiêm trọng (cover off, dữ liệu hỏng, chưa có message) | `RciWarnings.cpp`, `AppController.cpp` |
| R35 | Cảnh báo/lỗi của máy hiển thị trên phần mềm (ô "CẢNH BÁO MÁY IN"), kể cả bit warning chưa đặt tên | `RciWarnings.cpp`, `UIManager.cpp`, `CommonTypes.h` |
| R36 | Nút "Tải Về": đọc tên + kích thước remote field (157D), tên message + print count (141D) về phần mềm để sửa nội dung/số lượng rồi tải lại. RCI không có lệnh đọc GIÁ TRỊ hiện tại của field nên nội dung lấy từ bản lưu cục bộ | `AppController.cpp`, `WindowManager.cpp` |
| R37 | In bù/trừ khi máy đang chạy ("Bù/Trừ (+/-)" + "Điều chỉnh"): đổi mục tiêu ở phần mềm, không đụng count trên máy, không gián đoạn lệnh in; từ chối nếu mục tiêu mới < số đã in | `AppController.cpp`, `PrinterModel.h` |
| R38 | Đủ sản lượng thì tự gửi Stop Print (1 lần). Số đã in = count hiện tại - count lúc bắt đầu in (sửa luôn TC-PRD-04/05) | `AppController.cpp` |

Kiểm chứng 2026-11-18 (bản debug, không ASan): `RciFrameTests` 58/58, `RciSimTests` 50/50, `AppControllerTests` 24/24, `ManualFlowTests` 28 test case 0 FAIL (thêm TC-NEW-01..04; sửa race trong TC-CON-01). **Chưa thử với máy in thật.**
R40 (2026-11-18): `RciClient::SendFrame` không còn cắt bộ đệm 8192 byte khi reply hợp lệ lớn (chỉ cắt khi không thấy header ESC ACK/NAK). Cổng test đổi được bằng `set TEST_PORT=...`. (Tính năng "sửa bản tin trực tiếp" 26D/25D đã thử rồi HỦY theo yêu cầu; dùng phương án đọc kích thước field bằng 157D ở R36.)

Chưa làm: mục 3 (ý nghĩa count "số lần hoàn thành 1 lần in") — theo yêu cầu bỏ qua.

## 2026-11-16 (lô sửa 2
, ~18:00–18:40 +07:00) — luồng xử lý, quản lý thread, đóng gói

| Mã | Nội dung | File chính |
|---|---|---|
| R18 | `PrinterStatus.valid`: status timeout/NAK/sai khung không còn bị dùng như dữ liệu thật; 5 lần lỗi liên tiếp -> coi như mất kết nối | `RciClient.*`, `AppController.cpp` |
| R19 | `GetCurrentJobContent()` trả bản sao dưới mutex (hết data race UI/worker) | `PrinterModel.h` |
| R20 | `PrinterState` chỉ được GHI trong worker; `Connect()` phía UI chỉ đẩy request; bỏ qua yêu cầu Connect trùng | `AppController.cpp` |
| R21 | Watchdog 5 phút cho Start/Stop Jet; kết nối lại thì bỏ trạng thái chờ cũ; bỏ log lặp mỗi chu kỳ poll | `AppController.*` |
| R22 | `OnConnectionLost()`: mất kết nối là cập nhật model + UI ngay | `AppController.*` |
| R23 | Bỏ mọi `sleep_for` trong worker: chờ trên hàng đợi, bị đánh thức bởi request mới hoặc `Interrupt()` khi dừng | `RequestQueue.h`, `AppController.cpp` |
| R24 | Stop Print / Stop Jet / Disconnect chen lên đầu hàng đợi (`PushFront`); poll nhường cho request của UI; timeout đọc print count 3000 -> 500 ms | `RequestQueue.h`, `AppController.cpp` |
| R25 | Giải phóng các `WM_APP_*` còn trong hàng đợi khi thoát; bỏ biến `static` trong `HandleDestroy` | `WindowManager.*` |
| R26 | `Logger` được tạo trước `g_windowManager` nên còn sống khi `~AppController` ghi log lúc thoát | `main.cpp` |
| R27 | `Logger` ghi UTF-8 (trước đây log dừng ở ký tự có dấu đầu tiên) | `Logger.h` |
| R28 | `SetPrintMessageName` có mutex | `AppController.*` |
| R29 | Test mới: `tests/AppControllerTests.cpp` (AppController thật + máy in giả), `tests/FakePrinter.h`; thêm test `RequestQueue`, `Logger` | `tests/` |
| R30 | `RciClient::Connect` huỷ được (`Disconnect()` đặt cờ), nhận biết kết nối bị từ chối ngay thay vì đợi hết timeout | `RciClient.*` |
| R31 | Đóng gói: `build_release.bat` -> `dist/LinxController.exe` (portable, CRT tĩnh) và `dist/LinxController_Setup.exe` (IExpress) | `build_release.bat`, `installer/` |

| R32 | Test case theo manual: `tests/ManualFlowTests.cpp` + `tests/TEST_CASES.md` (24 ca: kết nối, khung truyền, trình tự trạng thái, sản lượng). Kết quả 2026-11-16 ~19:00: 22 PASS, **2 FAIL** (TC-PRD-04, TC-PRD-05: logic hoàn thành sản lượng của ứng dụng — chưa sửa) | `tests/` |

### Trạng thái kiểm chứng (2026-11-16 ~18:30 +07:00)
- Bản debug `/Od /Zi /RTC1 /MDd /fsanitize=address`: `RciFrameTests` 58/58, `RciSimTests` 50/50, `AppControllerTests` 24/24 đạt.
- Số đo trong `AppControllerTests` với máy in giả trên loopback: UI biết mất kết nối sau ~350 ms; Stop Print tới máy ngay (~0 ms) dù đang poll chậm;
  `StopWorkerThread` ~1 ms khi máy in im lặng, ~30 ms khi đang chờ reconnect.
- Bản Release (`dist/LinxController.exe`): khởi động được, đóng cửa sổ thì thoát với mã 0, log ghi đủ tới bước cleanup cuối. Chỉ phụ thuộc DLL hệ thống.
- Bộ cài: đã giải nén thử để kiểm tra nội dung, **chưa chạy cài đặt thật**. **Chưa thử với máy in thật.**

### Còn lại / lưu ý
- File cài đặt và exe **chưa ký số**: Windows SmartScreen trên máy khác có thể cảnh báo "Unknown publisher".
- Watchdog jet (R21) 5 phút chưa có test tự động (không muốn test chạy 5 phút).
- Rò rỉ thông điệp (R25) không đo được bằng ASan trên Windows (không có leak detection); mới kiểm tra bằng đọc code.
- Các mục "Chưa giải quyết" của lô 1 (layout 157D, UTF-8 trên máy, chưa có ô nhập tên message trên UI, chưa hỗ trợ tải message 0x19, checksum tắt) vẫn còn nguyên.

## 2026-11-13 (lô sửa 1, ~17:30–18:30 +07:00)

| Mã | Nội dung | File chính | Căn cứ tài liệu |
|---|---|---|---|
| R01 | Cổng RCI mặc định 9100 -> **29043** (hằng `kRciDefaultPort`) | `CommonTypes.h`, `RciClient.*`, `AppController.cpp`, `PrinterModel.h` | Chapter 1, "Connecting the printer" |
| R02 | Unit test cho khung/checksum/parse reply (`tests/`) | `tests/RciFrameTests.cpp`, `tests/build_tests.bat` | Ví dụ hex ở Chapter 1 và Appendix D |
| R03 | Mọi lệnh ghi (Start/Stop Print, Load, Download...) kiểm tra ACK + Command ID, log C-STATUS khi NAK; `StartPrint` kiểm tra thêm P-STATUS | `RciClient.cpp` (`SendAndWaitAckEx`) | "Receiving data"; mô tả lệnh 17D |
| R04 | Bảng P-STATUS đổi sang **số thập phân** (10-13, 20-36, 255) | `RciWarnings.cpp` | "Printer fault codes (P-STATUS)" |
| R05 | Bổ sung C-STATUS 0x5B-0x66 | `RciWarnings.cpp` | Table 3-2 |
| R06 | Tìm `ESC ETX` theo cặp, tránh nhầm `ESC ESC 03` (dữ liệu 1B 03) là kết thúc khung | `RciClient.cpp` (`RciFindEscEtx`) | "Data transfer" |
| R07 | Quy trình in: bật jet -> Load Print Message (30D) message có sẵn -> Download Remote Field (29D) -> Start Print. Bỏ tên "JOBn" không tồn tại | `AppController.cpp` | Lệnh 30D, 29D, 17D |
| R08 | Remote field 158D: đọc độ dài bằng 157D, đệm khoảng trắng, báo lỗi nếu quá dài | `RciClient.cpp`, `AppController.cpp` | Lệnh 157D/158D |
| R09 | Nội dung in mã hoá UTF-8 (giữ tiếng Việt) cho 29D | `MessageBuilder.h` | "UTF-8 encoded message text" |
| R10 | Cờ `static` -> thành viên của `AppController` | `AppController.*` | (độ tin cậy) |
| R11 | Dừng worker trước khi lưu dữ liệu | `AppController.cpp` | (độ tin cậy) |
| R12 | `StopWorkerThread` viết lại: đóng socket + `join`, bỏ `std::async` + `detach` | `AppController.cpp` | (độ tin cậy) |
| R13 | `ErrMask` in hex đúng; `e.what()` -> wide đúng bằng `MultiByteToWideChar` | `AppController.cpp` | (độ chính xác log) |
| R14 | Dọn code chết, biến không dùng, thiếu kiểm tra kết nối | nhiều file | — |
| R15 | `RequestQueue::Clear()` và dùng trong cleanup | `RequestQueue.h`, `AppController.cpp` | — |
| R17 | Test tích hợp với máy in giả TCP loopback (`tests/RciSimTests.cpp`); build debug `/Od /Zi /RTC1 /fsanitize=address`; sửa test UTF-8 (escape `é`) | `tests/` | Ví dụ hex ở Chapter 1 / Appendix D |
| R16 | Giới hạn số lượng 1..999.999.999 | `AppController.cpp` | Lệnh 140D |

### Trạng thái kiểm chứng
- Biên dịch (`cl /Zs`, `/W3`): `AppController`, `RciClient`, `RciWarnings`, `PrintPersistStorage`, `UIManager`, `WindowManager`, `main` — sạch, không cảnh báo.
- 2026-11-13 18:5x +07:00, bản debug (`/Od /Zi /RTC1 /MDd /fsanitize=address`): `RciFrameTests` **39/39 đạt**, `RciSimTests` **45/45 đạt**, không có báo lỗi ASan/RTC.
- Máy in giả chỉ mô phỏng theo tài liệu; **chưa thử với máy in thật**. `AppController`/UI chưa có test tự động.

### Chưa giải quyết / cần xác nhận trên máy thật
- Layout reply của 157D (R08) suy ra từ bảng trong tài liệu (37 byte/field); nếu khác, code chỉ log lỗi và gửi nguyên giá trị.
- UTF-8 (R09) chỉ đúng khi bật "UTF-8 Message Text" trên máy; nếu tắt, máy sẽ in ký tự sai.
- Tên message cần load (R07) phải đặt bằng `AppController::SetPrintMessageName(...)`; hiện **chưa có ô nhập trên UI**, mặc định dùng message đang chọn trên máy.
- Mỗi lệnh vẫn chờ tối đa theo timeout socket (khoảng 3 giây) nên khi mất mạng worker có thể chậm phản hồi.
- Tải message mới lên máy (lệnh 0x19, định dạng Chapter 4) **chưa được hỗ trợ**; message phải được tạo sẵn trên máy.
- Trạng thái `paused` của `ConvertFromRciStatus` vẫn ánh xạ thành `Idle`.
- Tắt checksum trên máy (Appendix D) chưa được hỗ trợ: code luôn gửi và kiểm tra checksum.
- Repo vẫn còn file `.vs/` và `x64/` đang được git theo dõi (xem `.gitignore`).
