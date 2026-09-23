#include "FileOperations.h"
#include "ComPtr.h"
#include "RecycleBinOps.h"
#include "ShellSelection.h"

#include <algorithm>
#include <cwctype>

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>

namespace {

ComPtr<IShellItem> itemFromPath(const std::wstring& path) {
    IShellItem* raw = nullptr;
    SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&raw));
    return ComPtr<IShellItem>(raw);
}

std::wstring displayName(IShellItem* item, SIGDN form) {
    if (!item) return {};
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(form, &raw))) return {};
    std::wstring name = raw;
    CoTaskMemFree(raw);
    return name;
}

std::wstring lowered(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
}

// Collects what each operation actually produced (after conflict renames,
// into the Recycle Bin, ...) for Undo. Only the items the caller asked for
// are kept: the shell also reports every file inside a copied folder.
// Stack-allocated for one PerformOperations call, so not refcounted.
class ResultRecorder final : public IFileOperationProgressSink {
public:
    ResultRecorder(Undo::Record* record, const std::vector<std::wstring>& requested) : record_(record) {
        for (const auto& path : requested) requested_.push_back(lowered(path));
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IFileOperationProgressSink) {
            *ppv = static_cast<IFileOperationProgressSink*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP PostRenameItem(DWORD, IShellItem* item, LPCWSTR, HRESULT hr, IShellItem* created) override {
        return add(item, hr, created, SIGDN_FILESYSPATH);
    }
    STDMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* created) override {
        return add(item, hr, created, SIGDN_FILESYSPATH);
    }
    STDMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr, IShellItem* created) override {
        return add(item, hr, created, SIGDN_FILESYSPATH);
    }
    STDMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hr, IShellItem* created) override {
        // `created` is the recycled file itself (C:\$Recycle.Bin\<SID>\$R...,
        // null for a permanent delete) - RecycleBinOps::restoreItems finds
        // the Recycle Bin entry backed by it.
        return add(item, hr, created, SIGDN_FILESYSPATH);
    }
    STDMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT hr, IShellItem* created) override {
        if (record_ && SUCCEEDED(hr) && created) record_->changes.push_back({L"", displayName(created, SIGDN_FILESYSPATH)});
        return S_OK;
    }

    STDMETHODIMP StartOperations() override { return S_OK; }
    STDMETHODIMP FinishOperations(HRESULT) override { return S_OK; }
    STDMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP PreDeleteItem(DWORD, IShellItem*) override { return S_OK; }
    STDMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override { return S_OK; }
    STDMETHODIMP UpdateProgress(UINT, UINT) override { return S_OK; }
    STDMETHODIMP ResetTimer() override { return S_OK; }
    STDMETHODIMP PauseTimer() override { return S_OK; }
    STDMETHODIMP ResumeTimer() override { return S_OK; }

private:
    HRESULT add(IShellItem* item, HRESULT hr, IShellItem* created, SIGDN resultForm) {
        // Skipped items (conflict "skip", ...) succeed without creating anything.
        if (!record_ || FAILED(hr) || !created) return S_OK;
        std::wstring source = displayName(item, SIGDN_FILESYSPATH);
        if (std::ranges::find(requested_, lowered(source)) == requested_.end()) return S_OK;
        record_->changes.push_back({std::move(source), displayName(created, resultForm)});
        return S_OK;
    }

    Undo::Record* record_;
    std::vector<std::wstring> requested_;
};

// Runs `build` (which queues operations on the IFileOperation) then
// performs them, letting the shell own all progress/confirmation UI. With
// a `record`, what actually happened to `requested` is appended to it.
template <typename Build>
bool runFileOperation(HWND owner, DWORD extraFlags, Build build, Undo::Record* record = nullptr,
                      const std::vector<std::wstring>& requested = {}) {
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

    ResultRecorder recorder(record, requested);
    DWORD cookie = 0;
    const bool advised = record && SUCCEEDED(pfo->Advise(&recorder, &cookie));
    HRESULT hr = pfo->PerformOperations();
    if (advised) pfo->Unadvise(cookie);
    if (FAILED(hr)) {
        return false;
    }

    BOOL aborted = FALSE;
    pfo->GetAnyOperationsAborted(&aborted);
    return !aborted;
}

// Brings Recycle Bin items back via their own "restore" command - the same
// one the bin's context menu shows as 元に戻す (verb "undelete").
}  // namespace

namespace FileOperations {

bool copyItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir, Undo::Record* record) {
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
    }, record, sources);
}

bool moveItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir, Undo::Record* record) {
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
    }, record, sources);
}

bool deleteItems(HWND owner, const std::vector<std::wstring>& sources, bool permanent, Undo::Record* record) {
    if (sources.empty()) return true;

    return runFileOperation(owner, permanent ? 0 : FOF_ALLOWUNDO, [&](ComPtr<IFileOperation>& pfo) {
        for (const auto& src : sources) {
            auto item = itemFromPath(src);
            if (!item) continue;
            pfo->DeleteItem(item.get(), nullptr);
        }
        return true;
    }, record, sources);
}

bool renameItem(HWND owner, const std::wstring& path, const std::wstring& newName, Undo::Record* record) {
    return runFileOperation(owner, 0, [&](ComPtr<IFileOperation>& pfo) {
        auto item = itemFromPath(path);
        if (!item) return false;
        pfo->RenameItem(item.get(), newName.c_str(), nullptr);
        return true;
    }, record, {path});
}

bool createDirectory(HWND owner, const std::wstring& parentDir, const std::wstring& name, Undo::Record* record) {
    return runFileOperation(owner, 0, [&](ComPtr<IFileOperation>& pfo) {
        auto parent = itemFromPath(parentDir);
        if (!parent) return false;
        return SUCCEEDED(pfo->NewItem(parent.get(), FILE_ATTRIBUTE_DIRECTORY, name.c_str(), nullptr, nullptr));
    }, record);
}

bool undo(HWND owner, const std::vector<Undo::Step>& steps) {
    bool ok = true;
    bool anyFileStep = false;
    const bool fileOpsOk = runFileOperation(owner, FOF_ALLOWUNDO, [&](ComPtr<IFileOperation>& pfo) {
        for (const auto& step : steps) {
            if (step.action == Undo::Step::Action::Restore) continue;
            auto item = itemFromPath(step.target);
            if (!item) {
                ok = false;  // gone (or renamed) since - nothing to reverse
                continue;
            }
            anyFileStep = true;
            switch (step.action) {
                case Undo::Step::Action::Rename:
                    pfo->RenameItem(item.get(), step.newName.c_str(), nullptr);
                    break;
                case Undo::Step::Action::Recycle:
                    pfo->DeleteItem(item.get(), nullptr);
                    break;
                case Undo::Step::Action::MoveTo:
                    if (auto dest = itemFromPath(step.destDir)) pfo->MoveItem(item.get(), dest.get(), step.newName.c_str(), nullptr);
                    else ok = false;
                    break;
                case Undo::Step::Action::Restore:
                    break;
            }
        }
        return anyFileStep;
    });
    if (anyFileStep && !fileOpsOk) ok = false;

    std::vector<std::wstring> recycled;
    for (const auto& step : steps) {
        if (step.action == Undo::Step::Action::Restore) recycled.push_back(step.target);
    }
    if (!recycled.empty() && !RecycleBinOps::restoreItems(owner, recycled)) ok = false;
    return ok;
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

    // SHDoDragDrop (rather than a bare DoDragDrop) supplies the shell's own
    // drop source, which handles right-button drags and asks `owner` (the
    // ListView, via DI_GETDRAGIMAGE) for the translucent drag image.
    DWORD effect = DROPEFFECT_NONE;
    const HRESULT dragHr =
        SHDoDragDrop(owner, dataObj.get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    return dragHr == DRAGDROP_S_DROP;
}

}  // namespace FileOperations
