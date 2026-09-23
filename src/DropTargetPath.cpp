#include "DropTargetPath.h"
#include "Types.h"

#include <algorithm>
#include <cwctype>

namespace DropTargetPath {
namespace {

std::wstring lowered(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
}

std::wstring withoutTrailingSlash(std::wstring s) {
    if (s.size() > 1 && s.back() == L'\\') s.pop_back();
    return s;
}

std::wstring parentOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

}  // namespace

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir == kThisPcPath) return name;

    std::wstring full = dir;
    if (!full.empty() && full.back() != L'\\') full += L'\\';
    full += name;
    return full;
}

std::vector<std::wstring> candidates(const std::wstring& currentDir, const std::wstring& hitName, bool hitIsDirectory) {
    // Recycle Bin rows aren't real paths; any drop there means "delete".
    if (currentDir == kRecycleBinPath) return {currentDir};
    if (currentDir == kThisPcPath) {
        if (hitName.empty() || !hitIsDirectory) return {};
        return {joinPath(currentDir, hitName)};
    }
    if (hitName.empty()) return {currentDir};
    return {joinPath(currentDir, hitName), currentDir};
}

bool isOntoSource(const std::vector<std::wstring>& sources, const std::wstring& target, bool explicitCopy) {
    const std::wstring t = lowered(withoutTrailingSlash(target));
    for (const auto& source : sources) {
        const std::wstring s = lowered(withoutTrailingSlash(source));
        if (t == s || (t.size() > s.size() && t.starts_with(s) && t[s.size()] == L'\\')) return true;
        if (!explicitCopy && t == withoutTrailingSlash(parentOf(s))) return true;
    }
    return false;
}

}  // namespace DropTargetPath
