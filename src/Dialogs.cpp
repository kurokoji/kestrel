#include "Dialogs.h"

#include <windowsx.h>

namespace {

constexpr wchar_t kPromptClass[] = L"KestrelPromptBox";
constexpr int IDC_EDIT = 101;
constexpr int IDC_OK = 102;
constexpr int IDC_CANCEL = 103;

struct PromptState {
    std::wstring label;
    HWND edit = nullptr;
    std::wstring text;
    bool ok = false;
    bool done = false;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<PromptState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HINSTANCE hInst = cs->hInstance;

            HWND lbl = CreateWindowExW(0, L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE, 10, 10, 260, 16,
                                        hwnd, nullptr, hInst, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->text.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 10, 30, 260, 22,
                                           hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT)), hInst,
                                           nullptr);
            SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 104, 60, 80, 24, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK)), hInst, nullptr);
            SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            HWND cancel = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 190, 60, 90,
                                           24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)), hInst,
                                           nullptr);
            SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

            SetFocus(state->edit);
            Edit_SetSel(state->edit, 0, -1);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDC_OK) {
                int len = GetWindowTextLengthW(state->edit);
                std::wstring buf(len, L'\0');
                if (len > 0) GetWindowTextW(state->edit, buf.data(), len + 1);
                state->text = buf;
                state->ok = true;
                state->done = true;
                DestroyWindow(hwnd);
            } else if (id == IDC_CANCEL) {
                state->ok = false;
                state->done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            state->ok = false;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ensureClassRegistered(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PromptWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kPromptClass;
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

namespace Dialogs {

std::optional<std::wstring> promptForText(HWND owner, const wchar_t* title, const wchar_t* label,
                                           const wchar_t* initialValue) {
    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    ensureClassRegistered(hInstance);

    PromptState state;
    state.label = label;
    state.text = initialValue ? initialValue : L"";

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int w = 284, h = 128;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title,
                                 WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, nullptr, hInstance, &state);
    if (!hwnd) return std::nullopt;

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.hwnd != hwnd && GetParent(msg.hwnd) == hwnd) {
            if (msg.wParam == VK_RETURN) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_OK, BN_CLICKED), 0);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CANCEL, BN_CLICKED), 0);
                continue;
            }
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (state.ok && !state.text.empty()) return state.text;
    return std::nullopt;
}

void showError(HWND owner, const wchar_t* title, const wchar_t* message) {
    MessageBoxW(owner, message, title, MB_OK | MB_ICONERROR);
}

}  // namespace Dialogs
