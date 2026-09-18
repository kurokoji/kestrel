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
    std::wstring name = entry.name;
    std::ranges::transform(name, name.begin(), ::towlower);
    return name.find(lowercaseQuery) != std::wstring::npos;
}

}  // namespace FileEntrySort
