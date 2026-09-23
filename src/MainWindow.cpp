#include "MainWindow.h"
#include "WindowLayout.h"
#include "WindowPlacement.h"
#include "ClipboardFiles.h"
#include "Dialogs.h"
#include "FileOperations.h"
#include "Formatting.h"
#include "Messages.h"
#include "RecycleBinOps.h"
#include "Resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>

namespace {

constexpr wchar_t kClassName[] = L"KestrelMainWindow";
constexpr int kActiveFrameWidth = WindowLayout::activeFrameWidth;
constexpr UINT kDirChangeDebounceMs = 400;

// lfHeight is DPI/device dependent, so this always recomputes it via
// MulDiv rather than storing a raw LOGFONT - shared by chooseFont() (when
// re-opening the picker on top of an existing custom font) and onCreate()
// (restoring one from SessionData).
LOGFONTW buildLogFont(const std::wstring& family, int pointSize, bool bold, HDC hdc) {
    LOGFONTW lf{};
    wcsncpy_s(lf.lfFaceName, family.c_str(), _TRUNCATE);
    lf.lfHeight = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    return lf;
}

LRESULT CALLBACK AddressBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                         DWORD_PTR refData) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        reinterpret_cast<MainWindow*>(refData)->onAddressBarEnter();
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Mouse "Back"/"Forward" side buttons land on whichever child control the
// cursor happens to be over (tree, either file pane), not on MainWindow
// itself, so each of those gets subclassed with this to forward the click
// up rather than hooking mouse input globally.
LRESULT CALLBACK XButtonForwardSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*id*/,
                                             DWORD_PTR refData) {
    if (msg == WM_XBUTTONUP) {
        reinterpret_cast<MainWindow*>(refData)->onXButton(GET_XBUTTON_WPARAM(wParam));
        return TRUE;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

}  // namespace

bool MainWindow::create(HINSTANCE hInstance, int nCmdShow) {
    hInstance_ = hInstance;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = staticWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hIconSm = wc.hIcon;
        RegisterClassExW(&wc);
        registered = true;
    }

    hAccel_ = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDA_MAIN));

    pendingSession_ = Session::load();
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 1000, h = 650;
    if (pendingSession_) {
        x = pendingSession_->windowX;
        y = pendingSession_->windowY;
        w = pendingSession_->windowW;
        h = pendingSession_->windowH;
    }

    // The guide has its own layered surface; keep native controls out of
    // whole-window composition when committing their new bounds.
    hwnd_ = CreateWindowExW(0, kClassName, L"Kestrel Filer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr,
                             hInstance, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, (pendingSession_ && pendingSession_->maximized) ? SW_MAXIMIZE : nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK MainWindow::staticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->wndProc(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            onCreate();
            return 0;
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
            cancelSplitterDrag();
            return 0;
        case WM_SIZE:
            cancelSplitterDrag();
            layoutChildren();
            return 0;
        case WM_COMMAND: {
            const HWND ctrl = reinterpret_cast<HWND>(lParam);
            if (ctrl && HIWORD(wParam) == EN_CHANGE) {
                if (ctrl == left_.searchBoxHwnd()) {
                    left_.onSearchTextChanged();
                    return 0;
                }
                if (ctrl == right_.searchBoxHwnd()) {
                    right_.onSearchTextChanged();
                    return 0;
                }
            }
            onCommand(LOWORD(wParam), ctrl);
            return 0;
        }
        case WM_NOTIFY:
            return onNotify(lParam);
        case WM_PAINT:
            onPaint();
            return 0;
        case WM_CONTEXTMENU:
            onContextMenu(reinterpret_cast<HWND>(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_DRAWITEM:
            // The tab strips paint themselves now (a plain window, not
            // an owner-drawn SysTabControl32) - this is only still hit
            // for the shell context menu's own items, forwarded below.
        case WM_INITMENUPOPUP:
        case WM_MEASUREITEM:
        case WM_MENUCHAR: {
            // A real shell context menu is being tracked - let it handle
            // its own submenus/icons/mnemonics via these, exactly as
            // Explorer does.
            LRESULT result = 0;
            if (shellMenu_.forwardMenuMessage(msg, wParam, lParam, result)) return result;
            break;
        }
        case WM_APP_DIR_RESULT: {
            auto* pane = reinterpret_cast<FilePane*>(wParam);
            std::unique_ptr<EnumerationResult> result(reinterpret_cast<EnumerationResult*>(lParam));
            pane->handleDirResult(std::move(result));
            return 0;
        }
        case WM_APP_TREE_CHILDREN: {
            std::unique_ptr<TreeChildrenResult> result(reinterpret_cast<TreeChildrenResult*>(lParam));
            tree_.handleChildrenResult(std::move(result));
            return 0;
        }
        case WM_APP_DIR_CHANGED: {
            // Debounce: a burst of filesystem events (e.g. a big copy)
            // just keeps restarting this timer, so the actual refresh
            // only happens once things settle for kDirChangeDebounceMs.
            const UINT_PTR timerId = (reinterpret_cast<FilePane*>(wParam) == &left_) ? 1 : 2;
            SetTimer(hwnd_, timerId, kDirChangeDebounceMs, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) {
                KillTimer(hwnd_, 1);
                left_.refresh();
            } else if (wParam == 2) {
                KillTimer(hwnd_, 2);
                right_.refresh();
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            onLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            onLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd_, &pt);
                if (splitter_.draggingSplitter() == 3 ||
                    (!splitter_.dragging() && PtInRect(&splitter_.splitter3Rect(), pt))) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
                if (splitter_.dragging() || PtInRect(&splitter_.splitter1Rect(), pt) ||
                    PtInRect(&splitter_.splitter2Rect(), pt)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        }
        case WM_XBUTTONUP:
            onXButton(GET_XBUTTON_WPARAM(wParam));
            return TRUE;
        case WM_DESTROY:
            saveSession();
            if (customFont_) DeleteObject(customFont_);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::saveSession() {
    SessionData data;

    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    GetWindowPlacement(hwnd_, &wp);
    data.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    data.windowX = wp.rcNormalPosition.left;
    data.windowY = wp.rcNormalPosition.top;
    data.windowW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    data.windowH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

    data.treeWidth = treeWidth_;
    data.leftWidth = leftWidth_;
    data.previewHeight = previewHeight_;
    data.activePane = activePaneId_;
    data.singlePane = singlePaneMode_;

    data.leftTabs = left_.tabPaths();
    data.leftActiveTab = left_.activeTabIndex();
    data.rightTabs = right_.tabPaths();
    data.rightActiveTab = right_.activeTabIndex();

    data.leftColumnWidths = left_.columnWidths();
    data.rightColumnWidths = right_.columnWidths();

    data.fontFamily = fontFamily_;
    data.fontSize = fontSize_;
    data.fontBold = fontBold_;

    data.defaultSortColumn = sortColumn_;
    data.defaultSortAscending = sortAscending_;

    Session::save(data);
}

void MainWindow::applyDefaultSort(int column, bool ascending) {
    sortColumn_ = column;
    sortAscending_ = ascending;
    FilePane::setDefaultSort(sortColumn_, sortAscending_);

    static constexpr UINT kColumnIds[] = {IDM_SORT_NAME, IDM_SORT_TYPE, IDM_SORT_SIZE, IDM_SORT_MODIFIED};
    CheckMenuRadioItem(sortMenu_, IDM_SORT_NAME, IDM_SORT_MODIFIED, kColumnIds[sortColumn_], MF_BYCOMMAND);
    CheckMenuItem(sortMenu_, IDM_SORT_DESCENDING, MF_BYCOMMAND | (sortAscending_ ? MF_UNCHECKED : MF_CHECKED));
}

void MainWindow::chooseFont() {
    HDC hdc = GetDC(hwnd_);

    LOGFONTW lf{};
    if (fontFamily_.empty()) {
        HFONT stock = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        GetObjectW(stock, sizeof(lf), &lf);
    } else {
        lf = buildLogFont(fontFamily_, fontSize_, fontBold_, hdc);
    }

    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwnd_;
    cf.hDC = hdc;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS | CF_FORCEFONTEXIST;

    if (ChooseFontW(&cf)) {
        fontFamily_ = lf.lfFaceName;
        fontSize_ = cf.iPointSize / 10;
        fontBold_ = lf.lfWeight >= FW_BOLD;
        applyFont(lf);
    }

    ReleaseDC(hwnd_, hdc);
}

void MainWindow::applyFont(const LOGFONTW& lf) {
    HFONT newFont = CreateFontIndirectW(&lf);
    if (!newFont) return;

    HFONT old = customFont_;
    customFont_ = newFont;

    auto setFont = [this](HWND h) {
        if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(customFont_), TRUE);
    };
    setFont(tree_.hwnd());
    setFont(addressBar_);
    setFont(statusBar_);
    for (FilePane* pane : {&left_, &right_}) {
        setFont(pane->hwnd());
        setFont(pane->tabHwnd());
        setFont(pane->searchBoxHwnd());
        setFont(pane->newTabButtonHwnd());
        setFont(pane->emptyRecycleBinButtonHwnd());
    }

    if (old) DeleteObject(old);

    layoutChildren();  // row/column-header metrics can change with the font
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::onCreate() {
    createMenuBar();
    createToolbar();
    createAddressBar();
    createStatusBar();

    // Before any pane is created, so its very first tab already starts
    // out with the configured default (FilePane::create() pushes an
    // initial tab using whatever FilePane::setDefaultSort was last called
    // with).
    applyDefaultSort(pendingSession_ ? pendingSession_->defaultSortColumn : 0,
                      pendingSession_ ? pendingSession_->defaultSortAscending : true);

    tree_.create(hwnd_, hInstance_, IDC_TREE);
    preview_.create(hwnd_, hInstance_, IDC_PREVIEW);
    left_.create(hwnd_, hInstance_, IDC_LIST_LEFT, 0);
    right_.create(hwnd_, hInstance_, IDC_LIST_RIGHT, 1);

    SetWindowSubclass(tree_.hwnd(), XButtonForwardSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(left_.hwnd(), XButtonForwardSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(right_.hwnd(), XButtonForwardSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

    tree_.onNavigate = [this](const std::wstring& path) { activePane().navigate(path, true); };

    // Both panes navigate independently and finish asynchronously in
    // background threads; only refresh the address bar/tree/status bar
    // from whichever pane is actually active, otherwise the *other*
    // pane's completion (e.g. its own startup load) can race in and
    // stomp the UI with its (unrelated) path.
    auto onNav = [this](FilePane& p) {
        if (&p == &activePane()) {
            refreshUiForActivePane();
            updatePreview();
        }
    };
    auto onSel = [this](FilePane& p) {
        if (&p == &activePane()) {
            updateStatusBar();
            updatePreview();
        }
    };
    left_.onNavigated = onNav;
    right_.onNavigated = onNav;
    left_.onSelectionChanged = onSel;
    right_.onSelectionChanged = onSel;
    left_.onFocusChanged = [this](FilePane& p) {
        activePaneId_ = p.paneId();
        refreshUiForActivePane();
        updateActivePaneFrame();
        updatePreview();
    };
    right_.onFocusChanged = left_.onFocusChanged;

    auto onSearchVisibility = [this] { layoutChildren(); };
    left_.onSearchVisibilityChanged = onSearchVisibility;
    right_.onSearchVisibilityChanged = onSearchVisibility;

    auto onTabCountChanged = [this] { layoutChildren(); };
    left_.onTabCountChanged = onTabCountChanged;
    right_.onTabCountChanged = onTabCountChanged;

    auto onEmptyButtonVisibility = [this] { layoutChildren(); };
    left_.onEmptyButtonVisibilityChanged = onEmptyButtonVisibility;
    right_.onEmptyButtonVisibilityChanged = onEmptyButtonVisibility;

    std::wstring startPath = L"C:\\";
    PWSTR profile = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile))) {
        startPath = profile;
        CoTaskMemFree(profile);
    }

    if (pendingSession_) {
        treeWidth_ = pendingSession_->treeWidth;
        leftWidth_ = pendingSession_->leftWidth;
        previewHeight_ = pendingSession_->previewHeight;
        activePaneId_ = pendingSession_->activePane == 1 ? 1 : 0;
        singlePaneMode_ = pendingSession_->singlePane;
        SendMessageW(toolbar_, TB_CHECKBUTTON, IDM_VIEW_SINGLEPANE, MAKELONG(singlePaneMode_ ? TRUE : FALSE, 0));

        left_.setColumnWidths(pendingSession_->leftColumnWidths);
        right_.setColumnWidths(pendingSession_->rightColumnWidths);

        if (!pendingSession_->fontFamily.empty()) {
            fontFamily_ = pendingSession_->fontFamily;
            fontSize_ = pendingSession_->fontSize;
            fontBold_ = pendingSession_->fontBold;

            HDC hdc = GetDC(hwnd_);
            LOGFONTW lf = buildLogFont(fontFamily_, fontSize_, fontBold_, hdc);
            ReleaseDC(hwnd_, hdc);
            applyFont(lf);
        }

        if (!pendingSession_->leftTabs.empty()) left_.restoreTabs(pendingSession_->leftTabs, pendingSession_->leftActiveTab);
        else left_.navigate(startPath, false);

        if (!pendingSession_->rightTabs.empty()) right_.restoreTabs(pendingSession_->rightTabs, pendingSession_->rightActiveTab);
        else right_.navigate(startPath, false);

        pendingSession_.reset();
    } else {
        left_.navigate(startPath, false);
        right_.navigate(startPath, false);
    }

    layoutChildren();
}

void MainWindow::createMenuBar() {
    HMENU menuBar = CreateMenu();

    HMENU fileMenu = CreatePopupMenu();
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_VIEW, L"表示(&V)\tF3");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EDIT, L"編集(&E)\tF4");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_COPY, L"コピー(&C)\tF5");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_MOVE, L"移動(&M)\tF6");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_MKDIR, L"新しいフォルダー(&F)\tF7");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_DELETE, L"削除(&D)\tF8");
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_RENAME, L"名前の変更(&R)\tF2");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"終了(&X)");

    HMENU editMenu = CreatePopupMenu();
    AppendMenuW(editMenu, MF_STRING, IDM_EDIT_COPY, L"コピー(&C)\tCtrl+C");
    AppendMenuW(editMenu, MF_STRING, IDM_EDIT_CUT, L"切り取り(&T)\tCtrl+X");
    AppendMenuW(editMenu, MF_STRING, IDM_EDIT_PASTE, L"貼り付け(&P)\tCtrl+V");
    AppendMenuW(editMenu, MF_STRING, IDM_EDIT_SELECTALL, L"すべて選択(&A)");
    AppendMenuW(editMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(editMenu, MF_STRING, IDM_EDIT_FIND, L"検索(&F)\tCtrl+F");

    HMENU viewMenu = CreatePopupMenu();
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_REFRESH, L"更新(&R)");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_TREE, L"ツリー(&T)\tCtrl+Shift+T");
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_SINGLEPANE, L"シングルペイン表示(&S)\tCtrl+U");

    HMENU goMenu = CreatePopupMenu();
    AppendMenuW(goMenu, MF_STRING, IDM_GO_BACK, L"戻る(&B)\tAlt+Left");
    AppendMenuW(goMenu, MF_STRING, IDM_GO_FORWARD, L"進む(&F)\tAlt+Right");
    AppendMenuW(goMenu, MF_STRING, IDM_GO_UP, L"上へ(&U)\tAlt+Up");
    AppendMenuW(goMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(goMenu, MF_STRING, IDM_TAB_NEW, L"新しいタブ(&N)\tCtrl+T");
    AppendMenuW(goMenu, MF_STRING, IDM_TAB_CLOSE, L"タブを閉じる(&C)\tCtrl+W");
    AppendMenuW(goMenu, MF_STRING, IDM_TAB_NEXT, L"次のタブ(&X)\tCtrl+Tab");
    AppendMenuW(goMenu, MF_STRING, IDM_TAB_PREV, L"前のタブ(&P)\tCtrl+Shift+Tab");

    HMENU settingsMenu = CreatePopupMenu();
    AppendMenuW(settingsMenu, MF_STRING, IDM_TOOLS_FONT, L"フォント(&F)...");

    sortMenu_ = CreatePopupMenu();
    AppendMenuW(sortMenu_, MF_STRING, IDM_SORT_NAME, L"名前(&N)");
    AppendMenuW(sortMenu_, MF_STRING, IDM_SORT_TYPE, L"種類(&T)");
    AppendMenuW(sortMenu_, MF_STRING, IDM_SORT_SIZE, L"サイズ(&S)");
    AppendMenuW(sortMenu_, MF_STRING, IDM_SORT_MODIFIED, L"更新日時(&M)");
    AppendMenuW(sortMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sortMenu_, MF_STRING, IDM_SORT_DESCENDING, L"降順を既定にする(&D)");
    AppendMenuW(settingsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sortMenu_), L"並び順(既定)(&O)");

    HMENU toolsMenu = CreatePopupMenu();
    AppendMenuW(toolsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(settingsMenu), L"設定(&S)");

    HMENU helpMenu = CreatePopupMenu();
    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, L"Kestrelについて(&A)...");

    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"ファイル(&F)");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(editMenu), L"編集(&E)");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), L"表示(&V)");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(goMenu), L"移動(&G)");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(toolsMenu), L"ツール(&T)");
    AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"ヘルプ(&H)");

    SetMenu(hwnd_, menuBar);
}

void MainWindow::createToolbar() {
    // TBSTYLE_LIST puts a button's text next to its icon instead of below
    // it; since these buttons have no icon (I_IMAGENONE) at all, without
    // it the control still reserves the icon's height above the text,
    // which pushes the label down and makes the row look bottom-aligned.
    toolbar_ = CreateWindowExW(
        0, TOOLBARCLASSNAME, nullptr,
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_NOPARENTALIGN | CCS_NODIVIDER,
        0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TOOLBAR)), hInstance_, nullptr);

    SendMessageW(toolbar_, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    // No image list is ever attached (every button uses I_IMAGENONE), but
    // without telling the control the icon size is 0x0 it still reserves
    // a default icon-width gutter before the label, leaving the text
    // looking left-crammed with a lopsided gap after it.
    SendMessageW(toolbar_, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));

    auto mk = [](int cmd, LPCWSTR text) {
        TBBUTTON b{};
        b.idCommand = cmd;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
        b.iBitmap = I_IMAGENONE;
        b.iString = reinterpret_cast<INT_PTR>(text);
        return b;
    };

    TBBUTTON sep{};
    sep.fsStyle = BTNS_SEP;

    // BTNS_CHECK gives it a pressed/"stuck down" look while active, kept
    // in sync with singlePaneMode_ via TB_CHECKBUTTON wherever else that
    // flag changes (menu, Ctrl+U, session restore).
    TBBUTTON singlePane{};
    singlePane.idCommand = IDM_VIEW_SINGLEPANE;
    singlePane.fsState = TBSTATE_ENABLED;
    singlePane.fsStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT | BTNS_CHECK;
    singlePane.iBitmap = I_IMAGENONE;
    singlePane.iString = reinterpret_cast<INT_PTR>(L"1ペイン");

    TBBUTTON buttons[] = {
        mk(IDM_GO_BACK, L"戻る"),
        mk(IDM_GO_FORWARD, L"進む"),
        mk(IDM_GO_UP, L"上へ"),
        mk(IDM_VIEW_REFRESH, L"更新"),
        sep,
        singlePane,
    };
    SendMessageW(toolbar_, TB_ADDBUTTONSW, ARRAYSIZE(buttons), reinterpret_cast<LPARAM>(buttons));
    SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
}

void MainWindow::createAddressBar() {
    addressBar_ =
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd_,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ADDRESSBAR)), hInstance_, nullptr);
    SendMessageW(addressBar_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    SetWindowSubclass(addressBar_, AddressBarSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
}

void MainWindow::createStatusBar() {
    statusBar_ = CreateWindowExW(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUSBAR)), hInstance_,
                                  nullptr);
}

void MainWindow::layoutChildren() {
    cancelSplitterDrag(); // Any external layout change invalidates the drag snapshot.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    SendMessageW(toolbar_, TB_AUTOSIZE, 0, 0);
    SIZE tbSize{};
    SendMessageW(toolbar_, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&tbSize));
    SendMessageW(statusBar_, WM_SIZE, 0, 0);
    RECT sbRect{};
    GetWindowRect(statusBar_, &sbRect);

    layoutInput_ = {width, height,
        static_cast<int>(tbSize.cx), static_cast<int>(tbSize.cy), static_cast<int>(sbRect.bottom - sbRect.top),
        treeWidth_, leftWidth_, previewHeight_, lastLayoutWidth_, singlePaneMode_, activePaneId_};
    const auto layout = WindowLayout::calculate(layoutInput_);
    lastLayoutWidth_ = width;
    layoutInput_.previousWidth = width;
    layoutInput_.treeWidth = layout.treeWidth;
    layoutInput_.leftWidth = layout.leftWidth;
    layoutInput_.previewHeight = layout.previewHeight;
    treeWidth_ = layout.treeWidth;
    leftWidth_ = layout.leftWidth;
    previewHeight_ = layout.previewHeight;
    contentTop_ = layout.contentTop;
    contentHeight_ = layout.contentHeight;

    auto nativeRect = [](WindowLayout::Rect r) -> RECT { return {r.left, r.top, r.right, r.bottom}; };
    auto move = [](HWND window, WindowLayout::Rect r) {
        placeWithoutRedraw(window, r.left, r.top, r.right - r.left, r.bottom - r.top);
    };
    move(toolbar_, layout.toolbar);
    move(addressBar_, layout.address);
    move(tree_.hwnd(), layout.tree);
    move(preview_.hwnd(), layout.preview);
    splitter_.setRects(nativeRect(layout.splitter1), nativeRect(layout.splitter2), nativeRect(layout.splitter3));
    splitter_.setLayoutInput(layoutInput_);
    leftOuterRect_ = nativeRect(layout.leftOuter);
    rightOuterRect_ = nativeRect(layout.rightOuter);

    auto showPane = [](FilePane& p, bool visible) {
        const int cmd = visible ? SW_SHOW : SW_HIDE;
        ShowWindow(p.hwnd(), cmd);
        ShowWindow(p.tabHwnd(), cmd);
        ShowWindow(p.newTabButtonHwnd(), cmd);
        // Re-shown (if applicable) by the setBounds() call right below,
        // which only shows it when that pane's currentPath() is actually
        // the Recycle Bin - hiding it outright here just makes sure it
        // doesn't linger visible while its whole pane is supposed to be
        // hidden (singlePaneMode_'s inactive pane).
        if (!visible) ShowWindow(p.emptyRecycleBinButtonHwnd(), SW_HIDE);
    };
    if (singlePaneMode_) {
        showPane(inactivePane(), false);
        showPane(activePane(), true);
        activePane().setBounds(nativeRect(activePaneId_ == 0 ? layout.leftInner : layout.rightInner));
    } else {
        showPane(left_, true);
        showPane(right_, true);
        left_.setBounds(nativeRect(layout.leftInner));
        right_.setBounds(nativeRect(layout.rightInner));
    }

    // Repaint only after every control has its final bounds (including the
    // tab strip's row-count probe). WS_CLIPCHILDREN keeps the parent's
    // background erase off the controls; ALLCHILDREN explicitly schedules
    // their paints too. Keep ERASE/FRAME for old frame pixels and borders.
    // Complete the committed layout before returning to input processing.
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

FilePane& MainWindow::activePane() { return activePaneId_ == 0 ? left_ : right_; }
FilePane& MainWindow::inactivePane() { return activePaneId_ == 0 ? right_ : left_; }

void MainWindow::switchActivePane() {
    activePaneId_ = activePaneId_ == 0 ? 1 : 0;
    if (singlePaneMode_) layoutChildren();  // swaps which pane is actually visible
    SetFocus(activePane().hwnd());
    refreshUiForActivePane();
    updateActivePaneFrame();
}

void MainWindow::updateActivePaneFrame() {
    InvalidateRect(hwnd_, &leftOuterRect_, FALSE);
    InvalidateRect(hwnd_, &rightOuterRect_, FALSE);
}

void MainWindow::onPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    auto drawFrame = [&](const RECT& outer, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        RECT r;
        r = {outer.left, outer.top, outer.right, outer.top + kActiveFrameWidth};
        FillRect(hdc, &r, brush);
        r = {outer.left, outer.bottom - kActiveFrameWidth, outer.right, outer.bottom};
        FillRect(hdc, &r, brush);
        r = {outer.left, outer.top, outer.left + kActiveFrameWidth, outer.bottom};
        FillRect(hdc, &r, brush);
        r = {outer.right - kActiveFrameWidth, outer.top, outer.right, outer.bottom};
        FillRect(hdc, &r, brush);
        DeleteObject(brush);
    };

    drawFrame(leftOuterRect_, activePaneId_ == 0 ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE));
    drawFrame(rightOuterRect_, activePaneId_ == 1 ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_BTNFACE));

    EndPaint(hwnd_, &ps);
}

void MainWindow::refreshUiForActivePane() {
    FilePane& p = activePane();
    SetWindowTextW(addressBar_, p.currentPath().c_str());
    tree_.trySelectPath(p.currentPath());
    updateStatusBar();
}

void MainWindow::updatePreview() {
    preview_.showPreview(activePane().focusedItemPath());
}

void MainWindow::updateStatusBar() {
    const auto& s = activePane().stats();
    std::wstring text;
    if (s.selectedCount > 0) {
        text = std::format(L"{} 個のファイル | {} 個のフォルダー | {} 個選択 | {}", s.fileCount, s.dirCount,
                            s.selectedCount, Formatting::formatSize(s.selectedSize));
    } else {
        text = std::format(L"{} 個のファイル | {} 個のフォルダー", s.fileCount, s.dirCount);
    }
    SendMessageW(statusBar_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void MainWindow::doCopyToOther() {
    auto paths = activePane().selectedPaths();
    if (paths.empty()) return;
    if (FileOperations::copyItems(hwnd_, paths, inactivePane().currentPath())) {
        inactivePane().refresh();
    }
}

void MainWindow::doMoveToOther() {
    auto paths = activePane().selectedPaths();
    if (paths.empty()) return;
    if (FileOperations::moveItems(hwnd_, paths, inactivePane().currentPath())) {
        inactivePane().refresh();
        activePane().refresh();
    }
}

void MainWindow::doClipboardCopy(bool cut) {
    auto paths = activePane().selectedPaths();
    ClipboardFiles::set(hwnd_, paths, cut);
    // Only one pane's items can be the pending cut at a time - a fresh
    // cut/copy anywhere supersedes whatever was marked before, in either
    // pane, so the dimmed-icon mark can't linger on stale items.
    left_.clearCutPaths();
    right_.clearCutPaths();
    if (cut) activePane().setCutPaths(std::move(paths));
}

void MainWindow::doClipboardPaste() {
    auto cf = ClipboardFiles::get(hwnd_);
    if (!cf) return;
    const bool ok = cf->move ? FileOperations::moveItems(hwnd_, cf->paths, activePane().currentPath())
                              : FileOperations::copyItems(hwnd_, cf->paths, activePane().currentPath());
    if (ok) {
        if (cf->move) {
            left_.clearCutPaths();
            right_.clearCutPaths();
            // The moved-away items' source folder(s) may be sitting
            // cached in some other tab (or the other pane's live tab) -
            // that tab's own DirectoryWatcher isn't the one that just
            // saw this move, so it would otherwise keep showing the
            // items as still there. See invalidateTabsMatchingPath.
            std::vector<std::wstring> sourceDirs;
            for (const auto& p : cf->paths) {
                std::wstring dir = std::filesystem::path(p).parent_path().wstring();
                if (std::ranges::find(sourceDirs, dir) == sourceDirs.end()) sourceDirs.push_back(std::move(dir));
            }
            for (const auto& dir : sourceDirs) {
                left_.invalidateTabsMatchingPath(dir);
                right_.invalidateTabsMatchingPath(dir);
            }
        }
        activePane().refresh();
    }
}

void MainWindow::onAddressBarEnter() {
    const int len = GetWindowTextLengthW(addressBar_);
    std::wstring path(len, L'\0');
    if (len > 0) GetWindowTextW(addressBar_, path.data(), len + 1);
    if (!path.empty()) activePane().navigate(path, true);
}

void MainWindow::onXButton(WORD xButton) {
    if (xButton == XBUTTON1) activePane().goBack();
    else if (xButton == XBUTTON2) activePane().goForward();
}

void MainWindow::onContextMenu(HWND target, int screenX, int screenY) {
    FilePane* pane = nullptr;
    if (target == left_.hwnd()) pane = &left_;
    else if (target == right_.hwnd()) pane = &right_;
    if (!pane) return;  // not one of the file panes (tree, toolbar, ...) - nothing to show

    POINT screenPt{screenX, screenY};
    if (screenX == -1 && screenY == -1) {
        // Keyboard-invoked (Shift+F10 / Menu key): operate on whatever is
        // already focused/selected rather than hit-testing, and anchor the
        // menu near the pane instead of at a meaningless (-1,-1).
        RECT r;
        GetWindowRect(pane->hwnd(), &r);
        screenPt = {r.left + 20, r.top + 20};
    } else {
        POINT clientPt = screenPt;
        ScreenToClient(pane->hwnd(), &clientPt);
        pane->selectSingleItemAtClientPoint(clientPt);
    }

    if (pane->currentPath() == kRecycleBinPath) {
        auto names = pane->selectedNames();
        if (!names.empty()) {
            if (shellMenu_.showAndInvoke(hwnd_, RecycleBinOps::get<IContextMenu>(hwnd_, names), screenPt)) {
                pane->refresh();
            }
        }
        return;
    }

    auto paths = pane->selectedPaths();
    if (!paths.empty()) {
        if (shellMenu_.showAndInvoke(hwnd_, paths, screenPt)) pane->refresh();
        return;
    }

    // Empty-area right-click: a real shell "background" context menu
    // needs a different API (IShellFolder::CreateViewObject), so this
    // covers just the handful of actions that make sense with nothing
    // selected instead.
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_FILE_MKDIR, L"新しいフォルダー(&N)\tF7");
    AppendMenuW(menu, MF_STRING, IDM_EDIT_PASTE, L"貼り付け(&P)\tCtrl+V");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_VIEW_REFRESH, L"更新(&R)");
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

bool MainWindow::forwardToAddressBar(HWND focus, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (focus != addressBar_) return false;
    SendMessageW(addressBar_, msg, wParam, lParam);
    return true;
}

void MainWindow::onCommand(int id, HWND ctrl) {
    if (ctrl && ctrl == left_.newTabButtonHwnd()) {
        left_.activate();
        left_.newTab();
        return;
    }
    if (ctrl && ctrl == right_.newTabButtonHwnd()) {
        right_.activate();
        right_.newTab();
        return;
    }
    if (ctrl && ctrl == left_.emptyRecycleBinButtonHwnd()) {
        left_.emptyRecycleBin();
        return;
    }
    if (ctrl && ctrl == right_.emptyRecycleBinButtonHwnd()) {
        right_.emptyRecycleBin();
        return;
    }

    const HWND focus = GetFocus();
    switch (id) {
        case IDM_FILE_EXIT:
            DestroyWindow(hwnd_);
            break;
        case IDM_FILE_OPEN:
            activePane().openFocusedOrSelected();
            break;
        case IDM_FILE_VIEW:
            activePane().doView();
            break;
        case IDM_FILE_EDIT:
            activePane().doEdit();
            break;
        case IDM_FILE_COPY:
            doCopyToOther();
            break;
        case IDM_FILE_MOVE:
            doMoveToOther();
            break;
        case IDM_FILE_MKDIR:
            activePane().doMkdir();
            break;
        case IDM_FILE_DELETE:
            activePane().doDelete();
            break;
        case IDM_FILE_RENAME:
            activePane().doRename();
            break;

        case IDM_EDIT_COPY:
            if (!forwardToAddressBar(focus, WM_COPY, 0, 0)) doClipboardCopy(false);
            break;
        case IDM_EDIT_CUT:
            if (!forwardToAddressBar(focus, WM_CUT, 0, 0)) doClipboardCopy(true);
            break;
        case IDM_EDIT_PASTE:
            if (!forwardToAddressBar(focus, WM_PASTE, 0, 0)) doClipboardPaste();
            break;
        case IDM_EDIT_SELECTALL:
            if (!forwardToAddressBar(focus, EM_SETSEL, 0, -1))
                ListView_SetItemState(activePane().hwnd(), -1, LVIS_SELECTED, LVIS_SELECTED);
            break;

        case IDM_EDIT_FIND:
            activePane().toggleSearch();
            layoutChildren();
            break;

        case IDM_VIEW_REFRESH:
            activePane().refresh();
            break;
        case IDM_VIEW_TREE:
            SetFocus(tree_.hwnd());
            break;
        case IDM_VIEW_SINGLEPANE:
            singlePaneMode_ = !singlePaneMode_;
            SendMessageW(toolbar_, TB_CHECKBUTTON, IDM_VIEW_SINGLEPANE, MAKELONG(singlePaneMode_ ? TRUE : FALSE, 0));
            layoutChildren();
            updateActivePaneFrame();
            break;

        case IDM_GO_BACK:
            activePane().goBack();
            break;
        case IDM_GO_FORWARD:
            activePane().goForward();
            break;
        case IDM_GO_UP:
            activePane().goUp();
            break;
        case IDM_GO_ADDRESSBAR:
            SetFocus(addressBar_);
            SendMessageW(addressBar_, EM_SETSEL, 0, -1);
            break;

        case IDM_TAB_NEW:
            activePane().newTab();
            break;
        case IDM_TAB_CLOSE:
            activePane().closeTab();
            break;
        case IDM_TAB_NEXT:
            activePane().cycleTab(true);
            break;
        case IDM_TAB_PREV:
            activePane().cycleTab(false);
            break;

        case IDM_TOOLS_FONT:
            chooseFont();
            break;

        case IDM_SORT_NAME:
            applyDefaultSort(0, sortAscending_);
            break;
        case IDM_SORT_TYPE:
            applyDefaultSort(1, sortAscending_);
            break;
        case IDM_SORT_SIZE:
            applyDefaultSort(2, sortAscending_);
            break;
        case IDM_SORT_MODIFIED:
            applyDefaultSort(3, sortAscending_);
            break;
        case IDM_SORT_DESCENDING:
            applyDefaultSort(sortColumn_, !sortAscending_);
            break;
        case IDM_HELP_ABOUT:
            MessageBoxW(hwnd_, L"Kestrel Filer\n軽量な Win32 ファイラーです。", L"Kestrelについて",
                        MB_OK | MB_ICONINFORMATION);
            break;
        default:
            break;
    }
}

LRESULT MainWindow::onNotify(LPARAM lParam) {
    auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
    if (nmhdr->hwndFrom == tree_.hwnd()) return tree_.handleNotify(nmhdr);
    if (nmhdr->hwndFrom == left_.hwnd() || nmhdr->hwndFrom == left_.tabHwnd()) return left_.handleNotify(nmhdr);
    if (nmhdr->hwndFrom == right_.hwnd() || nmhdr->hwndFrom == right_.tabHwnd()) return right_.handleNotify(nmhdr);
    return 0;
}

void MainWindow::onLButtonDown(int x, int y) {
    POINT pt{x, y};
    int splitterId = 0;
    if (PtInRect(&splitter_.splitter1Rect(), pt)) {
        splitterId = 1;
    } else if (PtInRect(&splitter_.splitter2Rect(), pt)) {
        splitterId = 2;
    } else if (PtInRect(&splitter_.splitter3Rect(), pt)) {
        splitterId = 3;
    } else if (PtInRect(&leftOuterRect_, pt)) {
        // Reaching here at all means the click landed on MainWindow's own
        // background, not any child control - i.e. some sliver of the
        // pane's rect (e.g. the tab strip row) isn't actually covered by
        // tabHwnd_/newTabButton_/the list. Whatever the cause, a click
        // anywhere in a pane's outer rect should still activate it.
        left_.activate();
    } else if (PtInRect(&rightOuterRect_, pt)) {
        right_.activate();
    }
    if (splitterId) {
        if (!splitter_.createGuide(hwnd_, hInstance_)) return;
        SetCapture(hwnd_);
        if (GetCapture() != hwnd_) { splitter_.cancelDrag(); return; }
        splitter_.beginDrag(splitterId, hwnd_, x, y);
    }
}

void MainWindow::onMouseMove(int x, int y) {
    if (!splitter_.dragging()) return;
    splitter_.updateDrag(hwnd_, x, y);
}

void MainWindow::cancelSplitterDrag() {
    if (!splitter_.dragging()) return;
    splitter_.cancelDrag();
    if (GetCapture() == hwnd_) ReleaseCapture();
}

void MainWindow::onLButtonUp(int x, int y) {
    if (!splitter_.dragging()) return;
    // Use the release location, even without a final mouse-move message.
    const auto destination = splitter_.endDrag(hwnd_, x, y);
    if (GetCapture() == hwnd_) ReleaseCapture();
    treeWidth_ = destination.treeWidth;
    leftWidth_ = destination.leftWidth;
    previewHeight_ = destination.previewHeight;
    layoutChildren();
}
