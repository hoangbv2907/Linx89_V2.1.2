// [2026-11-23 R45] Hộp thoại đăng nhập (modal): nhập mật khẩu; sai thì báo và cho nhập lại, không giới hạn số lần.
//   fixedRole = -1 : hiện ô chọn cấp (Vận hành / Giám sát) - dùng khi mở phần mềm và khi đổi người dùng.
//   fixedRole = 1 / 2 : chỉ hỏi mật khẩu của cấp đó (2 = Giám sát viên, dùng khi cần nâng quyền).
// Giá trị cấp trùng với auth::Role (1 = Operator, 2 = Supervisor).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <string>

class PinDialog {
public:
    // verify(pin, role) trả true nếu mật khẩu đúng. Trả true khi đăng nhập đúng (roleOut = cấp đã vào); false nếu bấm Hủy.
    static bool Run(HWND parent, const std::wstring& title, int fixedRole,
        const std::function<bool(const std::string& pin, int role)>& verify, int* roleOut = nullptr);
};
