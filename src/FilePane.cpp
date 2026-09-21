#include "FilePane.h"
#include "Dialogs.h"
#include "FileEntrySort.h"
#include "FileOperations.h"
#include "IconCache.h"

#include <windowsx.h>

#include <algorithm>
#include <filesystem>

namespace {

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    // The This-PC view's entries are already full drive roots ("C:\"),
    // not names relative to a real containing folder - kThisPcPath itself
    // isn't a real path to prepend.
    if (dir == kThisPcPath) return name;

    std::wstring full = dir;
    if (!full.empty() && full.back() != L'\\') full += L'\\';
    full += name;
    return full;
}

constexpr int kTabStripHeight = 22;

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
    // TCS_MULTILINE so once tabs no longer fit one row, they wrap onto
    // additional rows instead of the default scroll-arrow behavior -
    // setBounds() sizes the control's height to match however many rows
    // that ends up being.
    tabHwnd_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
                                WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER | TCS_TOOLTIPS | TCS_OWNERDRAWFIXED |
                                    TCS_MULTILINE,
                                0, 0, 0, 0, parent, nullptr, hInstance, nullptr);
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

    ListView_SetExtendedListViewStyle(hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
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

    // TCS_MULTILINE wraps onto more rows as needed, but only figures out
    // how many once it knows its actual width - so size it once at a
    // single row's height first, ask how many rows that produced, then
    // resize to fit them all. Neither step paints an intermediate size;
    // MainWindow schedules the repaint after all panes have been placed.
    MoveWindow(tabHwnd_, outer.left, outer.top, tabStripW, kTabStripHeight, FALSE);
    const int tabRows = std::max(1, TabCtrl_GetRowCount(tabHwnd_));
    const int tabStripHeight = tabRows * kTabStripHeight;
    MoveWindow(tabHwnd_, outer.left, outer.top, tabStripW, tabStripHeight, FALSE);
    MoveWindow(newTabButton_, outer.left + tabStripW, outer.top, kNewTabButtonWidth, kTabStripHeight, FALSE);

    int listTop = outer.top + tabStripHeight;
    if (searchVisible_) {
        MoveWindow(searchBox_, outer.left, listTop, w, kSearchBoxHeight, FALSE);
        listTop += kSearchBoxHeight;
    }

    const int h = std::max(0, static_cast<int>(outer.bottom - listTop));
    MoveWindow(hwnd_, outer.left, listTop, w, h, FALSE);
}

void FilePane::navigate(std::wstring path, bool addToHistory) {
    if (path.size() > 3 && !path.empty() && path.back() == L'\\') path.pop_back();

    pendingNavPath_ = path;
    pendingPrevPath_ = live_.path;
    pendingAddToHistory_ = addToHistory;
    pendingRequestId_ = model_.requestEnumeration(path, parentWnd_, reinterpret_cast<WPARAM>(this));
}

void FilePane::refresh() {
    if (!live_.path.empty()) navigate(live_.path, false);
}

void FilePane::goBack() {
    if (live_.back.empty()) return;
    live_.forward.push_back(live_.path);
    std::wstring target = live_.back.back();
    live_.back.pop_back();
    navigate(target, false);
}

void FilePane::goForward() {
    if (live_.forward.empty()) return;
    live_.back.push_back(live_.path);
    std::wstring target = live_.forward.back();
    live_.forward.pop_back();
    navigate(target, false);
}

void FilePane::goUp() {
    std::filesystem::path p(live_.path);
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
        live_.back.push_back(pendingPrevPath_);
        live_.forward.clear();
    }

    live_.path = pendingNavPath_;
    applyEntries(std::move(result->entries));
    updateActiveTabLabel();
    watcher_.watch(live_.path, parentWnd_, reinterpret_cast<WPARAM>(this));

    if (onNavigated) onNavigated(*this);
}

void FilePane::applyEntries(std::vector<FileEntry> entries) {
    live_.entries = std::move(entries);
    sortEntries();

    live_.stats.fileCount = 0;
    live_.stats.dirCount = 0;
    for (const auto& e : live_.entries) {
        if (e.isDirectory()) ++live_.stats.dirCount;
        else ++live_.stats.fileCount;
    }
    live_.stats.selectedCount = 0;
    live_.stats.selectedSize = 0;

    // LVS_OWNERDATA only repaints automatically when the item COUNT
    // changes; if the new folder happens to have the same number of
    // entries as the old one, the control would otherwise keep showing
    // the previous folder's cached rows. Force a repaint unconditionally.
    ListView_SetItemCountEx(hwnd_, static_cast<int>(live_.entries.size()), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FilePane::sortEntries() {
    FileEntrySort::sort(live_.entries, live_.sortColumn, live_.sortAscending);
}

void FilePane::recomputeSelectionStats() {
    size_t count = 0;
    uint64_t size = 0;
    const int total = static_cast<int>(live_.entries.size());
    for (int i = 0; i < total; ++i) {
        if (ListView_GetItemState(hwnd_, i, LVIS_SELECTED) & LVIS_SELECTED) {
            ++count;
            if (!live_.entries[i].isDirectory()) size += live_.entries[i].size;
        }
    }
    live_.stats.selectedCount = count;
    live_.stats.selectedSize = size;
}

std::wstring FilePane::pathForIndex(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= live_.entries.size()) return L"";
    return joinPath(live_.path, live_.entries[index].name);
}

void FilePane::activateEntry(int index) {
    if (index < 0 || static_cast<size_t>(index) >= live_.entries.size()) return;
    const FileEntry& e = live_.entries[index];
    if (e.isDirectory()) {
        navigate(joinPath(live_.path, e.name), true);
    } else {
        FileOperations::openItem(parentWnd_, joinPath(live_.path, e.name));
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
    if (FileOperations::createDirectory(parentWnd_, live_.path, *name)) {
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

std::array<int, 4> FilePane::columnWidths() const {
    std::array<int, 4> widths{};
    for (int i = 0; i < 4; ++i) widths[i] = ListView_GetColumnWidth(hwnd_, i);
    return widths;
}

void FilePane::setColumnWidths(const std::array<int, 4>& widths) {
    for (int i = 0; i < 4; ++i) ListView_SetColumnWidth(hwnd_, i, widths[i]);
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
    if (searchQuery_.empty() || live_.entries.empty()) return;

    const int count = static_cast<int>(live_.entries.size());
    const int current = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);  // -1 if nothing focused yet

    // Walks every item exactly once, starting right after whatever's
    // currently focused (or from the top if nothing is) and wrapping
    // around - so repeated Enters cycle through matches in order, and a
    // single match keeps re-selecting itself rather than doing nothing.
    for (int step = 1; step <= count; ++step) {
        const int i = (current + step) % count;
        if (matchesSearch(live_.entries[i])) {
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
            if (idx < 0 || static_cast<size_t>(idx) >= live_.entries.size()) return 0;
            const FileEntry& e = live_.entries[idx];

            if (di->item.mask & LVIF_TEXT) {
                // Points straight at each FileEntry's own (already-formatted,
                // for size/date) strings rather than building a fresh
                // std::wstring per cell - this runs on every row the list
                // paints, including while scrolling a large directory.
                const wchar_t* text = L"";
                switch (di->item.iSubItem) {
                    case 0: text = e.name.c_str(); break;
                    case 1:
                        text = e.isDirectory() ? L"フォルダー" : (e.extension.empty() ? L"ファイル" : e.extension.c_str());
                        break;
                    case 2: text = e.formattedSize.c_str(); break;
                    case 3: text = e.formattedModified.c_str(); break;
                    default: break;
                }
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, text, _TRUNCATE);
            }
            if (di->item.mask & LVIF_IMAGE) {
                di->item.iImage = e.isDirectory() ? IconCache::instance().iconForDirectory()
                                                   : IconCache::instance().iconForFile(e.extension);
            }
            return 0;
        }
        case LVN_COLUMNCLICK: {
            auto* nmlv = reinterpret_cast<NMLISTVIEW*>(nmhdr);
            if (nmlv->iSubItem == live_.sortColumn) {
                live_.sortAscending = !live_.sortAscending;
            } else {
                live_.sortColumn = nmlv->iSubItem;
                live_.sortAscending = true;
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
                    if (wasSel != isSel && static_cast<size_t>(nmlv->iItem) < live_.entries.size()) {
                        if (isSel) {
                            ++live_.stats.selectedCount;
                            if (!live_.entries[nmlv->iItem].isDirectory()) live_.stats.selectedSize += live_.entries[nmlv->iItem].size;
                        } else {
                            --live_.stats.selectedCount;
                            if (!live_.entries[nmlv->iItem].isDirectory()) live_.stats.selectedSize -= live_.entries[nmlv->iItem].size;
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
            if (idx < 0 || static_cast<size_t>(idx) >= live_.entries.size()) return FALSE;
            std::wstring newName = di->item.pszText;
            if (newName.empty() || newName == live_.entries[idx].name) return FALSE;
            std::wstring oldPath = joinPath(live_.path, live_.entries[idx].name);
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
                    } else if (idx >= 0 && static_cast<size_t>(idx) < live_.entries.size() && matchesSearch(live_.entries[idx])) {
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
