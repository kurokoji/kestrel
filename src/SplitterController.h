#pragma once

#include "WindowLayout.h"

#include <windows.h>

// Owns the three-splitter (tree|left, left|right, tree|preview) drag state
// and the dotted layered-popup guide used to preview a drag before it
// commits on release. Mouse capture stays with MainWindow (SetCapture /
// ReleaseCapture need the owner's HWND and MainWindow already reacts to
// WM_CAPTURECHANGED/WM_CANCELMODE) - this class only tracks which splitter
// is being dragged, the pending (not-yet-applied) layout, and the guide
// window itself.
class SplitterController {
public:
    // Creates the guide popup on first use; a no-op (returns true) if it
    // already exists. Returns false only if window creation itself fails.
    bool createGuide(HWND owner, HINSTANCE instance);

    void setRects(const RECT& splitter1, const RECT& splitter2, const RECT& splitter3);
    const RECT& splitter1Rect() const { return splitter1Rect_; }
    const RECT& splitter2Rect() const { return splitter2Rect_; }
    const RECT& splitter3Rect() const { return splitter3Rect_; }

    void setLayoutInput(const WindowLayout::Input& input) { layoutInput_ = input; }

    // 0 = none, 1 = tree|left, 2 = left|right, 3 = tree|preview
    int draggingSplitter() const { return draggingSplitter_; }
    bool dragging() const { return draggingSplitter_ != 0; }

    void beginDrag(int id, HWND owner, int x, int y);
    void updateDrag(HWND owner, int x, int y);
    // Applies the release-point position, ends the drag, and returns the
    // layout the caller should commit (treeWidth/leftWidth/previewHeight).
    WindowLayout::Result endDrag(HWND owner, int x, int y);
    // Hides the guide and drops the pending drag without committing it.
    void cancelDrag();

private:
    HWND guide_ = nullptr;
    int draggingSplitter_ = 0;
    RECT splitter1Rect_{};
    RECT splitter2Rect_{};
    RECT splitter3Rect_{};
    RECT guideBounds_{};
    WindowLayout::Input layoutInput_{};
    WindowLayout::Result pendingLayout_{};
};
