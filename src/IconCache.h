#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <unordered_map>

// Thin wrapper around the Shell's system image list.
//
// We never build or own icons ourselves - the shell already keeps one
// process-wide small-icon image list that every Explorer-like app shares.
// This class just remembers, per file extension, which index into that
// shared list to use, so we don't call SHGetFileInfo more than once per
// distinct extension. Must be used from the UI thread only (it is only
// ever touched from ListView LVN_GETDISPINFO handling).
class IconCache {
public:
    static IconCache& instance();

    HIMAGELIST systemImageList();

    int iconForDirectory();
    int iconForFile(const std::wstring& extensionLower);

private:
    IconCache() = default;

    HIMAGELIST himl_ = nullptr;
    int folderIcon_ = -1;
    std::unordered_map<std::wstring, int> extensionIcons_;
};
