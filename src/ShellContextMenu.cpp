#include "ShellContextMenu.h"
#include "ComPtr.h"
#include "ShellSelection.h"

#include <shellapi.h>
#include <shobjidl.h>

bool ShellContextMenu::showAndInvoke(HWND owner, const std::vector<std::wstring>& paths, POINT screenPt) {
    ComPtr<IContextMenu> menu = ShellSelection::get<IContextMenu>(owner, paths);
    if (!menu) return false;

    ComPtr<IContextMenu3> menu3;
    menu->QueryInterface(IID_PPV_ARGS(menu3.addressOf()));

    HMENU hMenu = CreatePopupMenu();
    if (FAILED(menu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL))) {
        DestroyMenu(hMenu);
        return false;
    }

    // Forwarded to via forwardMenuMessage() while TrackPopupMenu's nested
    // loop runs.
    activeMenu_ = menu3.get();
    const UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, owner, nullptr);
    activeMenu_ = nullptr;

    bool invoked = false;
    if (cmd != 0) {
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = owner;
        info.lpVerb = MAKEINTRESOURCEA(cmd - 1);
        info.lpVerbW = MAKEINTRESOURCEW(cmd - 1);
        info.nShow = SW_SHOWNORMAL;
        menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        invoked = true;
    }

    DestroyMenu(hMenu);
    return invoked;
}

bool ShellContextMenu::forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result) {
    if (!activeMenu_) return false;
    if (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && msg != WM_MENUCHAR) return false;
    result = 0;
    return SUCCEEDED(activeMenu_->HandleMenuMsg2(msg, wParam, lParam, &result));
}
