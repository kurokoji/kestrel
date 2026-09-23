#include "FileDropTarget.h"
#include "DropTargetPath.h"

#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace {

constexpr int kAutoScrollZone = 16;  // px from the top/bottom edge that scrolls while dragging
constexpr ULONGLONG kAutoScrollIntervalMs = 80;

// The shell's own drop handler for `path` - a folder's (or drive's, or the
// Recycle Bin's) drop target, or a file's drop handler (exe, zip) if it has one.
ComPtr<IDropTarget> bindDropTarget(const std::wstring& path) {
    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.addressOf())))) return {};
    ComPtr<IDropTarget> target;
    if (FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(target.addressOf())))) return {};
    return target;
}

}  // namespace

FileDropTarget::InternalDragScope::InternalDragScope(std::vector<std::wstring> sources) {
    internalSources_ = std::move(sources);
}

FileDropTarget::InternalDragScope::~InternalDragScope() { internalSources_.clear(); }

bool FileDropTarget::registerOn(HWND hwnd, Callbacks callbacks) {
    auto* target = new FileDropTarget(hwnd, std::move(callbacks));
    const HRESULT hr = RegisterDragDrop(hwnd, target);  // AddRefs on success
    target->Release();
    return SUCCEEDED(hr);
}

FileDropTarget::FileDropTarget(HWND hwnd, Callbacks callbacks) : hwnd_(hwnd), callbacks_(std::move(callbacks)) {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(helper_.addressOf()));
}

FileDropTarget::~FileDropTarget() = default;

STDMETHODIMP FileDropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) FileDropTarget::AddRef() { return ++refCount_; }

STDMETHODIMP_(ULONG) FileDropTarget::Release() {
    const ULONG count = --refCount_;
    if (count == 0) delete this;
    return count;
}

void FileDropTarget::releaseTarget() {
    if (target_) target_->DragLeave();
    target_.reset();
    targetPath_.clear();
    targetEffect_ = DROPEFFECT_NONE;
}

void FileDropTarget::setHighlight(intptr_t key) {
    if (key == highlightKey_) return;
    highlightKey_ = key;
    if (callbacks_.setHighlight) callbacks_.setHighlight(key);
}

DWORD FileDropTarget::retarget(DWORD keyState, POINTL pt, DWORD allowed) {
    POINT client{pt.x, pt.y};
    ScreenToClient(hwnd_, &client);
    autoScroll(client);

    const Hit hit = callbacks_.hitTest ? callbacks_.hitTest(client) : Hit{};
    const bool explicitCopy = (keyState & MK_CONTROL) && !(keyState & MK_SHIFT);

    for (size_t i = 0; i < hit.candidates.size(); ++i) {
        const std::wstring& candidate = hit.candidates[i];
        if (!internalSources_.empty() && DropTargetPath::isOntoSource(internalSources_, candidate, explicitCopy)) {
            continue;
        }

        if (target_ && candidate == targetPath_) {
            targetEffect_ = allowed;
            target_->DragOver(keyState, pt, &targetEffect_);
        } else {
            releaseTarget();
            ComPtr<IDropTarget> bound = bindDropTarget(candidate);
            if (!bound) continue;
            DWORD effect = allowed;
            if (FAILED(bound->DragEnter(dataObject_.get(), keyState, pt, &effect))) continue;
            // A file row without a real drop handler answers "none" - fall
            // back to the folder it sits in, as Explorer does.
            if (effect == DROPEFFECT_NONE && i + 1 < hit.candidates.size()) {
                bound->DragLeave();
                continue;
            }
            target_ = std::move(bound);
            targetPath_ = candidate;
            targetEffect_ = effect;
        }
        setHighlight(i == 0 ? hit.highlightKey : 0);
        return targetEffect_;
    }

    releaseTarget();
    setHighlight(0);
    return DROPEFFECT_NONE;
}

void FileDropTarget::autoScroll(POINT clientPt) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (HWND header = FindWindowExW(hwnd_, nullptr, WC_HEADERW, nullptr); header && IsWindowVisible(header)) {
        RECT hr{};
        GetWindowRect(header, &hr);
        rc.top += hr.bottom - hr.top;
    }

    int direction = 0;
    if (clientPt.y >= rc.top && clientPt.y < rc.top + kAutoScrollZone) direction = SB_LINEUP;
    else if (clientPt.y < rc.bottom && clientPt.y >= rc.bottom - kAutoScrollZone) direction = SB_LINEDOWN;
    else return;

    const ULONGLONG now = GetTickCount64();
    if (now - lastScrollTick_ < kAutoScrollIntervalMs) return;
    lastScrollTick_ = now;
    if (helper_) helper_->Show(FALSE);
    SendMessageW(hwnd_, WM_VSCROLL, direction, 0);
    UpdateWindow(hwnd_);
    if (helper_) helper_->Show(TRUE);
}

STDMETHODIMP FileDropTarget::DragEnter(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) {
    dataObject_.reset();
    if (dataObject) {
        dataObject->AddRef();
        dataObject_ = ComPtr<IDataObject>(dataObject);
    }
    const DWORD allowed = *effect;
    *effect = retarget(keyState, pt, allowed);
    POINT screen{pt.x, pt.y};
    if (helper_) helper_->DragEnter(hwnd_, dataObject, &screen, *effect);
    return S_OK;
}

STDMETHODIMP FileDropTarget::DragOver(DWORD keyState, POINTL pt, DWORD* effect) {
    *effect = retarget(keyState, pt, *effect);
    POINT screen{pt.x, pt.y};
    if (helper_) helper_->DragOver(&screen, *effect);
    return S_OK;
}

STDMETHODIMP FileDropTarget::DragLeave() {
    releaseTarget();
    setHighlight(0);
    dataObject_.reset();
    if (helper_) helper_->DragLeave();
    return S_OK;
}

STDMETHODIMP FileDropTarget::Drop(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) {
    // No retarget here: the last DragOver already chose the target at this
    // point, and forwarding another DragOver with the buttons already
    // released would make the shell forget a right-drag (no drop menu).
    const DWORD allowed = *effect;
    const DWORD resolved = target_ ? targetEffect_ : DROPEFFECT_NONE;
    POINT screen{pt.x, pt.y};
    if (helper_) helper_->Drop(dataObject, &screen, resolved);
    setHighlight(0);

    if (target_ && resolved != DROPEFFECT_NONE) {
        // Drop replaces DragLeave for the chosen target. The shell may show
        // its own right-drag menu or conflict UI from inside this call.
        ComPtr<IDropTarget> target = std::move(target_);
        targetPath_.clear();
        *effect = allowed;
        target->Drop(dataObject, keyState, pt, effect);
    } else {
        releaseTarget();
        *effect = DROPEFFECT_NONE;
    }
    dataObject_.reset();
    return S_OK;
}
