#pragma once

#include <windows.h>

// Layout callers repaint once all siblings have reached their final bounds.
// Never copy old client pixels across the other pane during relocation.
inline void placeWithoutRedraw(HWND window, int x, int y, int width, int height) {
    SetWindowPos(window, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
}
