#include "NameParts.h"
#include "Types.h"

#include <algorithm>
#include <cwctype>
#include <format>

namespace NameParts {

size_t renameSelectionEnd(const std::wstring& name, bool isDirectory) {
    if (isDirectory) return name.size();
    const size_t dot = name.find_last_of(L'.');
    return (dot == std::wstring::npos || dot == 0) ? name.size() : dot;
}

std::wstring uniqueName(const std::vector<std::wstring>& existingNames, const std::wstring& base) {
    auto lowered = [](std::wstring s) {
        std::ranges::transform(s, s.begin(), ::towlower);
        return s;
    };
    std::vector<std::wstring> taken;
    taken.reserve(existingNames.size());
    for (const auto& name : existingNames) taken.push_back(lowered(name));

    std::wstring candidate = base;
    for (int n = 2; std::ranges::find(taken, lowered(candidate)) != taken.end(); ++n) {
        candidate = std::format(L"{} ({})", base, n);
    }
    return candidate;
}

std::wstring fallbackTypeName(const std::wstring& extension, bool isDirectory) {
    if (isDirectory) return L"ファイル フォルダー";
    if (extension.size() <= 1) return L"ファイル";
    std::wstring upper = extension.substr(1);  // without the leading dot
    std::ranges::transform(upper, upper.begin(), ::towupper);
    return upper + L" ファイル";
}

std::wstring tabLabel(const std::wstring& path) {
    if (path == kThisPcPath) return L"PC";
    if (path == kRecycleBinPath) return L"ゴミ箱";
    const size_t slash = path.find_last_of(L'\\');
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return name.empty() ? path : name;  // e.g. "C:\" has nothing after its trailing slash
}

}  // namespace NameParts
