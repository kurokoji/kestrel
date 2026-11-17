#pragma once

#include "LatestWorker.h"

#include <windows.h>
#include <string>
#include <thread>

// Watches a single directory (non-recursively) for changes via
// ReadDirectoryChangesW and posts WM_APP_DIR_CHANGED (see Messages.h) to
// `notifyWnd` whenever something changes, so the owner can refresh its
// view. The worker cancels the old watch before starting the latest one.
//
// Deliberately dumb: it doesn't say *what* changed, just that something
// did - the caller just re-enumerates. Debouncing rapid bursts of changes
// (e.g. a big copy in progress) is the caller's job (MainWindow does it
// with a short timer), not this class's.
class DirectoryWatcher {
public:
    ~DirectoryWatcher();

    void watch(std::wstring path, HWND notifyWnd, WPARAM token);
    void stop();

private:
    static void run(std::stop_token stopToken, std::wstring path, HWND notifyWnd, WPARAM token);

    LatestWorker worker_;
};
