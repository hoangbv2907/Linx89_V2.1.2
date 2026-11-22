// [2026-11-22 R43] Bố cục màn hình chính (thuần tính toán, không phụ thuộc control Win32 nên test được).
// Mọi số đo viết theo đơn vị 96 DPI rồi nhân tỉ lệ theo DPI thật. Chiều rộng/cao client co giãn:
//   - ô IP, nội dung in, ô chọn bản tin, thông tin field, trạng thái, cảnh báo: giãn theo chiều ngang;
//   - 3 nút KHỞI ĐỘNG / IN / DỪNG chia đều chiều rộng;
//   - ô nhật ký chiếm toàn bộ phần chiều cao còn lại.
// [2026-11-22 R44] Thêm hàng "Bản tin" (ô chọn + nút Làm mới) dưới ô thông tin field.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>

constexpr int kMinClientW96 = 520;   // kích thước client tối thiểu (đơn vị 96 DPI)
constexpr int kMinClientH96 = 590;

struct UiRects {
    RECT lblIp, ip, toggle, settings;
    RECT lblContent, content, upload, download, fieldInfo;
    RECT lblMessage, message, msgRefresh;
    RECT lblCount, count, lblReached, reached, set;
    RECT lblAdjust, adjust, adjustBtn;
    RECT start, print, stop;
    RECT status, lblWarn, warn;
    RECT lblLog, clear, log;
};

inline UiRects ComputeLayout(int W, int H, int dpi) {
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    auto box = [&](int x, int y, int w, int h) { return RECT{ S(x), S(y), S(x) + S(w), S(y) + S(h) }; };
    auto hspan = [&](int x, int rightMargin, int y, int h) {   // từ x (96dpi) tới W - rightMargin (96dpi)
        return RECT{ S(x), S(y), (std::max)(S(x) + 1, W - S(rightMargin)), S(y) + S(h) };
    };
    const int M = 20;                       // lề
    UiRects r{};
    // hàng 1: IP + công tắc + biểu tượng cài đặt (sát phải)
    r.lblIp = box(M, 17, 100, 20);
    r.ip = box(130, 14, 150, 23);
    r.toggle = box(300, 14, 60, 26);
    r.settings = RECT{ W - S(M) - S(36), S(12), W - S(M), S(12) + S(30) };
    // hàng 2: nội dung in (giãn ngang) + Upload / Download sát phải
    r.lblContent = box(M, 57, 100, 20);
    r.content = RECT{ S(130), S(54), (std::max)(S(131), W - S(M) - S(98)), S(54) + S(60) };
    r.upload = RECT{ W - S(M) - S(90), S(54), W - S(M), S(54) + S(28) };
    r.download = RECT{ W - S(M) - S(90), S(86), W - S(M), S(86) + S(28) };
    r.fieldInfo = hspan(130, M, 118, 16);
    // hàng 3 [R44]: chọn bản tin trên máy (giãn ngang) + nút Làm mới sát phải
    r.lblMessage = box(M, 143, 100, 20);
    r.message = RECT{ S(130), S(140), (std::max)(S(131), W - S(M) - S(98)), S(140) + S(24) };
    r.msgRefresh = RECT{ W - S(M) - S(90), S(139), W - S(M), S(139) + S(26) };
    // hàng 4: số lượng
    r.lblCount = box(M, 173, 100, 20);
    r.count = box(130, 170, 80, 23);
    r.lblReached = box(222, 173, 110, 20);
    r.reached = box(335, 170, 70, 22);
    r.set = RECT{ W - S(M) - S(70), S(170), W - S(M), S(170) + S(23) };
    // hàng 5: in bù / trừ
    r.lblAdjust = box(M, 203, 110, 20);
    r.adjust = box(130, 200, 80, 23);
    r.adjustBtn = box(222, 200, 120, 23);
    // hàng 6: 3 nút lớn chia đều
    {
        const int gap = S(12), left = S(M), avail = (std::max)(3, W - 2 * S(M) - 2 * gap);
        const int bw = avail / 3, y = S(234), h = S(40);
        r.start = RECT{ left, y, left + bw, y + h };
        r.print = RECT{ left + bw + gap, y, left + 2 * bw + gap, y + h };
        r.stop = RECT{ left + 2 * bw + 2 * gap, y, left + 3 * bw + 2 * gap + (avail - 3 * bw), y + h };
    }
    // trạng thái + cảnh báo (giãn ngang)
    r.status = hspan(M, M, 284, 24);
    r.lblWarn = box(M, 318, 200, 20);
    r.warn = hspan(M, M, 340, 62);
    // nhật ký: nhãn + nút Xoá (sát phải) + ô log chiếm phần còn lại
    r.lblLog = box(M, 414, 100, 20);
    r.clear = RECT{ W - S(M) - S(110), S(410), W - S(M), S(410) + S(22) };
    const int logTop = S(436);
    const int logBottom = (std::max)(logTop + S(60), H - S(M));
    r.log = RECT{ S(M), logTop, (std::max)(S(M) + 1, W - S(M)), logBottom };
    return r;
}
