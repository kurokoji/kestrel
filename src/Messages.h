#pragma once

#include <windows.h>

// Custom messages used to hand data from background threads back to the UI
// thread. Kept as a flat list - no message bus, no dispatcher abstraction.

// Posted by DirectoryModel's worker thread when enumeration finishes.
// lParam = EnumerationResult* (owned by receiver, must delete).
inline constexpr UINT WM_APP_DIR_RESULT = WM_APP + 1;

// Posted by FilePane/TreePane to MainWindow when the active pane's
// directory changes, so the address bar / status bar / tree selection can
// be kept in sync. wParam = pane id (0 = left, 1 = right).
inline constexpr UINT WM_APP_PANE_NAVIGATED = WM_APP + 2;

// Posted by FilePane to MainWindow when the selection within a pane
// changes, so the status bar can be refreshed.
inline constexpr UINT WM_APP_SELECTION_CHANGED = WM_APP + 3;

// Posted by DirectoryWatcher's worker thread when the currently displayed
// directory changes on disk (from any source - another process, Explorer,
// the other pane). wParam = the FilePane* to refresh. MainWindow debounces
// this with a short timer rather than refreshing on every single event.
inline constexpr UINT WM_APP_DIR_CHANGED = WM_APP + 4;

// Posted by TreePane's worker thread when a node's child-folder
// enumeration finishes. lParam = TreeChildrenResult* (owned by receiver,
// must delete).
inline constexpr UINT WM_APP_TREE_CHILDREN = WM_APP + 5;
