/*
dùng để quản lý font trong ứng dụng : 
tạo font dùng chưng cho toàn bộ UI, 
tránh memory leak,
tự động xóa khi font không còn sử dụng
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

	// delete fonts khi FontManager bị hủy
    void Cleanup() {
		hDefaultFont_.reset();  // gọi deleter để xóa font
        hToggleFont_.reset();   
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
        HDC hdc = GetDC(NULL);
        int defaultSize = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        int toggleSize = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);
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
    }
    // ✅ Member variables là unique_ptr
    UniqueHFONT hDefaultFont_;
    UniqueHFONT hToggleFont_;
};