#include "doctest.h"

#include "FileEntrySort.h"

#include <vector>

namespace {

FileEntry makeEntry(const std::wstring& name, bool isDir = false, uint64_t size = 0,
                     const std::wstring& extension = L"", ULARGE_INTEGER modified = {}) {
    FileEntry e;
    e.name = name;
    e.extension = extension;
    e.size = size;
    e.modified.dwLowDateTime = modified.LowPart;
    e.modified.dwHighDateTime = modified.HighPart;
    if (isDir) e.attributes |= FILE_ATTRIBUTE_DIRECTORY;
    return e;
}

std::vector<std::wstring> namesOf(const std::vector<FileEntry>& entries) {
    std::vector<std::wstring> names;
    for (const auto& e : entries) names.push_back(e.name);
    return names;
}

}  // namespace

TEST_CASE("sort by name puts directories before files regardless of name order") {
    std::vector<FileEntry> entries{makeEntry(L"zeta.txt"), makeEntry(L"alpha", true), makeEntry(L"beta.txt")};
    FileEntrySort::sort(entries, /*sortColumn=*/0, /*ascending=*/true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"alpha", L"beta.txt", L"zeta.txt"});
}

TEST_CASE("sort by name is case-insensitive and respects ascending/descending") {
    std::vector<FileEntry> entries{makeEntry(L"Banana"), makeEntry(L"apple"), makeEntry(L"Cherry")};

    FileEntrySort::sort(entries, 0, true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"apple", L"Banana", L"Cherry"});

    FileEntrySort::sort(entries, 0, false);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"Cherry", L"Banana", L"apple"});
}

TEST_CASE("sort by extension falls back to name on a tie") {
    std::vector<FileEntry> entries{
        makeEntry(L"b.txt", false, 0, L".txt"),
        makeEntry(L"a.txt", false, 0, L".txt"),
        makeEntry(L"c.md", false, 0, L".md"),
    };
    FileEntrySort::sort(entries, /*sortColumn=*/1, /*ascending=*/true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"c.md", L"a.txt", L"b.txt"});
}

TEST_CASE("sort by size orders smallest to largest ascending") {
    std::vector<FileEntry> entries{
        makeEntry(L"big", false, 300),
        makeEntry(L"small", false, 10),
        makeEntry(L"medium", false, 100),
    };
    FileEntrySort::sort(entries, /*sortColumn=*/2, /*ascending=*/true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"small", L"medium", L"big"});
}

TEST_CASE("sort by modified time orders oldest to newest ascending") {
    ULARGE_INTEGER older{};
    older.QuadPart = 100;
    ULARGE_INTEGER newer{};
    newer.QuadPart = 200;

    std::vector<FileEntry> entries{
        makeEntry(L"new", false, 0, L"", newer),
        makeEntry(L"old", false, 0, L"", older),
    };
    FileEntrySort::sort(entries, /*sortColumn=*/3, /*ascending=*/true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"old", L"new"});
}

TEST_CASE("matchesSearch with an empty query never matches") {
    CHECK_FALSE(FileEntrySort::matchesSearch(makeEntry(L"report.txt"), L""));
}

TEST_CASE("matchesSearch is a case-insensitive substring match on the name") {
    CHECK(FileEntrySort::matchesSearch(makeEntry(L"MyReport.txt"), L"report"));
    CHECK_FALSE(FileEntrySort::matchesSearch(makeEntry(L"MyReport.txt"), L"invoice"));
}
