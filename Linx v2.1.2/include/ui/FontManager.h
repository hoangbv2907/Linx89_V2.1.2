/*
dùng để quản lý font trong ứng dụng :
tạo font dùng chưng cho toàn bộ UI,
tránh memory leak,
tự động xóa khi font không còn sử dụng
[2026-11-22 R43] Font theo DPI của cửa sổ (SetDpi) + font biểu tượng (⚙) cho nút Cài đặt.
*/
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>

// tạo font cho UI, lưu font trong unique_ptr, trả về HFONT khi UI cần
//, tự động xóa font khi không còn sử dụng
class FontManager {
public:
	static FontManager& GetInstance() { // Singleton pattern
		static FontManager instance;    // Instance duy nhất được tạo khi lần đầu gọi
		return instance;    // Trả về tham chiếu đến instance duy nhất
    }

	// Không cho phép copy hoặc gán instance
    HFONT GetDefaultFont() {
		if (!hDefaultFont_) CreateFonts(); //Tạo font nếu chưa tồn tại
		return hDefaultFont_.get();  // trả về raw handle từ unique_ptr
    }

	// Toggle font có kích thước nhỏ hơn, dùng cho các thành phần như toggle switch
    HFONT GetToggleFont() {
        if (!hToggleFont_) CreateFonts();
        return hToggleFont_.get();
    }

    // [R43] Font cho biểu tượng bánh răng (Segoe UI Symbol)
    HFONT GetIconFont() {
        if (!hIconFont_) CreateFonts();
        return hIconFont_.get();
    }

    // [R43] Đặt DPI (96 = 100%). Gọi khi tạo cửa sổ và khi WM_DPICHANGED; font cũ bị huỷ, font mới tạo lại khi cần.
    // Sau khi gọi, UI phải gán lại font cho các control (UIManager::ApplyFontToAllControls).
    void SetDpi(int dpi) {
        if (dpi < 48) dpi = 96;
        if (dpi == dpi_) return;
        dpi_ = dpi;
        Cleanup();
    }
    int Dpi() const { return dpi_; }

	// delete fonts khi FontManager bị hủy
    void Cleanup() {
		hDefaultFont_.reset();  // gọi deleter để xóa font
        hToggleFont_.reset();
        hIconFont_.reset();
    }

private:
	FontManager() = default;    // Constructor mặc định
    // Custom deleter cho HFONT
    struct FontDeleter {
        void operator()(HFONT font) const {
            if (font) DeleteObject(font);
        }
    };

    using UniqueHFONT = std::unique_ptr<std::remove_pointer<HFONT>::type, FontDeleter>;

    void CreateFonts() {
        int dpi = dpi_;
        if (dpi <= 0) {
            HDC hdc = GetDC(NULL);
            dpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(NULL, hdc);
        }
        int defaultSize = -MulDiv(9, dpi, 72);
        int toggleSize = -MulDiv(8, dpi, 72);
        int iconSize = -MulDiv(15, dpi, 72);
        // ✅ Tạo unique_ptr với custom deleter
        hDefaultFont_ = UniqueHFONT(CreateFontW(defaultSize, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"));
        hToggleFont_ = UniqueHFONT(CreateFontW(toggleSize, 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"));
        hIconFont_ = UniqueHFONT(CreateFontW(iconSize, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Symbol"));
    }
    // ✅ Member variables là unique_ptr
    UniqueHFONT hDefaultFont_;
    UniqueHFONT hToggleFont_;
    UniqueHFONT hIconFont_;
    int dpi_ = 0;           // 0 = lấy DPI của màn hình khi tạo font
};
