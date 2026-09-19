#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// Plain data describing one directory entry. Kept intentionally simple: no
// virtual functions, no inheritance, cheap to move around in a vector.
struct FileEntry {
    std::wstring name;       // file/folder name only (no path)
    std::wstring extension;  // lower-cased, includes leading dot; empty for directories
    uint64_t size = 0;       // 0 for directories
    FILETIME modified{};
    DWORD attributes = 0;
    int iconIndex = -1;      // resolved lazily, cached by IconCache

    // Precomputed once by DirectoryModel::run (on its background thread)
    // rather than reformatted on every LVN_GETDISPINFOW - the list can
    // ask for these many times per second while scrolling a large
    // directory, and neither depends on anything that changes afterward.
    std::wstring formattedSize;      // empty for directories, matching the display convention
    std::wstring formattedModified;

    [[nodiscard]] bool isDirectory() const noexcept {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
};
