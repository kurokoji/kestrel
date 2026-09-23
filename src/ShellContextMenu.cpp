#include "ShellContextMenu.h"
#include "ComPtr.h"
#include "ShellSelection.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

// Shell commands occupy ids [kShellFirst, kShellLast]; our own extra items
// in a background menu are numbered from kOwnFirst so the two never clash.
constexpr UINT kShellFirst = 1;
constexpr UINT kShellLast = 0x7FFF;
constexpr UINT kOwnFirst = 0x8000;

bool shiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

}  // namespace

bool ShellContextMenu::showAndInvoke(HWND owner, const std::vector<std::wstring>& paths, POINT screenPt) {
    return showAndInvoke(owner, ShellSelection::get<IContextMenu>(owner, paths), screenPt);
}

bool ShellContextMenu::showAndInvoke(HWND owner, ComPtr<IContextMenu> menu, POINT screenPt) {
    return track(owner, menu, screenPt, {}).shellInvoked;
}

ShellContextMenu::Result ShellContextMenu::showBackground(HWND owner, const std::wstring& folderPath, POINT screenPt,
                                                          const std::vector<OwnItem>& ownItems) {
    // BHID_SFViewObject = IShellFolder::CreateViewObject, i.e. the folder's
    // own background menu (New, Paste, Properties, "Open in Terminal", ...).
    ComPtr<IContextMenu> menu;
    ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(folderPath.c_str(), nullptr, IID_PPV_ARGS(item.addressOf())))) {
        item->BindToHandler(nullptr, BHID_SFViewObject, IID_PPV_ARGS(menu.addressOf()));
    }
    return track(owner, menu, screenPt, ownItems);
}

ShellContextMenu::Result ShellContextMenu::track(HWND owner, const ComPtr<IContextMenu>& menu, POINT screenPt,
                                                 const std::vector<OwnItem>& ownItems) {
    if (!menu && ownItems.empty()) return {};

    HMENU hMenu = CreatePopupMenu();

    // Shift+right-click adds the extended verbs ("パスとしてコピー", ...),
    // exactly as Explorer does.
    const bool extended = shiftDown();
    ComPtr<IContextMenu3> menu3;
    bool hasShellItems = false;
    if (menu) {
        const UINT flags = CMF_NORMAL | (extended ? CMF_EXTENDEDVERBS : 0);
        hasShellItems = SUCCEEDED(menu->QueryContextMenu(hMenu, 0, kShellFirst, kShellLast, flags));
        if (!hasShellItems && ownItems.empty()) {
            DestroyMenu(hMenu);
            return {};
        }
        menu->QueryInterface(IID_PPV_ARGS(menu3.addressOf()));
    }

    // Inserted after the shell's items, not before: the shell tidies
    // separators around what it adds and dropped ours when it came first.
    if (hasShellItems && !ownItems.empty()) InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    for (size_t i = ownItems.size(); i-- > 0;) {
        InsertMenuW(hMenu, 0, MF_BYPOSITION | MF_STRING, kOwnFirst + static_cast<UINT>(i), ownItems[i].label.c_str());
    }

    // Forwarded to via forwardMenuMessage() while TrackPopupMenu's nested
    // loop runs.
    activeMenu_ = menu3.get();
    const UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, owner, nullptr);
    activeMenu_ = nullptr;

    Result result;
    if (cmd >= kOwnFirst && cmd < kOwnFirst + ownItems.size()) {
        result.ownCommand = ownItems[cmd - kOwnFirst].id;
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

bool ShellContextMenu::forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (!activeMenu_) return false;
    if (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && msg != WM_MENUCHAR) return false;
    result = 0;
    return SUCCEEDED(activeMenu_->HandleMenuMsg2(msg, wParam, lParam, &result));
}
