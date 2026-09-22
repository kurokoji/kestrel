#include "IconCache.h"
#include "Types.h"

#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>

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

int IconCache::iconForPath(const std::wstring& path) {
    systemImageList();

    std::wstring key = path;
    std::ranges::transform(key, key.begin(), ::towlower);
    if (auto it = pathIcons_.find(key); it != pathIcons_.end()) {
        return it->second;
    }

    int icon = -1;
    SHFILEINFOW sfi{};
    if (path == kThisPcPath) {
        // Not a real filesystem path - SHGetFileInfoW needs a PIDL for the
        // virtual "This PC" namespace root rather than a path string.
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &pidl))) {
            if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi, sizeof(sfi),
                                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_PIDL)) {
                icon = sfi.iIcon;
            }
            CoTaskMemFree(pidl);
        }
    } else if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) {
        // Deliberately no SHGFI_USEFILEATTRIBUTES here (unlike
        // iconForDirectory/iconForFile) - we want the shell to actually
        // resolve this specific path, so special folders (Downloads,
        // custom desktop.ini icons, per-drive icons) get their real icon
        // instead of the generic folder/file-type one.
        icon = sfi.iIcon;
    }

    if (icon < 0) icon = iconForDirectory();
    pathIcons_.emplace(std::move(key), icon);
    return icon;
}
