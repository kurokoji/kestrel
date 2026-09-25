# Agent notes for Kestrel

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

- Commit each completed unit of work after verification; the user wants
  commits as work is completed, not accumulated for a later request.
- After every change, also run `build.bat` so `build\kestrel.exe` (the
  exe the user actually launches) is current - the user asked for this
  after testing a stale exe and finding a committed feature "missing".
  Verifying in a separate build directory doesn't update it. If the
  user's instance is running, the link fails with `LNK1104`: tell them
  to close it rather than killing it.

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
  `cl`/`cmake` are on PATH without it. It resolves the repo path from
  `%~dp0` (its own location), not a hardcoded path - keep it that way so
  it still works after a clone to a different user/path. The VS
  Community 18 install path is still hardcoded (no portable way to probe
  that without `vswhere`/an env var, and it wasn't worth the complexity
  for a single-dev-machine build script).
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

- Resize layout uses `placeWithoutRedraw` (`SetWindowPos` with
  `SWP_NOREDRAW | SWP_NOCOPYBITS`) and one final
  `RedrawWindow(RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW)`.
  MainWindow uses WS_CLIPCHILDREN and native children use WS_CLIPSIBLINGS
  to prevent painting over adjacent controls. Keep ERASE/FRAME to clear
  old frame pixels and borders. Lists/tree retain native double buffering.
  Do not restore whole-window WS_EX_COMPOSITED: the user reported headers
  and list contents at different positions after committing a splitter
  move. Removed that style and disabled old-pixel copying during placement;
  the user verified the resulting fix. The drag guide keeps its separate
  layered popup; resizing still happens only on release.

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
- **The accelerator table's Ctrl+C/X/V (file copy/cut/paste) is
  translated in App's message loop before any focused `EDIT` sees the
  key**, so selected text in the rename label edit/address bar/search box
  couldn't be copied. App's loop now skips `TranslateAcceleratorW` when
  the focus window's class is `Edit` and `EditKeys::editOwnsKey` claims
  the chord. Add any new accelerator that collides with standard edit
  keys there too.
- **`ListView_EnsureVisible` is a no-op if the target row is already
  inside the current visible range** - it doesn't scroll to make a row
  the *top* row, only to make it *visible somewhere*. Restoring a tab's
  scroll position (`FilePane::loadTabIntoLive`) by calling it once with
  the saved top index silently did nothing whenever that index was small
  enough to already be on-screen after the fresh `LVSICF_NOSCROLL` reset
  (which is the common case). Fix: call `EnsureVisible` on the *last*
  item first (forces a scroll to the bottom, guaranteeing the real target
  is now off-screen), then call it again on the actual target - the
  second call is then forced to actually scroll, landing the target at
  the top. Caught by directly querying `LVM_GETTOPINDEX` after a
  tab-switch round-trip, not by eyeballing a screenshot.
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
- **Update: `loadTabIntoLive` now always calls `navigate()` for the tab
  it shows (cached entries are displayed first, then replaced), which
  re-arms `watcher_` and refreshes a stale cache on every tab switch.**
  This became necessary once dropping onto a tab (hover-to-switch) made
  "files moved while their tab was in the background" routine. The
  history below explains why the cache could go stale.
- **`FilePane`'s `watcher_` is one `DirectoryWatcher` instance per pane,
  not per tab, and it's only re-armed inside `handleDirResult` (i.e. on
  an actual `navigate()`).** `switchToTab`/`loadTabIntoLive` restore a
  cached tab's `entries` straight from `tabs_` without calling
  `navigate()` when that cache is non-empty, so switching tabs does
  *not* re-arm the watcher onto the newly-live tab's path - it keeps
  watching whatever path it last actually navigated to, which can now be
  a completely different (background) tab's folder. A cut-then-move
  paste especially exposed this: the *source* folder's watch may not be
  live at the moment the move actually happens (it's watching whichever
  tab was live last), so nothing ever notices the moved-away item is
  gone, and simply re-arming the watcher on tab switch wouldn't have
  fixed it either - by the time you switch back to the source tab the
  removal already happened unobserved, and `loadTabIntoLive` still just
  restores the stale cached `entries` without re-enumerating. The actual
  fix has to be pinned to the move itself, not tab-switch timing:
  `MainWindow::doClipboardPaste` calls the new
  `FilePane::invalidateTabsMatchingPath` (both panes, for each moved
  item's parent dir) right after a successful move, which clears that
  path's cached `entries` in any matching tab (forcing a fresh
  `navigate()` next time it's switched to) and immediately `refresh()`es
  it if it's a pane's *live* tab. `MainWindow::doMoveToOther` doesn't
  need this - it already `refresh()`es both panes' live tabs directly -
  but still leaves the same background-tab-cache staleness for any
  *other*, non-live tab sitting on the source folder; not fixed, since
  nothing currently exercises it.
- **Dimming a ListView icon (cut-file fade) via `ImageList_DrawEx`'s
  `ILD_BLEND50`/`ILD_BLEND25` visibly does nothing on modern shell
  icons.** Those blend flags mix the icon against a solid color, but
  today's shell icons carry their own real alpha channel and mostly
  ignore that blend - drawing it once, twice, whatever, looks identical.
  Also, painting *any* translucent version on top from
  `CDDS_ITEMPOSTPAINT` is a no-op if you skip clearing first: comctl32
  already drew the icon at full opacity during its own default draw
  (POSTPAINT fires *after* that), so a translucent copy layered on top
  of the opaque one still reads as opaque. The fix needs both pieces:
  `FillRect` the icon's `LVIR_ICON` rect back to the row's real
  background color (selected/search-highlight/plain - has to match
  whatever `CDDS_ITEMPREPAINT` decided that row's background is, or the
  erase itself looks wrong) to actually erase the opaque icon, *then*
  draw the dimmed one into the now-empty space via GDI+
  (`Gdiplus::Bitmap` from `ImageList_GetIcon`, a `ColorMatrix` scaling
  just the alpha row, `Graphics::DrawImage`) - GDI+ correctly respects
  the icon's real alpha this way, unlike `ImageList_DrawEx`. See
  `FilePane::handleNotify`'s `NM_CUSTOMDRAW`/`CDDS_ITEMPOSTPAINT` case.
- **SysTabControl32 (`TCS_MULTILINE`) always renders whichever row holds
  the *selected* tab as the bottom-most row, and re-sorts row order on
  every `TabCtrl_SetCurSel` - including the one it does internally on a
  plain click.** This made drag-to-reorder-tabs fundamentally unworkable
  while still using the real control: a drag that crossed rows looked
  fine while nothing was touching selection, but the instant selection
  was touched again (drag end, or even just the next ordinary tab
  click) the rows visibly snapped back into "selected tab's row last"
  order - undoing the reorder or scrambling unrelated tabs' rows. Tried
  and failed: skipping `SetCurSel` mid-drag (row order still exploded
  the moment a later click set it for real), and forcing "no tab
  selected" (`TabCtrl_SetCurSel(hwnd, -1)`) for the drag's duration
  (same problem, deferred one step). There's no documented way to
  disable this reflow. The fix was to stop using SysTabControl32
  entirely - `FilePane`'s tab strip (`tabHwnd_`) is now a plain
  `WS_CHILD` window with a from-scratch `WNDCLASSW` (`lpfnWndProc =
  DefWindowProcW`), and `FilePaneTabs.cpp` owns everything the real
  control used to give for free: fixed-width left-to-right row-wrapping
  layout (`computeTabLayout`/`relayoutTabs`, cached in `tabRects_`),
  hit-testing (`hitTestTab`/`hitTestTabApprox`), painting
  (`drawTabItem`, called from `WM_PAINT` instead of `WM_DRAWITEM`), and
  selection (`activeTab_` alone - no separate "control's own selected
  item" exists any more to fight with). `moveTab` now just
  erases/inserts in `tabs_` and calls `relayoutTabs()`; there's nothing
  resembling "selected row" left to reshuffle.
- **A plain `WS_CHILD` window made from a from-scratch `WNDCLASSW`
  doesn't track `WM_SETFONT`/`WM_GETFONT` on its own** - unlike a real
  control (button, edit, the old SysTabControl32), `DefWindowProcW`
  just drops `WM_SETFONT` on the floor and answers `WM_GETFONT` with
  whatever it always would (nothing useful). Sending `WM_SETFONT` to
  `FilePane::tabHwnd_` (the tab strip above) silently did nothing to
  what it later painted with, until `TabStripSubclassProc` grew its own
  `WM_SETFONT`/`WM_GETFONT` handlers storing/returning `tabFont_` -
  `drawTabItem` reads `tabFont_` directly rather than round-tripping
  through `WM_GETFONT`. Also has to be sent *after*
  `SetWindowSubclass`, not before - sent any earlier it only reaches the
  raw `DefWindowProcW`, which still drops it, since the subclass isn't
  installed yet to catch it.

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
  `TCM_GETITEMRECT`, `SB_GETTEXT`, `LVM_SETITEMSTATE`, etc.) to another
  process's window from a PowerShell-side buffer.** The target process
  dereferences a pointer that's only valid in *your* process's address
  space and crashes. This happened three times during development
  (`LVM_SETITEMSTATE` with a `Marshal.AllocHGlobal`'d `LVITEM`, while
  trying to select+activate a list row to test drive navigation, crashed
  the whole app outright). Don't assume a message is handle-only just
  because it *looks* like the flag-based ones (`TVM_EXPAND`,
  `TVM_SELECTITEM`) that are actually safe - check whether its lParam is
  documented as a pointer to a struct before sending it cross-process.
  If you need that data, there's usually no safe cross-process way short
  of `ReadProcessMemory`
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
- Keyboard input via `keybd_event` only reaches the app if it's really
  foreground, and plain `SetForegroundWindow` from the test process is
  usually refused. Tapping Alt (`keybd_event(VK_MENU)`) right before
  `SetForegroundWindow` got it granted; check `GetForegroundWindow()`
  before sending keys - otherwise they land in whatever the user has
  focused. `GetKeyState`-dependent paths (Shift+Delete, Shift+right-click)
  need this; `PostMessage`d key messages don't update key state.
- A tracked popup menu (`#32768`) can be read cross-process: `MN_GETHMENU`
  returns its `HMENU`, and `GetMenuItemCount`/`GetMenuString` work on it.
  `WM_CANCELMODE` to the main window did *not* close it; posting
  `WM_KEYDOWN VK_ESCAPE` to the `#32768` window did. Don't screen-capture
  to see a menu: `CopyFromScreen` grabs the user's whole desktop.
- `.ps1` files written without a BOM are read as ANSI by Windows
  PowerShell 5, so Japanese literals in them (window titles to match)
  silently turn into mojibake - enumerate windows and filter by class
  instead of matching a Japanese title.
- The user may be running their own `build\kestrel.exe` while you work
  (it locks the exe: `LNK1104`). Don't close or kill it - build into a
  separate directory instead (`cmake -B <scratch>\kbuild`, same
  sources), start that exe with `Start-Process -PassThru`, and find *its*
  main window by process id rather than `FindWindow` (which can return
  the user's window). End it with `Stop-Process -Id`: a killed instance
  never writes session.ini, so the user's saved session stays untouched.
- Don't test "entering a file path opens it" style features with a real
  file: it launches the user's associated app (a Notepad window stayed
  open this way). Check the branch taken some other way.
- Heredocs and inline strings passed through the Bash tool can lose a
  level of backslash escaping (`L"C:\\x"` arrived as `L"C:\x"`, a hex
  escape). Write C++ sources/tests containing backslashes with the
  Write/Edit tools, or put edit scripts in a file first.
- Stop the app with `WM_CLOSE`, not `Stop-Process`, when the session file
  matters: a killed process doesn't save, so the next launch restores
  whatever was saved before (possibly the user's real folders, not your
  test folder). Back up `%APPDATA%\Kestrel\session.ini` before testing
  and copy it back afterward.
- If a real, previously-saved session gets restored during testing (this
  app persists open folders across runs - see Session.h), you may end up
  looking at the user's actual files/directory listings, not test data.
  Prefer navigating to an isolated temp folder you created yourself before
  screenshotting, and delete any screenshot immediately after checking it
  rather than describing its contents.

## Design decisions worth preserving

- Splitter drags preview only a dotted, owned layered popup overlay
  above the panes; do not call layoutChildren or modify saved widths on
  WM_MOUSEMOVE. The user explicitly chose deferred resize to avoid paint
  churn. WindowLayout::previewSplitter reuses layout limits for the guide;
  WM_LBUTTONUP commits once. Capture loss/cancel mode/external relayout
  hides the guide without applying the pending position. Clear drag state
  before ReleaseCapture (which synchronously triggers WM_CAPTURECHANGED).
  A normal moving child guide still invalidated the underlying controls and
  the user reported stutter. Use WS_EX_LAYERED + a white color key to reuse
  the guide bitmap on moves, independently of the parent's child controls.
  WS_EX_NOACTIVATE/TRANSPARENT keep focus and input in the main window;
  popup positions must use ClientToScreen. Skip unchanged guide positions.

- Live and stored tab data share `FilePane::TabContent` (path, entries,
  stats, history, sort). Save/restore it as a whole rather than copying
  members separately, so new fields cannot be omitted on tab switches.
  `TabState` separately stores selection/focus/scroll snapshots: the live
  values come from the ListView and must still be captured/restored there.

- `WindowLayout::calculate` owns window-independent geometry and resize
  scaling; `MainWindow::layoutChildren` measures native controls and
  applies the result. Preserve the existing minimum-size/clamping rules
  and `erase=TRUE` repaint; geometry regressions belong in LayoutTests.
- Context menus and outbound dragging share `ShellSelection::get<T>`.
  Keep its same-parent-folder contract and skip-unparseable-child behavior.
  PIDLs are owned with `unique_ptr` + `CoTaskMemFree`; the deleter's
  `pointer` alias must remain `PIDLIST_RELATIVE` to preserve the SDK's
  `__unaligned` qualifier (plain `ITEMIDLIST*` caused MSVC C4090 warnings).

- `FilePane`'s tab state transitions, owner-drawing, and mouse subclass
  live together in `FilePaneTabs.cpp`; control creation and pane layout
  remain in `FilePane.cpp`. Keep close-button geometry helpers beside
  both drawing and hit-testing so their rectangles stay in sync.
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
- `FilePane::TabState` carries `selectedIndices`/`focusedIndex`/
  `topIndex` (captured in `syncActiveTabIntoStorage`, reapplied in
  `loadTabIntoLive`) so switching tabs and back doesn't look like the
  selection got cleared and the view jumped to the top - reported as a
  usability bug, since `LVS_OWNERDATA` has no memory of its own for
  per-tab selection/scroll (each tab reuses the same physical ListView).
  Indices are reapplied against the cached `entries` they were taken
  from; the re-enumeration `loadTabIntoLive` then starts carries the
  selection over *by name* (`handleDirResult`'s same-path branch,
  `FileEntrySort::indicesOfNames`), since files may have been added or
  removed meanwhile and row numbers would then point at other files. Restoring selection via `LVM_SETITEMSTATE` fires `LVN_ITEMCHANGED`
  the same as a real click would, which would double-count on top of the
  `live_ = t.content` restore already done - so `recomputeSelectionStats()`
  is called afterward to get the authoritative count from the control's
  actual state rather than trust the incremental tally through that bulk
  restore. See the `ListView_EnsureVisible` gotcha above for the scroll
  half of this.
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
  via `ShellSelection::get<T>` in `ShellSelection.cpp`. This gets CF_HDROP and
  every other format Explorer would offer "for free" instead of us having
  to build a custom `IDataObject`. **`DoDragDrop` requires `OleInitialize`
  (not just `CoInitializeEx`) on the calling thread** - App.cpp's message
  loop thread was switched from one to the other for this; don't revert
  it back to plain `CoInitializeEx` or dragging silently stops working.
- Inbound drag-and-drop (`FileDropTarget`, registered on both file lists
  and the tree) never performs file operations itself: each hover is
  resolved (`DropTargetPath::candidates` - folder row, else file row for
  exe/zip handlers, else the pane's folder) to a shell item whose own
  `IDropTarget` (`BHID_SFUIObject`) gets DragEnter/Over/Drop forwarded.
  That's what gives Explorer's move-vs-copy-by-drive, modifier keys,
  right-drag menu, zip/exe/Recycle Bin handling for free - don't replace
  it with a CF_HDROP parse + `FileOperations::moveItems`. `Drop` must not
  forward another `DragOver` first: by then the buttons are up and the
  shell forgets it was a right-drag (no drop menu). Our own drags set
  `FileDropTarget::InternalDragScope` so dropping onto a dragged item or
  back into its own folder is refused (`DropTargetPath::isOntoSource`;
  Ctrl allows the same-folder copy). Outbound drags use `SHDoDragDrop`
  from the ListView HWND (not `DoDragDrop` with a hand-rolled
  `IDropSource`) for the shell drag image and right-drag support. Drop
  targets are revoked in `MainWindow`'s `WM_DESTROY` since children are
  destroyed after it. Verified live with a real `mouse_event` drag in a
  temp folder: pane-to-pane onto a folder row, onto empty list space,
  same-pane onto a folder row, and drop-back-onto-self (refused).
- Empty-area right-click shows the folder's real background menu
  (`ShellContextMenu::showBackground`, `BHID_SFViewObject` =
  `IShellFolder::CreateViewObject`). It has no Paste/Refresh (Explorer's
  view adds those, not the folder), so ours are prepended - *after*
  `QueryContextMenu`, not before: added first, our separator vanished
  (the shell tidies separators around its own items). Some handlers
  (ATOK) still insert at position 0 regardless; not ours to fix. Shift
  held adds `CMF_EXTENDEDVERBS`/`CMIC_MASK_SHIFT_DOWN`, for item menus too.
- Undo (`Undo.h`, `FileOperations::undo`, Ctrl+Z) only covers operations
  we ran through `FileOperations` with a record: an `IFileOperation
  ProgressSink` (`ResultRecorder`) keeps what each *requested* item
  became (Post*Item's `psiNewlyCreated`; nested files of a copied folder
  are reported too and filtered out). Drag-and-drop and shell menu
  commands run inside the shell and are deliberately not recorded - no
  outcome comes back to us. For a recycle, `psiNewlyCreated` is the raw
  `C:\$Recycle.Bin\<SID>\$R...` file, *not* a Recycle Bin namespace item:
  binding a context menu to it gives an ordinary file menu without
  元に戻す. `RecycleBinOps::restoreItems` instead enumerates the bin and
  matches entries by `SHGDN_FORPARSING` (which is that `$R` path), then
  invokes their "undelete" verb - confirmed live as the verb behind 元に戻す.
  Undoing a copy/new folder recycles rather than permanently deletes.
  Verified live in a temp folder: rename, new folder, delete->restore,
  F5 copy, F6 move, each undone.
- The shell leaves 名前の変更 out of an item context menu that has no
  Explorer view (`IShellView`) behind it - it isn't just hidden, invoking
  a "rename" verb would do nothing either. `ShellContextMenu::show` takes
  own items for the top and for just above the プロパティ group (found by
  its `"properties"` verb); the file list adds 名前の変更 there (starting
  our own label edit) and 新しいタブで開く on top for a single folder.
  Tree nodes bind by parsing name (`menuForItem`, `BHID_SFUIObject`) so
  drive roots and `::{CLSID}` nodes get menus too; a node whose folder is
  gone after a command is removed (`TreePane::removeIfGone`), since the
  tree has no watcher. Tree nodes are also drag sources now (not drive
  roots/virtual nodes, `DropTargetPath::isDraggableFolder`) - not
  verified live, since every reachable node is a real user folder.
- **A ListView keeps selected/focused row *indices* across
  `ListView_SetItemCountEx`**, even in `LVS_OWNERDATA`. Navigating to a
  different folder therefore opened with the old folder's row numbers
  still selected, and switching tabs merged the outgoing tab's selection
  into the restored one; the status bar hid it by zeroing its count in
  `applyEntries`. `handleDirResult` now clears selection only when the
  path changes (a same-folder watcher refresh keeps it),
  `loadTabIntoLive` clears before restoring, and `applyEntries`
  recounts from the control instead of zeroing.
- Type-to-select needs `LVN_ODFINDITEMW` (owner-data lists can't search
  themselves) - `FileEntrySort::findByPrefix`. The F2 extension-less
  preselect is a *posted* `EM_SETSEL` from `LVN_BEGINLABELEDITW`: the
  control selects all after that notification returns. F7 creates
  `NameParts::uniqueName` right away and `beginPendingRename` starts the
  label edit once `handleDirResult` lists it (enumeration is async).
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
  folder name via `NameParts::tabLabel`) - for the active tab that's the live
  `live_.path`, for any other tab it's `tabs_[idx].content.path` (only synced
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
- **`TreePane::pathIndex_` can legitimately have two different
  `HTREEITEM`s wanting the same path key** - e.g. the root "ダウンロード"
  shortcut node and, once "ユーザープロファイル" is expanded, its real
  Downloads subfolder child, both real paths to the same folder.
  `addNode` used to just overwrite the map entry (`pathIndex_[key] =
  item`), so whichever got inserted *last* won - meaning expanding
  "ユーザープロファイル" silently rebound every shortcut path it happens
  to share with a root node onto its own descendant. Reported as: click
  the root "ダウンロード"/"ドキュメント" node after having expanded
  "ユーザープロファイル", and the *descendant* node under "ユーザープロ
  ファイル" lights up instead. Fixed by switching to
  `pathIndex_.try_emplace(key, item)` - keeps whichever entry got there
  first rather than the most recent, and since `addRootItems` always
  inserts every root shortcut before any `populateChildren` call can run,
  "first" is always the shortcut. Verified live: expanded "ユーザープロ
  ファイル" via `TVM_EXPAND`, selected+navigated the root "ダウンロード"
  node via `TVM_SELECTITEM`/Enter, then confirmed `TVM_GETNEXTITEM`/
  `TVGN_CARET` returned that same root node's handle, not the descendant's.
- Clicking the tree's "PC" node navigates the active pane to a synthetic
  drive listing (C:\, D:\, ...), matching what other file managers do.
  It's keyed off `kThisPcPath` (`Types.h`) - the shell's own
  `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}` CLSID string for the
  virtual "This PC" namespace root, reused here purely as a sentinel
  value (not resolved via the shell in any way) so it reads as
  intentional rather than garbage if it ever shows up somewhere visible
  like the address bar - which it does, exactly like Explorer's own
  address bar would show it. `DirectoryModel::run` special-cases this
  path to enumerate drives via `GetLogicalDrives()` instead of
  `FindFirstFileExW`. `FilePane`'s `joinPath()` special-cases it too:
  entries under it are already full drive roots ("C:\"), not names
  relative to a real containing folder, so the normal
  dir-plus-backslash-plus-name concatenation would double them up
  wrong. The "PC" tree node itself gets `childrenLoaded = true` set
  immediately in `addRootItems` (its drive children are inserted
  synchronously right there, not lazily via `populateChildren`) so a
  later collapse+re-expand doesn't try to enumerate `kThisPcPath` as a
  *tree* node too and duplicate them. Each drive's `FileEntry` also gets
  `GetDiskFreeSpaceExW`'s free/total bytes - `size` is set to the total
  (so sorting the size column by a drive means sorting by capacity, not
  free space) and `formattedSize` is "free 空き / total". A drive that
  isn't ready (e.g. an empty optical drive) just fails that call and
  shows a blank size, same as any other entry the model couldn't get
  info for - not specially handled as an error.
- `IconCache::iconForPath` resolves the shell's real per-path icon
  (`SHGetFileInfoW` on the actual path, no `SHGFI_USEFILEATTRIBUTES`)
  rather than the one generic folder icon `iconForDirectory`/
  `iconForFile` give every folder/extension - this is what makes special
  folders (Downloads, Desktop, custom desktop.ini icons) and per-drive
  icons (optical/removable) show their real shell icon instead of a
  plain folder glyph. `kThisPcPath` is special-cased since it isn't a
  real filesystem path - `SHGetFileInfoW` needs a PIDL for it
  (`SHGetKnownFolderIDList(FOLDERID_ComputerFolder, ...)` +
  `SHGFI_PIDL`), not a path string. Used by both `TreePane::addNode`
  (every root/child node already carries a real path) and `FilePane`'s
  `LVN_GETDISPINFOW` (built via the existing `joinPath` - which already
  returns full drive roots as-is for the This-PC view, so drives get
  their real per-drive icon too, not just tree "PC" children). Cached by
  lowercased path, same unbounded-for-the-session pattern as
  `extensionIcons_`. Deliberately not merged into `iconForDirectory`:
  that one stays a single fast cached lookup for the plain "no real path
  available yet" case.
- The default sort column/direction setting (Tools > Options > 並び順
  (既定)) is deliberately scoped to *new* tabs only - `FilePane::
  setDefaultSort` is a static (both panes share one process-wide default)
  consulted only at the three points a `TabState`/its `TabContent` is
  freshly constructed (`create()`'s initial tab, `newTab()`,
  `restoreTabs()`'s per-path loop). It does not re-sort already-open
  tabs, matching how each tab's own sort state already behaves as
  independent/sticky (clicking a column header only ever affects that
  one tab, per the existing "タブ" feature note above). `MainWindow`
  mirrors the static as `sortColumn_`/`sortAscending_` purely to drive
  the settings submenu's `CheckMenuRadioItem`/`CheckMenuItem` marks and
  to persist it (`SessionData::defaultSortColumn/defaultSortAscending`,
  the `"O"` session record) - `applyDefaultSort()` is the single place
  that updates all three (the static, the menu, and what gets saved) so
  they can't drift apart. Column index convention (0=Name, 1=Type,
  2=Size, 3=Modified) matches the existing `LVN_COLUMNCLICK`/
  `FileEntrySort::sort` convention already used for the interactive
  per-tab sort, not a separate numbering.
- The tree's "ゴミ箱" node (`kRecycleBinPath`, same `::{CLSID}` sentinel
  convention as `kThisPcPath` - see Types.h) lists the real Recycle Bin's
  contents, not a fake/static view. `DirectoryModel::run` special-cases
  it like it does `kThisPcPath`, but the enumeration itself is a
  different mechanism entirely: `$Recycle.Bin`'s on-disk names/layout
  (per-SID folders, `$Rxxxx` mangled names) aren't the shell-visible
  ones, so it has to go through `IShellFolder2` (bound via
  `SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, ...)` +
  `IShellFolder::BindToObject`, mirroring `ShellSelection.cpp`'s
  PIDL-binding pattern) rather than `FindFirstFileExW`. This needs COM on
  the enumeration thread - `CoInitializeEx`/`CoUninitialize` are scoped to
  just this branch (see `PreviewPane::loadWorker` for the same
  per-worker-thread pattern), since the plain-directory path below it
  doesn't need COM at all.
  `IShellFolder2::GetDisplayNameOf` needs `SHGDN_INFOLDER`, not
  `SHGDN_NORMAL` - `SHGDN_NORMAL` returns the item's fully-qualified
  *original* location string (e.g. `C:\Users\...\Downloads\foo.zip`)
  instead of the bare filename Explorer's own Name column shows. Caught
  by a live screenshot after wiring this up with `SHGDN_NORMAL` first -
  the list showed full original paths in the Name column instead of
  filenames.
  `GetDetailsEx`'s `VARIANT` out-param has no `FILETIME` member (that's
  only on `PROPVARIANT`, a different type) - `PKEY_DateModified` comes
  back as `VT_DATE` (a `double`, days-since-1899), so it has to be run
  through `VariantTimeToSystemTime` + `SystemTimeToFileTime` to get the
  `FILETIME` the rest of the app's formatting/sorting code expects.
  `VariantTimeToSystemTime`/`VariantClear` need `oleaut32` linked (added
  to `CMakeLists.txt` - `ole32` alone doesn't export them).
  Entries here are display-only synthesized `FileEntry`s, not real
  filesystem paths, so opening, renaming, cutting/copying, or dragging
  them would either silently fail or (worse) construct a bogus
  concatenated path that happens not to crash but does nothing sane.
  Rather than guard every operation's entry point separately,
  `FilePane::selectedPaths()` - the single choke point every
  cut/copy/drag/normal-context-menu action reads its target list from -
  returns empty for this view, which disables all of them in one place;
  `activateEntry()` (open/navigate) and `doRename()` (F2) are guarded
  directly since they don't go through `selectedPaths()`. Restore,
  permanent delete, and emptying the bin *are* supported (see the next
  entry) - "read-only" only ever meant "not a real path you can
  copy/move/rename", not "no file operations at all".
- Recycle Bin file operations (`RecycleBinOps.h/.cpp`) are split across
  two different mechanisms depending on how reliable the verb name is:
  - **Restore is right-click-menu only** (`MainWindow::onContextMenu`'s
    `kRecycleBinPath` branch -> `RecycleBinOps::get<IContextMenu>` ->
    `ShellContextMenu::showAndInvoke(HWND, ComPtr<IContextMenu>, POINT)`,
    a new overload that skips `ShellSelection`'s real-path binding and
    invokes whatever command id the user actually clicked in the real,
    shown menu). There's no verb-name lookup on this path at all - confirmed
    live that the recycle bin's real IContextMenu correctly shows "元に戻す"
    as its first item for a selected item. Not wired to a key in the
    Recycle Bin view; the verb itself (`"undelete"`) has since been
    confirmed live on this machine and is what Ctrl+Z's restore uses
    (`RecycleBinOps::restoreItems`).
  - **Permanent delete and empty-the-bin *are* invoked directly**
    (`FilePane::doDelete()`'s `kRecycleBinPath` branch, and the
    `emptyRecycleBinButton_`/`FilePane::emptyRecycleBin()` button below
    the tab strip - shown/hidden by `setBounds()` and
    `onEmptyButtonVisibilityChanged`, the same shape as
    `onSearchVisibilityChanged`/`onTabCountChanged`). Delete uses
    `RecycleBinOps::deleteItemsPermanently()`'s verb-lookup
    (`GetCommandString(GCS_VERBW)` matched against `L"delete"`) - trusted
    without the same live-verification restore got, since `"delete"` is
    the universal, standard verb every shell item's context menu
    supports (unlike the recycle-bin-specific restore/undelete), and
    invoking it on an item *already inside* the Recycle Bin is exactly
    what Explorer's own "permanently delete" confirmation does. Empty
    uses the plain `SHEmptyRecycleBinW` API, no verb involved.
  - `RecycleBinOps::resolvePidls()` re-enumerates the Recycle Bin fresh
    and matches by `SHGDN_INFOLDER` display name every time an operation
    runs, rather than holding a PIDL from the original listing - adding a
    live-PIDL member to `FileEntry` would have made it move-only, which
    would have broken `FilePane::TabContent`'s copy-as-a-unit contract
    (see the "タブ" design-decision note) for every ordinary folder too,
    not just this view. Two distinct deleted items sharing a display name
    is an accepted, unhandled edge case.
  - Never live-invoke delete/empty from an agent session without the
    user's explicit go-ahead on that specific action - both are
    real, irreversible operations on whatever the user's actual Recycle
    Bin holds at the time (verified this whole feature by checking the
    "空にする" button's show/hide and the real right-click menu's item
    labels only, via `WM_CANCELMODE` to dismiss without picking anything -
    never by actually clicking delete/empty).
- Cross-pane tab drag (drag a tab from one pane's strip and drop it onto
  the other pane's) reuses the existing same-pane reorder drag
  (`TabStripSubclassProc`'s capture-based drag in FilePaneTabs.cpp) rather
  than a second mechanism - once `SetCapture` is held, every subsequent
  mouse message still targets the dragging pane's `tabHwnd_` regardless of
  screen position, so crossing into the other pane's strip is detected by
  converting the move point to screen coordinates and testing it against
  `otherPane_->tabStripScreenRect()`, not by anything arriving at the
  other pane's own window. `FilePane::otherPane_` is a plain raw pointer
  MainWindow wires both directions right after `create()`ing both panes
  (`setOtherPane`) - null and unused in any context that only builds one
  pane (e.g. future tests). While the drag point is over the other pane's
  strip, reordering in the source pane is suspended (no `moveTab` calls)
  and `crossPaneDropIndex_` tracks the would-be drop index instead;
  `updateTabDragGhost`'s clamp rect was widened from "this pane's strip
  only" to `UnionRect` of both panes' `tabStripScreenRect()`s so the ghost
  can actually reach the other strip rather than stopping dead at the
  first pane's edge. The actual hand-off on drop
  (`FilePane::receiveTabFromOtherPane`) reuses `removeTab` - refactored
  out of the old inline body of `closeTab` so both "discard this tab" and
  "the other pane is taking this tab" share the same index-fixup/
  active-tab-reassignment logic, the only difference being what happens
  to the returned `TabState` (dropped vs. inserted into the other pane's
  `tabs_`). Still refuses to leave a pane with zero tabs, same as
  `closeTab` always has - dragging a pane's last tab onto the other pane
  is a no-op, not an accidental "this pane closes". The moved tab becomes
  the *destination* pane's active tab (matches "a tab you just dropped
  somewhere is now what you're looking at"); the *source* pane's own
  active-tab focus/selection is untouched beyond whatever index shift the
  removal caused.
  - **First version of this shipped with a real bug, caught by the user
    live** (not reproducible in this sandbox - see the synthetic-input
    gotchas above): dragging a non-active tab across to the other pane's
    strip visibly tracked the ghost the whole way, but dropping it did
    nothing - only dragging the already-active tab actually worked. Cause:
    `TabStripSubclassProc`'s `WM_LBUTTONDOWN` used to call `switchToTab(idx)`
    immediately on press, before any drag was even confirmed, so clicking
    a background tab synchronously kicked off `switchToTab` ->
    `loadTabIntoLive` -> `navigate()`'s re-enumeration right there. A fast
    click-drag-release could complete the whole gesture (press, past the
    drag threshold, release) before that call returned, which meant
    `dragActive_` might never even become true and the eventual
    `WM_LBUTTONUP` saw nothing to drop. Clicking the tab that was already
    active hit `switchToTab`'s `index == activeTab_` early-return - a
    no-op - so that path never had the delay and always looked fine,
    which is what made it read as "only the active tab moves". Fixed by
    deferring `switchToTab` to `WM_LBUTTONUP` and only calling it when
    `dragActive_` never went true (a plain click) - a real drag now
    reorders/hands off the tab without ever synchronously reselecting it
    mid-gesture. Same root cause exposed a second bug worth keeping fixed
    together: `removeTab` (both `closeTab` and this hand-off use it) took
    `tabs_[index]` straight from storage without syncing `live_` into it
    first, so removing/handing off the *active* tab could revert it to
    however it looked as of the last actual tab switch rather than its
    current path/scroll/selection - `removeTab` now calls
    `syncActiveTabIntoStorage()` first when `index == activeTab_`.
  - Still not verified with a real mouse drag in this sandbox for the
    reasons above; the fix was verified by re-reading the corrected
    control flow against the bug report, plus a clean build +
    `kestrel_tests` pass. Confirm live before trusting this note over a
    fresh bug report.
- Dragging a pane's *last* tab to the other pane, or plain closeTab()ing
  it (× glyph, middle-click, Ctrl+W - closeTab's old "always keep one tab"
  refusal is gone), can leave a pane with zero tabs, on purpose -
  explicitly requested over refusing either. `FilePane` doesn't need a
  tab open to stay on screen: a pane left with zero tabs just shows an
  empty listing (`FilePane::removeTab` resets `live_` to a blank
  `TabContent{}`, zeroes the ListView's item count, and recomputes the
  (now-zero) selection stats) rather than falling back to single-pane
  mode or auto-refilling a fresh tab - "+" or dragging another tab onto
  it are how it gets contents again. No MainWindow-side handling needed
  for this at all (tried an earlier version that detected the zero-tab
  pane from `onTabCountChanged` and forced `singlePaneMode_` on -
  scrapped per explicit feedback that a pane need not always hold a tab).
  The close (×) glyph and its hover highlight used to be skipped entirely
  when `tabs_.size() <= 1` (`drawTabItem`'s `if (tabs_.size() > 1)`) -
  that guard is gone too, so a single remaining tab still shows and
  responds to its own close button. One real trap here: `removeTab` computes
  `loadTabIntoLive`'s target index as `tabs_.size() - 1` when the removed
  tab was active - with `tabs_` now empty that's `-1`, so the empty-listing
  reset path is a genuinely separate branch, not something that could be
  folded into `loadTabIntoLive` by relaxing its bounds check (it still
  assumes a valid index and always will - every other caller has one).

- **i18n (English/Japanese)**: `StringId` enum (`Strings.h`) + two
  `constexpr std::array<const wchar_t*, N>` tables (`Strings.cpp`, one per
  language) indexed positionally by the enum, not a hash/map - StringId is
  already a dense integer key, so array indexing is both simpler and
  faster than any keyed lookup would be. Entries are `const wchar_t*` into
  string-literal storage, not `std::wstring`, specifically so the tables
  stay `constexpr` (no heap allocation, no per-call construction) even
  though `tr()` is called from hot paths (ListView owner-draw, menu
  rebuilds). The two tables are positional arrays rather than
  designated/keyed initializers, so a missing or extra entry in either one
  is a `static_assert(kEn.size() == kCount)` compile failure, not a
  silent runtime bug - this is deliberately relied on instead of a
  separate "tables in sync" unit test. `Language.h`'s
  `languageFromLangId(LANGID)` is the only actual detection logic and is
  pure/unit-tested; `currentLanguage()`/`setLanguage()` are simple global
  state `Strings.cpp`'s `tr()` reads. Tests that assert on translated
  text (`UndoTests`, `NamePartsTests`, `FormattingTests`) must call
  `setLanguage(Language::Ja)` explicitly first - the process-wide current
  language otherwise starts from a real `GetUserDefaultUILanguage()` call
  at static-init time, so leaving it implicit would make those tests
  depend on the machine's OS language.
- A format string that's itself translated (looked up via `tr()` at
  runtime) can't go through plain `std::format(tr(...), args...)` -
  `std::format` requires the format string as a compile-time constant
  expression (`std::format_string`), and a `const wchar_t*` returned from
  a function doesn't qualify even though the underlying literal is
  constexpr. Use `std::vformat(tr(...), std::make_wformat_args(args...))`
  instead everywhere a translated string has `{}` placeholders. Also:
  every argument passed to `make_wformat_args` must be an lvalue (a named
  local), not a temporary - it captures by reference into the
  `format_args` object, and MSVC's dangling-reference protection rejects
  an rvalue there (`Undo::describe(kind)`, `Formatting::formatSize(...)`
  and similar helper calls all had to be hoisted into a named
  `const std::wstring` first before being handed to
  `make_wformat_args`/`vformat`).
- `TB_SETBUTTONTEXTW` does not exist in this SDK's `CommCtrl.h` (looked
  plausible by analogy with `TB_SETBUTTONINFOW`/other `...W` message
  pairs, but grepping the actual header turned up nothing under that
  name in any `_WIN32_IE` gate). To change a toolbar button's text after
  it's already been added (needed for live language switching), use
  `TB_SETBUTTONINFOW` with a `TBBUTTONINFOW{ dwMask = TBIF_TEXT, pszText
  = ... }`, keyed by the button's command id - not by index, so it's
  unaffected by button order.
- Retranslating live (Tools > Options > 言語, no restart required) means
  every place a translatable string was baked in at one-time
  construction needs its own explicit re-apply path, since Win32 doesn't
  re-query text on its own: `MainWindow::retranslate()` destroys and
  fully rebuilds the whole menu bar (simplest correct option - a `HMENU`
  has no "change this item's text and mnemonic" operation that's less
  work than just rebuilding, and `createMenuBar()` already existed as a
  single source of truth for the menu's structure) via `createMenuBar()`
  itself detecting and `DestroyMenu`-ing the previous one, then
  re-applies the state that rebuild doesn't know about on its own
  (`applyDefaultSort` for the fresh `sortMenu_`'s radio marks,
  `updateUndoMenu` for the Edit menu's dynamic undo label); the toolbar's
  five button labels via `TB_SETBUTTONINFOW` (see above); each
  `FilePane`'s `retranslate()` (column headers via `ListView_SetColumn`,
  the two tab-button tooltips via `TTM_UPDATETIPTEXTW`, the Recycle-Bin
  empty button via `SetWindowTextW`, plus an `InvalidateRect` on the tab
  strip so `drawTabItem` recomputes tab labels - those are already
  translated live at paint time via `NameParts::tabLabel`, so a repaint
  alone is enough, no stored text to update); `TreePane::retranslate()`
  walks a small `translatedRoots_` list (`HTREEITEM` + `StringId` pairs
  recorded when `addRootItems()` created them) and `TreeView_SetItem`s
  each one - deliberately not "walk the whole tree", since every other
  node is a real folder name that must never be translated, only the
  fixed Desktop/Documents/.../Recycle Bin roots are. Forgetting a piece
  here doesn't show up as a build/test failure - it shows up as some
  corner of the UI silently staying in the old language after switching,
  which is how `updateFreeSpace()` got missed on the first pass (caught
  by an actual screenshot-verified language switch in the running app,
  not by the test suite) and had to be added to `retranslate()`
  separately from `updateStatusBar()`, since the free-space text is
  cached in `freeSpaceText_` rather than recomputed by
  `updateStatusBar()` itself.
- **`TBSTYLE_LIST` draws a toolbar button's `iString` as an inline caption
  even without `BTNS_SHOWTEXT` on that button** - confirmed live
  (screenshot of a real running instance, not synthetic): setting
  `TBBUTTON::iString` but leaving `fsStyle` as plain `BTNS_AUTOSIZE`
  (no `BTNS_SHOWTEXT`) still showed the text next to the icon. There's
  no per-button way to opt out of this once `TBSTYLE_LIST` is set on the
  toolbar (needed for the one remaining text button, `singlePane`, to
  lay out correctly). Fix: don't set `iString` at all for icon-only
  buttons (`MainWindow::createToolbar`'s `mk()`); supply their hover
  tooltip text on demand instead by handling `TBN_GETINFOTIPW` in
  `onNotify()` (`NMTBGETINFOTIPW::iItem` is the button's command id,
  not an index) and writing into `pszText`/`cchTextMax`. This also means
  `retranslate()` doesn't need to touch those buttons at all - the
  tooltip is computed fresh from `tr()` on every hover.
- Toolbar icons are rasterized at runtime from "Segoe MDL2 Assets" glyph
  codepoints (Back `U+E72B`, Forward `U+E72A`, Up `U+E74A`, Refresh
  `U+E72C` - `MainWindow.cpp`'s `kGlyph*` constants) rather than shipping
  bitmap resources, matching the icon-font approach `FilePane.cpp`
  already used for the duplicate-tab button. Plain GDI can't produce
  these as an `HIMAGELIST` entry with real transparency - text drawn by
  `TextOutW`/`DrawTextW` onto a 32bpp DIB never touches the alpha
  channel, so every pixel comes out `alpha=0` (invisible) once used as
  an icon. `Gdiplus::Graphics::DrawString` onto a
  `PixelFormat32bppARGB` `Gdiplus::Bitmap` does anti-alias correctly
  into alpha, so glyph rendering goes through GDI+
  (`MainWindow.cpp`'s `glyphToIcon`) purely for that reason, then
  `Bitmap::GetHICON` + `ImageList_AddIcon` hand the result to the
  ordinary (GDI) toolbar. Icon size is DPI-scaled
  (`MulDiv(16, GetDpiForWindow(hwnd_), 96)`) once at `createToolbar()`
  time - there's no live-DPI-change handling for these (matches the
  rest of the app, which doesn't retranslate/redraw the whole UI on a
  DPI change either).
- `PreviewPane`'s `detail_` label (e.g. "Folder") is translated only at
  the moment a folder is selected (`tr(StringId::PreviewFolder)`
  assigned once, not re-read at paint time) - switching language while
  that exact label is still showing leaves it stale until the next
  selection change. Deliberately left as a known, low-priority gap
  rather than adding another retranslate() hook for it; revisit if it's
  ever reported as user-visible.
