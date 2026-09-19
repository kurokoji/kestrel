#include "DriveBadge.h"

#include <cwctype>

namespace DriveBadge {

std::optional<wchar_t> driveLetterOf(const std::wstring& path) {
    if (path.size() < 2) return std::nullopt;
    if (!std::iswalpha(path[0]) || path[1] != L':') return std::nullopt;
    return static_cast<wchar_t>(std::towupper(path[0]));
}

COLORREF colorForDrive(wchar_t driveLetter) {
    // Muted/pastel so dark text stays readable on top - order chosen so
    // adjacent letters (the common C/D/E/F... case) don't land on visually
    // similar colors.
    static constexpr COLORREF kPalette[] = {
        RGB(186, 219, 255),  // pale blue
        RGB(255, 214, 165),  // pale orange
        RGB(198, 236, 203),  // pale green
        RGB(255, 199, 206),  // pale red/pink
        RGB(225, 205, 255),  // pale purple
        RGB(255, 244, 168),  // pale yellow
        RGB(184, 233, 233),  // pale teal
        RGB(255, 218, 235),  // pale magenta
    };
    const wchar_t upper = static_cast<wchar_t>(std::towupper(driveLetter));
    const size_t index = static_cast<size_t>(upper - L'A') % std::size(kPalette);
    return kPalette[index];
}

}  // namespace DriveBadge
