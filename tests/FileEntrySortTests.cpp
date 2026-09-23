#include "doctest.h"

#include "FileEntrySort.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace {

FileEntry makeEntry(const std::wstring& name, bool isDir = false, uint64_t size = 0,
                     const std::wstring& extension = L"", ULARGE_INTEGER modified = {}) {
    FileEntry e;
    e.name = name;
    e.lowercaseName = name;
    std::ranges::transform(e.lowercaseName, e.lowercaseName.begin(), ::towlower);
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

TEST_CASE("sort by type orders by the displayed type name, falling back to name on a tie") {
    auto typed = [](const std::wstring& name, const std::wstring& ext, const std::wstring& type) {
        FileEntry e = makeEntry(name, false, 0, ext);
        e.typeName = type;
        return e;
    };
    // By type name, not extension: ".zip" ("Archive") sorts before ".md" here.
    std::vector<FileEntry> entries{
        typed(L"b.txt", L".txt", L"Text Document"),
        typed(L"c.md", L".md", L"Markdown"),
        typed(L"a.txt", L".txt", L"text document"),  // case-insensitive tie
        typed(L"d.zip", L".zip", L"Archive"),
    };
    FileEntrySort::sort(entries, /*sortColumn=*/1, /*ascending=*/true);
    CHECK(namesOf(entries) == std::vector<std::wstring>{L"d.zip", L"c.md", L"a.txt", L"b.txt"});
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

TEST_CASE("matchesSearch reads the precomputed lowercaseName rather than re-deriving it from name") {
    FileEntry e = makeEntry(L"Report.TXT");
    e.lowercaseName = L"mismatched";  // deliberately inconsistent with e.name
    CHECK(FileEntrySort::matchesSearch(e, L"mismatch"));
    CHECK_FALSE(FileEntrySort::matchesSearch(e, L"report"));
}

TEST_CASE("findByPrefix finds the first case-insensitive name prefix match from start") {
    const std::vector<FileEntry> entries{makeEntry(L"Alpha"), makeEntry(L"beta"), makeEntry(L"Bravo"), makeEntry(L"charlie")};
    CHECK(FileEntrySort::findByPrefix(entries, L"B", 0, false) == 1);
    CHECK(FileEntrySort::findByPrefix(entries, L"br", 0, false) == 2);
    CHECK(FileEntrySort::findByPrefix(entries, L"b", 2, false) == 2);  // start itself is a candidate
    CHECK(FileEntrySort::findByPrefix(entries, L"z", 0, true) == -1);
}

TEST_CASE("findByPrefix wraps past the end only when asked to") {
    const std::vector<FileEntry> entries{makeEntry(L"Alpha"), makeEntry(L"beta"), makeEntry(L"charlie")};
    CHECK(FileEntrySort::findByPrefix(entries, L"a", 1, false) == -1);
    CHECK(FileEntrySort::findByPrefix(entries, L"a", 1, true) == 0);
    CHECK(FileEntrySort::findByPrefix(entries, L"a", 99, true) == 0);  // out-of-range start begins at the top
}
