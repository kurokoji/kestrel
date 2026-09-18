#include "doctest.h"

#include "TabCycle.h"

TEST_CASE("nextIndex advances by one going forward") {
    CHECK(TabCycle::nextIndex(0, 3, true) == 1);
    CHECK(TabCycle::nextIndex(1, 3, true) == 2);
}

TEST_CASE("nextIndex wraps from the last tab to the first going forward") {
    CHECK(TabCycle::nextIndex(2, 3, true) == 0);
}

TEST_CASE("nextIndex retreats by one going backward") {
    CHECK(TabCycle::nextIndex(2, 3, false) == 1);
    CHECK(TabCycle::nextIndex(1, 3, false) == 0);
}

TEST_CASE("nextIndex wraps from the first tab to the last going backward") {
    CHECK(TabCycle::nextIndex(0, 3, false) == 2);
}

TEST_CASE("nextIndex with a single tab stays put in either direction") {
    CHECK(TabCycle::nextIndex(0, 1, true) == 0);
    CHECK(TabCycle::nextIndex(0, 1, false) == 0);
}

TEST_CASE("nextIndex with two tabs toggles between them") {
    CHECK(TabCycle::nextIndex(0, 2, true) == 1);
    CHECK(TabCycle::nextIndex(1, 2, true) == 0);
    CHECK(TabCycle::nextIndex(0, 2, false) == 1);
}
