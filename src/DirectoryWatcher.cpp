#include "DirectoryWatcher.h"
#include "Messages.h"

DirectoryWatcher::~DirectoryWatcher() { stop(); }

void DirectoryWatcher::stop() {
    if (worker_.joinable()) worker_.request_stop();
}

void DirectoryWatcher::watch(std::wstring path, HWND notifyWnd, WPARAM token) {
    // Reassigning a jthread requests-stop + joins the previous one first.
    worker_ = std::jthread(run, std::move(path), notifyWnd, token);
}

void DirectoryWatcher::run(std::stop_token stopToken, std::wstring path, HWND notifyWnd, WPARAM token) {
    HANDLE dir = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return;

    HANDLE wakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!wakeEvent) {
        CloseHandle(dir);
        return;
    }

    // Lets request_stop() wake the blocked wait below immediately instead
    // of leaving this thread parked until the next real filesystem event.
    std::stop_callback wakeOnStop(stopToken, [wakeEvent] { SetEvent(wakeEvent); });

    BYTE buffer[8192];
    while (!stopToken.stop_requested()) {
        OVERLAPPED ov{};
        ov.hEvent = wakeEvent;
        ResetEvent(wakeEvent);

        DWORD bytesReturned = 0;
        const BOOL issued = ReadDirectoryChangesW(
            dir, buffer, sizeof(buffer), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
            &bytesReturned, &ov, nullptr);
        if (!issued && GetLastError() != ERROR_IO_PENDING) break;

        WaitForSingleObject(wakeEvent, INFINITE);

        if (stopToken.stop_requested()) {
            CancelIoEx(dir, &ov);
            break;
        }

        DWORD transferred = 0;
        if (GetOverlappedResult(dir, &ov, &transferred, FALSE)) {
            PostMessageW(notifyWnd, WM_APP_DIR_CHANGED, token, 0);
        }
    }

    CloseHandle(wakeEvent);
    CloseHandle(dir);
}
