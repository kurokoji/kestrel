#pragma once

#include <windows.h>
#include <optional>
#include <string>

// Tiny modal popup helpers built from raw Win32 controls - no dialog
// resource, no DLGTEMPLATE juggling. Used for the handful of MVP prompts
// (new folder name) that don't warrant a full dialog resource.
namespace Dialogs {

std::optional<std::wstring> promptForText(HWND owner, const wchar_t* title, const wchar_t* label,
                                           const wchar_t* initialValue);

void showError(HWND owner, const wchar_t* title, const wchar_t* message);

}  // namespace Dialogs
