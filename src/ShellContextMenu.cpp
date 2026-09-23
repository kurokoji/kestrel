#include "ShellContextMenu.h"
#include "ComPtr.h"
#include "ShellSelection.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

// Shell commands occupy ids [kShellFirst, kShellLast]; our own extra items
// are numbered from kOwnFirst so the two never clash.
constexpr UINT kShellFirst = 1;
constexpr UINT kShellLast = 0x7FFF;
constexpr UINT kOwnFirst = 0x8000;

bool shiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

ComPtr<IContextMenu> bindItemHandler(const std::wstring& path, REFGUID handler) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.addressOf())))) return {};
    ComPtr<IContextMenu> menu;
    item->BindToHandler(nullptr, handler, IID_PPV_ARGS(menu.addressOf()));
    return menu;
}

// Position of the shell's "properties" command in `hMenu`, moved up past
// the separator above it so inserted items join the group before it.
// Returns the item count (i.e. the bottom) if there is none.
int positionBeforeProperties(HMENU hMenu, IContextMenu* menu) {
    const int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i) {
        const UINT id = GetMenuItemID(hMenu, i);
        if (id < kShellFirst || id > kShellLast) continue;
        wchar_t verb[64] = L"";
        if (FAILED(menu->GetCommandString(id - kShellFirst, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb),
                                          ARRAYSIZE(verb))) ||
            _wcsicmp(verb, L"properties") != 0) {
            continue;
        }
        MENUITEMINFOW info{sizeof(info), MIIM_FTYPE};
        if (i > 0 && GetMenuItemInfoW(hMenu, i - 1, TRUE, &info) && (info.fType & MFT_SEPARATOR)) return i - 1;
        return i;
    }
    return count;
}

}  // namespace

ComPtr<IContextMenu> ShellContextMenu::menuForPaths(HWND owner, const std::vector<std::wstring>& paths) {
    return ShellSelection::get<IContextMenu>(owner, paths);
}

ComPtr<IContextMenu> ShellContextMenu::menuForItem(const std::wstring& path) {
    return bindItemHandler(path, BHID_SFUIObject);
}

ComPtr<IContextMenu> ShellContextMenu::menuForBackground(const std::wstring& folderPath) {
    return bindItemHandler(folderPath, BHID_SFViewObject);
}

ShellContextMenu::Result ShellContextMenu::show(HWND owner, const ComPtr<IContextMenu>& menu, POINT screenPt,
                                                const std::vector<OwnItem>& top,
                                                const std::vector<OwnItem>& beforeProperties) {
    if (!menu && top.empty() && beforeProperties.empty()) return {};

    HMENU hMenu = CreatePopupMenu();

    // Shift+right-click adds the extended verbs ("パスとしてコピー", ...),
    // exactly as Explorer does.
    const bool extended = shiftDown();
    ComPtr<IContextMenu3> menu3;
    bool hasShellItems = false;
    if (menu) {
        const UINT flags = CMF_NORMAL | (extended ? CMF_EXTENDEDVERBS : 0);
        hasShellItems = SUCCEEDED(menu->QueryContextMenu(hMenu, 0, kShellFirst, kShellLast, flags));
        menu->QueryInterface(IID_PPV_ARGS(menu3.addressOf()));
    }
    if (!hasShellItems && top.empty() && beforeProperties.empty()) {
        DestroyMenu(hMenu);
        return {};
    }

    // Own items are ids kOwnFirst.. in `top` then `beforeProperties` order.
    // Inserted after the shell's items, not before: the shell tidies
    // separators around what it adds and dropped ours when it came first.
    UINT ownId = kOwnFirst + static_cast<UINT>(top.size());
    if (!beforeProperties.empty()) {
        UINT pos = static_cast<UINT>(hasShellItems ? positionBeforeProperties(hMenu, menu.get()) : GetMenuItemCount(hMenu));
        for (const auto& item : beforeProperties) {
            InsertMenuW(hMenu, pos++, MF_BYPOSITION | MF_STRING, ownId++, item.label.c_str());
        }
    }
    if (GetMenuItemCount(hMenu) > 0 && !top.empty()) InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    for (size_t i = top.size(); i-- > 0;) {
        InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, kOwnFirst + static_cast<UINT>(i), top[i].label.c_str());
    }

    // Forwarded to via forwardMenuMessage() while TrackPopupMenu's nested
    // loop runs.
    activeMenu_ = menu3.get();
    const UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, owner, nullptr);
    activeMenu_ = nullptr;

    Result result;
    const size_t ownCount = top.size() + beforeProperties.size();
    if (cmd >= kOwnFirst && cmd < kOwnFirst + ownCount) {
        const size_t i = cmd - kOwnFirst;
        result.ownCommand = i < top.size() ? top[i].id : beforeProperties[i - top.size()].id;
    } else if (cmd >= kShellFirst && cmd <= kShellLast && menu) {
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | (extended ? CMIC_MASK_SHIFT_DOWN : 0);
        info.hwnd = owner;
        info.lpVerb = MAKEINTRESOURCEA(cmd - kShellFirst);
        info.lpVerbW = MAKEINTRESOURCEW(cmd - kShellFirst);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = screenPt;
        menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        result.shellInvoked = true;
    }

    DestroyMenu(hMenu);
    return result;
}

bool ShellContextMenu::showAndInvoke(HWND owner, ComPtr<IContextMenu> menu, POINT screenPt) {
    return show(owner, menu, screenPt).shellInvoked;
}

bool ShellContextMenu::forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (!activeMenu_) return false;
    if (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && msg != WM_MENUCHAR) return false;
    result = 0;
    return SUCCEEDED(activeMenu_->HandleMenuMsg2(msg, wParam, lParam, &result));
}
