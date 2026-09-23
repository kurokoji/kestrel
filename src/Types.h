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

// Sentinel "path" for the virtual Recycle Bin view (TreePane's "ゴミ箱"
// node). Same shell `::{CLSID}` convention as kThisPcPath, and for the same
// reason: the recycle bin's real children are enumerated through
// IShellFolder (DirectoryModel::run special-cases this path), not
// FindFirstFileExW, since $Recycle.Bin's on-disk names/layout aren't the
// shell-visible ones. Unlike kThisPcPath, entries under it are NOT
// navigable/operable real paths - FilePane disables rename/cut/copy/
// delete/drag/open for this view (see FilePane::selectedPaths,
// FilePane::activateEntry, FilePane::doRename) rather than trying to
// synthesize a real path for a deleted item.
inline constexpr wchar_t kRecycleBinPath[] = L"::{645FF040-5081-101B-9F08-00AA002F954E}";

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

    // Also precomputed by DirectoryModel::run, for the same reason:
    // FileEntrySort::matchesSearch is called from NM_CUSTOMDRAW's
    // CDDS_ITEMPREPAINT while a search is active, i.e. once per visible
    // row on every repaint/scroll - re-lower-casing `name` there would
    // mean a fresh heap-allocated copy per row per paint.
    std::wstring lowercaseName;

    // Shell type name for the Type column ("テキスト ドキュメント"), looked
    // up once per extension by DirectoryModel::run - also what the Type
    // column sorts by.
    std::wstring typeName;

    [[nodiscard]] bool isDirectory() const noexcept {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
};
