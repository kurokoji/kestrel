#include "IconCache.h"

#include <shellapi.h>

IconCache& IconCache::instance() {
    static IconCache cache;
    return cache;
}

HIMAGELIST IconCache::systemImageList() {
    if (!himl_) {
        SHFILEINFOW sfi{};
        himl_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
            L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
    }
    return himl_;
}

int IconCache::iconForDirectory() {
    if (folderIcon_ < 0) {
        systemImageList();
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) {
            folderIcon_ = sfi.iIcon;
        }
    }
    return folderIcon_;
}

int IconCache::iconForFile(const std::wstring& extensionLower) {
    systemImageList();

    if (auto it = extensionIcons_.find(extensionLower); it != extensionIcons_.end()) {
        return it->second;
    }

    std::wstring probe = L"x" + extensionLower;
    SHFILEINFOW sfi{};
    int icon = -1;
    if (SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES)) {
        icon = sfi.iIcon;
    }
    extensionIcons_.emplace(extensionLower, icon);
    return icon;
}
