#pragma once

// Pure index arithmetic for Ctrl+Tab/Ctrl+Shift+Tab tab cycling, kept out
// of FilePane so it's testable without a tab-strip HWND.
namespace TabCycle {

// Returns the tab index to switch to from `current` out of `count` tabs,
// wrapping around at either end. `count` <= 1 has no other tab to go to,
// so `current` is returned unchanged (callers should treat that as a
// no-op, matching FilePane::switchToTab's existing same-index guard).
int nextIndex(int current, int count, bool forward);

}  // namespace TabCycle
