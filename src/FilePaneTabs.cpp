#include "FilePane.h"
#include "DriveBadge.h"
#include "DropTargetPath.h"
#include "NameParts.h"
#include "TabCycle.h"

#include <windowsx.h>

#include <algorithm>
#include <unordered_map>

// The tab strip used to be SysTabControl32 (WC_TABCONTROLW) with
// TCS_MULTILINE. That had to go: comctl32's TCS_MULTILINE always renders
// whichever row holds the *selected* tab as the bottom-most row, and
// re-sorts the rows every time TabCtrl_SetCurSel runs (including the one
// it does internally on a plain click) - so a drag-to-reorder across rows
// kept fighting that reflow: mid-drag it looked fine (nothing was calling
// SetCurSel), but the instant selection was touched again - on drag end,
// or on the next ordinary tab click - the rows visibly snapped back into
// "selected tab's row last" order, undoing the reorder or scrambling
// unrelated tabs' rows. There's no documented way to disable that
// behavior. This file now owns everything SysTabControl32 used to do
// for free: per-tab rect layout (computeTabLayout/relayoutTabs),
// hit-testing (hitTestTab/hitTestTabApprox), drawing (drawTabItem), and
// selection (activeTab_ alone - no separate "control's own selected
// item" exists to fight with any more).

namespace {

constexpr int kTabStripHeight = 22;
constexpr int kTabWidth = 120;  // room for a readable label plus the close glyph
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

// drawTabItem() paints on every WM_PAINT for the tab strip, so brushes
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

constexpr wchar_t kDragGhostClass[] = L"KestrelTabDragGhost";

// Just blits whatever bitmap the owner stashed in GWLP_USERDATA - the
// snapshot is captured once, at drag start, from the real tab strip.
LRESULT CALLBACK TabDragGhostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (HBITMAP bmp = reinterpret_cast<HBITMAP>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
            BITMAP bm{};
            GetObject(bmp, sizeof(bm), &bm);
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP old = static_cast<HBITMAP>(SelectObject(memDC, bmp));
            BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, old);
            DeleteDC(memDC);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

// Intercepted on button-DOWN (not click/up) and swallowed when it lands on
// a tab's close glyph, so a click there can't also select the tab that's
// about to disappear.
LRESULT CALLBACK FilePane::TabStripSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                       DWORD_PTR refData) {
    auto* pane = reinterpret_cast<FilePane*>(refData);

    if (msg == WM_SETFONT) {
        pane->tabFont_ = reinterpret_cast<HFONT>(wParam);
        if (LOWORD(lParam)) InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    } else if (msg == WM_GETFONT) {
        return reinterpret_cast<LRESULT>(pane->tabFont_);
    } else if (msg == WM_ERASEBKGND) {
        return 1;  // WM_PAINT below fills the whole client rect itself
    } else if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(hdc, &client, GetSysColorBrush(COLOR_BTNFACE));  // strip background past the last tab
        for (size_t i = 0; i < pane->tabRects_.size(); ++i) {
            pane->drawTabItem(hdc, pane->tabRects_[i], static_cast<int>(i));
        }
        EndPaint(hwnd, &ps);
        return 0;
    } else if (msg == WM_LBUTTONDOWN) {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int idx = pane->hitTestTab(pt);
        if (idx >= 0) {
            const RECT& tabRect = pane->tabRects_[idx];
            RECT closeRect = closeButtonRectFor(tabRect);
            if (PtInRect(&closeRect, pt)) {
                pane->closeTab(idx);
                pane->activate();
                return 0;
            }
            // Not the close glyph - this press might turn into a
            // reorder/cross-pane drag; WM_MOUSEMOVE below decides once it
            // crosses the drag threshold. Selecting the tab is deferred to
            // WM_LBUTTONUP (only if no drag actually happened) rather than
            // done here immediately: switchToTab() kicks off a
            // re-enumeration (FilePane::navigate) whose cost scales with
            // the tab's folder, and a fast click-drag-release on a
            // non-active tab could finish the whole gesture (button down,
            // past the drag threshold, button up) before that call even
            // returns - swallowing the drag threshold check and the
            // eventual drop entirely, since dragActive_ never got set.
            // Reported as: dragging a background tab across to the other
            // pane visibly tracks the ghost but never actually drops it,
            // while dragging the already-active tab (switchToTab() is a
            // same-tab no-op there) works fine.
            pane->dragTabIndex_ = idx;
            pane->dragActive_ = false;
            pane->dragStartPt_ = pt;
        }

        // Reassert focus onto the file list - this is what makes this
        // the active pane, same as clicking inside its file list would
        // (covers a tab, its close glyph already handled above, and
        // empty tab-strip space).
        pane->activate();
        return 0;
    } else if (msg == WM_LBUTTONUP) {
        if (pane->dragActive_) {
            if (pane->otherPane_) pane->otherPane_->setTabDropHighlight(-1);
            if (pane->crossPaneDropIndex_ >= 0 && pane->otherPane_) {
                pane->otherPane_->receiveTabFromOtherPane(*pane, pane->dragTabIndex_, pane->crossPaneDropIndex_);
            }
            pane->endTabDrag();
        } else if (pane->dragTabIndex_ >= 0) {
            // Never crossed the drag threshold - a plain click, which
            // still needs to select the tab like clicking any tab strip
            // normally would.
            pane->switchToTab(pane->dragTabIndex_);
        }
        pane->crossPaneDropIndex_ = -1;
        pane->dragTabIndex_ = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        pane->activate();
        return 0;
    } else if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);  // re-arm each move; harmless if already tracking

        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        if ((wParam & MK_LBUTTON) && pane->dragTabIndex_ >= 0) {
            if (!pane->dragActive_) {
                const int cx = GetSystemMetrics(SM_CXDRAG);
                const int cy = GetSystemMetrics(SM_CYDRAG);
                if (std::abs(pt.x - pane->dragStartPt_.x) > cx || std::abs(pt.y - pane->dragStartPt_.y) > cy) {
                    pane->dragActive_ = true;
                    pane->beginTabDrag(pane->dragTabIndex_, pt);
                    // Once the cursor leaves the tab strip (e.g. down into
                    // the file list) it stops being this window's mouse
                    // at all - without an explicit capture, the eventual
                    // button-up fires on whatever's under the cursor
                    // instead, so this handler's WM_LBUTTONUP (and its
                    // endTabDrag()) never runs and the ghost popup is
                    // left on screen. Capture pins every subsequent mouse
                    // message to this window regardless of what it's
                    // over, and WM_CAPTURECHANGED below covers capture
                    // being stolen out from under the drag.
                    SetCapture(hwnd);
                }
            }
            if (pane->dragActive_) {
                POINT screenPt = pt;
                ClientToScreen(hwnd, &screenPt);
                RECT otherRect{};
                const bool overOther = pane->otherPane_ && (otherRect = pane->otherPane_->tabStripScreenRect(),
                                                              PtInRect(&otherRect, screenPt));
                if (overOther) {
                    pane->crossPaneDropIndex_ = pane->otherPane_->hitTestScreenPoint(screenPt);
                    pane->otherPane_->setTabDropHighlight(pane->crossPaneDropIndex_);
                } else {
                    if (pane->crossPaneDropIndex_ != -1 && pane->otherPane_) pane->otherPane_->setTabDropHighlight(-1);
                    pane->crossPaneDropIndex_ = -1;

                    const int overIdx = pane->hitTestTabApprox(pt);
                    if (overIdx >= 0 && overIdx != pane->dragTabIndex_) {
                        pane->moveTab(pane->dragTabIndex_, overIdx);
                        pane->dragTabIndex_ = overIdx;  // keep tracking the same logical tab as it slides past others
                    }
                }
                pane->updateTabDragGhost(pt);
                return 0;  // skip the close-hover hit-test below while mid-drag
            }
        }

        const int idx = pane->hitTestTab(pt);
        int hovered = -1;
        if (idx >= 0) {
            RECT hoverRect = closeHoverRectFor(pane->tabRects_[idx]);
            if (PtInRect(&hoverRect, pt)) hovered = idx;
        }
        pane->setHoveredCloseTab(hovered);
    } else if (msg == WM_MBUTTONUP) {
        // Middle-click closes a tab, as in browsers and Explorer.
        const int idx = pane->hitTestTab({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (idx >= 0) pane->closeTab(idx);
        return 0;
    } else if (msg == WM_MOUSELEAVE) {
        pane->setHoveredCloseTab(-1);
    } else if (msg == WM_CAPTURECHANGED) {
        // Something else stole the mouse capture mid-drag (e.g. a dialog
        // popped up) - drop the ghost rather than leave it stuck on
        // screen with no matching button-up ever arriving.
        if (pane->dragActive_) {
            if (pane->otherPane_) pane->otherPane_->setTabDropHighlight(-1);
            pane->endTabDrag();
        }
        pane->crossPaneDropIndex_ = -1;
        pane->dragTabIndex_ = -1;
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
    updateSortArrow();  // each tab has its own sort column/direction

    // The same physical ListView still holds the outgoing tab's selected
    // rows; clear them so they don't merge into this tab's restored set.
    ListView_SetItemState(hwnd_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);

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

    // Always re-enumerate, even with cached entries: the cache is shown
    // immediately, but it may be stale (a background tab has no watcher -
    // e.g. its files were just dragged onto another tab), and this is also
    // what re-arms watcher_ onto this tab's folder. Same-path results keep
    // the selection/scroll restored above.
    if (!live_.path.empty()) navigate(live_.path, false);
}

void FilePane::switchToTab(int index) {
    if (index == activeTab_ || index < 0 || index >= static_cast<int>(tabs_.size())) return;
    const bool wasRecycleBin = (live_.path == kRecycleBinPath);
    syncActiveTabIntoStorage();
    loadTabIntoLive(index);
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    const bool isRecycleBin = (live_.path == kRecycleBinPath);
    if (isRecycleBin != wasRecycleBin && onEmptyButtonVisibilityChanged) onEmptyButtonVisibilityChanged();
    if (onNavigated) onNavigated(*this);
}

void FilePane::cycleTab(bool forward) {
    if (tabs_.size() <= 1) return;
    switchToTab(TabCycle::nextIndex(activeTab_, static_cast<int>(tabs_.size()), forward));
}

void FilePane::beginTabDrag(int index, POINT clientPt) {
    if (!dragGhost_) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = TabDragGhostProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kDragGhostClass;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

        // Plain opaque popup, not WS_EX_LAYERED - a layered window forces
        // DWM to recomposite on every SetWindowPos while it follows the
        // cursor, which visibly lagged. An opaque snapshot moves for free.
        dragGhost_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kDragGhostClass, L"", WS_POPUP, 0, 0, 0, 0,
                                     tabHwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!dragGhost_) return;
    if (index < 0 || static_cast<size_t>(index) >= tabRects_.size()) return;

    const RECT tabRect = tabRects_[index];
    const int w = tabRect.right - tabRect.left;
    const int h = tabRect.bottom - tabRect.top;
    if (w <= 0 || h <= 0) return;

    HDC srcDC = GetDC(tabHwnd_);
    HDC memDC = CreateCompatibleDC(srcDC);
    HBITMAP bmp = CreateCompatibleBitmap(srcDC, w, h);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(memDC, bmp));
    BitBlt(memDC, 0, 0, w, h, srcDC, tabRect.left, tabRect.top, SRCCOPY);
    SelectObject(memDC, old);
    DeleteDC(memDC);
    ReleaseDC(tabHwnd_, srcDC);

    if (dragGhostBitmap_) DeleteObject(dragGhostBitmap_);
    dragGhostBitmap_ = bmp;
    SetWindowLongPtrW(dragGhost_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(bmp));

    dragGhostOffset_ = {clientPt.x - tabRect.left, clientPt.y - tabRect.top};

    POINT screenOrigin{tabRect.left, tabRect.top};
    ClientToScreen(tabHwnd_, &screenOrigin);
    SetWindowPos(dragGhost_, HWND_TOPMOST, screenOrigin.x, screenOrigin.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(dragGhost_, nullptr, FALSE);

    // The snapshot above is captured *before* this - blanking the real
    // tab's own slot now that the ghost is ready to stand in for it.
    // dragActive_ is already true by the time this runs, so the repaint
    // this triggers paints that rect blank (see drawTabItem).
    InvalidateRect(tabHwnd_, &tabRect, TRUE);
}

void FilePane::updateTabDragGhost(POINT clientPt) {
    if (!dragGhost_) return;

    // Only a reorder within the tab strip (or, now, a drop onto the other
    // pane's strip) is meaningful - once the cursor leaves both, a
    // floating tab snapshot sitting over unrelated UI just reads as a
    // stray glitch. Clamp the ghost's own position to the union of this
    // pane's strip and the other pane's strip instead of hiding it, so it
    // stays put at whichever edge rather than popping in and out as the
    // cursor wanders back and forth across a boundary.
    RECT clampRect = tabStripScreenRect();
    if (otherPane_) {
        RECT otherRect = otherPane_->tabStripScreenRect();
        UnionRect(&clampRect, &clampRect, &otherRect);
    }
    POINT topLeft{clampRect.left, clampRect.top};
    POINT bottomRight{clampRect.right, clampRect.bottom};

    RECT ghostRect{};
    GetWindowRect(dragGhost_, &ghostRect);
    const int gw = ghostRect.right - ghostRect.left;
    const int gh = ghostRect.bottom - ghostRect.top;

    POINT screenPt = clientPt;
    ClientToScreen(tabHwnd_, &screenPt);
    LONG x = screenPt.x - dragGhostOffset_.x;
    LONG y = screenPt.y - dragGhostOffset_.y;
    x = std::clamp<LONG>(x, topLeft.x, std::max<LONG>(topLeft.x, bottomRight.x - gw));
    y = std::clamp<LONG>(y, topLeft.y, std::max<LONG>(topLeft.y, bottomRight.y - gh));

    SetWindowPos(dragGhost_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void FilePane::endTabDrag() {
    // Resets the drag state itself (not just the ghost) so every call
    // site can just call this instead of separately remembering to also
    // clear dragActive_/dragTabIndex_ and repaint the real tab's slot
    // that was left blank for it.
    dragActive_ = false;
    dragTabIndex_ = -1;
    if (dragGhost_) ShowWindow(dragGhost_, SW_HIDE);
    if (dragGhostBitmap_) {
        DeleteObject(dragGhostBitmap_);
        dragGhostBitmap_ = nullptr;
    }
    InvalidateRect(tabHwnd_, nullptr, TRUE);
}

std::vector<RECT> FilePane::computeTabLayout(int width) const {
    std::vector<RECT> rects;
    rects.reserve(tabs_.size());
    int x = 0, y = 0;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (x > 0 && x + kTabWidth > width) {
            x = 0;
            y += kTabStripHeight;
        }
        rects.push_back(RECT{x, y, x + kTabWidth, y + kTabStripHeight});
        x += kTabWidth;
    }
    return rects;
}

void FilePane::relayoutTabs() {
    tabRects_ = computeTabLayout(tabStripWidth_);
}

int FilePane::tabRowCount() const {
    if (tabRects_.empty()) return 1;
    return tabRects_.back().top / kTabStripHeight + 1;
}

int FilePane::hitTestTab(POINT pt) const {
    for (size_t i = 0; i < tabRects_.size(); ++i) {
        if (PtInRect(&tabRects_[i], pt)) return static_cast<int>(i);
    }
    return -1;
}

// Used while dragging: a point can land in the strip's trailing margin
// or between rows, where no tab rect actually covers it - falls back to
// whichever tab rect is physically nearest, so a drag can still register
// crossing into a mostly-empty row.
int FilePane::hitTestTabApprox(POINT pt) const {
    const int exact = hitTestTab(pt);
    if (exact >= 0) return exact;

    int best = -1;
    long long bestDist = 0;
    for (size_t i = 0; i < tabRects_.size(); ++i) {
        const RECT& r = tabRects_[i];
        long dx = 0, dy = 0;
        if (pt.x < r.left) dx = r.left - pt.x;
        else if (pt.x >= r.right) dx = pt.x - r.right + 1;
        if (pt.y < r.top) dy = r.top - pt.y;
        else if (pt.y >= r.bottom) dy = pt.y - r.bottom + 1;
        const long long dist = static_cast<long long>(dx) * dx + static_cast<long long>(dy) * dy;
        if (best < 0 || dist < bestDist) {
            best = static_cast<int>(i);
            bestDist = dist;
        }
    }
    return best;
}

void FilePane::moveTab(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= static_cast<int>(tabs_.size()) ||
        to >= static_cast<int>(tabs_.size())) {
        return;
    }

    // Make sure the active tab's stored content (path/entries/sort/etc.)
    // is current before reindexing it - the active tab's real state lives
    // in live_, not tabs_, until this runs.
    syncActiveTabIntoStorage();

    TabState moved = std::move(tabs_[from]);
    tabs_.erase(tabs_.begin() + from);
    tabs_.insert(tabs_.begin() + to, std::move(moved));

    // The active tab keeps its identity across the reorder even though
    // its numeric index shifts.
    if (activeTab_ == from) {
        activeTab_ = to;
    } else if (from < activeTab_ && activeTab_ <= to) {
        --activeTab_;
    } else if (to <= activeTab_ && activeTab_ < from) {
        ++activeTab_;
    }
    hoveredCloseTab_ = -1;  // indices just shifted; next WM_MOUSEMOVE recomputes this

    // Tab count and width are unchanged, so this is always the same
    // fixed-size rects in a new order - never a different row count,
    // unlike the old SysTabControl32 version of this.
    relayoutTabs();
    InvalidateRect(tabHwnd_, nullptr, TRUE);
}

void FilePane::invalidateTabsMatchingPath(const std::wstring& path) {
    bool liveMatched = false;
    for (auto& t : tabs_) {
        if (_wcsicmp(t.content.path.c_str(), path.c_str()) == 0) t.content.entries.clear();
    }
    if (_wcsicmp(live_.path.c_str(), path.c_str()) == 0) liveMatched = true;
    if (liveMatched) refresh();
}

void FilePane::reloadAllTabs() {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (static_cast<int>(i) == activeTab_) continue;  // live_ is refreshed below
        tabs_[i].content.entries.clear();  // re-enumerated next time the tab is shown
        tabs_[i].selectedIndices.clear();
        tabs_[i].focusedIndex = -1;
    }
    refresh();
}

const std::wstring& FilePane::tabPathAt(int index) const {
    static const std::wstring kNone;
    if (index == activeTab_) return live_.path;
    if (index < 0 || static_cast<size_t>(index) >= tabs_.size()) return kNone;
    return tabs_[index].content.path;
}

FileDropTarget::Hit FilePane::tabDropHitTest(POINT pt) {
    const int idx = hitTestTab(pt);
    const ULONGLONG now = GetTickCount64();
    // OLE calls this repeatedly while the cursor rests; a gap means a new
    // drag (or re-entry), which starts its own delay.
    if (idx != tabDropTimingIndex_ || now - tabDropLastHit_ > 250) {
        tabDropTimingIndex_ = idx;
        tabDropHoverSince_ = now;
    } else if (idx >= 0 && idx != activeTab_ && now - tabDropHoverSince_ >= kTabDropSwitchDelayMs) {
        switchToTab(idx);  // so the drag can continue into a folder inside that tab
    }
    tabDropLastHit_ = now;
    if (idx < 0) return {};
    return {DropTargetPath::candidates(tabPathAt(idx), L"", false), idx + 1};
}

void FilePane::setTabDropHighlight(int index) {
    tabDropHoverIndex_ = index;
    InvalidateRect(tabHwnd_, nullptr, FALSE);
}

void FilePane::openInBackgroundTab(const std::wstring& path) {
    TabState t;
    t.content.path = path;
    t.content.sortColumn = defaultSortColumn_;
    t.content.sortAscending = defaultSortAscending_;
    tabs_.insert(tabs_.begin() + activeTab_ + 1, std::move(t));  // after the active one; its index is unchanged
    hoveredCloseTab_ = -1;
    relayoutTabs();
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    if (onTabCountChanged) onTabCountChanged();
}

void FilePane::newTab() {
    syncActiveTabIntoStorage();

    TabState t;
    t.content.path = kThisPcPath;  // a fresh tab starts at the drive list; duplicateTab() copies the current one
    t.content.sortColumn = defaultSortColumn_;
    t.content.sortAscending = defaultSortAscending_;
    tabs_.push_back(std::move(t));
    const int newIndex = static_cast<int>(tabs_.size()) - 1;

    relayoutTabs();
    loadTabIntoLive(newIndex);  // starts empty, so this also kicks off the enumeration
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    if (onTabCountChanged) onTabCountChanged();
}

void FilePane::duplicateTab() {
    syncActiveTabIntoStorage();
    TabState copy = tabs_[activeTab_];
    const int newIndex = activeTab_ + 1;
    tabs_.insert(tabs_.begin() + newIndex, std::move(copy));
    hoveredCloseTab_ = -1;  // indices after the insertion point just shifted
    relayoutTabs();
    loadTabIntoLive(newIndex);  // shows the copied listing at once, then re-enumerates it
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    if (onTabCountChanged) onTabCountChanged();
}

void FilePane::closeTab(int index) {
    if (tabs_.size() <= 1) return;  // always keep at least one tab
    if (index < 0) index = activeTab_;
    if (index >= static_cast<int>(tabs_.size())) return;
    removeTab(index);
}

// Erases tabs_[index] and returns its saved content, fixing up
// activeTab_/hoveredCloseTab_/layout the same way whether the tab is being
// discarded (closeTab, which never lets this empty tabs_) or handed off to
// the other pane (receiveTabFromOtherPane, which does allow it - taking a
// pane's very last tab empties it, and the caller is responsible for then
// hiding that pane; see FilePane::hasNoTabs).
FilePane::TabState FilePane::removeTab(int index) {
    const bool closingActive = (index == activeTab_);
    // tabs_[activeTab_] only ever holds a snapshot from the last time
    // something synced it (a tab switch/reorder) - live_ is where the
    // active tab's actual current state (path/scroll/selection) lives in
    // the meantime. Bring it up to date before taking tabs_[index],
    // otherwise removing/handing off the active tab silently reverts it
    // to however it looked as of that last sync.
    if (closingActive) syncActiveTabIntoStorage();
    TabState removed = std::move(tabs_[index]);
    tabs_.erase(tabs_.begin() + index);
    hoveredCloseTab_ = -1;  // indices just shifted; next WM_MOUSEMOVE recomputes this
    relayoutTabs();

    if (closingActive) {
        // tabs_ can be empty here (the last tab was just taken) - nothing
        // left to load into live_, and the pane is about to be hidden by
        // the caller rather than kept showing whatever live_ still holds.
        if (!tabs_.empty()) {
            const int newIndex = std::min(index, static_cast<int>(tabs_.size()) - 1);
            loadTabIntoLive(newIndex);
            if (onNavigated) onNavigated(*this);
        }
    } else if (activeTab_ > index) {
        --activeTab_;  // a tab before the active one shifted left
    }
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    if (onTabCountChanged) onTabCountChanged();
    return removed;
}

RECT FilePane::tabStripScreenRect() const {
    RECT r{};
    GetClientRect(tabHwnd_, &r);
    MapWindowPoints(tabHwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

int FilePane::hitTestScreenPoint(POINT screenPt) const {
    POINT client = screenPt;
    ScreenToClient(tabHwnd_, &client);
    return hitTestTabApprox(client);
}

// `source` gives up tabs_[sourceIndex] - even its last-remaining tab: a
// pane with zero tabs left is expected (FilePane::hasNoTabs, checked by
// MainWindow's onTabCountChanged handler, which falls back to single-pane
// mode showing whichever pane still has tabs - a dedicated pane for
// browsing is a bigger commitment than a single tab, so losing the last
// one closes the pane rather than refusing the drag). Inserted here at
// atIndex (clamped to a valid position) and made the active tab, matching
// how a dropped/dragged-in tab reads as "now showing" rather than a silent
// background addition.
void FilePane::receiveTabFromOtherPane(FilePane& source, int sourceIndex, int atIndex) {
    if (&source == this) return;
    if (sourceIndex < 0 || static_cast<size_t>(sourceIndex) >= source.tabs_.size()) return;

    TabState moved = source.removeTab(sourceIndex);

    if (atIndex < 0 || atIndex > static_cast<int>(tabs_.size())) atIndex = static_cast<int>(tabs_.size());
    tabs_.insert(tabs_.begin() + atIndex, std::move(moved));
    hoveredCloseTab_ = -1;
    relayoutTabs();
    loadTabIntoLive(atIndex);
    InvalidateRect(tabHwnd_, nullptr, TRUE);
    if (onTabCountChanged) onTabCountChanged();
    if (onNavigated) onNavigated(*this);
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
    for (const auto& path : paths) {
        TabState t;
        t.content.path = path;
        t.content.sortColumn = defaultSortColumn_;
        t.content.sortAscending = defaultSortAscending_;
        tabs_.push_back(std::move(t));
    }

    relayoutTabs();
    const int idx = std::clamp(activeIndex, 0, static_cast<int>(tabs_.size()) - 1);
    loadTabIntoLive(idx);  // entries start empty, so this kicks off enumeration for the active tab
    InvalidateRect(tabHwnd_, nullptr, TRUE);
}

void FilePane::setHoveredCloseTab(int index) {
    if (index == hoveredCloseTab_) return;

    auto invalidateTab = [this](int idx) {
        if (idx < 0 || static_cast<size_t>(idx) >= tabRects_.size()) return;
        InvalidateRect(tabHwnd_, &tabRects_[idx], FALSE);
    };
    invalidateTab(hoveredCloseTab_);
    hoveredCloseTab_ = index;
    invalidateTab(hoveredCloseTab_);
}

void FilePane::drawTabItem(HDC hdc, const RECT& r, int index) {
    const bool selected = (index == activeTab_);

    if (dragActive_ && index == dragTabIndex_) {
        // The real tab's own slot sits blank while its snapshot ghost is
        // being dragged around - otherwise the same label would visibly
        // show twice, once for real and once floating under the cursor.
        FillRect(hdc, &r, GetSysColorBrush(COLOR_BTNFACE));
        return;
    }

    FillRect(hdc, &r, GetSysColorBrush(selected ? COLOR_WINDOW : COLOR_BTNFACE));

    if (selected) {
        RECT underline{r.left, r.bottom - 2, r.right, r.bottom};
        FillRect(hdc, &underline, cachedBrushFor(kActiveTabAccent));
    }

    const std::wstring& tabPath = (index == activeTab_) ? live_.path
                                   : (index >= 0 && static_cast<size_t>(index) < tabs_.size()) ? tabs_[index].content.path
                                                                                                : std::wstring{};
    const std::wstring label = NameParts::tabLabel(tabPath);

    HFONT font = tabFont_ ? tabFont_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
    SetBkMode(hdc, TRANSPARENT);

    RECT textRect = r;
    textRect.left += 6;
    textRect.right -= (kCloseGlyphSize + kCloseGlyphMargin * 2);

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
    DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (index == tabDropHoverIndex_) {
        // Drop target outline: a 2px highlight-colored frame.
        RECT frame = r;
        FrameRect(hdc, &frame, GetSysColorBrush(COLOR_HIGHLIGHT));
        InflateRect(&frame, -1, -1);
        FrameRect(hdc, &frame, GetSysColorBrush(COLOR_HIGHLIGHT));
    }

    if (tabs_.size() > 1) {
        const bool hovered = (index == hoveredCloseTab_);
        if (hovered) {
            RECT hoverRect = closeHoverRectFor(r);
            HRGN rgn = CreateRoundRectRgn(hoverRect.left, hoverRect.top, hoverRect.right + 1, hoverRect.bottom + 1, 4, 4);
            FillRgn(hdc, rgn, GetSysColorBrush(COLOR_BTNSHADOW));
            DeleteObject(rgn);
        }
        // Drawn as two GDI lines rather than the "×" glyph - a font's
        // glyph has its own ascent/descent baked in, off-center from its
        // nominal box in a way that made DT_VCENTER read a bit low no
        // matter how the rect was nudged. Plain lines land exactly where
        // the rect says, in any font, at any DPI.
        RECT closeRect = closeButtonRectFor(r);
        HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(hovered ? COLOR_WINDOW : COLOR_GRAYTEXT));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        constexpr int kGlyphInset = 3;
        // LineTo excludes its own endpoint pixel - without the +1s the
        // bottom tip of each stroke was left undrawn.
        MoveToEx(hdc, closeRect.left + kGlyphInset, closeRect.top + kGlyphInset, nullptr);
        LineTo(hdc, closeRect.right - kGlyphInset + 1, closeRect.bottom - kGlyphInset + 1);
        MoveToEx(hdc, closeRect.right - kGlyphInset, closeRect.top + kGlyphInset, nullptr);
        LineTo(hdc, closeRect.left + kGlyphInset - 1, closeRect.bottom - kGlyphInset + 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    SelectObject(hdc, old);
}
