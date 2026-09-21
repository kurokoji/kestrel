#include "ShellSelection.h"

#include <memory>

namespace ShellSelection {
namespace {
struct PidlDeleter {
    using pointer = PIDLIST_RELATIVE;
    void operator()(pointer pidl) const { CoTaskMemFree(pidl); }
};
using OwnedPidl = std::unique_ptr<ITEMIDLIST, PidlDeleter>;
}

HRESULT getUIObject(HWND owner, const std::vector<std::wstring>& paths, REFIID iid, void** object) {
    *object = nullptr;
    if (paths.empty()) return E_INVALIDARG;
    std::wstring parentDir = paths[0];
    if (const size_t slash = parentDir.find_last_of(L'\\'); slash != std::wstring::npos) parentDir.resize(slash);

    ComPtr<IShellFolder> desktop;
    HRESULT hr = SHGetDesktopFolder(desktop.addressOf());
    if (FAILED(hr)) return hr;

    PIDLIST_ABSOLUTE rawParent = nullptr;
    hr = SHParseDisplayName(parentDir.c_str(), nullptr, &rawParent, 0, nullptr);
    OwnedPidl parentPidl(rawParent);
    if (FAILED(hr)) return hr;
    if (!parentPidl) return E_FAIL;

    ComPtr<IShellFolder> parentFolder;
    hr = desktop->BindToObject(parentPidl.get(), nullptr, IID_PPV_ARGS(parentFolder.addressOf()));
    if (FAILED(hr)) return hr;

    std::vector<OwnedPidl> childPidls;
    for (const auto& path : paths) {
        std::wstring name = path;
        if (const size_t slash = name.find_last_of(L'\\'); slash != std::wstring::npos) name = name.substr(slash + 1);
        PIDLIST_RELATIVE rawChild = nullptr;
        hr = parentFolder->ParseDisplayName(owner, nullptr, name.data(), nullptr, &rawChild, nullptr);
        OwnedPidl child(rawChild);
        if (SUCCEEDED(hr)) childPidls.push_back(std::move(child));
    }
    if (childPidls.empty()) return E_FAIL;

    std::vector<PCUITEMID_CHILD> childPointers;
    childPointers.reserve(childPidls.size());
    for (const auto& child : childPidls) childPointers.push_back(child.get());
    return parentFolder->GetUIObjectOf(owner, static_cast<UINT>(childPointers.size()), childPointers.data(),
                                      iid, nullptr, object);
}
}  // namespace ShellSelection
