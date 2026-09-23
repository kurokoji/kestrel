#include "RecycleBinOps.h"
#include "Types.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>

namespace {

struct ChildPidlDeleter {
    using pointer = PITEMID_CHILD;
    void operator()(pointer p) const { CoTaskMemFree(p); }
};
using OwnedChildPidl = std::unique_ptr<ITEMIDLIST, ChildPidlDeleter>;

struct AbsolutePidlDeleter {
    using pointer = PIDLIST_ABSOLUTE;
    void operator()(pointer p) const { CoTaskMemFree(p); }
};
using OwnedAbsolutePidl = std::unique_ptr<ITEMIDLIST, AbsolutePidlDeleter>;

ComPtr<IShellFolder2> bindRecycleBin() {
    ComPtr<IShellFolder2> folder;
    PIDLIST_ABSOLUTE rawPidl = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &rawPidl))) return folder;
    OwnedAbsolutePidl pidl(rawPidl);

    ComPtr<IShellFolder> desktop;
    if (FAILED(SHGetDesktopFolder(desktop.addressOf()))) return folder;
    desktop->BindToObject(pidl.get(), nullptr, IID_PPV_ARGS(folder.addressOf()));
    return folder;
}

// Re-resolves `names` (in-folder display names, as FileEntry::name carries
// for this view) to their current child PIDLs by enumerating the Recycle
// Bin fresh - there's no live PIDL held over from the listing that produced
// those names (see RecycleBinOps.h). Matches by name only, so two distinct
// deleted items that happen to share a display name resolve to whichever
// the shell enumerates first - an accepted limitation, same trade-off
// DirectoryModel::run already takes enumerating this folder in the first
// place.
std::vector<OwnedChildPidl> resolvePidls(IShellFolder2* folder, const std::vector<std::wstring>& names,
                                         SHGDNF nameForm = SHGDN_INFOLDER) {
    std::vector<OwnedChildPidl> result;
    ComPtr<IEnumIDList> enumIds;
    if (FAILED(folder->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN,
                                    enumIds.addressOf()))) {
        return result;
    }

    std::vector<std::wstring> remaining = names;
    PITEMID_CHILD rawChild = nullptr;
    while (!remaining.empty() && enumIds->Next(1, &rawChild, nullptr) == S_OK) {
        OwnedChildPidl child(rawChild);

        STRRET strret{};
        wchar_t nameBuf[MAX_PATH] = L"";
        if (SUCCEEDED(folder->GetDisplayNameOf(child.get(), nameForm, &strret))) {
            StrRetToBufW(&strret, child.get(), nameBuf, MAX_PATH);
        }

        const auto it = std::ranges::find_if(remaining, [&](const std::wstring& n) {
            return nameForm == SHGDN_INFOLDER ? n == nameBuf : _wcsicmp(n.c_str(), nameBuf) == 0;
        });
        if (it != remaining.end()) {
            remaining.erase(it);
            result.push_back(std::move(child));
        }
    }
    return result;
}

// QueryContextMenu/InvokeCommand for a single named verb, bypassing any
// actual on-screen menu - used for deleteItemsPermanently, triggered by the
// Delete key, not a shown menu (see ShellContextMenu for the right-click
// case, which shows the real menu instead of picking a verb - that's also
// how restore is exposed, rather than guessing/matching its verb string
// here).
bool invokeVerb(HWND owner, IShellFolder2* folder, const std::vector<OwnedChildPidl>& pidls, const wchar_t* verb) {
    if (pidls.empty()) return false;

    std::vector<PCUITEMID_CHILD> apidl;
    apidl.reserve(pidls.size());
    for (const auto& p : pidls) apidl.push_back(p.get());

    ComPtr<IContextMenu> menu;
    if (FAILED(folder->GetUIObjectOf(owner, static_cast<UINT>(apidl.size()), apidl.data(), IID_IContextMenu,
                                      nullptr, reinterpret_cast<void**>(menu.addressOf())))) {
        return false;
    }

    HMENU hmenu = CreatePopupMenu();
    if (!hmenu) return false;
    if (FAILED(menu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL))) {
        DestroyMenu(hmenu);
        return false;
    }

    int foundCmd = -1;
    const int count = GetMenuItemCount(hmenu);
    for (int i = 0; i < count; ++i) {
        const UINT id = GetMenuItemID(hmenu, i);
        if (id == static_cast<UINT>(-1)) continue;  // a submenu, not a leaf command
        wchar_t verbBuf[64] = L"";
        if (SUCCEEDED(menu->GetCommandString(id - 1, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verbBuf),
                                              std::size(verbBuf))) &&
            _wcsicmp(verbBuf, verb) == 0) {
            foundCmd = static_cast<int>(id - 1);
            break;
        }
    }

    bool invoked = false;
    if (foundCmd >= 0) {
        CMINVOKECOMMANDINFO info{};
        info.cbSize = sizeof(info);
        info.hwnd = owner;
        info.lpVerb = MAKEINTRESOURCEA(foundCmd);
        info.nShow = SW_SHOWNORMAL;
        invoked = SUCCEEDED(menu->InvokeCommand(&info));
    }

    DestroyMenu(hmenu);
    return invoked;
}

}  // namespace

namespace RecycleBinOps {

HRESULT getUIObject(HWND owner, const std::vector<std::wstring>& names, REFIID iid, void** object) {
    *object = nullptr;
    ComPtr<IShellFolder2> folder = bindRecycleBin();
    if (!folder) return E_FAIL;

    auto pidls = resolvePidls(folder.get(), names);
    if (pidls.empty()) return E_FAIL;

    std::vector<PCUITEMID_CHILD> apidl;
    apidl.reserve(pidls.size());
    for (const auto& p : pidls) apidl.push_back(p.get());

    return folder->GetUIObjectOf(owner, static_cast<UINT>(apidl.size()), apidl.data(), iid, nullptr, object);
}

bool deleteItemsPermanently(HWND owner, const std::vector<std::wstring>& names) {
    ComPtr<IShellFolder2> folder = bindRecycleBin();
    if (!folder) return false;
    return invokeVerb(owner, folder.get(), resolvePidls(folder.get(), names), L"delete");
}

bool restoreItems(HWND owner, const std::vector<std::wstring>& recycledFiles) {
    ComPtr<IShellFolder2> folder = bindRecycleBin();
    if (!folder) return false;
    // An entry's for-parsing name is the path of its $R storage file.
    return invokeVerb(owner, folder.get(), resolvePidls(folder.get(), recycledFiles, SHGDN_FORPARSING), L"undelete");
}

bool emptyRecycleBin(HWND owner) {
    return SHEmptyRecycleBinW(owner, nullptr, 0) == S_OK;
}

}  // namespace RecycleBinOps
