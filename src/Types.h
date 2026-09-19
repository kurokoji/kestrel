#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// Sentinel "path" for the virtual This-PC/drive-list view (TreePane's "PC"
// node, whose real children are drives, not files under a real folder).
// Matches the shell's own `::{CLSID}` convention for namespace roots that
// don't have a filesystem path, so it reads as intentional rather than
// garbage if it ever ends up somewhere visible (e.g. the address bar).
// DirectoryModel::run special-cases it (drive enumeration instead of
// FindFirstFileExW); FilePane's joinPath() special-cases it too, since
// entries under it are already full drive roots ("C:\") rather than names
// relative to a real containing folder.
inline constexpr wchar_t kThisPcPath[] = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";

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
