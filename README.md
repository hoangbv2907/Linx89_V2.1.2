# Linx Controller - MVC Multithreading

Ứng dụng Win32 (C++17, Visual Studio 2022+, toolset v143) điều khiển máy in phun Linx 8800/8900/CJ400/Linx 10
qua giao thức RCI trên TCP. Tham chiếu: *Linx RCI Reference Manual MP65969-7*.

## Kiến trúc
- UI (`WindowManager`, `UIManager`, `ModernButton`, `ToggleSwitch`) chỉ đẩy `Request` vào `RequestQueue`.
- `AppController` chạy một worker thread: xử lý request, poll trạng thái, tự reconnect, rồi `PostMessage` về UI.
- `RciClient` xử lý TCP + khung RCI (`ESC STX cmd data ESC ETX chk`), checksum, parse reply.
- `PrinterModel` giữ trạng thái (có mutex). `RciWarnings` giải mã P-STATUS / C-STATUS / error mask.

## Kết nối
Máy in lắng nghe Ethernet ở **cổng 29043** (hằng `kRciDefaultPort` trong `CommonTypes.h`).
RCI không có xác thực/mã hoá — chỉ dùng trong mạng nội bộ cách ly.

## Quy trình in
1. Nhập số lượng và nhấn SET (lệnh 140D).
2. Bật jet nếu cần, Load Print Message (30D) message có sẵn trên máy (`SetPrintMessageName`; để trống = message đang chọn).
3. Gửi nội dung remote field (29D, UTF-8), rồi Start Print (17D). Máy cần được cấu hình remote field và bật UTF-8 nếu in tiếng Việt.

## Build và test
- Mở `Linx v2.1.2.sln` bằng Visual Studio, cấu hình x64.
- Unit test (không cần phần cứng): mở *Developer Command Prompt*, chạy `tests\build_tests.bat`.

## Đóng gói / cài trên máy khác
Mở *Developer Command Prompt*, chạy `build_release.bat`. Kết quả trong `dist/`:
- `LinxController.exe` — chạy trực tiếp, không cần cài gì thêm (Windows 10/11 x64).
- `LinxController_Setup.exe` — bộ cài cho người dùng hiện tại (không cần quyền Administrator): chép vào
  `%LOCALAPPDATA%\Programs\LinxController`, tạo shortcut Desktop + Start Menu. Gỡ: chạy `uninstall.cmd` trong thư mục đó.

Chạy cả ba bộ test: `tests\build_tests.bat` (bộ `AppControllerTests` cần cổng 127.0.0.1:29043 còn trống).

## Nhật ký thay đổi
Xem [CHANGELOG.md](CHANGELOG.md) — các thay đổi được gắn mã `Rnn` kèm ngày trong code.
