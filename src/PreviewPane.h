#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <memory>
#include <string>

// A small, read-only preview area: shows a scaled-down image/video
// thumbnail, the first few KB of a text file, or just an icon + name for
// anything else. Entirely custom-drawn (its own window class, WM_PAINT)
// rather than built from a stack of standard controls, since it has to
// switch between several very different content types.
class PreviewPane {
public:
    ~PreviewPane();

    bool create(HWND parent, HINSTANCE hInstance, int controlId);
    HWND hwnd() const { return hwnd_; }

    // Shows a preview for `path` (empty clears it). The icon-based
    // fallback is resolved immediately (cheap, cached) so the selection
    // highlight never waits on it; anything slower - decoding an image,
    // asking the shell for a video thumbnail, reading a text file - runs
    // on a detached background thread and swaps in once ready. A stale
    // result (superseded by a newer showPreview() call) is discarded when
    // it arrives.
    void showPreview(const std::wstring& path);

    // Public so the background-worker's result struct (an implementation
    // detail living in PreviewPane.cpp's anonymous namespace, not a
    // member/friend of this class) can name it.
    enum class Mode { Empty, Icon, Text, Image };

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void paint(HDC hdc, const RECT& client);
    void loadFor(const std::wstring& path);
    void loadIconFallback(const std::wstring& path);
    void reset();

    static void loadWorker(std::wstring path, std::wstring ext, uint64_t size, uint64_t requestId, HWND hwnd);

    HWND hwnd_ = nullptr;
    Mode mode_ = Mode::Empty;
    std::wstring name_;
    std::wstring detail_;
    std::wstring textContent_;
    std::unique_ptr<Gdiplus::Bitmap> image_;
    HICON icon_ = nullptr;
    uint64_t requestId_ = 0;

    // The actual decode/thumbnail/read is debounced (via a timer, reset on
    // every showPreview() call) rather than spawned immediately - arrow-key
    // scrolling through a list otherwise starts and immediately discards a
    // background thread per row. Only the cheap icon+size shown by loadFor()
    // itself is immediate. These hold the most recent call's parameters for
    // whenever the timer actually fires.
    std::wstring pendingLoadPath_;
    std::wstring pendingLoadExt_;
    uint64_t pendingLoadSize_ = 0;

    // Lazily created, reused across paints instead of a CreateFontW/
    // DeleteObject pair on every WM_PAINT while showing a text preview.
    HFONT textFont_ = nullptr;
};
