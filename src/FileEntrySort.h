#pragma once

#include "Types.h"

#include <vector>

// Pure sort/search logic pulled out of FilePane so it can be unit tested
// without an HWND. Column numbering and comparison rules must stay in sync
// with FilePane's list-view columns (0=name, 1=extension, 2=size, 3=modified).
namespace FileEntrySort {

void sort(std::vector<FileEntry>& entries, int sortColumn, bool ascending);

// `lowercaseQuery` must already be lower-cased (FilePane keeps searchQuery_
// pre-lowered so this doesn't redo that work per entry per keystroke).
bool matchesSearch(const FileEntry& entry, const std::wstring& lowercaseQuery);

}  // namespace FileEntrySort
