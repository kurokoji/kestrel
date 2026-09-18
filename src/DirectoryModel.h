#pragma once

#include "Types.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

// Result of a background directory enumeration, posted to the UI thread as
// a raw heap pointer via WM_APP_DIR_RESULT (lParam). Receiver owns it.
struct EnumerationResult {
    uint64_t requestId = 0;
    std::wstring path;
    std::vector<FileEntry> entries;
    bool success = false;
    DWORD errorCode = 0;
};

// Enumerates a single directory on a background jthread and posts the
// result back to a target window. One DirectoryModel is owned by each
// FilePane. Starting a new enumeration cancels and joins any enumeration
// already in flight (std::jthread does this automatically on reassignment).
class DirectoryModel {
public:
    DirectoryModel() = default;
    ~DirectoryModel();

    DirectoryModel(const DirectoryModel&) = delete;
    DirectoryModel& operator=(const DirectoryModel&) = delete;

    // Starts enumerating `path` in the background. `notifyWnd` receives
    // WM_APP_DIR_RESULT once done (or once cancelled work bails out
    // silently). Returns the request id assigned to this enumeration so
    // the caller can ignore stale results.
    uint64_t requestEnumeration(std::wstring path, HWND notifyWnd, WPARAM token);

    void cancel();

private:
    static void run(std::stop_token stopToken, std::wstring path, HWND notifyWnd, WPARAM token, uint64_t requestId);

    std::jthread worker_;
    std::atomic<uint64_t> nextRequestId_{1};
};
