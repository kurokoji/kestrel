#pragma once

#include "ComPtr.h"

#include <windows.h>
#include <string>
#include <vector>

struct IContextMenu;   // avoids pulling <shobjidl.h> into every includer of this header
struct IContextMenu3;

// Wraps a real shell context menu (IContextMenu::QueryContextMenu /
// TrackPopupMenu / InvokeCommand), plus the WM_INITMENUPOPUP/WM_DRAWITEM/
// WM_MEASUREITEM/WM_MENUCHAR forwarding a tracked menu needs for submenus
// (e.g. "Send to") and icons to render.
class ShellContextMenu {
public:
    // The shell menu for a selection (all in the same parent folder, as
    // with a single pane's selection - same PIDL binding as outbound
    // drag-and-drop, see ShellSelection).
    static ComPtr<IContextMenu> menuForPaths(HWND owner, const std::vector<std::wstring>& paths);
    // For one item given by any parsing name, including drive roots and
    // "::{CLSID}" locations (tree nodes).
    static ComPtr<IContextMenu> menuForItem(const std::wstring& path);
    // A folder's own background menu (New, Properties, "Open in Terminal",
    // ...), i.e. IShellFolder::CreateViewObject.
    static ComPtr<IContextMenu> menuForBackground(const std::wstring& folderPath);

    // One of our own commands shown alongside the shell's items; `id` is
    // what Result::ownCommand reports back.
    struct OwnItem {
        UINT id;
        std::wstring label;
    };
    struct Result {
        bool shellInvoked = false;  // a shell command ran (caller should refresh)
        UINT ownCommand = 0;        // an OwnItem::id was picked instead
    };

    // Tracks `menu` at `screenPt` with `top` items above the shell's and
    // `beforeProperties` just above its プロパティ group (where Explorer
    // has 名前の変更). Shift held adds the extended verbs. Returns an empty
    // Result if nothing was chosen or there is no menu and no own items.
    Result show(HWND owner, const ComPtr<IContextMenu>& menu, POINT screenPt, const std::vector<OwnItem>& top = {},
                const std::vector<OwnItem>& beforeProperties = {});

    // Shell commands only - e.g. RecycleBinOps::get<IContextMenu>, whose
    // items aren't real paths. Returns true if a command ran.
    bool showAndInvoke(HWND owner, ComPtr<IContextMenu> menu, POINT screenPt);

    // Forward from the owner's wndProc while show()'s TrackPopupMenu
    // nested loop is running (only WM_INITMENUPOPUP/WM_DRAWITEM/
    // WM_MEASUREITEM/WM_MENUCHAR are relevant). Sets `result` and returns
    // true if the message was handled by the tracked menu.
    bool forwardMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
    // Non-owning; set only while TrackPopupMenu is running.
    IContextMenu3* activeMenu_ = nullptr;
};
