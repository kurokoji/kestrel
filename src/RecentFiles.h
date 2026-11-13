#pragma once

#include <cstdint>

namespace RecentFiles {
// Windows file timestamps are UTC, in 100 ns units.
inline constexpr bool isRecent(uint64_t created, uint64_t now, bool directory) noexcept {
    constexpr uint64_t day = 24ULL * 60 * 60 * 10'000'000;
    return !directory && created != 0 && created <= now && now - created <= day;
}
}
