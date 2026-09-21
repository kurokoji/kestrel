#include "ClipboardFiles.h"

#include <shellapi.h>
#include <shlobj.h>

namespace ClipboardFiles {

namespace {

UINT preferredDropEffectFormat() {
    static const UINT fmt = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    return fmt;
}

}  // namespace

bool set(HWND owner, const std::vector<std::wstring>& paths, bool cut) {
    if (paths.empty()) return false;

    size_t chars = 1;  // final extra null terminator
    for (const auto& p : paths) chars += p.size() + 1;

    const size_t total = sizeof(DROPFILES) + chars * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GHND, total);
    if (!hMem) return false;

    auto* df = static_cast<DROPFILES*>(GlobalLock(hMem));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    auto* dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
    for (const auto& p : paths) {
        wcscpy_s(dst, p.size() + 1, p.c_str());
        dst += p.size() + 1;
    }
    *dst = 0;
    GlobalUnlock(hMem);

    HGLOBAL hEffect = GlobalAlloc(GHND, sizeof(DWORD));
    if (hEffect) {
        auto* eff = static_cast<DWORD*>(GlobalLock(hEffect));
        *eff = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        GlobalUnlock(hEffect);
    }

    if (!OpenClipboard(owner)) {
        GlobalFree(hMem);
        if (hEffect) GlobalFree(hEffect);
        return false;
    }
    EmptyClipboard();
    SetClipboardData(CF_HDROP, hMem);
    if (hEffect) SetClipboardData(preferredDropEffectFormat(), hEffect);
    CloseClipboard();
    return true;
}

std::optional<Files> get(HWND owner) {
    if (!OpenClipboard(owner)) return std::nullopt;

    Files result;
    if (HANDLE hDrop = GetClipboardData(CF_HDROP)) {
        auto hdrop = static_cast<HDROP>(hDrop);
        const UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(hdrop, i, buf, MAX_PATH)) result.paths.emplace_back(buf);
        }
    }
    if (HANDLE hEff = GetClipboardData(preferredDropEffectFormat())) {
        if (auto* eff = static_cast<DWORD*>(GlobalLock(hEff))) {
            result.move = (*eff & DROPEFFECT_MOVE) != 0;
            GlobalUnlock(hEff);
        }
    }
    CloseClipboard();

    if (result.paths.empty()) return std::nullopt;
    return result;
}

}  // namespace ClipboardFiles
