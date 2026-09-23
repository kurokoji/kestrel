#include "FileEntrySort.h"

#include <algorithm>
#include <cwctype>

namespace FileEntrySort {

void sort(std::vector<FileEntry>& entries, int sortColumn, bool ascending) {
    std::ranges::sort(entries, [sortColumn, ascending](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory() != b.isDirectory()) return a.isDirectory();

        int cmp = 0;
        switch (sortColumn) {
            case 1:
                cmp = _wcsicmp(a.extension.c_str(), b.extension.c_str());
                break;
            case 2:
                cmp = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
                break;
            case 3:
                cmp = CompareFileTime(&a.modified, &b.modified);
                break;
            default:
                cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
                break;
        }
        if (cmp == 0) cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
        return ascending ? (cmp < 0) : (cmp > 0);
    });
}

bool matchesSearch(const FileEntry& entry, const std::wstring& lowercaseQuery) {
    if (lowercaseQuery.empty()) return false;
    return entry.lowercaseName.find(lowercaseQuery) != std::wstring::npos;
}

int findByPrefix(const std::vector<FileEntry>& entries, const std::wstring& prefix, int start, bool wrap) {
    const int count = static_cast<int>(entries.size());
    if (count == 0) return -1;
    std::wstring lowered = prefix;
    std::ranges::transform(lowered, lowered.begin(), ::towlower);
    if (start < 0 || start >= count) start = 0;

    const int steps = wrap ? count : count - start;
    for (int step = 0; step < steps; ++step) {
        const int i = (start + step) % count;
        if (entries[i].lowercaseName.starts_with(lowered)) return i;
    }
    return -1;
}

}  // namespace FileEntrySort
