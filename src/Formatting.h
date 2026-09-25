#pragma once

#include "Strings.h"

#include <windows.h>
#include <format>
#include <string>

namespace Formatting {

inline std::wstring formatSize(uint64_t bytes) {
    constexpr const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return std::format(L"{} {}", bytes, units[unit]);
    }
    return std::format(L"{:.1f} {}", value, units[unit]);
}

// "1.5 GB 空き / 4.0 GB" (English: "1.5 GB free / 4.0 GB") - the drive
// list's size column and the status bar.
inline std::wstring formatFreeSpace(uint64_t freeBytes, uint64_t totalBytes) {
    const std::wstring freeText = formatSize(freeBytes);
    const std::wstring totalText = formatSize(totalBytes);
    return std::vformat(tr(StringId::FreeSpace), std::make_wformat_args(freeText, totalText));
}

inline std::wstring formatFileTime(const FILETIME& ft) {
    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) return L"";
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        return L"";
    }
    return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

}  // namespace Formatting
