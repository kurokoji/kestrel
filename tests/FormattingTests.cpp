#include "doctest.h"

#include "Formatting.h"

TEST_CASE("formatSize renders bytes without a decimal point") {
    CHECK(Formatting::formatSize(0) == L"0 B");
    CHECK(Formatting::formatSize(1023) == L"1023 B");
}

TEST_CASE("formatSize renders KB/MB/GB with one decimal place") {
    CHECK(Formatting::formatSize(1024) == L"1.0 KB");
    CHECK(Formatting::formatSize(1536) == L"1.5 KB");
    CHECK(Formatting::formatSize(1024ull * 1024) == L"1.0 MB");
    CHECK(Formatting::formatSize(1024ull * 1024 * 1024) == L"1.0 GB");
}

TEST_CASE("formatFileTime returns empty string for a zero FILETIME") {
    FILETIME ft{};
    CHECK(Formatting::formatFileTime(ft) == L"");
}
