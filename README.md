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

## Giao diện (R43)
- Cửa sổ **co giãn được** (kéo viền / phóng to): các ô nhập giãn ngang, 3 nút KHỞI ĐỘNG / IN / DỪNG chia đều, ô nhật ký chiếm hết chiều cao còn lại.
  Kích thước tối thiểu 520×560 (ở 100% DPI). Hỗ trợ DPI cao và kéo sang màn hình khác tỉ lệ. Vị trí / kích thước được nhớ cho lần mở sau.
- Màn hình chính chỉ giữ thao tác vận hành: Upload, Download (đọc kích thước field), SET, Điều chỉnh (+/-), KHỞI ĐỘNG / IN / DỪNG, XÓA NHẬT KÝ.
- Biểu tượng **⚙ (góc trên bên phải)** mở cửa sổ **Cài đặt** có 4 thẻ: **Kết nối** (IP mặc định, tự kết nối lại, tên bản tin dùng khi in),
  **Nhật ký** (xem lại, mở thư mục, số ngày / MB giữ), **Bản quyền** (giờ còn lại, mã máy, nhập mã gia hạn), **Giới thiệu**.
  Cài đặt lưu ở `%LOCALAPPDATA%\LinxController\settings.json`. Hết hạn thì ⚙ tự mở sẵn thẻ Bản quyền.

## Nhật ký (R41)
- Nhật ký kỹ thuật: `%LOCALAPPDATA%\LinxController\logs\linx_YYYY-MM-DD.log` (mỗi ngày một file, tự xoá file quá 90 ngày hoặc khi tổng quá 200 MB).
- Nhật ký sự kiện cho người vận hành: `logs\events_YYYY-MM.csv` (UTF-8, mở được bằng Excel). Mọi dòng hiện trên ô nhật ký đều được lưu.
- ⚙ Cài đặt › Nhật ký › **Xem lại nhật ký**: chọn tháng, lọc theo ngày / mức độ / từ khoá, xuất CSV, mở thư mục. Nút "Xoá nhật ký" chỉ xoá màn hình, không xoá file.

## Giới hạn thời gian sử dụng và gia hạn (R42)
- Hạn mức ban đầu **6000 giờ chương trình chạy** (cộng dồn; chỉnh đồng hồ hệ thống không ảnh hưởng). Thanh tiêu đề hiện số giờ còn lại;
  nhắc khi còn ≤100 / ≤24 / ≤1 giờ.
- Hết hạn: khoá khởi động jet, in, SET số lượng, gửi nội dung, in bù/trừ. Vẫn kết nối / dừng in / dừng jet / xem nhật ký / nhập mã.
- Gia hạn: ⚙ Cài đặt › **Bản quyền** → "Sao chép thông tin" (mã máy tính + serial máy in, lệnh 152D) gửi nhà cung cấp → nhận mã → dán → Kích hoạt.
  Mã ký ECDSA P-256, gắn với **mã máy tính + serial máy in**, mỗi mã dùng một lần. Phần mềm chỉ giữ khoá công khai.
- Nhà cung cấp tạo mã bằng `tools\LicenseKeygen.cpp` (khoá riêng KHÔNG nằm trong repo). Xem `HUONG_DAN.txt` cạnh khoá riêng.
- Giới hạn: chống được người dùng thông thường, không chống được người có chuyên môn (xoá cả file lẫn registry, vá exe).

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
