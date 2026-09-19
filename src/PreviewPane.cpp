#include "PreviewPane.h"
#include "ComPtr.h"
#include "FileClassify.h"
#include "Formatting.h"

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"KestrelPreviewPane";
constexpr UINT kPreviewResultMsg = WM_APP + 100;

constexpr DWORD kMaxTextPreviewFile = 512 * 1024;
constexpr DWORD kMaxTextPreviewRead = 8192;
constexpr uint64_t kMaxImagePreviewFile = 32ull * 1024 * 1024;

constexpr UINT_PTR kLoadDebounceTimerId = 1;
constexpr UINT kLoadDebounceMs = 150;

// Result of a background loadWorker() run, posted to the preview window
// via kPreviewResultMsg (lParam). Receiver owns it.
struct PreviewResult {
    uint64_t requestId = 0;
    PreviewPane::Mode mode = PreviewPane::Mode::Empty;
    std::wstring detail;
    std::wstring textContent;
    std::unique_ptr<Gdiplus::Bitmap> image;
};

std::unique_ptr<Gdiplus::Bitmap> loadImageBitmap(const std::wstring& path) {
    auto bmp = std::make_unique<Gdiplus::Bitmap>(path.c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok || bmp->GetWidth() == 0 || bmp->GetHeight() == 0) return nullptr;
    return bmp;
}

// Asks the shell for a thumbnail - the same source Explorer uses for
// video (and many other) file types. Reads a cached/embedded frame rather
// than decoding the file ourselves, so cost doesn't scale with file size.
std::unique_ptr<Gdiplus::Bitmap> loadShellThumbnailBitmap(const std::wstring& path) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.addressOf())))) return nullptr;

    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(item->QueryInterface(IID_PPV_ARGS(factory.addressOf())))) return nullptr;

    HBITMAP hbmp = nullptr;
    const SIZE size{256, 256};
    if (FAILED(factory->GetImage(size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hbmp)) || !hbmp) return nullptr;

    auto bmp = std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromHBITMAP(hbmp, nullptr));
    DeleteObject(hbmp);
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok || bmp->GetWidth() == 0) return nullptr;
    return bmp;
}

bool loadTextFileContent(const std::wstring& path, std::wstring& outText) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    std::vector<BYTE> buf(kMaxTextPreviewRead);
    DWORD read = 0;
    const BOOL ok = ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(hFile);
    if (!ok) return false;
    buf.resize(read);

    auto decoded = FileClassify::decodeTextContent(buf);
    if (!decoded) return false;
    outText = std::move(*decoded);
    return true;
}

}  // namespace

PreviewPane::~PreviewPane() {
    reset();
    if (textFont_) DeleteObject(textFont_);
}

bool PreviewPane::create(HWND parent, HINSTANCE hInstance, int controlId) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
        registered = true;
    }

    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), hInstance, this);
    return hwnd_ != nullptr;
}

LRESULT CALLBACK PreviewPane::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PreviewPane* self;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<PreviewPane*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<PreviewPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PreviewPane::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT client;
            GetClientRect(hwnd_, &client);
            paint(hdc, client);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // paint() always fills the whole client rect itself
        case WM_TIMER: {
            if (wParam != kLoadDebounceTimerId) return DefWindowProcW(hwnd_, msg, wParam, lParam);
            KillTimer(hwnd_, kLoadDebounceTimerId);
            std::thread(&PreviewPane::loadWorker, pendingLoadPath_, pendingLoadExt_, pendingLoadSize_, requestId_,
                        hwnd_)
                .detach();
            return 0;
        }
        case kPreviewResultMsg: {
            std::unique_ptr<PreviewResult> result(reinterpret_cast<PreviewResult*>(lParam));
            if (result->requestId != requestId_) return 0;  // superseded by a newer selection

            if (result->mode == Mode::Image && result->image) {
                image_ = std::move(result->image);
                mode_ = Mode::Image;
                detail_ = result->detail;
                InvalidateRect(hwnd_, nullptr, TRUE);
            } else if (result->mode == Mode::Text) {
                textContent_ = std::move(result->textContent);
                mode_ = Mode::Text;
                detail_ = result->detail;
                InvalidateRect(hwnd_, nullptr, TRUE);
            }
            // Otherwise the worker found nothing better than the icon
            // already showing - leave it as is.
            return 0;
        }
        default:
            return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void PreviewPane::reset() {
    KillTimer(hwnd_, kLoadDebounceTimerId);  // no-op if none is pending
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
    image_.reset();
    textContent_.clear();
    detail_.clear();
    name_.clear();
    mode_ = Mode::Empty;
}

void PreviewPane::showPreview(const std::wstring& path) {
    loadFor(path);
    if (hwnd_) InvalidateRect(hwnd_, nullptr, TRUE);
}

void PreviewPane::loadFor(const std::wstring& path) {
    reset();
    ++requestId_;  // any in-flight worker's eventual result is now stale
    if (path.empty()) return;

    if (const size_t slash = path.find_last_of(L'\\'); slash != std::wstring::npos) {
        name_ = path.substr(slash + 1);
    } else {
        name_ = path;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        name_.clear();
        return;
    }

    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        loadIconFallback(path);
        detail_ = L"フォルダー";
        return;
    }

    const uint64_t size = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;

    std::wstring ext;
    if (const size_t dot = name_.find_last_of(L'.'); dot != std::wstring::npos) {
        ext = name_.substr(dot);
        std::ranges::transform(ext, ext.begin(), ::towlower);
    }

    // Show the (cheap, cached) icon immediately so the selection highlight
    // and preview never wait on disk or shell I/O. Anything better -
    // image, video thumbnail, text snippet - loads in the background and
    // swaps in via kPreviewResultMsg once ready.
    loadIconFallback(path);
    detail_ = Formatting::formatSize(size);

    // Debounced rather than spawned right away: arrow-key/selection
    // scrolling through a list otherwise starts (and immediately discards)
    // a background thread per row passed through. SetTimer with the same
    // ID just re-arms the delay on each call, so only the last selection
    // within kLoadDebounceMs actually gets a worker thread.
    pendingLoadPath_ = path;
    pendingLoadExt_ = ext;
    pendingLoadSize_ = size;
    SetTimer(hwnd_, kLoadDebounceTimerId, kLoadDebounceMs, nullptr);
}

void PreviewPane::loadWorker(std::wstring path, std::wstring ext, uint64_t size, uint64_t requestId, HWND hwnd) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    auto result = std::make_unique<PreviewResult>();
    result->requestId = requestId;
    result->detail = Formatting::formatSize(size);

    if (FileClassify::isImageExtension(ext)) {
        // Shell thumbnail first - same cheap cached-frame source as video,
        // costs nothing extra proportional to file size (unlike decoding
        // the full image just to shrink it into a small preview area).
        // Falls back to a real decode only when the shell has no
        // thumbnail for this format, still bounded by kMaxImagePreviewFile
        // since that path's cost does scale with the file.
        auto bmp = loadShellThumbnailBitmap(path);
        if (!bmp && size <= kMaxImagePreviewFile) bmp = loadImageBitmap(path);
        if (bmp) {
            result->mode = Mode::Image;
            result->image = std::move(bmp);
        }
    } else if (FileClassify::isVideoExtension(ext)) {
        if (auto bmp = loadShellThumbnailBitmap(path)) {
            result->mode = Mode::Image;
            result->image = std::move(bmp);
        }
    } else if (size <= kMaxTextPreviewFile) {
        std::wstring text;
        if (loadTextFileContent(path, text)) {
            result->mode = Mode::Text;
            result->textContent = std::move(text);
        }
    }

    CoUninitialize();

    if (result->mode != Mode::Empty) {
        PostMessageW(hwnd, kPreviewResultMsg, 0, reinterpret_cast<LPARAM>(result.release()));
    }
}

void PreviewPane::loadIconFallback(const std::wstring& path) {
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
        icon_ = sfi.hIcon;
        mode_ = Mode::Icon;
    }
}

void PreviewPane::paint(HDC hdc, const RECT& client) {
    FillRect(hdc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
    constexpr int kPad = 8;

    switch (mode_) {
        case Mode::Empty: {
            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            RECT r = client;
            DrawTextW(hdc, L"プレビューなし", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, old);
            break;
        }
        case Mode::Image: {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            const int availW = std::max(1, static_cast<int>(client.right - client.left) - 2 * kPad);
            const int availH = std::max(1, static_cast<int>(client.bottom - client.top) - 2 * kPad - 20);
            double scale = std::min(static_cast<double>(availW) / image_->GetWidth(),
                                     static_cast<double>(availH) / image_->GetHeight());
            scale = std::min(scale, 1.0);
            const int w = std::max(1, static_cast<int>(image_->GetWidth() * scale));
            const int h = std::max(1, static_cast<int>(image_->GetHeight() * scale));
            const int x = client.left + (static_cast<int>(client.right - client.left) - w) / 2;
            const int y = client.top + kPad;
            g.DrawImage(image_.get(), x, y, w, h);

            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            RECT r{client.left + kPad, y + h + 4, client.right - kPad, client.bottom - kPad};
            std::wstring label = name_ + L"  " + detail_;
            DrawTextW(hdc, label.c_str(), -1, &r, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, old);
            break;
        }
        case Mode::Text: {
            if (!textFont_) {
                textFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                         FIXED_PITCH | FF_MODERN, L"Consolas");
            }
            HFONT old = static_cast<HFONT>(SelectObject(hdc, textFont_));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            RECT r{client.left + kPad, client.top + kPad, client.right - kPad, client.bottom - kPad};
            DrawTextW(hdc, textContent_.c_str(), -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_EDITCONTROL);
            SelectObject(hdc, old);
            break;
        }
        case Mode::Icon: {
            constexpr int kIconSize = 32;
            const int x = client.left + (static_cast<int>(client.right - client.left) - kIconSize) / 2;
            const int y = client.top + kPad;
            if (icon_) DrawIconEx(hdc, x, y, icon_, kIconSize, kIconSize, 0, nullptr, DI_NORMAL);

            HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT old = static_cast<HFONT>(SelectObject(hdc, font));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            std::wstring label = name_;
            if (!detail_.empty()) label += L"\n" + detail_;
            RECT r{client.left + kPad, y + kIconSize + 6, client.right - kPad, client.bottom - kPad};
            DrawTextW(hdc, label.c_str(), -1, &r, DT_CENTER | DT_TOP | DT_WORDBREAK);
            SelectObject(hdc, old);
            break;
        }
    }
}
