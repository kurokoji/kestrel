#include "App.h"
#include "MainWindow.h"

#include <commctrl.h>
#include <objbase.h>
#include <objidl.h>
#include <ole2.h>
#include <gdiplus.h>

int App::run(HINSTANCE hInstance, int nCmdShow) {
    // OleInitialize (not plain CoInitializeEx) is required on this thread
    // before DoDragDrop will work - it's what FilePane's OLE drag-and-drop
    // source uses to let items be dragged out to Explorer/browsers/etc.
    OleInitialize(nullptr);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    INITCOMMONCONTROLSEX icc{sizeof(icc),
                              ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES |
                                  ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    MainWindow mainWindow;
    if (!mainWindow.create(hInstance, nCmdShow)) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
        OleUninitialize();
        return 1;
    }

    const HACCEL hAccel = mainWindow.accelTable();
    const HWND hwndMain = mainWindow.hwnd();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Tab switches the active file pane; handled here (not via the
        // accelerator table) so it works regardless of which child control
        // currently has focus.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000) && GetAncestor(msg.hwnd, GA_ROOT) == hwndMain) {
            mainWindow.switchActivePane();
            continue;
        }

        if (!TranslateAcceleratorW(hwndMain, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    Gdiplus::GdiplusShutdown(gdiplusToken);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
