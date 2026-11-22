#include "AppSettings.h"
#include "AppPaths.h"
#include "json.hpp"
#include <algorithm>
#include <fstream>

namespace nlohmann { using json = basic_json<>; }
using json = nlohmann::json;

bool IsValidIpv4(const std::string& s) {
    int parts = 0, val = 0, digits = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        const char c = (i < s.size()) ? s[i] : '.';
        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            if (++digits > 3 || val > 255) return false;
        }
        else if (c == '.') {
            if (digits == 0) return false;
            ++parts;
            val = 0; digits = 0;
        }
        else return false;
    }
    return parts == 4;
}

void AppSettings::Sanitize() {
    if (!IsValidIpv4(ip)) ip = "127.0.0.1";
    logKeepDays = std::clamp(logKeepDays, 7, 3650);
    logMaxMB = std::clamp(logMaxMB, 20, 5000);
    bool ok = printMessageName.size() <= 15;
    for (unsigned char c : printMessageName) if (c < 0x20 || c > 0x7E) ok = false;
    if (!ok) printMessageName.clear();
    if (window.w < 0 || window.h < 0 || window.w > 20000 || window.h > 20000) { window.w = window.h = 0; }
}

std::filesystem::path AppSettings::DefaultPath() {
    return LinxDataDir() / L"settings.json";
}

AppSettings AppSettings::Load(const std::filesystem::path& path) {
    AppSettings s;
    try {
        std::ifstream f(path, std::ios::binary);
        if (!f) return s;
        json j = json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) return s;
        s.ip = j.value("ip", s.ip);
        s.autoReconnect = j.value("autoReconnect", s.autoReconnect);
        s.printMessageName = j.value("printMessageName", s.printMessageName);
        s.logKeepDays = j.value("logKeepDays", s.logKeepDays);
        s.logMaxMB = j.value("logMaxMB", s.logMaxMB);
        if (j.contains("window") && j["window"].is_object()) {
            const auto& w = j["window"];
            s.window.x = w.value("x", 0);
            s.window.y = w.value("y", 0);
            s.window.w = w.value("w", 0);
            s.window.h = w.value("h", 0);
            s.window.maximized = w.value("maximized", false);
        }
    }
    catch (...) {
        s = AppSettings();       // kiểu dữ liệu sai (vd "logKeepDays": "abc") -> bỏ cả file, dùng mặc định
    }
    s.Sanitize();
    return s;
}

bool AppSettings::Save(const std::filesystem::path& path) const {
    try {
        AppSettings c = *this;
        c.Sanitize();
        json j;
        j["ip"] = c.ip;
        j["autoReconnect"] = c.autoReconnect;
        j["printMessageName"] = c.printMessageName;
        j["logKeepDays"] = c.logKeepDays;
        j["logMaxMB"] = c.logMaxMB;
        j["window"] = { {"x", c.window.x}, {"y", c.window.y}, {"w", c.window.w}, {"h", c.window.h}, {"maximized", c.window.maximized} };
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::path tmp = path;
        tmp += L".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            f << j.dump(2);
            if (!f) return false;
        }
        return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    catch (...) { return false; }
}

RECT ClampWindowToWorkArea(int x, int y, int w, int h, const RECT& work, int minW, int minH) {
    const int aw = work.right - work.left, ah = work.bottom - work.top;
    w = (std::max)(minW, (std::min)(w, aw));
    h = (std::max)(minH, (std::min)(h, ah));
    if (w > aw) w = aw;
    if (h > ah) h = ah;
    x = (std::max<int>)(work.left, (std::min<int>)(x, work.right - w));
    y = (std::max<int>)(work.top, (std::min<int>)(y, work.bottom - h));
    return RECT{ x, y, x + w, y + h };
}
