// [2026-11-22 R41] Nhật ký sự kiện cho người vận hành: mọi dòng hiện trên ô nhật ký của màn hình đều được lưu vào
//   %LOCALAPPDATA%\LinxController\logs\events_YYYY-MM.csv   (UTF-8 có BOM, mở được bằng Excel)
// Cột: thời gian, mức độ, nội dung.  Mức độ: 0 INFO, 1 WARN, 2 ERROR, 3 DEBUG, 4 OK.
// Khác với Logger (nhật ký kỹ thuật theo ngày), file này chỉ chứa thông báo cho người dùng và giữ lâu dài (nhỏ).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include "AppPaths.h"

class EventLog {
public:
    struct Entry {
        std::wstring time;     // "YYYY-MM-DD HH:MM:SS.mmm"
        int level = 0;
        std::wstring text;
    };

    static const wchar_t* LevelName(int level) {
        switch (level) {
        case 1: return L"WARN";
        case 2: return L"ERROR";
        case 3: return L"DEBUG";
        case 4: return L"OK";
        default: return L"INFO";
        }
    }
    static int LevelFromName(const std::wstring& s) {
        if (s == L"WARN") return 1;
        if (s == L"ERROR") return 2;
        if (s == L"DEBUG") return 3;
        if (s == L"OK") return 4;
        return 0;
    }

    // Ghi một sự kiện (thread-safe). Không ném ngoại lệ.
    static void Append(int level, const std::wstring& text) {
        try {
            std::lock_guard<std::mutex> lock(Mutex());
            SYSTEMTIME st; GetLocalTime(&st);
            wchar_t month[16];
            swprintf_s(month, L"%04d-%02d", st.wYear, st.wMonth);
            const auto path = PathFor(month);
            const bool isNew = !std::filesystem::exists(path);
            std::ofstream f(path, std::ios::app | std::ios::binary);
            if (!f) return;
            if (isNew) f.write("\xEF\xBB\xBF", 3);   // BOM để Excel đọc đúng tiếng Việt
            wchar_t ts[40];
            swprintf_s(ts, L"%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            std::wstring line = std::wstring(ts) + L"," + LevelName(level) + L",\"" + Escape(text) + L"\"";
            const std::string u8 = ToUtf8(line);
            f.write(u8.data(), (std::streamsize)u8.size());
            f.put('\n');
        }
        catch (...) {}
    }

    // File của một tháng ("YYYY-MM")
    static std::filesystem::path PathFor(const std::wstring& yyyymm) {
        return LinxLogDir() / (L"events_" + yyyymm + L".csv");
    }

    // Danh sách tháng có dữ liệu, mới nhất trước ("YYYY-MM")
    static std::vector<std::wstring> ListMonths() {
        std::vector<std::wstring> out;
        std::error_code ec;
        for (auto& e : std::filesystem::directory_iterator(LinxLogDir(), ec)) {
            const std::wstring n = e.path().filename().wstring();
            if (n.size() == 18 && n.rfind(L"events_", 0) == 0 && e.path().extension() == L".csv")
                out.push_back(n.substr(7, 7));
        }
        std::sort(out.begin(), out.end(), std::greater<std::wstring>());
        return out;
    }

    // Đọc toàn bộ sự kiện của một tháng, thứ tự thời gian.
    static std::vector<Entry> Load(const std::wstring& yyyymm) {
        std::vector<Entry> out;
        std::ifstream f(PathFor(yyyymm), std::ios::binary);
        if (!f) return out;
        std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (all.size() >= 3 && (unsigned char)all[0] == 0xEF && (unsigned char)all[1] == 0xBB && (unsigned char)all[2] == 0xBF)
            all.erase(0, 3);
        size_t pos = 0;
        while (pos < all.size()) {
            size_t nl = all.find('\n', pos);
            if (nl == std::string::npos) nl = all.size();
            std::string line = all.substr(pos, nl - pos);
            pos = nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            Entry e;
            if (ParseLine(FromUtf8(line), e)) out.push_back(std::move(e));
        }
        return out;
    }

    // Lọc để xem lại: minLevel 0 = tất cả; 1 = cảnh báo + lỗi; 2 = chỉ lỗi.
    // day: "YYYY-MM-DD" (rỗng = cả tháng); keyword: tìm không phân biệt hoa thường (rỗng = bỏ qua).
    static std::vector<Entry> Filter(const std::vector<Entry>& in, int minLevel, const std::wstring& day, const std::wstring& keyword) {
        auto lower = [](std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; };
        const std::wstring kw = lower(keyword);
        std::vector<Entry> out;
        for (const auto& e : in) {
            if (minLevel == 1 && !(e.level == 1 || e.level == 2)) continue;
            if (minLevel == 2 && e.level != 2) continue;
            if (!day.empty() && e.time.compare(0, day.size(), day) != 0) continue;
            if (!kw.empty() && lower(e.text).find(kw) == std::wstring::npos) continue;
            out.push_back(e);
        }
        return out;
    }

    // Dựng lại một dòng CSV (dùng khi xuất file)
    static std::wstring ToCsvLine(const Entry& e) {
        return e.time + L"," + LevelName(e.level) + L",\"" + Escape(e.text) + L"\"";
    }

private:
    static std::mutex& Mutex() { static std::mutex m; return m; }

    // Không cho xuống dòng trong nội dung; nhân đôi dấu nháy kép theo chuẩn CSV.
    static std::wstring Escape(const std::wstring& s) {
        std::wstring o;
        o.reserve(s.size() + 4);
        for (wchar_t c : s) {
            if (c == L'\r' || c == L'\n') { if (o.empty() || o.back() != L' ') o.push_back(L' '); }
            else if (c == L'"') { o += L"\"\""; }
            else o.push_back(c);
        }
        return o;
    }

    // "time,LEVEL,"text with ""quotes"""
    static bool ParseLine(const std::wstring& line, Entry& e) {
        const size_t c1 = line.find(L',');
        if (c1 == std::wstring::npos) return false;
        const size_t c2 = line.find(L',', c1 + 1);
        if (c2 == std::wstring::npos) return false;
        e.time = line.substr(0, c1);
        e.level = LevelFromName(line.substr(c1 + 1, c2 - c1 - 1));
        std::wstring rest = line.substr(c2 + 1);
        if (rest.size() >= 2 && rest.front() == L'"' && rest.back() == L'"') {
            rest = rest.substr(1, rest.size() - 2);
            std::wstring t;
            for (size_t i = 0; i < rest.size(); ++i) {
                if (rest[i] == L'"' && i + 1 < rest.size() && rest[i + 1] == L'"') ++i;
                t.push_back(rest[i]);
            }
            e.text = t;
        }
        else e.text = rest;
        return !e.time.empty();
    }

    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
        return out;
    }
    static std::wstring FromUtf8(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
        return out;
    }
};
