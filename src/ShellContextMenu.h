#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct IContextMenu3;  // avoids pulling <shobjidl.h> into every includer of this header

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

    // Forward from the owner's wndProc while showAndInvoke's TrackPopupMenu
    // nested loop is running (only WM_INITMENUPOPUP/WM_DRAWITEM/
    // WM_MEASUREITEM/WM_MENUCHAR are relevant). Sets `result` and returns
    // true if the message was handled by the tracked menu.
    bool forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
    // Non-owning; set only while TrackPopupMenu is running.
    IContextMenu3* activeMenu_ = nullptr;
};
