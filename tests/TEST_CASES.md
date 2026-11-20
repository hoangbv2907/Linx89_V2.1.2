# Test case theo tài liệu Linx RCI (MP65969-7)

- Mã nguồn: `tests/ManualFlowTests.cpp` (R32, 2026-11-16). Chạy: `tests\build_tests.bat` (cần cổng `127.0.0.1:29043` trống).
- Đối tượng test: `AppController` + `RciClient` thật. "UI" là một cửa sổ message-only ghi lại mọi thông điệp
  `WM_APP_*` mà worker gửi về (trạng thái, kết nối, log). Widget thật (`UIManager`/`WindowManager`) **không** nằm trong phạm vi.
- Máy in là **máy in giả có trạng thái** (`LinxSim`) hành xử theo manual. Nó kiểm tra checksum của mọi khung nhận được
  và trả NAK với mã C-STATUS theo Table 3-2. Đây là mô phỏng theo cách đọc tài liệu, không thay thế máy thật.
- Kết quả dưới đây: lần chạy 2026-11-16 ~19:00 +07:00, bản debug có AddressSanitizer.

## A. Giao thức kết nối

| Mã | Mục đích | Căn cứ manual | Bước | Mong đợi | Kết quả |
|---|---|---|---|---|---|
| TC-CON-01 | Kết nối thành công | Ch.1 "Connecting the printer": Ethernet port 29043 | Máy in giả nghe cổng 29043; gọi `Connect` | UI nhận `Connecting` rồi thông báo đã kết nối; đúng 1 kết nối TCP; lệnh đầu tiên gửi đi là Status (14H) | PASS |
| TC-CON-02 | UI phản ánh trạng thái máy sau khi nối | Lệnh 20D; "Jet States" | Máy: jet Stopped (3), print Idle (2) | UI `Idle`, `jetOn=false`, `printing=false` | PASS |
| TC-CON-03 | Người dùng ngắt kết nối | (hành vi ứng dụng) | Gọi `Disconnect`, chờ 3 giây | UI `Disconnected`; không tự kết nối lại; không gửi thêm lệnh | PASS |
| TC-CON-04 | Không có máy in (kết nối bị từ chối) | Ch.1 (cổng 29043) | Không có gì nghe ở cổng; gọi `Connect` | UI `Connecting` → `Reconnecting` → `Error`; đúng 5 lần thử tự động rồi dừng; không treo (đo được ~23 giây) | PASS |
| TC-CON-05 | Mất kết nối giữa chừng | (hành vi ứng dụng) | Máy in giả đóng kết nối | UI biết trong < 2 giây (`Reconnecting`), tự nối lại và đọc lại trạng thái | PASS |
| TC-CON-07 | Ngoại lệ khi nhận reply | "Data checksum calculation"; Table 3-2 (81 Printer busy) | 2 reply status sai checksum, 2 reply NAK, 2 lần không trả lời | Trạng thái UI giữ nguyên (`Ready`, jet ON); không báo mất kết nối | PASS |

Trường hợp máy im lặng kéo dài (≥ 5 lần liên tiếp → coi như mất kết nối) nằm ở `AppControllerTests` (S2, S3).
Chưa có ca cho địa chỉ không định tuyến được (timeout 3 giây thật) vì phụ thuộc mạng của máy chạy test.

## B. Gửi/nhận khung, các request

| Mã | Mục đích | Căn cứ manual | Cách kiểm | Mong đợi | Kết quả |
|---|---|---|---|---|---|
| TC-FRM-01 | Mọi khung gửi đi hợp lệ | "Sending data", "Data transfer", "Data checksum calculation" | Máy in giả giải mã và tính lại checksum của **mọi** khung trong cả phiên | 0 khung sai dạng `ESC STX … ESC ETX chk` | PASS |
| TC-FRM-02 | Không gửi lệnh mới khi chưa có reply | "no other commands can be sent until the reply is received" | Đếm số lần có 2 khung dồn trong một lần nhận | 0 | PASS |
| TC-FRM-03 | Chỉ dùng mã lệnh hợp lệ | Table 2-1 | Liệt kê mọi Command ID nhận được | Chỉ gồm 0F, 10, 11, 12, 14, 1D, 1E, 8C, 8D, 9D, 9E | PASS |

Byte-chính-xác của từng khung (ví dụ hex của manual: Start Jet `EC`, Start Print `EA`, Status `E7`, reply `DF`/`51`, Delete Message `44`),
escape `ESC ESC`, reply chia nhỏ, rác trước reply, sai Command ID: xem `RciFrameTests` và `RciSimTests`.
Ánh xạ request → lệnh được kiểm trong các ca nhóm C, D (ví dụ `SetCount` → 8CH, `StopPrinting` → 12H).

## C. Trình tự chuyển trạng thái

| Mã | Mục đích | Căn cứ manual | Bước | Mong đợi | Kết quả |
|---|---|---|---|---|---|
| TC-SEQ-01 | Start Jet | Lệnh 15D: reply ngay, phải theo dõi Jet State bằng 20D | `StartJet`; máy: Startup (1) vài lần poll rồi Running (0) | Gửi đúng 1 lệnh 0FH; UI `StartingJet` rồi mới `Ready` | PASS |
| TC-SEQ-02 | Stop Jet | Lệnh 16D; "Jet Shutdown… Jet Stopped" | `StopJet`; máy: Shutdown (2) rồi Stopped (3) | Gửi 10H; UI `StopingJet` rồi `Idle` | PASS |
| TC-SEQ-03 | Start Jet khi jet đang chạy | Lệnh 15D: bị từ chối với C-STATUS 19 "Jet Not Idle" | `StartJet` lần nữa | Log có "Jet not idle"; UI không kẹt ở `StartingJet` (về `Ready`) | PASS |
| TC-SEQ-04 | Jet lỗi khi khởi động | "Jet States: Fault"; P-STATUS 03 Phase Failure | Máy trả jet state 4 + P-STATUS 3 | UI `Error` có thông báo, không còn cờ đang chuyển trạng thái; hết lỗi thì về `Idle` | PASS |
| TC-SEQ-05 | Stop Print | Lệnh 18D; "Print States" | Đang in, gọi `StopPrinting` | Gửi 12H; UI `Ready` (jet vẫn chạy) | PASS |
| TC-SEQ-06 | Cảnh báo | "32-bit Error Mask", Table 3-3 (bit 3 Ink Low) | Bật rồi tắt bit 3 | Log "Warning ON … Ink Low" rồi "Warning OFF"; UI không sang `Error` | PASS |

## D. Nội dung, số lượng, hoàn thành sản lượng

| Mã | Mục đích | Căn cứ manual | Bước | Mong đợi | Kết quả |
|---|---|---|---|---|---|
| TC-PRD-01 | Đặt số lượng | Lệnh 140D: count 4 byte + tên 16 byte, tên toàn null = message hiện hành | `SetCount(3)` | Gửi 8CH, tên toàn 0; UI target = 3, printed = 0 | PASS |
| TC-PRD-02 | Chưa SET thì không in | (quy tắc ứng dụng) | `StartPrinting` khi chưa SET | Không gửi 11H | PASS |
| TC-PRD-03 | Quy trình bắt đầu in | Lệnh 30D, 29D, 17D | `SetPrintMessageName("MSG1")`, `StartPrinting("HELLO", 3)` | Thứ tự 14H → 1EH → 1DH → 11H; không gửi 0FH khi jet đang chạy; máy nhận đúng tên và nội dung; UI `Printing` | PASS |
| TC-PRD-04 | Theo dõi sản lượng | Lệnh 141D | Chưa in bản nào; rồi in từng bản (1, 2) | Có poll 8DH; UI printed = số bản in thực tế (0 → 1 → 2); **chưa** báo hoàn thành | **FAIL** |
| TC-PRD-05 | Hoàn thành sản lượng | Lệnh 30D ghi chú: CJ400/8800/8900 không hỗ trợ in theo số lượng (host phải tự dừng) | In bản thứ 3 | Báo hoàn thành đúng 1 lần; printed = target = 3; host gửi Stop Print (12H); UI rời `Printing`; không in thừa | **FAIL** |
| TC-PRD-06 | Chưa có message trên máy | Lệnh 29D: NAK 59 "No Print Message Loaded" | Máy không có message; `StartPrinting` | Log có mã lỗi; không gửi 11H; UI không sang `Printing` | PASS |
| TC-PRD-07 | Message không có remote field | Lệnh 29D: NAK 63 "No Remote Fields in Message" | `StartPrinting` | Huỷ in, không gửi 11H | PASS |
| TC-PRD-08 | Remote field theo tên | Lệnh 157D/158D: sai tên hoặc sai số ký tự → NAK 23 | Gửi "AB" (field 5 ký tự); gửi 7 ký tự; gửi tên không tồn tại | 9DH rồi 9EH với giá trị đệm thành "AB   ", không bị NAK; hai trường hợp sai không gửi 9EH | PASS |
| TC-PRD-09 | Start Print khi máy đang lỗi | Lệnh 17D: được ACK nhưng không in, phải xem P-STATUS | P-STATUS = 3, `StartPrinting` | UI không báo `Printing` | PASS |
| TC-NEW-01 | Điều kiện bắt đầu in (R34) | Hết mực (P-STATUS 7), hết dung môi (10), warning nghiêm trọng (cover off) | `StartPrinting` khi máy có từng lỗi đó | Log nêu lý do; không gửi 11H | PASS |
| TC-NEW-02 | Cảnh báo của máy trên phần mềm (R35) | Warning mask 0x14 (bit 3 Ink Low) | Bật rồi tắt bit | `PrinterState.warnings` có rồi rỗng (UI hiện ở ô CẢNH BÁO) | PASS |
| TC-NEW-03 | Tải về từ máy (R36) | 157D + 141D | `DownloadFromPrinter` | state có tên field, 5 ký tự, message MSG1 | PASS |
| TC-NEW-04 | In bù/trừ khi máy đang chạy (R37) | (chỉ phần mềm) | +5, -20, -5 với target 3 | 8; từ chối vì < số đã in; về 3 | PASS |

## Phân tích 2 ca FAIL cũ (TC-PRD-04/05) — ĐÃ SỬA 2026-11-18 (R38)

> Sửa: lấy mốc print count của máy trước Start Print (`printBaseline_`), số đã in = count - mốc; đủ sản lượng thì gửi Stop Print 1 lần. Hai ca này nay PASS.
> Phần phân tích bên dưới giữ lại để tham khảo nguyên nhân.


Giả định của máy in giả: *Message Print Count* (140D/141D) là bộ đếm **tăng 1 sau mỗi bản in**, 140D đặt giá trị cho nó.
Manual chỉ ghi "set or request the print count for the message", không nói rõ chiều đếm — cần xác nhận trên máy thật.

Với giả định đó, ứng dụng hiện tại:
1. `HandleSetCountRequest` gửi 140D với giá trị = **số lượng đích** (ví dụ 3), tức là đặt bộ đếm của máy thành 3.
2. Lần poll 141D đầu tiên khi đang in đọc về 3 ≥ đích nên báo "Đã đạt số lượng in" **khi chưa in bản nào**, và UI hiển thị printed = 3.
3. Sau đó in thêm thì bộ đếm của máy lên 4, 5, 6 nhưng UI vẫn giữ 3.
4. Khi đủ sản lượng, ứng dụng **không gửi Stop Print**; với 8800/8900 máy không tự dừng theo số lượng, nên máy vẫn ở trạng thái in (bản in tiếp theo sẽ vượt sản lượng).

Hướng sửa gợi ý: khi SET thì đặt bộ đếm của máy về 0 (140D với count = 0) và giữ đích ở phía ứng dụng;
khi 141D ≥ đích thì gửi Stop Print (12H), báo hoàn thành một lần.

## Chưa bao phủ
- Widget thật của UI (màu nút, toggle), lưu/khôi phục file `last_print_data.json`.
- Tải message mới lên máy (25D), checksum bị tắt (Appendix D), giao tiếp RS-232/flow control.
- Watchdog 5 phút của jet (R21), timeout tới địa chỉ không định tuyến được.
- Toàn bộ hành vi của máy in thật (thời gian jet khởi động/tắt thật, layout reply 157D, chiều đếm của 140D/141D).
