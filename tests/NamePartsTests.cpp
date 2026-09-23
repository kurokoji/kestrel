#include "doctest.h"

#include "NameParts.h"

#include <string>
#include <vector>

TEST_CASE("renameSelectionEnd stops before a file's extension") {
    CHECK(NameParts::renameSelectionEnd(L"report.txt", false) == 6);
    CHECK(NameParts::renameSelectionEnd(L"archive.tar.gz", false) == 11);
}

TEST_CASE("renameSelectionEnd selects the whole name when there is no real extension") {
    CHECK(NameParts::renameSelectionEnd(L"Makefile", false) == 8);
    CHECK(NameParts::renameSelectionEnd(L".gitignore", false) == 10);  // leading dot is not an extension
    CHECK(NameParts::renameSelectionEnd(L"my.folder", true) == 9);     // folders never have one
}

TEST_CASE("uniqueName returns the base name when it is free") {
    CHECK(NameParts::uniqueName({L"other"}, L"新しいフォルダー") == L"新しいフォルダー");
}

TEST_CASE("uniqueName numbers from (2) like Explorer, case-insensitively") {
    const std::vector<std::wstring> existing{L"New Folder", L"new folder (2)"};
    CHECK(NameParts::uniqueName(existing, L"New Folder") == L"New Folder (3)");
}

TEST_CASE("fallbackTypeName mimics Explorer's text for unregistered types") {
    CHECK(NameParts::fallbackTypeName(L".abc", false) == L"ABC ファイル");
    CHECK(NameParts::fallbackTypeName(L"", false) == L"ファイル");
    CHECK(NameParts::fallbackTypeName(L"", true) == L"ファイル フォルダー");
}
