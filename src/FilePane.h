#pragma once

#include "DirectoryModel.h"
#include "DirectoryWatcher.h"
#include "Types.h"

#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One file listing pane: a details-mode, owner-data ListView backed by a
// DirectoryModel, with a native tab strip above it (SysTabControl32) so
// the pane can hold several independent locations at once. Owner-data
// (LVS_OWNERDATA) is used deliberately so that directories with huge
// numbers of entries don't pay the cost of building one ListView item per
// file - the control only ever asks us (via LVN_GETDISPINFO) for the rows
// it is about to paint.
//
// Only the active tab's state (path/entries/history/sort) lives in the
// "live" members below at any moment; other tabs' state sits saved in
// tabs_. Switching tabs copies the outgoing tab's live state into tabs_
// and the incoming tab's saved state into the live members, rather than
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
    int paneId() const { return paneId_; }

    // Makes this the active pane by moving keyboard focus to its list
    // (which is what onFocusChanged/NM_SETFOCUS actually keys off of).
    // Called when clicking the tab strip - tabs, empty tab-strip space, or
    // the "+" button - of a pane that isn't currently active, so it
    // doesn't take a click inside the file list itself to switch panes.
    void activate() { SetFocus(hwnd_); }

    // Positions the tab strip and the ListView within `outer` (the pane's
    // allocated rect, already inset for the active-pane highlight frame).
    void setBounds(const RECT& outer);

    // The tab strip is owner-drawn (each tab needs its own close glyph),
    // so MainWindow forwards WM_DRAWITEM for tabHwnd() here.
    void drawTabItem(const DRAWITEMSTRUCT& dis);

    // Called by the tab strip's mouse-move/leave subclass so the close
    // glyph under the cursor can be highlighted like a real button.
    void setHoveredCloseTab(int index);

    const std::wstring& currentPath() const { return currentPath_; }
    const Stats& stats() const { return stats_; }

    void navigate(std::wstring path, bool addToHistory);
    void refresh();
    void goBack();
    void goForward();
    void goUp();
    bool canGoBack() const { return !back_.empty(); }
    bool canGoForward() const { return !forward_.empty(); }

    void newTab();
    void closeTab(int index = -1);  // -1 = the active tab; a no-op if it's the only one left
    void cycleTab(bool forward);  // Ctrl+Tab / Ctrl+Shift+Tab; wraps, no-op with one tab

    // Session persistence: the paths of every open tab (active tab first
    // synced so its path is current) and which one is active, for saving
    // on exit; and replacing the pane's tabs wholesale with a restored
    // set on startup (each tab's contents load lazily, on first visit).
    std::vector<std::wstring> tabPaths();
    int activeTabIndex() const { return activeTab_; }
    void restoreTabs(const std::vector<std::wstring>& paths, int activeIndex);

    // Called by MainWindow once it receives WM_APP_DIR_RESULT addressed to
    // this pane (the pane pointer travels as the message's wParam).
    void handleDirResult(std::unique_ptr<EnumerationResult> result);

    LRESULT handleNotify(NMHDR* nmhdr);

    std::vector<std::wstring> selectedPaths() const;

    // Path of the single item currently carrying the keyboard focus
    // rectangle, or empty if there is none (used to feed the preview
    // pane). Not the same as the selection - focus and selection can
    // differ, and a preview only ever shows one item anyway.
    std::wstring focusedItemPath() const;

    // Pane-local file operations - these never need to know about the
    // other pane, unlike copy/move which MainWindow drives directly.
    void doRename();
    void doDelete();
    void doMkdir();
    void doView();
    void doEdit();
    void openFocusedOrSelected();  // same as double-click: navigate into a folder, or open a file

    bool hasSelection() const { return ListView_GetSelectedCount(hwnd_) > 0; }

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

    std::function<void(FilePane&)> onNavigated;
    std::function<void(FilePane&)> onSelectionChanged;
    std::function<void(FilePane&)> onFocusChanged;
    std::function<void()> onSearchVisibilityChanged;

private:
    struct TabState {
        std::wstring path;
        std::vector<FileEntry> entries;
        Stats stats;
        std::vector<std::wstring> back;
        std::vector<std::wstring> forward;
        int sortColumn = 0;
        bool sortAscending = true;
    };

    void applyEntries(std::vector<FileEntry> entries);
    void sortEntries();
    void recomputeSelectionStats();
    void activateEntry(int index);
    std::wstring pathForIndex(int index) const;
    bool matchesSearch(const FileEntry& e) const;

    void syncActiveTabIntoStorage();
    void loadTabIntoLive(int index);
    void switchToTab(int index);
    void updateActiveTabLabel();
    LRESULT handleTabNotify(NMHDR* nmhdr);

    HWND hwnd_ = nullptr;
    HWND tabHwnd_ = nullptr;
    int hoveredCloseTab_ = -1;  // -1 = no tab's close glyph is under the cursor
    HWND newTabButton_ = nullptr;
    HWND searchBox_ = nullptr;
    bool searchVisible_ = false;
    std::wstring searchQuery_;  // lowercased; empty = no active search
    int currentMatchIndex_ = -1;  // custom-drawn blue (not just the yellow match color) regardless of list focus
    HWND parentWnd_ = nullptr;
    int paneId_ = 0;

    DirectoryModel model_;
    DirectoryWatcher watcher_;
    uint64_t pendingRequestId_ = 0;
    std::wstring pendingNavPath_;
    std::wstring pendingPrevPath_;
    bool pendingAddToHistory_ = false;

    std::wstring currentPath_;
    std::vector<FileEntry> entries_;
    Stats stats_;

    std::vector<std::wstring> back_;
    std::vector<std::wstring> forward_;

    int sortColumn_ = 0;
    bool sortAscending_ = true;

    std::vector<TabState> tabs_;
    int activeTab_ = 0;
};
