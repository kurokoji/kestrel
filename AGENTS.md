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
- **Never send a pointer-bearing message (`LVM_GETITEMRECT`,
  `TCM_GETITEMRECT`, `SB_GETTEXT`, etc.) to another process's window from
  a PowerShell-side buffer.** The target process dereferences a pointer
  that's only valid in *your* process's address space and crashes. This
  happened twice during development. If you need that data, there's
  usually no safe cross-process way to get it short of `ReadProcessMemory`
  - just don't.
- If a real, previously-saved session gets restored during testing (this
  app persists open folders across runs - see Session.h), you may end up
  looking at the user's actual files/directory listings, not test data.
  Prefer navigating to an isolated temp folder you created yourself before
  screenshotting, and delete any screenshot immediately after checking it
  rather than describing its contents.

## Design decisions worth preserving

- Deliberately no `LVS_SHOWSELALWAYS` on the file-list controls: the
  focus-sensitive blue/gray selection color is *the* indicator of which
  pane is active, on top of the custom-painted frame. Don't add it back
  without also reconsidering the frame.
- Search (`Ctrl+F`) highlights matches in place rather than filtering the
  list - explicitly requested; don't change this to a hide/filter model.
- File operations (copy/move/delete/rename) are delegated to
  `IFileOperation`/`ShellExecuteExW` on purpose, so Explorer's own
  progress UI, conflict resolution, and Recycle Bin behavior "just work"
  without us reimplementing any of it.
