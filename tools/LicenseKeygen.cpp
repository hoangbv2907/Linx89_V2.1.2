// [2026-11-22 R42] Công cụ của NHÀ CUNG CẤP: tạo cặp khoá và mã gia hạn. KHÔNG phát hành cùng phần mềm.
//   LicenseKeygen init  <thư_mục>                     tạo license_private.key (GIỮ BÍ MẬT), license_public.key, LicensePublicKey.h
//   LicenseKeygen gen   <private.key> <mã_máy> <serial_máy_in> <số_giờ> <số_thứ_tự_mã>
//   LicenseKeygen check <public.key>  <mã> <mã_máy> <serial_máy_in>      kiểm tra một mã
//   LicenseKeygen machine                              in mã máy của máy tính này
// Mã máy và serial máy in do khách hàng đọc trên cửa sổ "Gia hạn" của phần mềm gửi cho bạn.
// Số thứ tự mã (seq) phải DUY NHẤT cho mỗi mã bạn bán (mỗi mã chỉ dùng được một lần trên mỗi máy).
#include "../Linx v2.1.2/LicenseCore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <windows.h>

static std::wstring W(const char* s) {
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}
static bool WriteBin(const std::string& path, const lic::Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return (bool)f;
}
static bool ReadBin(const std::string& path, lic::Bytes& b) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    b.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !b.empty();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("Cach dung: xem dau file LicenseKeygen.cpp\n"); return 2; }
    const std::string cmd = argv[1];
    if (cmd == "machine") {
        std::wprintf(L"%ls\n", lic::MachineId().c_str());
        return 0;
    }
    if (cmd == "init" && argc == 3) {
        const std::string dir = argv[2];
        CreateDirectoryA(dir.c_str(), nullptr);
        const std::string priv = dir + "\\license_private.key";
        if (GetFileAttributesA(priv.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::printf("TU CHOI: %s da ton tai (ghi de se lam MAT hieu luc moi ma da ban).\n", priv.c_str());
            return 1;
        }
        lic::Bytes pr, pu;
        if (!lic::GenerateKeyPair(pr, pu)) { std::printf("Loi tao khoa\n"); return 1; }
        if (!WriteBin(priv, pr) || !WriteBin(dir + "\\license_public.key", pu)) { std::printf("Loi ghi file\n"); return 1; }
        std::ofstream h(dir + "\\LicensePublicKey.h");
        h << "// Khoa CONG KHAI dung de kiem tra ma gia han (do LicenseKeygen init tao). Khong chua bi mat.\n#pragma once\n";
        h << "static const unsigned char kLicensePublicKey[] = {";
        for (size_t i = 0; i < pu.size(); ++i) { if (i % 16 == 0) h << "\n    "; char b[8]; std::snprintf(b, sizeof(b), "0x%02X,", pu[i]); h << b; }
        h << "\n};\n";
        std::printf("Da tao trong %s:\n  license_private.key  <- GIU BI MAT, SAO LUU\n  license_public.key\n  LicensePublicKey.h   <- chep vao thu muc ma nguon phan mem\n", dir.c_str());
        return 0;
    }
    if (cmd == "gen" && argc == 7) {
        lic::Bytes pr;
        if (!ReadBin(argv[2], pr)) { std::printf("Khong doc duoc khoa rieng\n"); return 1; }
        const unsigned long hours = std::strtoul(argv[5], nullptr, 10), seq = std::strtoul(argv[6], nullptr, 10);
        const std::string code = lic::MakeCode(pr, W(argv[3]), argv[4], (uint32_t)hours, (uint32_t)seq);
        if (code.empty()) { std::printf("Loi tao ma (kiem tra serial khac rong, so gio > 0)\n"); return 1; }
        std::printf("%s\n", code.c_str());
        return 0;
    }
    if (cmd == "check" && argc == 6) {
        lic::Bytes pu;
        if (!ReadBin(argv[2], pu)) { std::printf("Khong doc duoc khoa cong khai\n"); return 1; }
        lic::CodeInfo info;
        const lic::CodeResult r = lic::VerifyCode(pu, argv[3], W(argv[4]), argv[5], info);
        const char* names[] = { "HOP LE", "SAI DINH DANG", "SAI CHU KY", "SAI MA MAY", "SAI MA MAY IN", "DA DUNG", "THIEU SERIAL" };
        std::printf("%s", names[(int)r]);
        if (r == lic::CodeResult::Ok) std::printf(" (+%u gio, seq %u)", info.hours, info.seq);
        std::printf("\n");
        return r == lic::CodeResult::Ok ? 0 : 1;
    }
    std::printf("Sai tham so. Xem dau file LicenseKeygen.cpp\n");
    return 2;
}
