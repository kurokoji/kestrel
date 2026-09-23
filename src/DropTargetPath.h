#pragma once

#include <string>
#include <vector>

// Pure path logic behind FilePane/TreePane's OLE drop targets (see
// FileDropTarget), pulled out so it can be unit tested without an HWND.
namespace DropTargetPath {

// `name` appended to `dir` with exactly one separator. kThisPcPath's
// entries are already full drive roots ("C:\"), so they're returned as-is.
std::wstring joinPath(const std::wstring& dir, const std::wstring& name);

// Shell items to try binding an IDropTarget for, in priority order, when
// something is dropped on a row named `hitName` (empty = empty list space)
// of a pane showing `currentDir`. A file row comes first so exe/zip-style
// drop handlers work; the folder itself is the fallback when the hit item
// has no drop handler. Empty = nowhere to drop (This PC's background).
std::vector<std::wstring> candidates(const std::wstring& currentDir, const std::wstring& hitName, bool hitIsDirectory);

// True if dropping our own drag's `sources` on `target` makes no sense:
// onto one of the dragged items itself or inside a dragged folder, or back
// into the folder they came from (unless `explicitCopy`, where Explorer
// makes a "- コピー" duplicate). Comparison is case-insensitive.
bool isOntoSource(const std::vector<std::wstring>& sources, const std::wstring& target, bool explicitCopy);

}  // namespace DropTargetPath
