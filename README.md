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
3. Gửi nội dung remote field theo tên (158D, UTF-8, đệm đúng kích thước field), rồi Start Print (17D). Máy cần được cấu hình remote field và bật UTF-8 nếu in tiếng Việt.

## Giao diện (R43)
- Cửa sổ **co giãn được** (kéo viền / phóng to): các ô nhập giãn ngang, 3 nút KHỞI ĐỘNG / IN / DỪNG chia đều, ô nhật ký chiếm hết chiều cao còn lại.
  Kích thước tối thiểu 520×560 (ở 100% DPI). Hỗ trợ DPI cao và kéo sang màn hình khác tỉ lệ. Vị trí / kích thước được nhớ cho lần mở sau.
- Màn hình chính chỉ giữ thao tác vận hành: Upload, Download (đọc kích thước field), SET, Điều chỉnh (+/-), KHỞI ĐỘNG / IN / DỪNG, XÓA NHẬT KÝ.
- Biểu tượng **⚙ (góc trên bên phải)** mở cửa sổ **Cài đặt** có 4 thẻ: **Kết nối** (IP mặc định, tự kết nối lại, tên bản tin dùng khi in),
  **Nhật ký** (xem lại, mở thư mục, số ngày giữ), **Bản quyền** (giờ còn lại, mã máy, nhập mã gia hạn), **Giới thiệu**.
  Cài đặt lưu ở `%LOCALAPPDATA%\LinxController\settings.json`. Hết hạn thì ⚙ tự mở sẵn thẻ Bản quyền.

## Chọn bản tin từ danh sách trên máy (R44)
- Ô **Bản tin** trên màn hình chính liệt kê các bản tin có trên máy (lệnh 97D, loại `M`). Chọn một bản tin = tải lên máy (30D) rồi đọc lại kích thước field (157D) và tên bản tin hiện hành (141D).
- Danh sách chỉ đọc được khi máy **không in** (manual: 97D không hợp lệ khi đang in) nên ô chọn và nút **Làm mới** bị khóa lúc đang in. Bản tin bị xóa trên máy thì phần mềm tự làm mới danh sách.
- Lựa chọn được nhớ cho lần mở sau. Ô nhập tên trong ⚙ Cài đặt › Kết nối chỉ dùng khi máy không đọc được danh sách.

## Cấu trúc thư mục mã nguồn (`Linx v2.1.2/`)

Header (`.h`) nằm trong `include/`, source (`.cpp`) nằm trong `src/`; cả hai chia thư mục con theo cùng một danh sách chức năng, nên header và source của một module luôn ở cùng tên thư mục (ví dụ `include/rci/RciClient.h` ↔ `src/rci/RciClient.cpp`).

| Thư mục con | Nhiệm vụ | File chính |
|---|---|---|
| `rci/` | Giao thức RCI với máy in (khung gói, lệnh, cảnh báo) | `RciClient`, `RciWarnings`, `MessageBuilder.h` |
| `controller/` | Logic điều khiển nền: worker thread, hàng đợi lệnh, trạng thái máy in, lưu dữ liệu in | `AppController`, `PrinterModel.h`, `RequestQueue.h`, `MessageDef.h`, `PrintPersist*` |
| `ui/` | Giao diện chính: cửa sổ, bố cục, nút, ô nhật ký | `WindowManager`, `UIManager`, `UILayout.h`, `ModernButton`, `ToggleSwitch`, `MessageLogger`, `ColorScheme.h`, `FontManager.h` |
| `ui/dialogs/` | Các hộp thoại | `SettingsDialog`, `PinDialog`, `LicenseDialog`, `LogViewerDialog` |
| `auth/` | Đăng nhập 2 cấp, phiên làm việc | `AuthCore` |
| `license/` | Giới hạn 6000 giờ, mã gia hạn ECDSA | `LicenseCore`, `LicensePublicKey.h` |
| `logging/` | Nhật ký kỹ thuật theo ngày, nhật ký sự kiện CSV (chỉ có header) | `Logger.h`, `EventLog.h` |
| `settings/` | Cài đặt người dùng, đường dẫn dữ liệu | `AppSettings`, `AppPaths.h` |
| `common/` | Kiểu dữ liệu, tiện ích dùng chung (chỉ có header) | `CommonTypes.h`, `CommonDefs.h`, `EncodingUtils.h`, `ResourceTracker.h`, `ThreadSafeQueue.h` |
| `third_party/` | Thư viện ngoài (chỉ có header) | `json.hpp` |

`src/main.cpp` là điểm vào. Quy ước `#include`: đường dẫn tính từ `include/` (ví dụ `#include "rci/RciClient.h"`); trình biên dịch cần `/I include` (đã cấu hình trong `.vcxproj`, `build_release.bat`, `testsuild_tests.bat`). Ngoài mã nguồn: `tests/` (bộ test), `tools/` (công cụ tạo mã gia hạn), `installer/`. Solution Explorer hiển thị cùng cây thư mục (Header Files\… / Source Files\…).

## Phân quyền 2 cấp (R51)
- **Vận hành viên** là cấp thấp nhất, **không dùng mật khẩu**: là cấp mặc định ngay khi mở phần mềm (không có màn hình đăng nhập), không giới hạn thời gian. Quyền: kết nối, KHỞI ĐỘNG / DỪNG jet, IN / DỪNG in, chọn bản tin, Upload / Download, SET, Điều chỉnh in bù/trừ; mở ⚙ Cài đặt ở **chế độ chỉ xem** (mọi ô chỉnh sửa, Lưu, Kích hoạt bị vô hiệu).
- **Giám sát viên** (mật khẩu **1234**, viết cứng trong `AuthCore.h`): thêm chỉnh sửa trong ⚙ Cài đặt + XÓA NHẬT KÝ. Vận hành viên bấm XÓA NHẬT KÝ hoặc ⚙ Cài đặt › Tài khoản › "Đăng nhập Giám sát..." sẽ được hỏi mật khẩu.
- Phiên Giám sát viên **tự chuyển về Vận hành viên khi đủ 24 giờ** kể từ lúc đăng nhập (thao tác không kéo dài); muốn tiếp tục phải nhập lại mật khẩu. Giám sát viên có thể bấm "Về Vận hành viên" để hạ ngay.
- Cấp người dùng **không hiển thị ở màn hình chính**, chỉ xem trong ⚙ Cài đặt › Tài khoản. Không có chức năng đổi / quên mật khẩu, không giới hạn số lần nhập sai, không có vết kiểm toán.
- Giới hạn: đây là kiểm soát vận hành, không phải bảo mật chống người có chuyên môn (mật khẩu nằm trong file thực thi).

## Nhật ký (R41, rút gọn R52: chỉ giữ thông báo quan trọng)
- **Chỉ lưu thông báo quan trọng**: cảnh báo, lỗi và sự kiện chính (kết nối / mất kết nối, bắt đầu in, đủ sản lượng, dừng in, đổi bản tin, SET / in bù-trừ, cảnh báo máy bật-tắt, gia hạn, lưu cài đặt). Thông tin thường và gỡ lỗi chỉ hiện trên màn hình.
- Nhật ký sự kiện: `%LOCALAPPDATA%\LinxController\logs\events_YYYY-MM.csv` (UTF-8, mở được bằng Excel). Nhật ký kỹ thuật `logs\linx_YYYY-MM-DD.log` chỉ ghi cảnh báo + lỗi; tự xóa file quá số ngày đặt trong Cài đặt (mặc định 90) hoặc khi tổng quá 200 MB (cố định).
- ⚙ Cài đặt › Nhật ký › **Xem lại nhật ký**: chọn tháng, lọc theo ngày / mức độ / từ khoá, xuất CSV, mở thư mục. Nút "Xóa nhật ký" chỉ xóa màn hình, không xóa file.

## Giới hạn thời gian sử dụng và gia hạn (R42)
- Hạn mức ban đầu **6000 giờ chương trình chạy** (cộng dồn; chỉnh đồng hồ hệ thống không ảnh hưởng). Thanh tiêu đề hiện số giờ còn lại;
  nhắc khi còn ≤100 / ≤24 / ≤1 giờ.
- Hết hạn: khoá khởi động jet, in, SET số lượng, gửi nội dung, in bù/trừ. Vẫn kết nối / dừng in / dừng jet / xem nhật ký / nhập mã.
- Gia hạn: ⚙ Cài đặt › **Bản quyền** → "Sao chép thông tin" (mã máy tính + serial máy in, lệnh 152D) gửi nhà cung cấp → nhận mã → dán → Kích hoạt.
  Mã ký ECDSA P-256, gắn với **mã máy tính + serial máy in**, mỗi mã dùng một lần. Phần mềm chỉ giữ khoá công khai.
- Nhà cung cấp tạo mã bằng **`LicenseKeygenGui.exe`** (cửa sổ, build bằng `toolsuild_keygen.bat`; tự tách mã máy + serial từ nội dung khách gửi, seq tự tăng, ghi lịch sử `issued_codes.csv`) hoặc bản dòng lệnh `tools\LicenseKeygen.cpp` (khoá riêng KHÔNG nằm trong repo). Xem `HUONG_DAN.txt` cạnh khoá riêng.
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
