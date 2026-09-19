#pragma once

#include <windows.h>
#include <optional>
#include <string>

// Pure logic for the per-drive-letter colored badge drawn at the start of
// each tab label, pulled out of FilePane::drawTabItem so it's testable
// without an HDC/tab control.
namespace DriveBadge {

// Returns the drive letter (uppercased) a path like "C:\Users\foo" or
// "C:\" starts with, or nullopt for anything else (UNC paths, relative
// paths, empty). Doesn't validate the drive actually exists.
std::optional<wchar_t> driveLetterOf(const std::wstring& path);

// Deterministic color per letter (same letter always gets the same
// color), cycling through a small fixed palette - not globally unique
// past 8 drives, but distinguishing C/D/E/F... at a glance is the goal,
// not a guarantee of no repeats.
COLORREF colorForDrive(wchar_t driveLetter);

}  // namespace DriveBadge
