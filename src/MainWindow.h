#pragma once

#include "FilePane.h"
#include "PreviewPane.h"
#include "ShellContextMenu.h"
#include "Session.h"
#include "SplitterController.h"
#include "TreePane.h"
#include "WindowLayout.h"

#include <windows.h>
#include <optional>

// The application's single top-level window: menu, toolbar + address bar,
// status bar, and the tree/left-pane/right-pane layout with three draggable
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
    // If `focus` is the address bar, forwards it a standard edit message
    // (WM_COPY/WM_CUT/WM_PASTE/EM_SETSEL) and returns true; otherwise
    // does nothing and returns false, so the caller can fall back to the
    // pane/clipboard equivalent.
    bool forwardToAddressBar(HWND focus, UINT msg, WPARAM wParam, LPARAM lParam);
    void chooseFont();
    void applyFont(const LOGFONTW& lf);
    void applyDefaultSort(int column, bool ascending);
    void applyShowHidden(bool show);
    LRESULT onNotify(LPARAM lParam);
    void onPaint();
    void onContextMenu(HWND target, int screenX, int screenY);

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
    void onLButtonUp(int x, int y);
    void cancelSplitterDrag();

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HACCEL hAccel_ = nullptr;

    // Loaded in create(), before the window/children exist, and consumed
    // once by onCreate() to restore tabs/splitters - nullopt on first run.
    std::optional<SessionData> pendingSession_;

    HWND toolbar_ = nullptr;
    HWND addressBar_ = nullptr;
    HWND statusBar_ = nullptr;

    // Non-null only when the user has picked a custom UI font via Tools >
    // Options (ChooseFontW) - null means every control just keeps using
    // its own default (DEFAULT_GUI_FONT). Owned here; destroyed on
    // replacement and on window teardown.
    HFONT customFont_ = nullptr;
    std::wstring fontFamily_;  // empty = no override, matches SessionData's convention
    int fontSize_ = 0;
    bool fontBold_ = false;

    // Sort column/direction new tabs start out with (Tools > Options >
    // 並び順(既定)) - mirrors FilePane::setDefaultSort's static state so
    // the menu's radio/checkbox marks can be kept in sync.
    HMENU sortMenu_ = nullptr;
    int sortColumn_ = 0;
    bool sortAscending_ = true;

    HMENU viewMenu_ = nullptr;
    bool showHidden_ = true;  // mirrors FilePane::setShowHidden for the menu check and the session

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
    WindowLayout::Input layoutInput_{};
    SplitterController splitter_;

    // Outer (un-inset) rects of the two file panes, used to paint a
    // highlighted frame around whichever one is active - selection color
    // alone isn't a reliable "which pane is active" cue when a pane has
    // nothing selected.
    RECT leftOuterRect_{};
    RECT rightOuterRect_{};

    ShellContextMenu shellMenu_;
};
