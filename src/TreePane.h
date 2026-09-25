#pragma once

#include "FileDropTarget.h"
#include "Strings.h"

#include <windows.h>
#include <commctrl.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Result of a background child-folder enumeration, posted to MainWindow
// via WM_APP_TREE_CHILDREN (lParam). Receiver owns it.
struct TreeChildrenResult {
    HTREEITEM item = nullptr;
    std::vector<std::pair<std::wstring, std::wstring>> children;  // name, full path
};

// Wraps a WC_TREEVIEW used purely for navigation (Desktop / user folders /
// This PC / drives). Lazily populated: a directory node gets a single
// placeholder child when created, and its real children are fetched only
// when the node is first expanded (TVN_ITEMEXPANDING) - on a background
// thread, same as FilePane/DirectoryModel, so expanding a slow (e.g.
// network) location doesn't block the UI thread.
class TreePane {
public:
    bool create(HWND parent, HINSTANCE hInstance, int controlId);
    HWND hwnd() const { return hwnd_; }

    // Handles WM_NOTIFY messages targeted at this tree. Returns a value
    // suitable for the WndProc's LRESULT when handled.
    LRESULT handleNotify(NMHDR* nmhdr);

    // Called by MainWindow once it receives WM_APP_TREE_CHILDREN.
    void handleChildrenResult(std::unique_ptr<TreeChildrenResult> result);

    // Best-effort selection sync when a file pane navigates somewhere.
    // Only walks nodes that are already expanded/loaded - never forces
    // enumeration of the whole tree just to find a match.
    void trySelectPath(const std::wstring& path);

    // Called when the user clicks or presses Enter on a directory node.
    std::function<void(const std::wstring& path)> onNavigate;

    // Middle-click on a node: open it in a new background tab.
    std::function<void(const std::wstring& path)> onOpenInNewTab;
    void openItemInNewTab(POINT clientPt);  // called from the tree's middle-button subclass

    // Context-menu support: the node under a screen point (on its label or
    // icon, else null), a node's path (empty for none/placeholders), and
    // dropping a node whose folder no longer exists after a shell command.
    HTREEITEM itemAtScreenPoint(POINT screenPt) const;
    std::wstring pathOf(HTREEITEM item) const;
    void removeIfGone(HTREEITEM item);

    // Re-applies the current language's text to the fixed root labels
    // (Desktop, Documents, ... Recycle Bin) after Tools > Options changes
    // the language - everything else in the tree is a real folder name,
    // which is never translated.
    void retranslate();

private:
    struct NodeData {
        std::wstring path;
        bool isDummy = false;
        bool childrenLoaded = false;
    };

    void addRootItems();
    HTREEITEM addNode(HTREEITEM parent, const std::wstring& text, const std::wstring& path, bool likelyHasChildren);
    void populateChildren(HTREEITEM item);
    NodeData* dataOf(HTREEITEM item) const;
    void navigateFromItem(HTREEITEM item);

    // OLE drop target hit-test (see FileDropTarget); also auto-expands a
    // node the drag has hovered over for kDropExpandDelayMs.
    FileDropTarget::Hit dropHitTest(POINT pt);
    static constexpr ULONGLONG kDropExpandDelayMs = 800;
    HTREEITEM dropHoverItem_ = nullptr;
    ULONGLONG dropHoverSince_ = 0;

    static void enumerateChildrenWorker(std::wstring path, HTREEITEM item, HWND notifyWnd);

    HWND hwnd_ = nullptr;
    HWND parentWnd_ = nullptr;

    // Maps a lower-cased path to the node for it, maintained alongside
    // addNode()/TVN_DELETEITEMW so trySelectPath() (called on every
    // navigation) doesn't need to walk the whole tree looking for a
    // match - paths are Windows-path-case-insensitive, hence lower-casing
    // the key rather than using _wcsicmp per node during a walk.
    std::unordered_map<std::wstring, HTREEITEM> pathIndex_;

    // Root nodes with a translated (not a real-folder-name) label, so
    // retranslate() knows which items to relabel and with what.
    std::vector<std::pair<HTREEITEM, StringId>> translatedRoots_;
};
