#include "SplitterController.h"

namespace {

// Owned layered popup: moving the guide reuses its cached bitmap rather
// than invalidating the file lists underneath a moving child window.
LRESULT CALLBACK SplitterGuideProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        DrawFocusRect(dc, &rect);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool SplitterController::createGuide(HWND owner, HINSTANCE instance) {
    if (guide_) return true;

    constexpr wchar_t name[] = L"KestrelSplitterGuide";
    WNDCLASSW wc{};
    wc.lpfnWndProc = SplitterGuideProc;
    wc.hInstance = instance;
    wc.lpszClassName = name;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    guide_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, name, L"",
                              WS_POPUP, 0, 0, 0, 0, owner, nullptr, instance, nullptr);
    if (guide_ && !SetLayeredWindowAttributes(guide_, RGB(255, 255, 255), 0, LWA_COLORKEY)) {
        DestroyWindow(guide_);
        guide_ = nullptr;
    }
    return guide_ != nullptr;
}

void SplitterController::setRects(const RECT& splitter1, const RECT& splitter2, const RECT& splitter3) {
    splitter1Rect_ = splitter1;
    splitter2Rect_ = splitter2;
    splitter3Rect_ = splitter3;
}

void SplitterController::beginDrag(int id, HWND owner, int x, int y) {
    draggingSplitter_ = id;
    updateDrag(owner, x, y);
}

void SplitterController::updateDrag(HWND owner, int x, int y) {
    if (!draggingSplitter_ || !guide_) return;

    pendingLayout_ = WindowLayout::previewSplitter(layoutInput_, draggingSplitter_, x, y);
    const auto& r = draggingSplitter_ == 1   ? pendingLayout_.splitter1
                     : draggingSplitter_ == 2 ? pendingLayout_.splitter2
                                               : pendingLayout_.splitter3;
    POINT origin{r.left, r.top};
    ClientToScreen(owner, &origin);  // Owned popups use screen coordinates.
    const RECT bounds{origin.x, origin.y, origin.x + r.right - r.left, origin.y + r.bottom - r.top};
    const bool visible = IsWindowVisible(guide_) != FALSE;
    if (visible && EqualRect(&bounds, &guideBounds_)) return;
    if (SetWindowPos(guide_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        guideBounds_ = bounds;
    }
    // Flush only the guide's initial/size-change paint. Position-only moves
    // reuse the layered window image without repainting either pane.
    UpdateWindow(guide_);
}

WindowLayout::Result SplitterController::endDrag(HWND owner, int x, int y) {
    updateDrag(owner, x, y);  // Use the release location, even without a final mouse-move message.
    const auto result = pendingLayout_;
    cancelDrag();
    return result;
}

void SplitterController::cancelDrag() {
    if (!draggingSplitter_) return;
    draggingSplitter_ = 0;
    if (guide_) ShowWindow(guide_, SW_HIDE);
}
