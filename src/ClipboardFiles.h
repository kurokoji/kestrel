#pragma once

#include <windows.h>
#include <optional>
#include <string>
#include <vector>

// CF_HDROP + CFSTR_PREFERREDDROPEFFECT clipboard access, for Ctrl+C/X/V on
// files - kept separate from FileOperations (which wraps IFileOperation /
// ShellExecuteExW) since this is plain clipboard/memory plumbing with no
// shell operation of its own.
namespace ClipboardFiles {

struct Files {
    std::vector<std::wstring> paths;
    bool move = false;
};

// Places `paths` on the clipboard as CF_HDROP, tagged with the preferred
// drop effect (move vs. copy) other apps (and our own paste) read back.
bool set(HWND owner, const std::vector<std::wstring>& paths, bool cut);

// Reads back a CF_HDROP placed by `set` (or by Explorer/another app).
// Returns std::nullopt if the clipboard holds no file drop.
std::optional<Files> get(HWND owner);

}  // namespace ClipboardFiles
