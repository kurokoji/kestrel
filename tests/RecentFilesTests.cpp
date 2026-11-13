#include "doctest.h"
#include "RecentFiles.h"

TEST_CASE("Only files with a valid creation time within 24 hours are recent") {
    constexpr uint64_t day = 24ULL * 60 * 60 * 10'000'000;
    constexpr uint64_t now = 10 * day;

    CHECK(RecentFiles::isRecent(now, now, false));
    CHECK(RecentFiles::isRecent(now - day, now, false));
    CHECK_FALSE(RecentFiles::isRecent(now - day - 1, now, false));
    CHECK_FALSE(RecentFiles::isRecent(now + 1, now, false));
    CHECK_FALSE(RecentFiles::isRecent(0, 1, false));
    CHECK_FALSE(RecentFiles::isRecent(now, now, true));
}
