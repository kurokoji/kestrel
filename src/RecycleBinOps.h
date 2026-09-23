#pragma once

#include "ComPtr.h"

#include <windows.h>
#include <string>
#include <vector>

// PIDL-based operations for the virtual Recycle Bin view (see kRecycleBinPath
// in Types.h). Entries there aren't real filesystem paths - FilePane's
// listing only carries each item's in-folder display name (FileEntry::name),
// not a live PIDL - so these can't go through FileOperations' IFileOperation-
// on-a-path calls the way a normal folder's items do. Everything here re-
// resolves names to the Recycle Bin's own PIDLs and drives its real shell
// verbs ("restore", "delete") via IContextMenu, so behavior (confirmation
// dialogs, undo, elevation) matches Explorer's own Recycle Bin exactly
// rather than us reimplementing it.
namespace RecycleBinOps {

// Binds the Recycle Bin's IShellFolder2 and resolves `names` (each an
// in-folder display name) to a UI object for them, same shape as
// ShellSelection::getUIObject/get. A name with no matching entry (already
// gone - emptied or restored by someone else since the listing was taken)
// is skipped rather than failing the whole call. Used for the real,
// shown shell context menu (MainWindow::onContextMenu +
// ShellContextMenu::showAndInvoke) - covers restore and anything else the
// shell itself offers for a Recycle Bin item, without us having to guess
// or match a specific verb string.
HRESULT getUIObject(HWND owner, const std::vector<std::wstring>& names, REFIID iid, void** object);

template <typename T>
ComPtr<T> get(HWND owner, const std::vector<std::wstring>& names) {
    ComPtr<T> object;
    if (FAILED(getUIObject(owner, names, IID_PPV_ARGS(object.addressOf())))) return {};
    return object;
}

// Permanently deletes `names` from the Recycle Bin via the shell's own
// "delete" verb - which shows its own (not skippable) confirmation dialog,
// matching Explorer. Unlike restore, "delete" is a universal, standard
// verb every shell item supports, so invoking it directly (rather than
// only through a shown menu) is safe to rely on here.
bool deleteItemsPermanently(HWND owner, const std::vector<std::wstring>& names);

// Restores the entries whose storage files are `recycledFiles` (the
// C:\$Recycle.Bin\<SID>\$R... paths IFileOperation reports for a delete)
// to where they were deleted from, via the entries' own "undelete" verb
// (the 元に戻す command). Used by Edit > 元に戻す. False if none matched.
bool restoreItems(HWND owner, const std::vector<std::wstring>& recycledFiles);

// Empties the whole Recycle Bin. The shell shows its own confirmation
// dialog (no SHERB_NOCONFIRMATION passed).
bool emptyRecycleBin(HWND owner);

}  // namespace RecycleBinOps
