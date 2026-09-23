#pragma once

#include "Types.h"

#include <vector>

// Pure sort/search logic pulled out of FilePane so it can be unit tested
// without an HWND. Column numbering and comparison rules must stay in sync
// with FilePane's list-view columns (0=name, 1=type name, 2=size, 3=modified).
namespace FileEntrySort {

void sort(std::vector<FileEntry>& entries, int sortColumn, bool ascending);

// `lowercaseQuery` must already be lower-cased (FilePane keeps searchQuery_
// pre-lowered so this doesn't redo that work per entry per keystroke).
bool matchesSearch(const FileEntry& entry, const std::wstring& lowercaseQuery);

// Index of the first entry at or after `start` whose name starts with
// `prefix` (case-insensitive), or -1. With `wrap`, continues from the top
// after the last entry. Backs the list's type-to-select (LVN_ODFINDITEM).
int findByPrefix(const std::vector<FileEntry>& entries, const std::wstring& prefix, int start, bool wrap);

// Removes FILE_ATTRIBUTE_HIDDEN entries (View > 隠しファイル off).
void removeHidden(std::vector<FileEntry>& entries);

}  // namespace FileEntrySort
