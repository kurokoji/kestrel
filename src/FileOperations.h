#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Thin wrapper around IFileOperation / ShellExecuteExW. Deliberately not an
// abstraction layer over the shell - callers still think in terms of shell
// verbs and paths. All functions run on the calling (UI) thread; the shell
// itself pumps its own progress/confirmation UI, so the app stays
// responsive without us managing a worker thread for file I/O.
namespace FileOperations {

// Copies `sources` into `destDir`. Conflicts, progress and elevation are
// handled by the shell's own UI.
bool copyItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir);

// Moves `sources` into `destDir`.
bool moveItems(HWND owner, const std::vector<std::wstring>& sources, const std::wstring& destDir);

// Sends `sources` to the Recycle Bin (FOF_ALLOWUNDO), or with `permanent`
// deletes them outright after the shell's own confirmation.
bool deleteItems(HWND owner, const std::vector<std::wstring>& sources, bool permanent = false);

// Renames a single item in place.
bool renameItem(HWND owner, const std::wstring& path, const std::wstring& newName);

// Creates a new subdirectory `name` inside `parentDir`.
bool createDirectory(HWND owner, const std::wstring& parentDir, const std::wstring& name);

// Opens `path` with its associated application (or as a folder).
void openItem(HWND owner, const std::wstring& path);

// Launches the associated "edit" verb for `path`, falling back to Notepad.
void editItem(HWND owner, const std::wstring& path);

// Starts an OLE drag-and-drop operation carrying `sources` (all assumed to
// live in the same folder, as with a single pane's selection) using a real
// shell IDataObject - so external drop targets (Explorer, browsers, mail
// clients) see the same CF_HDROP/file-group-descriptor formats Explorer
// itself would offer. `owner` is the window the drag starts from (it
// supplies the drag image). Blocks (pumping messages via DoDragDrop) until
// the drag ends; returns true if it ended in an actual drop.
bool startDrag(HWND owner, const std::vector<std::wstring>& sources);

}  // namespace FileOperations
