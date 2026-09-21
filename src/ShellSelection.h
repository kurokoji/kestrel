#pragma once

#include "ComPtr.h"
#include <shlobj.h>
#include <string>
#include <vector>

namespace ShellSelection {
// Paths must belong to the same parent folder, as selections in FilePane do.
// Unparseable children are skipped, preserving the existing shell behavior.
HRESULT getUIObject(HWND owner, const std::vector<std::wstring>& paths, REFIID iid, void** object);

template <typename T>
ComPtr<T> get(HWND owner, const std::vector<std::wstring>& paths) {
    ComPtr<T> object;
    if (FAILED(getUIObject(owner, paths, IID_PPV_ARGS(object.addressOf())))) return {};
    return object;
}
}  // namespace ShellSelection
