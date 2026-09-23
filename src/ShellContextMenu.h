#pragma once

#include "ComPtr.h"

#include <windows.h>
#include <string>
#include <vector>

struct IContextMenu;   // avoids pulling <shobjidl.h> into every includer of this header
struct IContextMenu3;

// Wraps a real shell context menu (IContextMenu::QueryContextMenu /
// TrackPopupMenu / InvokeCommand) for a right-clicked selection, plus the
// WM_INITMENUPOPUP/WM_DRAWITEM/WM_MEASUREITEM/WM_MENUCHAR forwarding a
// tracked menu needs for submenus (e.g. "Send to") and icons to render -
// same PIDL-binding pattern as outbound drag-and-drop (ShellSelection).
class ShellContextMenu {
public:
    // Builds and tracks the menu for `paths` (all in the same parent
    // folder, as with a single pane's selection) at `screenPt`. Returns
    // true if a command was actually invoked (caller should refresh the
    // pane), false if nothing matched the selection or the menu was
    // dismissed without a choice.
    bool showAndInvoke(HWND owner, const std::vector<std::wstring>& paths, POINT screenPt);

    // Same as above, but for a caller that already has an IContextMenu -
    // e.g. RecycleBinOps::get<IContextMenu>, whose items aren't real paths
    // ShellSelection could bind on its own.
    bool showAndInvoke(HWND owner, ComPtr<IContextMenu> menu, POINT screenPt);

    // One of our own commands shown above the shell's items in a
    // background menu; `id` is what Result::ownCommand reports back.
    struct OwnItem {
        UINT id;
        std::wstring label;
    };
    struct Result {
        bool shellInvoked = false;  // a shell command ran (caller should refresh)
        UINT ownCommand = 0;        // an OwnItem::id was picked instead
    };

    // Empty-area right-click: the real shell background menu of
    // `folderPath` (New, Paste, Properties, ...) with `ownItems` on top.
    // Falls back to just `ownItems` if the folder has no such menu.
    Result showBackground(HWND owner, const std::wstring& folderPath, POINT screenPt,
                          const std::vector<OwnItem>& ownItems);

    // Forward from the owner's wndProc while showAndInvoke's TrackPopupMenu
    // nested loop is running (only WM_INITMENUPOPUP/WM_DRAWITEM/
    // WM_MEASUREITEM/WM_MENUCHAR are relevant). Sets `result` and returns
    // true if the message was handled by the tracked menu.
    bool forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
    Result track(HWND owner, const ComPtr<IContextMenu>& menu, POINT screenPt, const std::vector<OwnItem>& ownItems);

    // Non-owning; set only while TrackPopupMenu is running.
    IContextMenu3* activeMenu_ = nullptr;
};
