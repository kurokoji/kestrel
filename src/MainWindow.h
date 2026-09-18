#pragma once

#include "FilePane.h"
#include "PreviewPane.h"
#include "Session.h"
#include "TreePane.h"

#include <windows.h>
#include <optional>

struct IContextMenu3;  // avoids pulling <shobjidl.h> into every includer of this header

// The application's single top-level window: menu, toolbar + address bar,
// status bar, and the tree/left-pane/right-pane layout with two draggable
// splitters. Owns and wires together the child components; does not itself
// know how to enumerate directories or perform file operations.
class MainWindow {
public:
    bool create(HINSTANCE hInstance, int nCmdShow);
    HWND hwnd() const { return hwnd_; }
    HACCEL accelTable() const { return hAccel_; }

    void switchActivePane();
    void onAddressBarEnter();
    void onXButton(WORD xButton);

private:
    static LRESULT CALLBACK staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void onCreate();
    void saveSession();
    void createMenuBar();
    void createToolbar();
    void createAddressBar();
    void createStatusBar();
    void layoutChildren();

    void onCommand(int id, HWND ctrl);
    LRESULT onNotify(LPARAM lParam);
    void onPaint();
    void onContextMenu(HWND target, int screenX, int screenY);
    void showShellContextMenuForItems(FilePane& pane, const std::vector<std::wstring>& paths, POINT screenPt);

    void refreshUiForActivePane();
    void updateStatusBar();
    void updateActivePaneFrame();
    void updatePreview();

    FilePane& activePane();
    FilePane& inactivePane();

    void doCopyToOther();
    void doMoveToOther();
    void doClipboardCopy(bool cut);
    void doClipboardPaste();

    void onLButtonDown(int x, int y);
    void onMouseMove(int x, int y);
    void onLButtonUp();

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HACCEL hAccel_ = nullptr;

    // Loaded in create(), before the window/children exist, and consumed
    // once by onCreate() to restore tabs/splitters - nullopt on first run.
    std::optional<SessionData> pendingSession_;

    HWND toolbar_ = nullptr;
    HWND addressBar_ = nullptr;
    HWND statusBar_ = nullptr;

    TreePane tree_;
    PreviewPane preview_;
    FilePane left_;
    FilePane right_;
    int activePaneId_ = 0;  // 0 = left, 1 = right
    bool singlePaneMode_ = false;  // true = only the active pane is shown, full width

    int treeWidth_ = 170;
    int previewHeight_ = 200;
    int leftWidth_ = 360;
    int lastLayoutWidth_ = 0;  // client width as of the previous layout pass, to scale columns proportionally on resize
    int contentTop_ = 0;      // y of the tree/pane row, cached for splitter drag math
    int contentHeight_ = 0;   // height of that row
    int draggingSplitter_ = 0;  // 0 = none, 1 = tree|left, 2 = left|right, 3 = tree|preview
    RECT splitter1Rect_{};
    RECT splitter2Rect_{};
    RECT splitter3Rect_{};

    // Outer (un-inset) rects of the two file panes, used to paint a
    // highlighted frame around whichever one is active - selection color
    // alone isn't a reliable "which pane is active" cue when a pane has
    // nothing selected.
    RECT leftOuterRect_{};
    RECT rightOuterRect_{};

    // Non-owning; set only while TrackPopupMenu is running for a real
    // shell context menu, so WM_INITMENUPOPUP/WM_DRAWITEM/WM_MEASUREITEM/
    // WM_MENUCHAR (sent to us, the menu's owner, during that nested loop)
    // can be forwarded to it - required for submenus like "Send to" and
    // for icons to render correctly.
    IContextMenu3* activeShellMenu_ = nullptr;
};
