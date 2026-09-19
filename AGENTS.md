# Agent notes for Kestrel Filer

Working notes for whichever coding agent touches this repo next. This is a
pure Win32/Common Controls C++23 app (see README.md for the feature/build
overview) - the gotchas below are things that cost real time to find during
development and aren't obvious from reading the Win32 docs alone.

**Keep this file up to date.** When you (the agent) hit something during
this project that cost real debugging time, was non-obvious from the
docs, or reflects a deliberate design decision the user made - add it
here, in the same terse style as the existing entries. This file is only
useful if it keeps growing; don't let a hard-won lesson evaporate at the
end of the session.

**Keep README.md up to date too.** It's user-facing (feature list, build
instructions, keyboard shortcut table). Any change that adds/removes a
feature, shortcut, or build step should update README.md in the same
commit - don't let it drift out of sync with what the app actually does.

## TDD workflow (t-wada style)

- New logic changes follow Red-Green-Refactor: write a failing test in
  `tests/` first, confirm it fails, write the minimum code to pass, then
  refactor with the test green. Don't write production code ahead of a
  failing test that demands it.
- Test framework is [doctest](https://github.com/doctest/doctest), vendored
  as a single header at `third_party/doctest/doctest.h` (not a submodule -
  update by re-downloading the file if ever needed).
- `kestrel_tests` is a separate CMake executable target (see
  `CMakeLists.txt`, `KESTREL_BUILD_TESTS` option, default ON) built
  alongside `kestrel` by the same `build.bat`. It links no Win32 UI code -
  only plain-logic headers/sources included directly.
- Only Win32-independent logic is realistically unit-testable here (see
  `Formatting.h`/`tests/FormattingTests.cpp` for the pattern). Code that's
  fundamentally message-loop/HWND-shaped (window procs, notification
  handlers) isn't a good TDD target with this harness - extract the pure
  logic out of it into a plain function/class first if it needs coverage,
  rather than trying to unit-test the HWND-bound code directly.
- Run tests with `build\kestrel_tests.exe` after building.

## Build & rebuild loop

- `build.bat` in the repo root loads the VS dev environment and runs
  `cmake` (Ninja generator) + `cmake --build`. Use it as-is; don't assume
  `cl`/`cmake` are on PATH without it.
- Before rebuilding, kill any running `kestrel.exe` first (`Stop-Process
  -Name kestrel -Force`) - the linker fails with `LNK1104` if the old exe
  is still locked by a running process.
- Embedding our own manifest resource (`1 24 "kestrel.manifest"` in
  `kestrel.rc`) conflicts with MSVC's automatic manifest generation unless
  `/MANIFEST:NO` is passed at link time (already set in CMakeLists.txt -
  don't remove it, or the link fails with a duplicate-resource CVTRES
  error).
- `<gdiplus.h>` fails to compile with bizarre errors (`PROPID` undefined,
  etc.) under `WIN32_LEAN_AND_MEAN` unless `<objidl.h>` is included first.
  See PreviewPane.h / App.cpp for the working include order.

## Win32 gotchas discovered the hard way

- **TreeView lazy-loading**: a node's `cChildren` flag has to say "I have
  children" (`TVIF_CHILDREN`, `cChildren = 1`) *at creation time*, even
  before any real child items exist. If you insert real children later
  and only then try `TVE_EXPAND`, comctl32 still refuses to expand
  because it trusted the original (wrong) hint. Cost a full debug session
  to find (see TreePane's `addRootItems` for the "This PC" node).
- **A native control's own default click handling can steal focus back
  after you've already set it.** `SysTabControl32` moves focus to itself
  on `WM_LBUTTONDOWN` as part of its normal (`DefSubclassProc`) handling -
  so calling `SetFocus` on some other window (e.g. the pane's file list,
  to make that pane "active") *before* letting the tab control process
  the click gets silently undone the moment `DefSubclassProc` runs. Call
  `SetFocus` *after* `DefSubclassProc`, not before. See
  `TabStripSubclassProc`'s `WM_LBUTTONDOWN` case / `FilePane::activate`.
- **Don't trust `TreeView_GetSelection`/`ListView` state from inside a
  click notification** if you can avoid it - depending on internal
  drag-threshold handling, the notification can fire before the control
  has actually committed the new selection, so you silently act on the
  *previous* item. Hit-test the real click point instead (pull it from
  `GetMessagePos()` since `NM_CLICK` carries no point of its own). See
  `TreePane::handleNotify`'s `NM_CLICK`/`NM_DBLCLK` case.
- **`LVS_OWNERDATA` only repaints automatically when the item *count*
  changes.** Swapping in a same-sized new dataset (e.g. navigating to a
  folder with the same file count) needs an explicit
  `InvalidateRect(hwnd, nullptr, TRUE)` or the old rows stay on screen.
- **Custom-painted overlays (the active-pane blue frame) need
  `erase=TRUE`** when the layout changes shape (e.g. single-pane <->
  dual-pane toggle), or the old frame's pixels linger next to the new one
  since nothing else clears that region.
- **A plain (non-multiline) `EDIT` control beeps on `WM_CHAR` for Enter or
  Escape** if you don't swallow it - intercepting `WM_KEYDOWN` alone isn't
  enough, because `TranslateMessage` still turns it into a `WM_CHAR` your
  subclass has to also eat. See `SearchBoxSubclassProc`.
- **A pane's tab strip can have a sliver of its rect not actually covered
  by `tabHwnd_`/`newTabButton_`/the list** (never pinned down exactly why
  - not reproducible via `PostMessage`-simulated clicks at any tested
  coordinate, only via a real click on a real running window at a
  specific screen position; possibly a DPI-rounding gap in
  `FilePane::setBounds`'s width split between the two). A click landing
  there goes to `MainWindow`'s own background instead of any child
  control, so **`MainWindow::onLButtonDown` has a fallback**: after the
  splitter checks, if the point falls in `leftOuterRect_`/
  `rightOuterRect_` but hit none of the actual child controls, it still
  calls that pane's `activate()`. Don't remove this thinking it's dead
  code just because the splitter/child-control paths look like they
  should cover everything - they don't, in a way that's hard to
  reproduce on demand. If you ever do pin down the real gap, fix that
  and you can probably drop this, but verify with a real mouse click
  (not a posted message) before doing so.
- **`TB_SETPADDING` on a toolbar only adds space *after* the label, not
  before it** - cranking it up makes buttons wider without recentering the
  text. There's no clean built-in fix for this without full owner-draw;
  we left the toolbar at default padding + `TB_SETBITMAPSIZE(0,0)` (no
  reserved icon gutter) rather than chase pixel-perfect centering.
- **Two `case WM_DRAWITEM:` in the same `switch` is a compile error** -
  when a new owner-draw need shows up (tab close glyphs) alongside an
  existing one (shell context menu message forwarding), merge them with a
  shared case + `[[fallthrough]]`, don't just add a second case.
- Two panes navigate independently and asynchronously
  (`DirectoryModel`/`DirectoryWatcher` are per-`FilePane`, on background
  `std::jthread`s). Any `onNavigated`/`onSelectionChanged` handler in
  `MainWindow` **must check `&pane == &activePane()`** before touching
  shared UI (address bar, tree sync, status bar) - otherwise the
  *inactive* pane's background completion can race in and stomp the
  active pane's freshly-displayed state. Bit us during startup (both
  panes load concurrently) and during tree-click navigation.
- `std::jthread`'s move-assignment auto-`request_stop()`s and joins the
  previous thread - this is relied on deliberately in `DirectoryModel`
  and `DirectoryWatcher` as the "cancel and restart" mechanism. Don't
  "simplify" that into a raw `std::thread` without re-adding cancellation.

## Testing in this sandbox (if you're doing UI verification)

- Real synthetic mouse input (`mouse_event`/`SendInput`) is **unreliable**
  here - clicks frequently don't register at all, with no error. Don't
  trust a lack of visible effect as "the app is broken"; it's usually the
  test harness. Prefer, in order of reliability: `BM_CLICK` sent directly
  to a button HWND, `WM_SETTEXT`/`WM_GETTEXT` (both are on Windows' short
  list of messages safely marshaled cross-process), posting `WM_COMMAND`
  for a known menu/accelerator ID, and `PrintWindow` for screenshots.
- **`PostMessage`-ing a click straight to a specific HWND is *not*
  equivalent to a real mouse click at that screen position** - it
  bypasses the OS's own hit-testing entirely, so it can "work" (the
  target genuinely receives and processes the message) even when a real
  click at that exact spot would actually land on a different, possibly
  invisible/overlapping window instead - `WindowFromPoint` at the real
  screen coordinate is the only way to confirm what a real click would
  actually hit. Chased a real bug in circles for a long time this way:
  every synthetic test "proved" a click handler worked while a real
  click at the same-looking spot did nothing (see the `onLButtonDown`
  fallback gotcha above). If a user reports a click not working
  somewhere that all your synthetic testing says should work, don't
  trust the synthetic testing over their report.
- Similarly, don't assume you and the user share visual context of the
  live window: if your own coordinate probing (`GetWindowRect`,
  `WindowFromPoint`) doesn't match what the user describes seeing, the
  window may have moved, been resized, or been covered by something
  since you last measured it - re-measure fresh rather than trusting
  cached coordinates from earlier in the session.
- For verifying a specific tree/list *action* (not a click location) -
  e.g. "does expanding this node work" - skip pixel-coordinate clicking
  entirely and drive the control directly: `TVM_GETNEXTITEM`
  (`TVGN_ROOT`/`TVGN_NEXT`/`TVGN_CHILD`) to walk to the `HTREEITEM` you
  want, then `TVM_EXPAND`/`TVE_EXPAND` on it. These pass plain handle
  values as wParam/lParam (not pointers to marshal), so they're safe
  cross-process the same way `WM_COMMAND` is, and far more reliable than
  guessing where an expand glyph is on screen.
- **Never send a pointer-bearing message (`LVM_GETITEMRECT`,
  `TCM_GETITEMRECT`, `SB_GETTEXT`, etc.) to another process's window from
  a PowerShell-side buffer.** The target process dereferences a pointer
  that's only valid in *your* process's address space and crashes. This
  happened twice during development. If you need that data, there's
  usually no safe cross-process way to get it short of `ReadProcessMemory`
  - just don't.
- Verifying a hover state (e.g. the tab close button's hover highlight)
  by posting a synthetic `WM_MOUSEMOVE` to a child control needs its
  *client*-coordinate point, which means chaining safe direct API calls
  (`GetWindowRect` + `ScreenToClient` - fine, these read via the window
  manager, not the target's message loop) off pixel coordinates read from
  a screenshot; small measurement error easily lands outside the target
  rect. Confirmed `TCM_GETITEMRECT` still can't be used to get that rect
  directly (returns failure cross-process, per the pointer-bearing
  warning above) even though it doesn't crash. Not worth spending much
  time chasing pixel-perfect proof of a hover repaint this way - a
  hover/leave handler that reuses the same hit-test rect the click
  handler already uses (so their behavior can't drift apart) is validated
  well enough by code review plus a build.
- If a real, previously-saved session gets restored during testing (this
  app persists open folders across runs - see Session.h), you may end up
  looking at the user's actual files/directory listings, not test data.
  Prefer navigating to an isolated temp folder you created yourself before
  screenshotting, and delete any screenshot immediately after checking it
  rather than describing its contents.

## Design decisions worth preserving

- Tab strip styling (`FilePane::drawTabItem`) is deliberately flat: no
  `DrawEdge` bevel, just a 2px navy underline (`kActiveTabAccent`) on the
  active tab and a plain background fill otherwise - chosen over the
  original 3D-beveled look for a lighter, more modern feel. The close
  glyph's hover highlight (rounded rect behind the ×, via
  `closeHoverRectFor`) is tracked in `TabStripSubclassProc` with
  `TrackMouseEvent`/`WM_MOUSEMOVE`/`WM_MOUSELEAVE`, calling
  `FilePane::setHoveredCloseTab` - which only invalidates the specific
  old/new tab rects, not the whole strip. `closeHoverRectFor` deliberately
  reuses `closeButtonRectFor` (just inflated a couple pixels) so the
  hover highlight and the actual click hit-test can't drift apart.
- The file-list controls use `LVS_SHOWSELALWAYS`. Without it, a
  ListView's selection isn't just dimmed when it lacks keyboard focus -
  it's fully hidden, and since Windows clears/restores focus across
  window activation, that meant Alt-Tabbing away from the app made the
  active pane's selection vanish entirely (reported as a usability bug,
  fixed by adding this style - see FilePane::create). With the style,
  the focus-sensitive blue/gray selection color is still *the* indicator
  of which pane is active, on top of the custom-painted frame - that
  part of the original design is unaffected; only the previously-fully-
  invisible unfocused state became visible-but-gray.
- Search (`Ctrl+F`) highlights matches in place rather than filtering the
  list - explicitly requested; don't change this to a hide/filter model.
- File operations (copy/move/delete/rename) are delegated to
  `IFileOperation`/`ShellExecuteExW` on purpose, so Explorer's own
  progress UI, conflict resolution, and Recycle Bin behavior "just work"
  without us reimplementing any of it.
- Outbound drag-and-drop (`FileOperations::startDrag`, wired from
  `FilePane`'s `LVN_BEGINDRAG`) builds a real shell `IDataObject` via
  `IShellFolder::GetUIObjectOf(..., IID_IDataObject, ...)` on the
  selection's PIDLs - same PIDL-binding pattern as the shell context menu
  in `MainWindow.cpp`'s `getShellContextMenu`. This gets CF_HDROP and
  every other format Explorer would offer "for free" instead of us having
  to build a custom `IDataObject`. **`DoDragDrop` requires `OleInitialize`
  (not just `CoInitializeEx`) on the calling thread** - App.cpp's message
  loop thread was switched from one to the other for this; don't revert
  it back to plain `CoInitializeEx` or dragging silently stops working.
- The custom UI font (Tools > Options, `MainWindow::chooseFont`/
  `applyFont`) is deliberately scoped to the tree/lists/tabs/address
  bar/status bar only - not the toolbar (icon-only, no visible text worth
  restyling) and not `PreviewPane`'s own text (its labels use
  `GetStockObject(DEFAULT_GUI_FONT)` directly in `paint()`, and the text
  preview is intentionally fixed to `Consolas` as a monospace code/text
  font, unrelated to the general UI font choice). Stored as face name +
  point size + bold in `SessionData`/`SessionFormat`'s `"F"` record
  rather than a raw `LOGFONT` blob, since `lfHeight` is DPI/device
  dependent - `chooseFont()`/the startup restore path both recompute it
  via `MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72)`.
- Each tab's label gets a colored drive-letter badge (`DriveBadge::
  driveLetterOf`/`colorForDrive`, drawn in `FilePane::drawTabItem`) -
  color is a deterministic `(letter - 'A') % paletteSize` lookup, not
  actually unique past 8 drives, since the goal is "tell C/D/E/F apart at
  a glance," not a guaranteed-unique color per drive. `drawTabItem` needs
  the tab's *path*, not just its label text (which is already just the
  folder name via `tabLabelFor`) - for the active tab that's the live
  `currentPath_`, for any other tab it's `tabs_[idx].path` (only synced
  on tab switch, so reading it directly here rather than caching
  separately is deliberate - it's always correct for non-active tabs).
- The tab strip is `TCS_MULTILINE`: once tabs stop fitting one row, they
  wrap onto more rows instead of showing scroll arrows (explicitly
  requested over the scroll-arrow behavior). `TCS_MULTILINE` only knows
  how many rows it needs *after* it's been sized at its actual width, so
  `FilePane::setBounds` moves the control once at a single row's height
  first, reads `TabCtrl_GetRowCount`, then resizes to
  `rows * kTabStripHeight` - a single `MoveWindow` call can't do this in
  one step. Because the row count (and therefore how much vertical space
  the list below it gets) can change whenever a tab is added or removed,
  `FilePane::newTab`/`closeTab` fire a new `onTabCountChanged` callback
  that `MainWindow` wires to `layoutChildren()`, the same way
  `onSearchVisibilityChanged` already worked for the search box's row.
  `restoreTabs()` doesn't need its own trigger since `onCreate()` already
  calls `layoutChildren()` unconditionally right after using it.
- `FileEntry` carries pre-formatted `formattedSize`/`formattedModified`
  strings, computed once in `DirectoryModel::run` (on its background
  thread) rather than in `FilePane`'s `LVN_GETDISPINFOW` handler. That
  handler fires on every row the list paints - including every frame
  while scrolling a large directory - so calling `Formatting::formatSize`/
  `formatFileTime` (each doing a `std::format` heap allocation) there
  meant reformatting the same unchanging value repeatedly. Format once
  when the entry is created instead, and hand out `const wchar_t*` into
  the entry's own strings from `LVN_GETDISPINFOW` (no per-cell
  `std::wstring` copy either) - this is a perf-only change with no
  behavior difference, verified with a live directory listing.
- `PreviewPane`'s actual load (image decode/shell thumbnail/text read) is
  debounced via `SetTimer`/`kLoadDebounceMs`, not spawned immediately from
  `loadFor()`. Arrow-key/selection scrolling through a list previously
  spawned-and-immediately-discarded a background thread (with its own
  `CoInitializeEx`) per row passed through. `SetTimer` with the same timer
  ID just re-arms the delay on each call - only the last selection within
  the debounce window actually gets a worker thread. The cheap immediate
  part (icon fallback + size) still happens synchronously in `loadFor()`
  itself, unchanged, so the preview never looks like it's lagging on a
  single selection. `reset()` also kills any pending timer, so clearing
  the selection (empty path) can't fire a stale load afterward.
  Image files now try `loadShellThumbnailBitmap` first (the same
  cached-frame source video already used) before falling back to a real
  GDI+ decode (`loadImageBitmap`, still bounded by
  `kMaxImagePreviewFile`) - a full decode's cost scales with the source
  file's resolution even though the preview area is small, while the
  shell thumbnail's doesn't. The text-preview `Consolas` font
  (`PreviewPane::textFont_`) is now created once and reused across
  paints instead of a `CreateFontW`/`DeleteObject` pair every `WM_PAINT`.
- `TreePane::populateChildren` enumerates a node's subfolders on a
  background thread (`enumerateChildrenWorker`, mirroring
  `DirectoryModel`'s pattern) instead of blocking `TVN_ITEMEXPANDINGW` -
  a slow location (network share, etc.) no longer freezes the whole UI
  just to expand a tree node. A "読み込み中..." placeholder child is
  inserted synchronously so the expand still shows something immediately;
  `handleChildrenResult` (called from `MainWindow`'s `WM_APP_TREE_CHILDREN`
  handler) deletes that placeholder and inserts the real children once
  the background enumeration posts its result. No request-id/staleness
  tracking needed here (unlike `DirectoryModel`) because tree nodes are
  never deleted during normal operation (append-only, lazily populated
  once) and `data->childrenLoaded` is set *before* the thread is spawned,
  so a node can never have two enumerations in flight. Verified live:
  expanded `C:\` via a direct `TVM_EXPAND` message (more reliable than a
  synthetic click at guessed pixel coordinates - see the testing-sandbox
  notes above) and confirmed real subfolders replaced the placeholder.
- `TreePane::trySelectPath` (called on every navigation, from
  `refreshUiForActivePane`) does an O(1) lookup into `pathIndex_`
  (lower-cased path -> `HTREEITEM`, maintained in `addNode`/
  `TVN_DELETEITEMW`) instead of walking the whole tree with
  `TreeView_GetChild`/`GetNextSibling` recursion every time - only
  matters once you have a lot of expanded nodes, but the walk was pure
  overhead on every single navigation regardless. Still only ever finds
  already-expanded/loaded nodes (the "best-effort, never force
  enumeration" contract is unchanged) - a path just isn't in the index if
  its node was never inserted. `DirectoryModel::run`'s cancellation check
  (`stopToken.stop_requested()`) is now every loop iteration instead of
  batched every 256 entries - `requestEnumeration()` joins the previous
  worker synchronously on the UI thread, and on a slow (e.g. network)
  share each `FindNextFileW` call can itself be slow enough that the old
  batching made that join noticeably laggy. `FilePane::drawTabItem`'s
  brushes for its handful of fixed custom colors (`kActiveTabAccent`, the
  `DriveBadge` palette) are now cached (`cachedBrushFor`, a small
  process-lifetime `COLORREF -> HBRUSH` map) instead of
  `CreateSolidBrush`/`DeleteObject` per paint; the system-color ones
  (tab background, close-hover) switched to `GetSysColorBrush` (owned by
  the system, no delete needed, and it stays correct if the user changes
  their color scheme, unlike a one-time cache would).
