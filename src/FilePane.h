#pragma once

#include "DirectoryModel.h"
#include "DirectoryWatcher.h"
#include "FileDropTarget.h"
#include "Types.h"
#include "Undo.h"

#include <windows.h>
#include <array>
#include <commctrl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One file listing pane: a details-mode, owner-data ListView backed by a
// DirectoryModel, with a self-drawn tab strip above it (a plain child
// window, not SysTabControl32 - see FilePaneTabs.cpp for why) so the pane
// can hold several independent locations at once. Owner-data
// (LVS_OWNERDATA) is used deliberately so that directories with huge
// numbers of entries don't pay the cost of building one ListView item per
// file - the control only ever asks us (via LVN_GETDISPINFO) for the rows
// it is about to paint.
//
// Only the active tab's state (path/entries/history/sort) lives in the
// live_ at any moment; other tabs' state sits saved in
// tabs_. Switching tabs copies the outgoing tab's live state into tabs_
// and the incoming tab's saved state into live_, rather than
// keeping N separate ListViews around.
class FilePane {
public:
    struct Stats {
        size_t fileCount = 0;
        size_t dirCount = 0;
        size_t selectedCount = 0;
        uint64_t selectedSize = 0;
    };

    bool create(HWND parent, HINSTANCE hInstance, int controlId, int paneId);
    HWND hwnd() const { return hwnd_; }
    HWND tabHwnd() const { return tabHwnd_; }
    HWND newTabButtonHwnd() const { return newTabButton_; }
    HWND searchBoxHwnd() const { return searchBox_; }
    HWND emptyRecycleBinButtonHwnd() const { return emptyRecycleBinButton_; }
    int paneId() const { return paneId_; }

    // Makes this the active pane by moving keyboard focus to its list
    // (which is what onFocusChanged/NM_SETFOCUS actually keys off of).
    // Called when clicking the tab strip - tabs, empty tab-strip space, or
    // the "+" button - of a pane that isn't currently active, so it
    // doesn't take a click inside the file list itself to switch panes.
    void activate() { SetFocus(hwnd_); }

    // Positions the tab strip and the ListView within `outer` (the pane's
    // allocated rect, already inset for the active-pane highlight frame).
    // Does not repaint: the caller must redraw the parent and children
    // after completing the layout.
    void setBounds(const RECT& outer);

    // Called by the tab strip's mouse-move/leave subclass so the close
    // glyph under the cursor can be highlighted like a real button.
    void setHoveredCloseTab(int index);

    const std::wstring& currentPath() const { return live_.path; }
    const Stats& stats() const { return live_.stats; }

    void navigate(std::wstring path, bool addToHistory);
    void refresh();
    void goBack();
    void goForward();
    void goUp();
    bool canGoBack() const { return !live_.back.empty(); }
    bool canGoForward() const { return !live_.forward.empty(); }

    void newTab();
    // Middle-click on a folder: a new tab right after the active one,
    // left in the background (it loads when first shown).
    void openInBackgroundTab(const std::wstring& path);
    void openRowInBackgroundTab(int index);  // no-op unless the row is a real folder
    void closeTab(int index = -1);  // -1 = the active tab; a no-op if it's the only one left
    void cycleTab(bool forward);  // Ctrl+Tab / Ctrl+Shift+Tab; wraps, no-op with one tab

    // Session persistence: the paths of every open tab (active tab first
    // synced so its path is current) and which one is active, for saving
    // on exit; and replacing the pane's tabs wholesale with a restored
    // set on startup (each tab's contents load lazily, on first visit).
    std::vector<std::wstring> tabPaths();
    int activeTabIndex() const { return activeTab_; }
    void restoreTabs(const std::vector<std::wstring>& paths, int activeIndex);

    // Name/Type/Size/Modified column widths - one ListView per pane
    // shared across all its tabs, so this isn't per-tab state.
    std::array<int, 4> columnWidths() const;
    void setColumnWidths(const std::array<int, 4>& widths);

    // Sort column/direction newly created tabs start out with (Tools >
    // Options). Shared process-wide (both panes' new tabs use the same
    // default) rather than per-pane; does not touch already-open tabs'
    // own sort state. `column` matches the ListView order: 0=Name,
    // 1=Type, 2=Size, 3=Modified.
    static void setDefaultSort(int column, bool ascending);

    // View > 隠しファイル, shared by both panes. Call reloadAllTabs() on
    // each pane after changing it so cached tabs don't keep the old view.
    static void setShowHidden(bool show) { showHidden_ = show; }
    void reloadAllTabs();

    // Called by MainWindow once it receives WM_APP_DIR_RESULT addressed to
    // this pane (the pane pointer travels as the message's wParam).
    void handleDirResult(std::unique_ptr<EnumerationResult> result);

    LRESULT handleNotify(NMHDR* nmhdr);

    std::vector<std::wstring> selectedPaths() const;

    // Selected entries' bare names (FileEntry::name), not full paths - what
    // RecycleBinOps needs, since entries in that view aren't real paths.
    std::vector<std::wstring> selectedNames() const;

    // Path of the single item currently carrying the keyboard focus
    // rectangle, or empty if there is none (used to feed the preview
    // pane). Not the same as the selection - focus and selection can
    // differ, and a preview only ever shows one item anyway.
    std::wstring focusedItemPath() const;

    // Pane-local file operations - these never need to know about the
    // other pane, unlike copy/move which MainWindow drives directly.
    void doRename();
    void doDelete(bool permanent = false);  // permanent = Shift+Delete (skip the Recycle Bin)
    // Recycle Bin view only - empties it entirely (the shell shows its own
    // confirmation). A no-op elsewhere. Restoring items is right-click only
    // (MainWindow::onContextMenu) - it goes through the Recycle Bin's real
    // shell menu rather than a verb we'd have to guess/match ourselves.
    void emptyRecycleBin();
    void doMkdir();  // creates "新しいフォルダー" and starts renaming it in place
    void showProperties();  // Alt+Enter: selection, or the shown folder when nothing is selected
    void doView();
    void doEdit();
    void openFocusedOrSelected();  // same as double-click: navigate into a folder, or open a file

    bool hasSelection() const { return ListView_GetSelectedCount(hwnd_) > 0; }

    // "Cut" visual marking (Explorer-style dimmed icon) for items awaiting
    // a move-paste. Paths are matched case-insensitively against this
    // pane's own entries at paint time - MainWindow owns clearing this on
    // copy/paste so it never lingers once the clipboard's contents move on.
    void setCutPaths(std::vector<std::wstring> paths);
    void clearCutPaths();

    // Called by MainWindow right after a cross-pane/cross-tab move so the
    // *source* folder's listing doesn't go stale. watcher_ is one instance
    // per pane, re-armed only on navigate() - a tab holding a cached
    // (non-live, or live-but-not-this-pane) listing of that source folder
    // never gets re-armed on the file's actual removal, so it would
    // otherwise keep showing the moved-away item until something else
    // happens to touch that folder. Clears any tab's cached entries for
    // `path` (forcing re-enumeration next time it's switched to) and, if
    // `path` is this pane's live tab, refreshes it immediately.
    void invalidateTabsMatchingPath(const std::wstring& path);

    // Right-click context-menu support: if `pt` (client coords) lands on
    // an item that ISN'T already part of the selection, replace the
    // selection with just that item (matches Explorer); right-clicking an
    // already-selected item leaves a multi-selection intact.
    void selectSingleItemAtClientPoint(POINT pt);

    // Incremental, non-recursive name search over the current directory's
    // listing only. Nothing is hidden - matching rows are just
    // highlighted (custom-drawn), so the list's indices/order never
    // change while searching. Shows/focuses the search box (a no-op if
    // already shown); Escape or closeSearch() hides it again and clears
    // the highlight. Called by MainWindow, which owns re-running
    // layoutChildren() afterward (via onSearchVisibilityChanged) since the
    // box takes a row of vertical space while open.
    void toggleSearch();
    void closeSearch();
    bool isSearchVisible() const { return searchVisible_; }
    void onSearchTextChanged();  // MainWindow calls this on EN_CHANGE from searchBox_
    void jumpToNextMatch();  // MainWindow calls this on Enter in searchBox_; cycles forward, wrapping around

    // A file operation this pane performed (rename, new folder, delete to
    // the Recycle Bin) that Edit > 元に戻す can reverse.
    std::function<void(Undo::Record)> onUndoable;

    std::function<void(FilePane&)> onNavigated;
    std::function<void(FilePane&)> onSelectionChanged;
    std::function<void(FilePane&)> onFocusChanged;
    std::function<void()> onSearchVisibilityChanged;

    // Fired after a tab is added, removed, or the strip's own width
    // changes how many rows the tabs wrap onto (see computeTabLayout),
    // so the number of rows (and therefore how much height setBounds()
    // needs to give it) can change. MainWindow re-runs layoutChildren()
    // on this same as it does for onSearchVisibilityChanged.
    std::function<void()> onTabCountChanged;

    // Fired when navigation crosses into or out of the Recycle Bin view -
    // i.e. exactly when the "空にする" button's shown/hidden state changes,
    // not on every navigation - so MainWindow knows to re-run
    // layoutChildren() (the button takes a row of space like the search
    // box does).
    std::function<void()> onEmptyButtonVisibilityChanged;

private:
    // Shared by the live tab and saved tabs; copy as a unit on switches.
    struct TabContent {
        std::wstring path;
        std::vector<FileEntry> entries;
        Stats stats;
        std::vector<std::wstring> back;
        std::vector<std::wstring> forward;
        int sortColumn = 0;
        bool sortAscending = true;
    };

    struct TabState {
        TabContent content;

        // Selection/scroll position, so switching tabs and back doesn't
        // look like the selection got cleared and the view jumped to the
        // top. Indices into `content.entries` - safe to reuse directly on
        // restore since a background (inactive) tab's DirectoryWatcher
        // isn't running, so entries can't have changed shape underneath it
        // while it wasn't the live tab.
        std::vector<int> selectedIndices;
        int focusedIndex = -1;
        int topIndex = 0;
    };

    void applyEntries(std::vector<FileEntry> entries);
    void sortEntries();
    void updateSortArrow();  // ▲/▼ on the live tab's sort column header
    void recomputeSelectionStats();
    void activateEntry(int index);
    std::wstring pathForIndex(int index) const;
    bool matchesSearch(const FileEntry& e) const;

    // OLE drop target callbacks (see FileDropTarget): what's under a client
    // point, and which row to paint as the drop destination (-1 = none).
    FileDropTarget::Hit dropHitTest(POINT pt) const;
    void setDropHighlight(int index);

    // Same for the tab strip: dropping on a tab puts the items in that
    // tab's folder, and hovering there for a moment switches to it.
    FileDropTarget::Hit tabDropHitTest(POINT pt);
    void setTabDropHighlight(int index);
    const std::wstring& tabPathAt(int index) const;
    static constexpr ULONGLONG kTabDropSwitchDelayMs = 600;
    int tabDropHoverIndex_ = -1;   // tab framed as the drop target by drawTabItem
    int tabDropTimingIndex_ = -1;  // tab the cursor rests on, for the hover-to-switch delay
    ULONGLONG tabDropHoverSince_ = 0;
    ULONGLONG tabDropLastHit_ = 0;

    // Starts the in-place rename doMkdir() queued, once a refresh lists
    // the new folder (called from handleDirResult).
    void beginPendingRename();

    // Tab state, layout, drawing and mouse handling are implemented in
    // FilePaneTabs.cpp.
    static LRESULT CALLBACK TabStripSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                 UINT_PTR id, DWORD_PTR refData);
    void syncActiveTabIntoStorage();
    void loadTabIntoLive(int index);
    void switchToTab(int index);
    void moveTab(int from, int to);

    // Tab strip layout: fixed-width tabs packed left-to-right, wrapping
    // to a new row once they no longer fit (same visual result as the
    // old TCS_MULTILINE, just computed ourselves - see FilePaneTabs.cpp
    // for why comctl32's own version of this had to go). relayoutTabs()
    // recomputes tabRects_ from tabs_.size() and tabStripWidth_ and
    // fires onTabCountChanged if the row count changed; call it after
    // anything that changes either one.
    void relayoutTabs();
    std::vector<RECT> computeTabLayout(int width) const;
    int tabRowCount() const;
    int hitTestTab(POINT pt) const;  // exact only; -1 if pt isn't over any tab
    int hitTestTabApprox(POINT pt) const;  // falls back to nearest tab by distance
    void drawTabItem(HDC hdc, const RECT& r, int index);

    // Tab-reorder drag visuals: a small layered popup showing a snapshot
    // of the tab being dragged, so it visibly follows the cursor while
    // moveTab() does the real (instant, no-animation) reordering
    // underneath. All in FilePaneTabs.cpp alongside the rest of the drag
    // handling in TabStripSubclassProc.
    void beginTabDrag(int index, POINT clientPt);
    void updateTabDragGhost(POINT clientPt);
    void endTabDrag();

    HWND hwnd_ = nullptr;
    HWND tabHwnd_ = nullptr;
    // tabHwnd_ is a plain window (DefWindowProc), which - unlike a real
    // control - doesn't track its own WM_SETFONT/WM_GETFONT; without
    // this, drawTabItem's WM_GETFONT query always came back empty and
    // the strip silently fell back to whatever HDC default font, never
    // picking up the user's chosen UI font. Set from WM_SETFONT in
    // TabStripSubclassProc, read directly by drawTabItem.
    HFONT tabFont_ = nullptr;
    std::vector<RECT> tabRects_;  // client-coordinate rect per tab, in tabs_ order; rebuilt by relayoutTabs()
    int tabStripWidth_ = 0;  // set by setBounds(); relayoutTabs() wraps tabs within this width
    int hoveredCloseTab_ = -1;  // -1 = no tab's close glyph is under the cursor
    int dragTabIndex_ = -1;  // -1 = no drag in progress; the tab currently being dragged, tracked as it moves
    bool dragActive_ = false;  // true once the press has moved past the drag threshold (vs. a plain click)
    POINT dragStartPt_{};  // client point of the WM_LBUTTONDOWN that might become a drag
    HWND dragGhost_ = nullptr;  // layered popup showing the dragged tab; lazily created on first drag
    HBITMAP dragGhostBitmap_ = nullptr;  // snapshot of the tab at drag start; owned, freed in endTabDrag
    POINT dragGhostOffset_{};  // grab point relative to the tab's own top-left, so the ghost doesn't jump under the cursor
    HWND newTabButton_ = nullptr;
    HWND searchBox_ = nullptr;
    // Shown only while currentPath() == kRecycleBinPath - see setBounds().
    HWND emptyRecycleBinButton_ = nullptr;
    bool searchVisible_ = false;
    std::wstring searchQuery_;  // lowercased; empty = no active search
    int currentMatchIndex_ = -1;  // custom-drawn blue (not just the yellow match color) regardless of list focus
    std::wstring pendingRenameDir_;   // folder doMkdir() created pendingRenameName_ in
    std::wstring pendingRenameName_;  // empty = no rename waiting for the next listing
    int dropHighlightIndex_ = -1;  // folder row under an incoming drag; custom-drawn like currentMatchIndex_
    HWND parentWnd_ = nullptr;
    int paneId_ = 0;

    DirectoryModel model_;
    DirectoryWatcher watcher_;
    uint64_t pendingRequestId_ = 0;
    std::wstring pendingNavPath_;
    std::wstring pendingPrevPath_;
    bool pendingAddToHistory_ = false;

    TabContent live_;

    std::vector<TabState> tabs_;
    int activeTab_ = 0;

    std::vector<std::wstring> cutPaths_;  // lowercased full paths

    static inline int defaultSortColumn_ = 0;
    static inline bool defaultSortAscending_ = true;
    static inline bool showHidden_ = true;
};
