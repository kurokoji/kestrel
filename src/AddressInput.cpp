#include "AddressInput.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace AddressInput {
namespace {

std::wstring trimmed(const std::wstring& s) {
    const size_t first = s.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    const size_t last = s.find_last_not_of(L" \t");
    return s.substr(first, last - first + 1);
}

bool startsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool hasDriveLetter(const std::wstring& s) { return s.size() >= 2 && std::iswalpha(s[0]) && s[1] == L':'; }

// Splits an absolute path into its root ("C:\" or "\\server\share") and
// the rest, then rebuilds it with "." and ".." resolved. ".." never climbs
// above the root.
std::wstring normalizeAbsolute(const std::wstring& path) {
    std::wstring root;
    size_t restStart = 0;
    if (path.starts_with(L"\\\\")) {
        const size_t serverEnd = path.find(L'\\', 2);
        const size_t shareEnd = serverEnd == std::wstring::npos ? std::wstring::npos : path.find(L'\\', serverEnd + 1);
        if (shareEnd == std::wstring::npos) return path;  // just "\\server" or "\\server\share"
        root = path.substr(0, shareEnd);
        restStart = shareEnd + 1;
    } else {
        root = path.substr(0, 2) + L"\\";  // "C:" (with or without a separator after it)
        restStart = (path.size() > 2 && path[2] == L'\\') ? 3 : 2;
    }

    std::vector<std::wstring> segments;
    size_t start = restStart;
    while (start <= path.size()) {
        size_t end = path.find(L'\\', start);
        if (end == std::wstring::npos) end = path.size();
        const std::wstring segment = path.substr(start, end - start);
        if (segment == L"..") {
            if (!segments.empty()) segments.pop_back();
        } else if (!segment.empty() && segment != L".") {
            segments.push_back(segment);
        }
        start = end + 1;
    }

    std::wstring out = root;
    for (size_t i = 0; i < segments.size(); ++i) {
        if (!out.ends_with(L'\\')) out += L'\\';
        out += segments[i];
    }
    return out;
}

}  // namespace

std::wstring resolve(const std::wstring& input, const std::wstring& currentDir) {
    std::wstring s = trimmed(input);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = trimmed(s.substr(1, s.size() - 2));
    if (s.empty()) return {};
    if (startsWithNoCase(s, L"shell:") || s.starts_with(L"::")) return s;

    std::ranges::replace(s, L'/', L'\\');
    if (hasDriveLetter(s) || s.starts_with(L"\\\\")) return normalizeAbsolute(s);

    // Relative input: only meaningful against a real folder.
    if (currentDir.empty() || currentDir.starts_with(L"::")) return s;
    if (s.starts_with(L'\\')) {
        // "\foo" = from the current drive's root.
        return hasDriveLetter(currentDir) ? normalizeAbsolute(currentDir.substr(0, 2) + s) : s;
    }
    return normalizeAbsolute(currentDir + L"\\" + s);
}

}  // namespace AddressInput
