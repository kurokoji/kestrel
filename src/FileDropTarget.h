#pragma once

#include "ComPtr.h"

#include <windows.h>
#include <oleidl.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct IDropTargetHelper;

// OLE drop target for a file list or the tree. It does no file operations
// itself: each hover is resolved to a real shell item and forwarded to that
// item's own IDropTarget (the same one Explorer uses). Explorer's defaults
// therefore apply: move vs copy by drive, Shift/Ctrl/Alt modifiers,
// right-drag menu, zip folders, dropping onto an exe, Recycle Bin = delete.
class FileDropTarget final : public IDropTarget {
public:
    struct Hit {
        // Shell items to try binding, in priority order (DropTargetPath::candidates).
        std::vector<std::wstring> candidates;
        // Owner-defined token for the hovered row (list index + 1, HTREEITEM),
        // highlighted only while candidates[0] is the chosen target. 0 = none.
        intptr_t highlightKey = 0;
    };

    struct Callbacks {
        std::function<Hit(POINT clientPt)> hitTest;
        std::function<void(intptr_t highlightKey)> setHighlight;  // 0 = clear
    };

    // Creates the target and RegisterDragDrop()s it on `hwnd`. The window
    // keeps it alive; call RevokeDragDrop(hwnd) before destroying it.
    static bool registerOn(HWND hwnd, Callbacks callbacks);

    // Paths of the drag this process itself started (FilePane's selection),
    // so dropping it onto itself / its own folder can be refused up front.
    // Held by FilePane only for the duration of the blocking DoDragDrop.
    class InternalDragScope {
    public:
        explicit InternalDragScope(std::vector<std::wstring> sources);
        ~InternalDragScope();
        InternalDragScope(const InternalDragScope&) = delete;
        InternalDragScope& operator=(const InternalDragScope&) = delete;
    };

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP DragEnter(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL pt, DWORD* effect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* dataObject, DWORD keyState, POINTL pt, DWORD* effect) override;

private:
    FileDropTarget(HWND hwnd, Callbacks callbacks);
    ~FileDropTarget();

    // Picks the target for the point under the cursor and makes it the
    // current one (DragLeave on the old, DragEnter on the new) if it changed.
    // Returns the effect to report back to OLE for `allowed` source effects.
    DWORD retarget(DWORD keyState, POINTL pt, DWORD allowed);
    void releaseTarget();
    void setHighlight(intptr_t key);
    void autoScroll(POINT clientPt);

    ULONG refCount_ = 1;
    HWND hwnd_ = nullptr;
    Callbacks callbacks_;
    ComPtr<IDropTargetHelper> helper_;

    ComPtr<IDataObject> dataObject_;  // only between DragEnter and DragLeave/Drop
    std::wstring targetPath_;
    ComPtr<IDropTarget> target_;
    DWORD targetEffect_ = DROPEFFECT_NONE;  // what target_ last answered
    intptr_t highlightKey_ = 0;
    ULONGLONG lastScrollTick_ = 0;

    static inline std::vector<std::wstring> internalSources_;
};
