#include "doctest.h"

#include "EditKeys.h"

TEST_CASE("clipboard shortcuts are left to a focused edit control") {
    CHECK(EditKeys::editOwnsKey('C', true, false, false));
    CHECK(EditKeys::editOwnsKey('X', true, false, false));
    CHECK(EditKeys::editOwnsKey('V', true, false, false));
}

TEST_CASE("other shortcuts still reach the accelerator table") {
    CHECK_FALSE(EditKeys::editOwnsKey('F', true, false, false));
    CHECK_FALSE(EditKeys::editOwnsKey('T', true, false, false));
    CHECK_FALSE(EditKeys::editOwnsKey('C', false, false, false));
}

TEST_CASE("modified clipboard chords are not claimed by the edit") {
    CHECK_FALSE(EditKeys::editOwnsKey('C', true, true, false));
    CHECK_FALSE(EditKeys::editOwnsKey('V', true, false, true));
}
