#include "FilePane.h"
#include "DriveBadge.h"
#include "TabCycle.h"

#include <windowsx.h>

#include <algorithm>
#include <unordered_map>

namespace {

std::wstring tabLabelFor(const std::wstring& path) {
    if (path.empty()) return L"";
    const size_t slash = path.find_last_of(L'\\');
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return name.empty() ? path : name;  // e.g. "C:\" has nothing after its trailing slash
}

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

// drawTabItem() paints on every WM_DRAWITEM for the tab strip, so brushes
// for its handful of fixed custom colors (the active-tab underline, each
// drive badge color) are cached here instead of Create/Delete per paint.
// Bounded to a small, fixed set of colors (kActiveTabAccent + the
// DriveBadge palette) - intentionally never freed, same as a process-wide
// GetSysColorBrush.
HBRUSH cachedBrushFor(COLORREF color) {
    static std::unordered_map<COLORREF, HBRUSH> cache;
    auto [it, inserted] = cache.try_emplace(color, nullptr);
    if (inserted) it->second = CreateSolidBrush(color);
    return it->second;
}

}  // namespace

// Intercepted on button-DOWN (not click/up) and swallowed when it lands on
// a tab's close glyph, so the tab control never sees the click and never
// changes the selection to the tab that's about to disappear.
LRESULT CALLBACK FilePane::TabStripSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
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

void FilePane::syncActiveTabIntoStorage() {
    if (tabs_.empty()) return;
    TabState& t = tabs_[activeTab_];
    t.content = live_;

    t.selectedIndices.clear();
    for (int i = ListView_GetNextItem(hwnd_, -1, LVNI_SELECTED); i != -1;
         i = ListView_GetNextItem(hwnd_, i, LVNI_SELECTED)) {
        t.selectedIndices.push_back(i);
    }
    t.focusedIndex = ListView_GetNextItem(hwnd_, -1, LVNI_FOCUSED);
    t.topIndex = ListView_GetTopIndex(hwnd_);
}

void FilePane::loadTabIntoLive(int index) {
    const TabState& t = tabs_[index];
    live_ = t.content;
    activeTab_ = index;

    ListView_SetItemCountEx(hwnd_, static_cast<int>(live_.entries.size()), LVSICF_NOSCROLL);
    InvalidateRect(hwnd_, nullptr, TRUE);

    // Restore selection/focus/scroll so switching tabs and back doesn't
    // look like the selection got cleared and the view jumped to the top.
    // LVM_SETITEMSTATE fires LVN_ITEMCHANGED same as a real click would,
    // which would double-count on top of the live_ = t.content above, so
    // recompute from the control's actual state afterward rather than
    // trust the running tally through this bulk restore.
    for (int idx : t.selectedIndices) {
        if (idx >= 0 && static_cast<size_t>(idx) < live_.entries.size()) {
            ListView_SetItemState(hwnd_, idx, LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    if (t.focusedIndex >= 0 && static_cast<size_t>(t.focusedIndex) < live_.entries.size()) {
        ListView_SetItemState(hwnd_, t.focusedIndex, LVIS_FOCUSED, LVIS_FOCUSED);
    }
    if (!live_.entries.empty()) {
        // EnsureVisible is a no-op if the target is already inside the
        // current (freshly-reset-to-top) visible range, which for a
        // small-ish topIndex it usually is - so it wouldn't actually
        // scroll. Scrolling to the bottom first guarantees the target is
        // then *outside* the visible range, so the second call is forced
        // to actually move the view and lands the target at the top.
        const int target = std::min(t.topIndex, static_cast<int>(live_.entries.size()) - 1);
        ListView_EnsureVisible(hwnd_, static_cast<int>(live_.entries.size()) - 1, FALSE);
        ListView_EnsureVisible(hwnd_, target, FALSE);
    }
    recomputeSelectionStats();

    // A tab that's never been visited (just restored from a saved
    // session, or freshly created) starts with no entries - load it now.
    // A genuinely empty folder just re-confirms as empty; harmless.
    if (live_.entries.empty() && !live_.path.empty()) {
        navigate(live_.path, false);
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
    std::wstring label = tabLabelFor(live_.path);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_SetItem(tabHwnd_, activeTab_, &item);
}

void FilePane::newTab() {
    syncActiveTabIntoStorage();

    TabState t;
    t.content.path = live_.path;  // new tab starts out at the same folder
    t.content.sortColumn = defaultSortColumn_;
    t.content.sortAscending = defaultSortAscending_;
    tabs_.push_back(std::move(t));
    const int newIndex = static_cast<int>(tabs_.size()) - 1;

    std::wstring label = tabLabelFor(live_.path);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<LPWSTR>(label.c_str());
    TabCtrl_InsertItem(tabHwnd_, newIndex, &item);
    TabCtrl_SetCurSel(tabHwnd_, newIndex);

    loadTabIntoLive(newIndex);  // starts empty, so this also kicks off the enumeration
    if (onTabCountChanged) onTabCountChanged();
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
    if (onTabCountChanged) onTabCountChanged();
}

std::vector<std::wstring> FilePane::tabPaths() {
    syncActiveTabIntoStorage();
    std::vector<std::wstring> paths;
    paths.reserve(tabs_.size());
    for (const auto& t : tabs_) paths.push_back(t.content.path);
    return paths;
}

void FilePane::restoreTabs(const std::vector<std::wstring>& paths, int activeIndex) {
    if (paths.empty()) return;

    tabs_.clear();
    TabCtrl_DeleteAllItems(tabHwnd_);

    for (size_t i = 0; i < paths.size(); ++i) {
        TabState t;
        t.content.path = paths[i];
        t.content.sortColumn = defaultSortColumn_;
        t.content.sortAscending = defaultSortAscending_;
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

    FillRect(hdc, &r, GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE));

    if (selected) {
        RECT underline{r.left, r.bottom - 2, r.right, r.bottom};
        FillRect(hdc, &underline, cachedBrushFor(kActiveTabAccent));
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

    const int idx = static_cast<int>(dis.itemID);
    const std::wstring& tabPath = (idx == activeTab_) ? live_.path
                                   : (idx >= 0 && static_cast<size_t>(idx) < tabs_.size()) ? tabs_[idx].content.path
                                                                                            : std::wstring{};
    if (auto drive = DriveBadge::driveLetterOf(tabPath)) {
        const std::wstring badgeText(1, *drive);
        SIZE badgeTextSize{};
        GetTextExtentPoint32W(hdc, badgeText.c_str(), static_cast<int>(badgeText.size()), &badgeTextSize);
        constexpr int kBadgePadX = 5;
        constexpr int kBadgePadY = 2;
        RECT badgeRect{textRect.left, r.top + kBadgePadY, textRect.left + badgeTextSize.cx + kBadgePadX * 2,
                        r.bottom - kBadgePadY};

        HRGN badgeRgn = CreateRoundRectRgn(badgeRect.left, badgeRect.top, badgeRect.right + 1, badgeRect.bottom + 1, 4, 4);
        FillRgn(hdc, badgeRgn, cachedBrushFor(DriveBadge::colorForDrive(*drive)));
        DeleteObject(badgeRgn);

        SetTextColor(hdc, RGB(30, 30, 30));  // dark text reads on every pastel badge color
        DrawTextW(hdc, badgeText.c_str(), -1, &badgeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

        textRect.left = badgeRect.right + 4;
    }

    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(hdc, buf, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (tabs_.size() > 1) {
        const bool hovered = (static_cast<int>(dis.itemID) == hoveredCloseTab_);
        if (hovered) {
            RECT hoverRect = closeHoverRectFor(r);
            HRGN rgn = CreateRoundRectRgn(hoverRect.left, hoverRect.top, hoverRect.right + 1, hoverRect.bottom + 1, 4, 4);
            FillRgn(hdc, rgn, GetSysColorBrush(COLOR_BTNSHADOW));
            DeleteObject(rgn);
        }
        RECT closeRect = closeButtonRectFor(r);
        SetTextColor(hdc, GetSysColor(hovered ? COLOR_WINDOW : COLOR_GRAYTEXT));
        DrawTextW(hdc, L"×", -1, &closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    SelectObject(hdc, old);
}

