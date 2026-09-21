#include "FileOperations.h"
#include "ComPtr.h"
#include "ShellSelection.h"

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>

namespace {

ComPtr<IShellItem> itemFromPath(const std::wstring& path) {
    IShellItem* raw = nullptr;
    SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&raw));
    return ComPtr<IShellItem>(raw);
}

// Minimal IDropSource for DoDragDrop: not refcounted (lives on the stack
// for the duration of a single blocking DoDragDrop call), just answers
// "keep dragging / drop now / cancel" from live mouse/keyboard state.
class DragDropSource : public IDropSource {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = this;
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & (MK_LBUTTON | MK_RBUTTON))) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};

// Runs `build` (which queues operations on the IFileOperation) then
// performs them, letting the shell own all progress/confirmation UI.
template <typename Build>
bool runFileOperation(HWND owner, DWORD extraFlags, Build build) {
    ComPtr<IFileOperation> pfo;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(pfo.addressOf())))) {
        return false;
    }

    DWORD flags = FOF_NOCONFIRMMKDIR | extraFlags;
    pfo->SetOperationFlags(flags);
    if (owner) {
        pfo->SetOwnerWindow(owner);
    }

    if (!build(pfo)) {
        return false;
    }

    HRESULT hr = pfo->PerformOperations();
    if (FAILED(hr)) {
        return false;
    }

    BOOL aborted = FALSE;
    pfo->GetAnyOperationsAborted(&aborted);
    return !aborted;
}

}  // namespace

namespace FileOperations {

bool copyItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir) {
    if (sources.empty()) return true;

    return runFileOperation(owner, FOF_ALLOWUNDO, [&](ComPtr<IFileOperation>& pfo) {
        auto dest = itemFromPath(destDir);
        if (!dest) return false;
        for (const auto& src : sources) {
            auto item = itemFromPath(src);
            if (!item) continue;
            pfo->CopyItem(item.get(), dest.get(), nullptr, nullptr);
        }
        return true;
    });
}

bool moveItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir) {
    if (sources.empty()) return true;

    return runFileOperation(owner, FOF_ALLOWUNDO, [&](ComPtr<IFileOperation>& pfo) {
        auto dest = itemFromPath(destDir);
        if (!dest) return false;
        for (const auto& src : sources) {
            auto item = itemFromPath(src);
            if (!item) continue;
            pfo->MoveItem(item.get(), dest.get(), nullptr, nullptr);
        }
        return true;
    });
}

bool deleteItems(HWND owner, const std::vector<std::wstring>& sources) {
    if (sources.empty()) return true;

    return runFileOperation(owner, FOF_ALLOWUNDO, [&](ComPtr<IFileOperation>& pfo) {
        for (const auto& src : sources) {
            auto item = itemFromPath(src);
            if (!item) continue;
            pfo->DeleteItem(item.get(), nullptr);
        }
        return true;
    });
}

bool renameItem(HWND owner, const std::wstring& path, const std::wstring& newName) {
    return runFileOperation(owner, 0, [&](ComPtr<IFileOperation>& pfo) {
        auto item = itemFromPath(path);
        if (!item) return false;
        pfo->RenameItem(item.get(), newName.c_str(), nullptr);
        return true;
    });
}

bool createDirectory(HWND owner, const std::wstring& parentDir, const std::wstring& name) {
    return runFileOperation(owner, 0, [&](ComPtr<IFileOperation>& pfo) {
        auto parent = itemFromPath(parentDir);
        if (!parent) return false;
        return SUCCEEDED(pfo->NewItem(parent.get(), FILE_ATTRIBUTE_DIRECTORY, name.c_str(), nullptr, nullptr));
    });
}

void openItem(HWND owner, const std::wstring& path) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = owner;
    sei.lpVerb = nullptr;
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

void editItem(HWND owner, const std::wstring& path) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = owner;
    sei.lpVerb = L"edit";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        const std::wstring quotedPath = L"\"" + path + L"\"";
        sei.lpVerb = L"open";
        sei.lpFile = L"notepad.exe";
        sei.lpParameters = quotedPath.c_str();
        ShellExecuteExW(&sei);
    }
}

bool startDrag(HWND owner, const std::vector<std::wstring>& sources) {
    auto dataObj = ShellSelection::get<IDataObject>(owner, sources);
    if (!dataObj) return false;

    DragDropSource dropSource;
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT dragHr =
        DoDragDrop(dataObj.get(), &dropSource, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    return dragHr == DRAGDROP_S_DROP;
}

}  // namespace FileOperations
