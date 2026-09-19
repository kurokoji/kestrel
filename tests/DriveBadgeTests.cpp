#include "doctest.h"

#include "DriveBadge.h"

TEST_CASE("driveLetterOf extracts and uppercases the drive letter") {
    CHECK(DriveBadge::driveLetterOf(L"C:\\Users\\foo") == L'C');
    CHECK(DriveBadge::driveLetterOf(L"d:\\") == L'D');
    CHECK(DriveBadge::driveLetterOf(L"E:") == L'E');
}

TEST_CASE("driveLetterOf returns nullopt for non-drive paths") {
    CHECK_FALSE(DriveBadge::driveLetterOf(L"").has_value());
    CHECK_FALSE(DriveBadge::driveLetterOf(L"\\\\server\\share").has_value());
    CHECK_FALSE(DriveBadge::driveLetterOf(L"relative\\path").has_value());
    CHECK_FALSE(DriveBadge::driveLetterOf(L"1:\\notaletter").has_value());
}

TEST_CASE("colorForDrive is deterministic - same letter always gives the same color") {
    CHECK(DriveBadge::colorForDrive(L'C') == DriveBadge::colorForDrive(L'C'));
    CHECK(DriveBadge::colorForDrive(L'c') == DriveBadge::colorForDrive(L'C'));  // case-insensitive
}

TEST_CASE("colorForDrive gives different colors to different nearby letters") {
    CHECK(DriveBadge::colorForDrive(L'C') != DriveBadge::colorForDrive(L'D'));
    CHECK(DriveBadge::colorForDrive(L'D') != DriveBadge::colorForDrive(L'E'));
}
