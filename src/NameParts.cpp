#include "NameParts.h"

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

}  // namespace NameParts
