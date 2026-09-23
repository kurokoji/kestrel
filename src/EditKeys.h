#pragma once

// Decides which keystrokes a focused EDIT control (rename label edit,
// address bar, search box) must receive itself instead of the main
// accelerator table. Without this, Ctrl+C/X/V are translated into the
// file copy/cut/paste commands before the edit ever sees them, so text
// selected in the edit can't be copied, and Ctrl+A would select files
// instead of the edit's text. Kept free of HWNDs so it's
// testable; App's message loop supplies the focus/modifier state.
namespace EditKeys {

inline bool editOwnsKey(unsigned vk, bool ctrl, bool shift, bool alt) {
    if (!ctrl || shift || alt) {
        return false;
    }
    return vk == 'C' || vk == 'X' || vk == 'V' || vk == 'A';
}

}  // namespace EditKeys
