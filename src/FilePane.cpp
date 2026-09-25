#include "FilePane.h"
#include "WindowPlacement.h"
#include "Dialogs.h"
#include "DropTargetPath.h"
#include "FileDropTarget.h"
#include "FileEntrySort.h"
#include "FileOperations.h"
#include "IconCache.h"
#include "NameParts.h"
#include "ShellSelection.h"
#include "RecycleBinOps.h"
#include "Strings.h"

#include <shlobj.h>
#include <windowsx.h>
#include <objidl.h>  // must precede gdiplus.h - see AGENTS.md
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace {

using DropTargetPath::joinPath;

std::wstring lowercaseCopy(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
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

// Middle-click on a folder row opens it in a background tab. The ListView
// itself ignores the middle button, so it's caught here.
LRESULT CALLBACK ListMiddleClickSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                             DWORD_PTR refData) {
    if (msg == WM_MBUTTONUP) {
        auto* pane = reinterpret_cast<FilePane*>(refData);
        LVHITTESTINFO hit{};
        hit.pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int idx = ListView_HitTest(hwnd, &hit);
        if (idx >= 0 && (hit.flags & LVHT_ONITEM)) pane->openRowInBackgroundTab(idx);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
}  // namespace

bool FilePane::create(HWND parent, HINSTANCE hInstance, int controlId, int paneId) {
    parentWnd_ = parent;
    paneId_ = paneId;

    // A plain, self-drawn child window - see FilePaneTabs.cpp for why
    // this isn't SysTabControl32 (WC_TABCONTROLW) any more. Everything
    // SysTabControl32 used to give for free - hit-testing, wrapping onto
    // more rows, drawing, selection - is now this pane's own job; see
    // relayoutTabs/hitTestTab/drawTabItem and TabStripSubclassProc.
    constexpr wchar_t kTabStripClass[] = L"KestrelTabStrip";
    static bool tabStripClassRegistered = false;
    if (!tabStripClassRegistered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInstance;
        wc.lpszClassName = kTabStripClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        tabStripClassRegistered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    tabHwnd_ = CreateWindowExW(0, kTabStripClass, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 0, 0, 0, 0, parent,
                                nullptr, hInstance, nullptr);
    // Deferred until after SetWindowSubclass below - sent any earlier, it
    // would only reach the raw DefWindowProcW (which drops it on the
    // floor) instead of TabStripSubclassProc's own WM_SETFONT handler.

    newTabButton_ = CreateWindowExW(0, L"BUTTON", L"+", WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
                                     nullptr, hInstance, nullptr);
    SendMessageW(newTabButton_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // "Copy" glyph (U+E8C8) from the system icon font, shared by both panes.
    static HFONT iconFont = CreateFontW(-MulDiv(10, static_cast<int>(GetDpiForWindow(parent)), 72), 0, 0, 0,
                                        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");
    duplicateTabButton_ = CreateWindowExW(0, L"BUTTON", L"\xE8C8", WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | BS_PUSHBUTTON,
                                          0, 0, 0, 0, parent, nullptr, hInstance, nullptr);
    SendMessageW(duplicateTabButton_, WM_SETFONT, reinterpret_cast<WPARAM>(iconFont), TRUE);

    // Hover tooltips for the two tab buttons.
    HWND tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, hInstance, nullptr);
    auto addTip = [&](HWND button, const wchar_t* text) {
        TTTOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = parent;
        info.uId = reinterpret_cast<UINT_PTR>(button);
        info.lpszText = const_cast<LPWSTR>(text);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    };
    tabButtonTooltip_ = tooltip;
    addTip(newTabButton_, tr(StringId::TipNewTab));
    addTip(duplicateTabButton_, tr(StringId::TipDuplicateTab));

    // Hidden until Ctrl+F; setBounds() only reserves a row for it while
    // searchVisible_ is true.
    searchBox_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
                                  nullptr, hInstance, nullptr);
    SendMessageW(searchBox_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(searchBox_, SearchBoxSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // Hidden outside the Recycle Bin view; setBounds() shows/positions it
    // based on currentPath().
    emptyRecycleBinButton_ = CreateWindowExW(0, L"BUTTON", tr(StringId::ButtonEmptyRecycleBin),
                                              WS_CHILD | WS_CLIPSIBLINGS | BS_PUSHBUTTON, 0, 0, 0, 0, parent,
                                              nullptr, hInstance, nullptr);
    SendMessageW(emptyRecycleBinButton_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

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
                             WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_EDITLABELS | LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                             hInstance, nullptr);
    if (!hwnd_ || !tabHwnd_) return false;

    ListView_SetExtendedListViewStyle(hwnd_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    if (HIMAGELIST himl = IconCache::instance().systemImageList()) {
        ListView_SetImageList(hwnd_, himl, LVSIL_SMALL);
    }

    struct ColSpec { StringId text; int width; };
    static constexpr ColSpec cols[] = {
        {StringId::ColumnName, 220}, {StringId::ColumnType, 120}, {StringId::ColumnSize, 80}, {StringId::ColumnModified, 130},
    };
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(tr(cols[i].text));
        col.cx = cols[i].width;
        col.iSubItem = i;
        ListView_InsertColumn(hwnd_, i, &col);
    }

    TabState initialTab;
    initialTab.content.sortColumn = defaultSortColumn_;
    initialTab.content.sortAscending = defaultSortAscending_;
    tabs_.push_back(std::move(initialTab));
    activeTab_ = 0;
    SetWindowSubclass(tabHwnd_, TabStripSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(tabHwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    relayoutTabs();

    FileDropTarget::registerOn(hwnd_, {
        [this](POINT pt) { return dropHitTest(pt); },
        [this](intptr_t key) { setDropHighlight(static_cast<int>(key) - 1); },
    });
    FileDropTarget::registerOn(tabHwnd_, {
        [this](POINT pt) { return tabDropHitTest(pt); },
        [this](intptr_t key) { setTabDropHighlight(static_cast<int>(key) - 1); },
    });
    SetWindowSubclass(hwnd_, ListMiddleClickSubclassProc, 2, reinterpret_cast<DWORD_PTR>(this));

    return true;
}

void FilePane::retranslate() {
    for (int i = 0; i < 4; ++i) {
        static constexpr StringId kColumnIds[] = {StringId::ColumnName, StringId::ColumnType, StringId::ColumnSize,
                                                    StringId::ColumnModified};
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<LPWSTR>(tr(kColumnIds[i]));
        ListView_SetColumn(hwnd_, i, &col);
    }

    if (tabButtonTooltip_) {
        auto setTip = [&](HWND button, const wchar_t* text) {
            TTTOOLINFOW info{};
            info.cbSize = sizeof(info);
            info.uFlags = TTF_IDISHWND;
            info.hwnd = GetParent(button);
            info.uId = reinterpret_cast<UINT_PTR>(button);
            info.lpszText = const_cast<LPWSTR>(text);
            SendMessageW(tabButtonTooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
        };
        setTip(newTabButton_, tr(StringId::TipNewTab));
        setTip(duplicateTabButton_, tr(StringId::TipDuplicateTab));
    }

    SetWindowTextW(emptyRecycleBinButton_, tr(StringId::ButtonEmptyRecycleBin));
    InvalidateRect(tabHwnd_, nullptr, TRUE);
}

void FilePane::setBounds(const RECT& outer) {
    constexpr int kNewTabButtonWidth = 22;
    constexpr int kSearchBoxHeight = 22;
    constexpr int kEmptyRecycleBinButtonHeight = 24;
    const int w = outer.right - outer.left;
    const int tabStripW = std::max(0, w - kNewTabButtonWidth * 2);  // "+" and the duplicate button

    tabStripWidth_ = tabStripW;
    relayoutTabs();  // wraps tabRects_ onto however many rows this width needs
    const int tabRows = std::max(1, tabRowCount());
    const int tabStripHeight = tabRows * kTabStripHeight;
    placeWithoutRedraw(tabHwnd_, outer.left, outer.top, tabStripW, tabStripHeight);
    placeWithoutRedraw(newTabButton_, outer.left + tabStripW, outer.top, kNewTabButtonWidth, kTabStripHeight);
    placeWithoutRedraw(duplicateTabButton_, outer.left + tabStripW + kNewTabButtonWidth, outer.top, kNewTabButtonWidth,
                       kTabStripHeight);

    int listTop = outer.top + tabStripHeight;

    const bool showEmptyButton = (live_.path == kRecycleBinPath);
    ShowWindow(emptyRecycleBinButton_, showEmptyButton ? SW_SHOWNA : SW_HIDE);
    if (showEmptyButton) {
        placeWithoutRedraw(emptyRecycleBinButton_, outer.left, listTop, w, kEmptyRecycleBinButtonHeight);
        listTop += kEmptyRecycleBinButtonHeight;
    }

    if (searchVisible_) {
        placeWithoutRedraw(searchBox_, outer.left, listTop, w, kSearchBoxHeight);
        listTop += kSearchBoxHeight;
    }

    const int h = std::max(0, static_cast<int>(outer.bottom - listTop));
    placeWithoutRedraw(hwnd_, outer.left, listTop, w, h);
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
        std::wstring msg = std::vformat(tr(StringId::ErrorAccessDenied), std::make_wformat_args(result->path));
        Dialogs::showError(parentWnd_, L"Kestrel", msg.c_str());
        return;
    }

    if (pendingAddToHistory_ && !pendingPrevPath_.empty() && pendingPrevPath_ != pendingNavPath_) {
        live_.back.push_back(pendingPrevPath_);
        live_.forward.clear();
    }

    const bool wasRecycleBin = (live_.path == kRecycleBinPath);
    // The ListView keeps selected/focused row *indices* across a new item
    // count, which point at different files once the listing changes
    // (another folder, or a file added/removed above the selection). A
    // same-folder refresh carries the selection over by *name* instead.
    const bool samePath = _wcsicmp(live_.path.c_str(), pendingNavPath_.c_str()) == 0;
    std::vector<std::wstring> keepSelected;
    std::wstring keepFocused;
    if (samePath) {
        keepSelected = selectedNames();
        if (const int f = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED); f >= 0 && static_cast<size_t>(f) < live_.entries.size()) {
            keepFocused = live_.entries[f].name;
        }
    }
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    live_.path = pendingNavPath_;
    applyEntries(std::move(result->entries));
    if (samePath) {
        for (int idx : FileEntrySort::indicesOfNames(live_.entries, keepSelected)) {
            ListView_SetItemState(hwnd_, idx, LVIS_SELECTED, LVIS_SELECTED);
        }
        if (!keepFocused.empty()) {
            for (int idx : FileEntrySort::indicesOfNames(live_.entries, {keepFocused})) {
                ListView_SetItemState(hwnd_, idx, LVIS_FOCUSED, LVIS_FOCUSED);
            }
        }
        recomputeSelectionStats();
    }
    beginPendingRename();
    // drawTabItem reads live_.path directly for the active tab's label,
    // so there's no separate tab-control item text to update here - just
    // repaint the strip so the new path actually shows.
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    watcher_.watch(live_.path, parentWnd_, reinterpret_cast<WPARAM>(this));

    const bool isRecycleBin = (live_.path == kRecycleBinPath);
    if (isRecycleBin != wasRecycleBin && onEmptyButtonVisibilityChanged) onEmptyButtonVisibilityChanged();

    if (onNavigated) onNavigated(*this);
}

void FilePane::applyEntries(std::vector<FileEntry> entries) {
    live_.entries = std::move(entries);
    if (!showHidden_) FileEntrySort::removeHidden(live_.entries);
    sortEntries();

    live_.stats.fileCount = 0;
    live_.stats.dirCount = 0;
    for (const auto& e : live_.entries) {
        if (e.isDirectory()) ++live_.stats.dirCount;
        else ++live_.stats.fileCount;
    }
    // LVS_OWNERDATA only repaints automatically when the item COUNT
    // changes; if the new folder happens to have the same number of
    // entries as the old one, the control would otherwise keep showing
    // the previous folder's cached rows. Force a repaint unconditionally.
    ListView_SetItemCountEx(hwnd_, static_cast<int>(live_.entries.size()), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, FALSE);

    // Count what the control actually has selected rather than assuming
    // nothing is (tab restores and same-folder refreshes reselect rows).
    recomputeSelectionStats();
}

void FilePane::sortEntries() {
    FileEntrySort::sort(live_.entries, live_.sortColumn, live_.sortAscending);
    updateSortArrow();
}

void FilePane::updateSortArrow() {
    HWND header = ListView_GetHeader(hwnd_);
    const int count = Header_GetItemCount(header);
    for (int i = 0; i < count; ++i) {
        HDITEMW item{};
        item.mask = HDI_FORMAT;
        if (!Header_GetItem(header, i, &item)) continue;
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == live_.sortColumn) item.fmt |= live_.sortAscending ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(header, i, &item);
    }
}

void FilePane::recomputeSelectionStats() {
    size_t count = 0;
    uint64_t size = 0;
    int i = -1;
    while ((i = ListView_GetNextItem(hwnd_, i, LVNI_SELECTED)) != -1) {
        if (static_cast<size_t>(i) >= live_.entries.size()) break;
        ++count;
        if (!live_.entries[i].isDirectory()) size += live_.entries[i].size;
    }
    live_.stats.selectedCount = count;
    live_.stats.selectedSize = size;
}

std::wstring FilePane::pathForIndex(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= live_.entries.size()) return L"";
    return joinPath(live_.path, live_.entries[index].name);
}

void FilePane::openRowInBackgroundTab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= live_.entries.size()) return;
    if (live_.path == kRecycleBinPath || !live_.entries[index].isDirectory()) return;
    openInBackgroundTab(joinPath(live_.path, live_.entries[index].name));
}

void FilePane::activateEntry(int index) {
    if (index < 0 || static_cast<size_t>(index) >= live_.entries.size()) return;
    // Recycle Bin entries aren't real paths (see kRecycleBinPath) - nothing
    // to navigate into or open. Restore/permanent-delete isn't implemented.
    if (live_.path == kRecycleBinPath) return;
    const FileEntry& e = live_.entries[index];
    if (e.isDirectory()) {
        navigate(joinPath(live_.path, e.name), true);
    } else {
        FileOperations::openItem(parentWnd_, joinPath(live_.path, e.name));
    }
}

std::vector<std::wstring> FilePane::selectedPaths() const {
    std::vector<std::wstring> result;
    // Recycle Bin entries aren't real paths - this single choke point feeds
    // cut/copy/delete/drag/context-menu, so returning empty here disables
    // all of them for this view rather than guarding each caller.
    if (live_.path == kRecycleBinPath) return result;
    int idx = -1;
    while ((idx = ListView_GetNextItem(hwnd_, idx, LVNI_SELECTED)) != -1) {
        result.push_back(pathForIndex(idx));
    }
    return result;
}

std::vector<std::wstring> FilePane::selectedNames() const {
    std::vector<std::wstring> result;
    int idx = -1;
    while ((idx = ListView_GetNextItem(hwnd_, idx, LVNI_SELECTED)) != -1) {
        if (static_cast<size_t>(idx) < live_.entries.size()) result.push_back(live_.entries[idx].name);
    }
    return result;
}

void FilePane::setCutPaths(std::vector<std::wstring> paths) {
    cutPaths_.clear();
    cutPaths_.reserve(paths.size());
    for (auto& p : paths) cutPaths_.push_back(lowercaseCopy(std::move(p)));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void FilePane::clearCutPaths() {
    if (cutPaths_.empty()) return;
    cutPaths_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring FilePane::focusedItemPath() const {
    const int idx = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    if (idx < 0) return L"";
    return pathForIndex(idx);
}

void FilePane::doRename() {
    if (live_.path == kRecycleBinPath) return;
    int idx = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    if (idx < 0) return;
    SetFocus(hwnd_);
    ListView_EditLabel(hwnd_, idx);
}

void FilePane::doDelete(bool permanent) {
    if (live_.path == kRecycleBinPath) {
        auto names = selectedNames();
        if (!names.empty() && RecycleBinOps::deleteItemsPermanently(parentWnd_, names)) {
            refresh();
        }
        return;
    }
    auto paths = selectedPaths();
    if (paths.empty()) return;
    Undo::Record record{Undo::Kind::Recycle, {}};
    const bool ok = FileOperations::deleteItems(parentWnd_, paths, permanent, permanent ? nullptr : &record);
    if (onUndoable) onUndoable(std::move(record));  // may be partial, e.g. cancelled midway
    if (ok) refresh();
}

void FilePane::emptyRecycleBin() {
    if (live_.path != kRecycleBinPath) return;
    if (RecycleBinOps::emptyRecycleBin(parentWnd_)) {
        refresh();
    }
}

void FilePane::doMkdir() {
    // This PC / Recycle Bin aren't folders anything can be created in.
    if (live_.path.starts_with(L"::")) return;

    // Explorer-style: create "新しいフォルダー" (numbered if taken) right
    // away, then drop straight into renaming it once the refresh lists it.
    std::vector<std::wstring> names;
    names.reserve(live_.entries.size());
    for (const auto& e : live_.entries) names.push_back(e.name);
    std::wstring name = NameParts::uniqueName(names, tr(StringId::NewFolderBaseName));
    Undo::Record record{Undo::Kind::NewFolder, {}};
    const bool created = FileOperations::createDirectory(parentWnd_, live_.path, name, &record);
    if (onUndoable) onUndoable(std::move(record));
    if (created) {
        pendingRenameDir_ = live_.path;
        pendingRenameName_ = std::move(name);
        refresh();
    }
}

void FilePane::beginPendingRename() {
    if (pendingRenameName_.empty()) return;
    if (_wcsicmp(pendingRenameDir_.c_str(), live_.path.c_str()) != 0) {
        pendingRenameName_.clear();  // navigated elsewhere meanwhile
        return;
    }
    const auto it = std::ranges::find_if(live_.entries, [&](const FileEntry& e) {
        return _wcsicmp(e.name.c_str(), pendingRenameName_.c_str()) == 0;
    });
    if (it == live_.entries.end()) return;  // not listed yet; a later refresh will
    pendingRenameName_.clear();

    const int idx = static_cast<int>(it - live_.entries.begin());
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(hwnd_, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hwnd_, idx, FALSE);
    SetFocus(hwnd_);
    ListView_EditLabel(hwnd_, idx);
}

void FilePane::showProperties() {
    if (live_.path == kRecycleBinPath) return;  // entries aren't real paths
    const auto paths = selectedPaths();
    if (paths.size() > 1) {
        if (auto data = ShellSelection::get<IDataObject>(parentWnd_, paths)) SHMultiFileProperties(data.get(), 0);
        return;
    }
    // Nothing selected = the folder being shown, as in Explorer.
    const std::wstring& target = paths.empty() ? live_.path : paths.front();
    if (!target.empty()) SHObjectProperties(parentWnd_, SHOP_FILEPATH, target.c_str(), nullptr);
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

void FilePane::setDefaultSort(int column, bool ascending) {
    defaultSortColumn_ = column;
    defaultSortAscending_ = ascending;
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

FileDropTarget::Hit FilePane::dropHitTest(POINT pt) const {
    LVHITTESTINFO hit{};
    hit.pt = pt;
    const int idx = ListView_HitTest(hwnd_, &hit);
    // Only a row's icon/label counts as "on" it, as in Explorer's details
    // view; the empty width to the right of the name is the folder itself.
    if (idx < 0 || !(hit.flags & (LVHT_ONITEMICON | LVHT_ONITEMLABEL)) ||
        static_cast<size_t>(idx) >= live_.entries.size()) {
        return {DropTargetPath::candidates(live_.path, L"", false), 0};
    }
    const FileEntry& e = live_.entries[idx];
    return {DropTargetPath::candidates(live_.path, e.name, e.isDirectory()), idx + 1};
}

void FilePane::setDropHighlight(int index) {
    if (index == dropHighlightIndex_) return;
    if (dropHighlightIndex_ >= 0) ListView_RedrawItems(hwnd_, dropHighlightIndex_, dropHighlightIndex_);
    dropHighlightIndex_ = index;
    if (index >= 0) ListView_RedrawItems(hwnd_, index, index);
    UpdateWindow(hwnd_);
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
    if (nmhdr->hwndFrom != hwnd_) return 0;  // tabHwnd_ is a plain window now - it never sends WM_NOTIFY

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
                    case 1: text = e.typeName.c_str(); break;
                    case 2: text = e.formattedSize.c_str(); break;
                    case 3: text = e.formattedModified.c_str(); break;
                    default: break;
                }
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, text, _TRUNCATE);
            }
            if (di->item.mask & LVIF_IMAGE) {
                di->item.iImage = e.isDirectory()
                                       ? IconCache::instance().iconForPath(joinPath(live_.path, e.name))
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
        case LVN_BEGINDRAG:
        case LVN_BEGINRDRAG: {
            auto paths = selectedPaths();
            if (!paths.empty()) {
                FileDropTarget::InternalDragScope internal(paths);
                FileOperations::startDrag(hwnd_, paths);
            }
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
                doDelete((GetKeyState(VK_SHIFT) & 0x8000) != 0);
            }
            return 0;
        }
        case LVN_ODFINDITEMW: {
            // Type-to-select: owner-data lists can't search their own
            // items, so the control asks us for the typed prefix's row.
            auto* fi = reinterpret_cast<NMLVFINDITEMW*>(nmhdr);
            if (!(fi->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) || !fi->lvfi.psz) return -1;
            return FileEntrySort::findByPrefix(live_.entries, fi->lvfi.psz, fi->iStart,
                                               (fi->lvfi.flags & LVFI_WRAP) != 0);
        }
        case LVN_BEGINLABELEDITW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nmhdr);
            const int idx = di->item.iItem;
            if (idx < 0 || static_cast<size_t>(idx) >= live_.entries.size()) return TRUE;
            // Preselect just the name, not the extension, as Explorer does.
            // Posted: the control applies its own select-all after this.
            if (HWND edit = ListView_GetEditControl(hwnd_)) {
                const FileEntry& e = live_.entries[idx];
                PostMessageW(edit, EM_SETSEL, 0,
                             static_cast<LPARAM>(NameParts::renameSelectionEnd(e.name, e.isDirectory())));
            }
            return FALSE;
        }
        case LVN_ENDLABELEDITW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nmhdr);
            if (!di->item.pszText) return FALSE;  // edit cancelled
            const int idx = di->item.iItem;
            if (idx < 0 || static_cast<size_t>(idx) >= live_.entries.size()) return FALSE;
            std::wstring newName = di->item.pszText;
            if (newName.empty() || newName == live_.entries[idx].name) return FALSE;
            std::wstring oldPath = joinPath(live_.path, live_.entries[idx].name);
            Undo::Record record{Undo::Kind::Rename, {}};
            const bool renamed = FileOperations::renameItem(parentWnd_, oldPath, newName, &record);
            if (onUndoable) onUndoable(std::move(record));
            if (renamed) {
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
                    return (searchQuery_.empty() && currentMatchIndex_ < 0 && cutPaths_.empty() && dropHighlightIndex_ < 0)
                               ? CDRF_DODEFAULT
                               : CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (idx == currentMatchIndex_ || idx == dropHighlightIndex_) {
                        // The item just jumped to via Enter - paint it in
                        // the real selection colors so it reads as
                        // "selected" even though focus is still in the
                        // search box, not the list.
                        cd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                        cd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);
                    } else if (idx >= 0 && static_cast<size_t>(idx) < live_.entries.size() && matchesSearch(live_.entries[idx])) {
                        cd->clrTextBk = RGB(255, 244, 160);  // pale yellow, like a highlighter
                    }
                    if (idx >= 0 && static_cast<size_t>(idx) < live_.entries.size() && !cutPaths_.empty()) {
                        const std::wstring full = lowercaseCopy(joinPath(live_.path, live_.entries[idx].name));
                        if (std::ranges::find(cutPaths_, full) != cutPaths_.end()) return CDRF_NOTIFYPOSTPAINT;
                    }
                    return CDRF_DODEFAULT;
                }
                case CDDS_ITEMPOSTPAINT: {
                    // Explorer dims a cut item's icon (but not its label) to
                    // show it's a pending move. comctl32 has already drawn
                    // the icon at full opacity by this point (POSTPAINT
                    // fires after the default draw), so painting a
                    // translucent copy on top of it would still show the
                    // opaque one showing through underneath - erase the
                    // icon rect back to the row's background first, then
                    // draw the dimmed icon into the cleared space.
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    RECT iconRect{};
                    if (idx >= 0 && static_cast<size_t>(idx) < live_.entries.size() &&
                        ListView_GetItemRect(hwnd_, idx, &iconRect, LVIR_ICON)) {
                        const FileEntry& e = live_.entries[idx];
                        const int iImage = e.isDirectory() ? IconCache::instance().iconForPath(joinPath(live_.path, e.name))
                                                            : IconCache::instance().iconForFile(e.extension);
                        HIMAGELIST himl = ListView_GetImageList(hwnd_, LVSIL_SMALL);
                        const bool selected = (ListView_GetItemState(hwnd_, idx, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                        const bool isMatch = matchesSearch(e);
                        const COLORREF bg = (idx == currentMatchIndex_ || idx == dropHighlightIndex_ || selected)
                                                 ? GetSysColor(COLOR_HIGHLIGHT)
                                             : isMatch                              ? RGB(255, 244, 160)
                                                                                    : GetSysColor(COLOR_WINDOW);
                        HBRUSH bgBrush = CreateSolidBrush(bg);
                        FillRect(cd->nmcd.hdc, &iconRect, bgBrush);
                        DeleteObject(bgBrush);
                        HICON hIcon = himl ? ImageList_GetIcon(himl, iImage, ILD_TRANSPARENT) : nullptr;
                        if (hIcon) {
                            Gdiplus::Bitmap bmp(hIcon);
                            Gdiplus::Graphics g(cd->nmcd.hdc);
                            Gdiplus::ColorMatrix matrix = {
                                1, 0, 0, 0, 0,
                                0, 1, 0, 0, 0,
                                0, 0, 1, 0, 0,
                                0, 0, 0, 0.5f, 0,
                                0, 0, 0, 0, 1,
                            };
                            Gdiplus::ImageAttributes attr;
                            attr.SetColorMatrix(&matrix);
                            const UINT w = bmp.GetWidth();
                            const UINT h = bmp.GetHeight();
                            Gdiplus::Rect dest(iconRect.left, iconRect.top, static_cast<INT>(w), static_cast<INT>(h));
                            g.DrawImage(&bmp, dest, 0, 0, static_cast<INT>(w), static_cast<INT>(h), Gdiplus::UnitPixel, &attr);
                            DestroyIcon(hIcon);
                        }
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
