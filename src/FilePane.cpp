#include "FilePane.h"
#include "Dialogs.h"
#include "FileEntrySort.h"
#include "FileOperations.h"
#include "Formatting.h"
#include "IconCache.h"
#include "TabCycle.h"

#include <windowsx.h>

#include <algorithm>
#include <filesystem>

namespace {

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    std::wstring full = dir;
    if (!full.empty() && full.back() != L'\\') full += L'\\';
    full += name;
    return full;
}

std::wstring tabLabelFor(const std::wstring& path) {
    if (path.empty()) return L"";
    const size_t slash = path.find_last_of(L'\\');
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return name.empty() ? path : name;  // e.g. "C:\" has nothing after its trailing slash
}

constexpr int kTabStripHeight = 22;
constexpr int kCloseGlyphSize = 12;
constexpr int kCloseGlyphMargin = 4;

// Shared by both drawing and hit-testing so the clickable area always
// matches exactly what's painted.
RECT closeButtonRectFor(const RECT& tabRect) {
    RECT r;
    r.right = tabRect.right - kCloseGlyphMargin;
    r.left = r.right - kCloseGlyphSize;
    r.top = tabRect.top + (tabRect.bottom - tabRect.top - kCloseGlyphSize) / 2;
    r.bottom = r.top + kCloseGlyphSize;
    return r;
}

// A couple pixels larger than the glyph itself so the hover highlight
// reads as a real button rather than hugging the × exactly.
RECT closeHoverRectFor(const RECT& tabRect) {
    RECT r = closeButtonRectFor(tabRect);
    InflateRect(&r, 2, 2);
    return r;
}

constexpr COLORREF kActiveTabAccent = RGB(0, 0, 128);  // matches the app icon's navy

// Intercepted on button-DOWN (not click/up) and swallowed when it lands on
// a tab's close glyph, so the tab control never sees the click and never
// changes the selection to the tab that's about to disappear.
LRESULT CALLBACK TabStripSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                       DWORD_PTR refData) {
    auto* pane = reinterpret_cast<FilePane*>(refData);

    if (msg == WM_LBUTTONDOWN) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        TCHITTESTINFO hit{};
        hit.pt = pt;
        const int idx = TabCtrl_HitTest(hwnd, &hit);
        if (idx >= 0) {
            RECT tabRect{};
            TabCtrl_GetItemRect(hwnd, idx, &tabRect);
            RECT closeRect = closeButtonRectFor(tabRect);
            if (PtInRect(&closeRect, pt)) {
                pane->closeTab(idx);
                pane->activate();
                return 0;
            }
        }

        // Let the native control process the click first, then reassert
        // focus onto the file list - this is what makes this the active
        // pane, same as clicking inside its file list would (covers a
        // tab, its close glyph already handled above, and empty
        // tab-strip space). Done on both DOWN and UP defensively, since
        // TCS_FOCUSNEVER's own focus handling isn't documented as tied to
        // one or the other.
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        pane->activate();
        return result;
    } else if (msg == WM_LBUTTONUP) {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        pane->activate();
        return result;
    } else if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);  // re-arm each move; harmless if already tracking

        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        TCHITTESTINFO hit{};
        hit.pt = pt;
        const int idx = TabCtrl_HitTest(hwnd, &hit);
        int hovered = -1;
        if (idx >= 0) {
            RECT tabRect{};
            TabCtrl_GetItemRect(hwnd, idx, &tabRect);
            RECT hoverRect = closeHoverRectFor(tabRect);
            if (PtInRect(&hoverRect, pt)) hovered = idx;
        }
        pane->setHoveredCloseTab(hovered);
    } else if (msg == WM_MOUSELEAVE) {
        pane->setHoveredCloseTab(-1);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Escape closes the search box (and clears the highlight); Enter jumps to
// the first match. Plain Edit controls don't surface either as a
// notification on their own.
LRESULT CALLBACK SearchBoxSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                        DWORD_PTR refData) {
    if (msg == WM_KEYDOWN) {
        auto* pane = reinterpret_cast<FilePane*>(refData);
        if (wParam == VK_ESCAPE) {
            pane->closeSearch();
            return 0;
        }
        if (wParam == VK_RETURN) {
            pane->jumpToNextMatch();
            return 0;
        }
    }
    // A single-line Edit control beeps on WM_CHAR for control characters
    // it doesn't handle (Enter's '\r', Escape's 0x1B) unless something
    // swallows them first - WM_KEYDOWN above stops our own handling from
    // double-firing, but TranslateMessage still turns the keydown into
    // this WM_CHAR regardless, so it has to be caught here too.
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
}  // namespace

bool FilePane::create(HWND parent, HINSTANCE hInstance, int controlId, int paneId) {
    parentWnd_ = parent;
    paneId_ = paneId;

    // TCS_FOCUSNEVER so clicking a tab doesn't steal keyboard focus away
    // from the list - matches how browser tab strips behave.
    // TCS_OWNERDRAWFIXED so each tab can paint its own close ("x") glyph.
    tabHwnd_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER | TCS_TOOLTIPS | TCS_OWNERDRAWFIXED, 0, 0, 0, 0,
                                parent, nullptr, hInstance, nullptr);
    SendMessageW(tabHwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    TabCtrl_SetMinTabWidth(tabHwnd_, 120);  // room for a readable label plus the close glyph

    newTabButton_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
                                     nullptr, hInstance, nullptr);
    SendMessageW(newTabButton_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Hidden until Ctrl+F; setBounds() only reserves a row for it while
    // searchVisible_ is true.
    searchBox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
                                  nullptr, hInstance, nullptr);
    SendMessageW(searchBox_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(searchBox_, SearchBoxSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // LVS_SHOWSELALWAYS: without it, a list-view's selection highlight
    // isn't just dimmed when the control lacks focus - it's fully hidden
    // (comctl32 default). That happens whenever the *whole app* loses
    // focus (Alt+Tab away), since Windows clears focus on deactivation
    // and restores it on reactivation - so without this style, you can't
    // tell what's selected while another window is in front. With it,
    // unfocused selection still renders gray, which conveniently doubles
    // as the active-pane indicator (blue focused / gray unfocused) on top
    // of the custom-painted frame.
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_EDITLABELS | LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                             hInstance, nullptr);
    if (!hwnd_ || !tabHwnd_) return false;

    ListView_SetExtendedListViewStyle(hwnd_, LVS_EX_FULLROWSELECT);
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    if (HIMAGELIST himl = IconCache::instance().systemImageList()) {
        ListView_SetImageList(hwnd_, himl, LVSIL_SMALL);
    }

    struct ColSpec { const wchar_t* text; int width; };
    static constexpr ColSpec cols[] = {
        {L"名前", 220}, {L"種類", 70}, {L"サイズ", 80}, {L"更新日時", 130},
    };
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(cols[i].text);
        col.cx = cols[i].width;
        col.iSubItem = i;
        ListView_InsertColumn(hwnd_, i, &col);
    }

    tabs_.push_back(TabState{});
    activeTab_ = 0;
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(L"");
    TabCtrl_InsertItem(tabHwnd_, 0, &item);
    SetWindowSubclass(tabHwnd_, TabStripSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    return true;
}

void FilePane::setBounds(const RECT& outer) {
    constexpr int kNewTabButtonWidth = 22;
    constexpr int kSearchBoxHeight = 22;
    const int w = outer.right - outer.left;
    const int tabStripW = std::max(0, w - kNewTabButtonWidth);

    MoveWindow(tabHwnd_, outer.left, outer.top, tabStripW, kTabStripHeight, TRUE);
    MoveWindow(newTabButton_, outer.left + tabStripW, outer.top, kNewTabButtonWidth, kTabStripHeight, TRUE);

    int listTop = outer.top + kTabStripHeight;
    if (searchVisible_) {
        MoveWindow(searchBox_, outer.left, listTop, w, kSearchBoxHeight, TRUE);
        listTop += kSearchBoxHeight;
    }

    const int h = std::max(0, static_cast<int>(outer.bottom - listTop));
    MoveWindow(hwnd_, outer.left, listTop, w, h, TRUE);
}

void FilePane::navigate(std::wstring path, bool addToHistory) {
    if (path.size() > 3 && !path.empty() && path.back() == L'\\') path.pop_back();

    pendingNavPath_ = path;
    pendingPrevPath_ = currentPath_;
    pendingAddToHistory_ = addToHistory;
    pendingRequestId_ = model_.requestEnumeration(path, parentWnd_, reinterpret_cast<WPARAM>(this));
}

void FilePane::refresh() {
    if (!currentPath_.empty()) navigate(currentPath_, false);
}

void FilePane::goBack() {
    if (back_.empty()) return;
    forward_.push_back(currentPath_);
    std::wstring target = back_.back();
    back_.pop_back();
    navigate(target, false);
}

void FilePane::goForward() {
    if (forward_.empty()) return;
    back_.push_back(currentPath_);
    std::wstring target = forward_.back();
    forward_.pop_back();
    navigate(target, false);
}

void FilePane::goUp() {
    std::filesystem::path p(currentPath_);
    std::filesystem::path parent = p.parent_path();
    if (parent.empty() || parent == p) return;
    navigate(parent.wstring(), true);
}

void FilePane::handleDirResult(std::unique_ptr<EnumerationResult> result) {
    if (result->requestId != pendingRequestId_) return;  // superseded by a newer navigation

    if (!result->success) {
        std::wstring msg = L"アクセスできません:\n" + result->path;
        Dialogs::showError(parentWnd_, L"Kestrel", msg.c_str());
        return;
    }

    if (pendingAddToHistory_ && !pendingPrevPath_.empty() && pendingPrevPath_ != pendingNavPath_) {
        back_.push_back(pendingPrevPath_);
        forward_.clear();
    }

    currentPath_ = pendingNavPath_;
    applyEntries(std::move(result->entries));
    updateActiveTabLabel();
    watcher_.watch(currentPath_, parentWnd_, reinterpret_cast<WPARAM>(this));

    if (onNavigated) onNavigated(*this);
}

void FilePane::applyEntries(std::vector<FileEntry> entries) {
    entries_ = std::move(entries);
    sortEntries();

    stats_.fileCount = 0;
    stats_.dirCount = 0;
    for (const auto& e : entries_) {
        if (e.isDirectory()) ++stats_.dirCount;
        else ++stats_.fileCount;
    }
    stats_.selectedCount = 0;
    stats_.selectedSize = 0;

    // LVS_OWNERDATA only repaints automatically when the item COUNT
    // changes; if the new folder happens to have the same number of
    // entries as the old one, the control would otherwise keep showing
    // the previous folder's cached rows. Force a repaint unconditionally.
    ListView_SetItemCountEx(hwnd_, static_cast<int>(entries_.size()), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FilePane::sortEntries() {
    FileEntrySort::sort(entries_, sortColumn_, sortAscending_);
}

void FilePane::recomputeSelectionStats() {
    size_t count = 0;
    uint64_t size = 0;
    const int total = static_cast<int>(entries_.size());
    for (int i = 0; i < total; ++i) {
        if (ListView_GetItemState(hwnd_, i, LVIS_SELECTED) & LVIS_SELECTED) {
            ++count;
            if (!entries_[i].isDirectory()) size += entries_[i].size;
        }
    }
    stats_.selectedCount = count;
    stats_.selectedSize = size;
}

std::wstring FilePane::pathForIndex(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= entries_.size()) return L"";
    return joinPath(currentPath_, entries_[index].name);
}

void FilePane::activateEntry(int index) {
    if (index < 0 || static_cast<size_t>(index) >= entries_.size()) return;
    const FileEntry& e = entries_[index];
    if (e.isDirectory()) {
        navigate(joinPath(currentPath_, e.name), true);
    } else {
        FileOperations::openItem(parentWnd_, joinPath(currentPath_, e.name));
    }
}

std::vector<std::wstring> FilePane::selectedPaths() const {
    std::vector<std::wstring> result;
    int idx = -1;
    while ((idx = ListView_GetNextItem(hwnd_, idx, LVNI_SELECTED)) != -1) {
        result.push_back(pathForIndex(idx));
    }
    return result;
}

std::wstring FilePane::focusedItemPath() const {
    const int idx = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    if (idx < 0) return L"";
    return pathForIndex(idx);
}

void FilePane::doRename() {
    int idx = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    if (idx < 0) return;
    SetFocus(hwnd_);
    ListView_EditLabel(hwnd_, idx);
}

void FilePane::doDelete() {
    auto paths = selectedPaths();
    if (paths.empty()) return;
    if (FileOperations::deleteItems(parentWnd_, paths)) {
        refresh();
    }
}

void FilePane::doMkdir() {
    auto name = Dialogs::promptForText(parentWnd_, L"新しいフォルダー", L"フォルダー名:", L"新しいフォルダー");
    if (!name) return;
    if (FileOperations::createDirectory(parentWnd_, currentPath_, *name)) {
        refresh();
    }
}

void FilePane::doView() {
    auto paths = selectedPaths();
    if (!paths.empty()) FileOperations::openItem(parentWnd_, paths.front());
}

void FilePane::doEdit() {
    auto paths = selectedPaths();
    if (!paths.empty()) FileOperations::editItem(parentWnd_, paths.front());
}

void FilePane::openFocusedOrSelected() {
    int idx = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    if (idx < 0) idx = ListView_GetNextItem(hwnd_, -1, LVNI_SELECTED);
    if (idx >= 0) activateEntry(idx);
}

void FilePane::syncActiveTabIntoStorage() {
    if (tabs_.empty()) return;
    TabState& t = tabs_[activeTab_];
    t.path = currentPath_;
    t.entries = entries_;
    t.stats = stats_;
    t.back = back_;
    t.forward = forward_;
    t.sortColumn = sortColumn_;
    t.sortAscending = sortAscending_;
}

void FilePane::loadTabIntoLive(int index) {
    const TabState& t = tabs_[index];
    currentPath_ = t.path;
    entries_ = t.entries;
    stats_ = t.stats;
    back_ = t.back;
    forward_ = t.forward;
    sortColumn_ = t.sortColumn;
    sortAscending_ = t.sortAscending;
    activeTab_ = index;

    ListView_SetItemCountEx(hwnd_, static_cast<int>(entries_.size()), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, TRUE);

    // A tab that's never been visited (just restored from a saved
    // session, or freshly created) starts with no entries - load it now.
    // A genuinely empty folder just re-confirms as empty; harmless.
    if (entries_.empty() && !currentPath_.empty()) {
        navigate(currentPath_, false);
    }
}

void FilePane::switchToTab(int index) {
    if (index == activeTab_ || index < 0 || index >= static_cast<int>(tabs_.size())) return;
    syncActiveTabIntoStorage();
    loadTabIntoLive(index);
    TabCtrl_SetCurSel(tabHwnd_, activeTab_);
    if (onNavigated) onNavigated(*this);
}

void FilePane::cycleTab(bool forward) {
    if (tabs_.size() <= 1) return;
    switchToTab(TabCycle::nextIndex(activeTab_, static_cast<int>(tabs_.size()), forward));
}

void FilePane::updateActiveTabLabel() {
    if (!tabHwnd_ || tabs_.empty()) return;
    std::wstring label = tabLabelFor(currentPath_);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_SetItem(tabHwnd_, activeTab_, &item);
}

void FilePane::newTab() {
    syncActiveTabIntoStorage();

    TabState t;
    t.path = currentPath_;  // new tab starts out at the same folder
    tabs_.push_back(std::move(t));
    const int newIndex = static_cast<int>(tabs_.size()) - 1;

    std::wstring label = tabLabelFor(currentPath_);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_InsertItem(tabHwnd_, newIndex, &item);
    TabCtrl_SetCurSel(tabHwnd_, newIndex);

    loadTabIntoLive(newIndex);  // starts empty, so this also kicks off the enumeration
}

void FilePane::closeTab(int index) {
    if (tabs_.size() <= 1) return;  // always keep at least one tab
    if (index < 0) index = activeTab_;
    if (index >= static_cast<int>(tabs_.size())) return;

    const bool closingActive = (index == activeTab_);
    tabs_.erase(tabs_.begin() + index);
    TabCtrl_DeleteItem(tabHwnd_, index);
    hoveredCloseTab_ = -1;  // indices just shifted; next WM_MOUSEMOVE recomputes this

    if (closingActive) {
        const int newIndex = std::min(index, static_cast<int>(tabs_.size()) - 1);
        loadTabIntoLive(newIndex);
        TabCtrl_SetCurSel(tabHwnd_, newIndex);
        if (onNavigated) onNavigated(*this);
    } else if (activeTab_ > index) {
        --activeTab_;  // a tab before the active one shifted left
        TabCtrl_SetCurSel(tabHwnd_, activeTab_);
    }
}

std::vector<std::wstring> FilePane::tabPaths() {
    syncActiveTabIntoStorage();
    std::vector<std::wstring> paths;
    paths.reserve(tabs_.size());
    for (const auto& t : tabs_) paths.push_back(t.path);
    return paths;
}

void FilePane::restoreTabs(const std::vector<std::wstring>& paths, int activeIndex) {
    if (paths.empty()) return;

    tabs_.clear();
    TabCtrl_DeleteAllItems(tabHwnd_);

    for (size_t i = 0; i < paths.size(); ++i) {
        TabState t;
        t.path = paths[i];
        tabs_.push_back(std::move(t));

        std::wstring label = tabLabelFor(paths[i]);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<LPWSTR>(label.c_str());
        TabCtrl_InsertItem(tabHwnd_, static_cast<int>(i), &item);
    }

    const int idx = std::clamp(activeIndex, 0, static_cast<int>(tabs_.size()) - 1);
    TabCtrl_SetCurSel(tabHwnd_, idx);
    loadTabIntoLive(idx);  // entries start empty, so this kicks off enumeration for the active tab
}

LRESULT FilePane::handleTabNotify(NMHDR* nmhdr) {
    // Double-click-for-new-tab and right-click-to-close are handled by
    // TabStripSubclassProc directly off the raw mouse messages instead
    // (see its comment for why).
    switch (nmhdr->code) {
        case TCN_SELCHANGE:
            switchToTab(TabCtrl_GetCurSel(tabHwnd_));
            return 0;
        default:
            return 0;
    }
}

void FilePane::setHoveredCloseTab(int index) {
    if (index == hoveredCloseTab_) return;

    auto invalidateTab = [this](int idx) {
        if (idx < 0) return;
        RECT r{};
        TabCtrl_GetItemRect(tabHwnd_, idx, &r);
        InvalidateRect(tabHwnd_, &r, FALSE);
    };
    invalidateTab(hoveredCloseTab_);
    hoveredCloseTab_ = index;
    invalidateTab(hoveredCloseTab_);
}

void FilePane::drawTabItem(const DRAWITEMSTRUCT& dis) {
    HDC hdc = dis.hDC;
    const RECT r = dis.rcItem;
    const bool selected = (dis.itemState & ODS_SELECTED) != 0;

    HBRUSH bg = CreateSolidBrush(GetSysColor(selected ? COLOR_WINDOW : COLOR_BTNFACE));
    FillRect(hdc, &r, bg);
    DeleteObject(bg);

    if (selected) {
        RECT underline{r.left, r.bottom - 2, r.right, r.bottom};
        HBRUSH accent = CreateSolidBrush(kActiveTabAccent);
        FillRect(hdc, &underline, accent);
        DeleteObject(accent);
    }

    wchar_t buf[128] = {};
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = buf;
    item.cchTextMax = ARRAYSIZE(buf);
    TabCtrl_GetItem(tabHwnd_, dis.itemID, &item);

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(tabHwnd_, WM_GETFONT, 0, 0));
    HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = r;
    textRect.left += 6;
    textRect.right -= (kCloseGlyphSize + kCloseGlyphMargin * 2);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(hdc, buf, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (tabs_.size() > 1) {
        const bool hovered = (static_cast<int>(dis.itemID) == hoveredCloseTab_);
        if (hovered) {
            RECT hoverRect = closeHoverRectFor(r);
            HBRUSH hoverBg = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
            HRGN rgn = CreateRoundRectRgn(hoverRect.left, hoverRect.top, hoverRect.right + 1, hoverRect.bottom + 1, 4, 4);
            FillRgn(hdc, rgn, hoverBg);
            DeleteObject(rgn);
            DeleteObject(hoverBg);
        }
        RECT closeRect = closeButtonRectFor(r);
        SetTextColor(hdc, GetSysColor(hovered ? COLOR_WINDOW : COLOR_GRAYTEXT));
        DrawTextW(hdc, L"×", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    SelectObject(hdc, old);
}

bool FilePane::matchesSearch(const FileEntry& e) const {
    return FileEntrySort::matchesSearch(e, searchQuery_);
}

void FilePane::toggleSearch() {
    if (!searchVisible_) {
        searchVisible_ = true;
        ShowWindow(searchBox_, SW_SHOW);
        if (onSearchVisibilityChanged) onSearchVisibilityChanged();
    }
    SetFocus(searchBox_);
    Edit_SetSel(searchBox_, 0, -1);
}

void FilePane::closeSearch() {
    if (!searchVisible_) return;
    searchVisible_ = false;
    ShowWindow(searchBox_, SW_HIDE);
    SetWindowTextW(searchBox_, L"");
    searchQuery_.clear();
    currentMatchIndex_ = -1;
    if (onSearchVisibilityChanged) onSearchVisibilityChanged();
    InvalidateRect(hwnd_, nullptr, FALSE);
    SetFocus(hwnd_);
}

void FilePane::onSearchTextChanged() {
    const int len = GetWindowTextLengthW(searchBox_);
    std::wstring text(len, L'\0');
    if (len > 0) GetWindowTextW(searchBox_, text.data(), len + 1);
    std::ranges::transform(text, text.begin(), ::towlower);
    searchQuery_ = text;
    currentMatchIndex_ = -1;  // stale position for a query that no longer applies
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FilePane::jumpToNextMatch() {
    if (searchQuery_.empty() || entries_.empty()) return;

    const int count = static_cast<int>(entries_.size());
    const int current = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);  // -1 if nothing focused yet

    // Walks every item exactly once, starting right after whatever's
    // currently focused (or from the top if nothing is) and wrapping
    // around - so repeated Enters cycle through matches in order, and a
    // single match keeps re-selecting itself rather than doing nothing.
    for (int step = 1; step <= count; ++step) {
        const int i = (current + step) % count;
        if (matchesSearch(entries_[i])) {
            ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(hwnd_, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(hwnd_, i, FALSE);
            // Typing/Enter happens in the search box, so the list itself
            // never gets keyboard focus - without LVS_SHOWSELALWAYS that
            // would normally mean the selection paints gray, not blue.
            // Custom-drawing this one item in the real highlight color
            // makes the jump visible without stealing focus from the box
            // (which would break cycling via repeated Enter).
            currentMatchIndex_ = i;
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }
    }
}

void FilePane::selectSingleItemAtClientPoint(POINT pt) {
    LVHITTESTINFO hit{};
    hit.pt = pt;
    const int idx = ListView_HitTest(hwnd_, &hit);
    if (idx < 0) return;
    if (ListView_GetItemState(hwnd_, idx, LVIS_SELECTED) & LVIS_SELECTED) return;  // keep existing multi-selection

    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(hwnd_, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hwnd_, idx, FALSE);
}

LRESULT FilePane::handleNotify(NMHDR* nmhdr) {
    if (!nmhdr) return 0;
    if (nmhdr->hwndFrom == tabHwnd_) return handleTabNotify(nmhdr);
    if (nmhdr->hwndFrom != hwnd_) return 0;

    switch (nmhdr->code) {
        case LVN_GETDISPINFOW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nmhdr);
            const int idx = di->item.iItem;
            if (idx < 0 || static_cast<size_t>(idx) >= entries_.size()) return 0;
            const FileEntry& e = entries_[idx];

            if (di->item.mask & LVIF_TEXT) {
                std::wstring text;
                switch (di->item.iSubItem) {
                    case 0: text = e.name; break;
                    case 1: text = e.isDirectory() ? L"フォルダー" : (e.extension.empty() ? L"ファイル" : e.extension); break;
                    case 2: text = e.isDirectory() ? L"" : Formatting::formatSize(e.size); break;
                    case 3: text = Formatting::formatFileTime(e.modified); break;
                    default: break;
                }
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, text.c_str(), _TRUNCATE);
            }
            if (di->item.mask & LVIF_IMAGE) {
                di->item.iImage = e.isDirectory() ? IconCache::instance().iconForDirectory()
                                                   : IconCache::instance().iconForFile(e.extension);
            }
            return 0;
        }
        case LVN_COLUMNCLICK: {
            auto* nmlv = reinterpret_cast<NMLISTVIEW*>(nmhdr);
            if (nmlv->iSubItem == sortColumn_) {
                sortAscending_ = !sortAscending_;
            } else {
                sortColumn_ = nmlv->iSubItem;
                sortAscending_ = true;
            }
            sortEntries();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case LVN_ITEMCHANGED: {
            auto* nmlv = reinterpret_cast<NMLISTVIEW*>(nmhdr);
            if (nmlv->uChanged & LVIF_STATE) {
                const bool wasSel = (nmlv->uOldState & LVIS_SELECTED) != 0;
                const bool isSel = (nmlv->uNewState & LVIS_SELECTED) != 0;
                if (nmlv->iItem >= 0) {
                    if (wasSel != isSel && static_cast<size_t>(nmlv->iItem) < entries_.size()) {
                        if (isSel) {
                            ++stats_.selectedCount;
                            if (!entries_[nmlv->iItem].isDirectory()) stats_.selectedSize += entries_[nmlv->iItem].size;
                        } else {
                            --stats_.selectedCount;
                            if (!entries_[nmlv->iItem].isDirectory()) stats_.selectedSize -= entries_[nmlv->iItem].size;
                        }
                    }
                } else {
                    recomputeSelectionStats();
                }
                if (onSelectionChanged) onSelectionChanged(*this);
            }
            return 0;
        }
        case LVN_BEGINDRAG: {
            auto paths = selectedPaths();
            if (!paths.empty()) FileOperations::startDrag(parentWnd_, paths);
            return 0;
        }
        case NM_DBLCLK: {
            auto* nmia = reinterpret_cast<NMITEMACTIVATE*>(nmhdr);
            if (nmia->iItem >= 0) activateEntry(nmia->iItem);
            return 0;
        }
        case LVN_KEYDOWN: {
            auto* kd = reinterpret_cast<NMLVKEYDOWN*>(nmhdr);
            if (kd->wVKey == VK_RETURN) {
                activateEntry(ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED));
            } else if (kd->wVKey == VK_BACK) {
                goUp();
            } else if (kd->wVKey == VK_DELETE) {
                doDelete();
            }
            return 0;
        }
        case LVN_ENDLABELEDITW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nmhdr);
            if (!di->item.pszText) return FALSE;  // edit cancelled
            const int idx = di->item.iItem;
            if (idx < 0 || static_cast<size_t>(idx) >= entries_.size()) return FALSE;
            std::wstring newName = di->item.pszText;
            if (newName.empty() || newName == entries_[idx].name) return FALSE;
            std::wstring oldPath = joinPath(currentPath_, entries_[idx].name);
            if (FileOperations::renameItem(parentWnd_, oldPath, newName)) {
                refresh();
                return TRUE;
            }
            return FALSE;
        }
        case NM_SETFOCUS: {
            if (onFocusChanged) onFocusChanged(*this);
            return 0;
        }
        case NM_CUSTOMDRAW: {
            auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(nmhdr);
            switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return (searchQuery_.empty() && currentMatchIndex_ < 0) ? CDRF_DODEFAULT : CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx == currentMatchIndex_) {
                        // The item just jumped to via Enter - paint it in
                        // the real selection colors so it reads as
                        // "selected" even though focus is still in the
                        // search box, not the list.
                        cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                        cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                    } else if (idx >= 0 && static_cast<size_t>(idx) < entries_.size() && matchesSearch(entries_[idx])) {
                        cd->clrTextBk = RGB(255, 244, 160);  // pale yellow, like a highlighter
                    }
                    return CDRF_DODEFAULT;
                }
                default:
                    return CDRF_DODEFAULT;
            }
        }
        default:
            return 0;
    }
}
