#include "DirectoryWatcher.h"
#include "Messages.h"

DirectoryWatcher::~DirectoryWatcher() { stop(); }

void DirectoryWatcher::stop() {
    worker_.cancel();
}

void DirectoryWatcher::watch(std::wstring path, HWND notifyWnd, WPARAM token) {
    worker_.submit([path = std::move(path), notifyWnd, token](std::stop_token stop) mutable {
        run(stop, std::move(path), notifyWnd, token);
    });
}

void DirectoryWatcher::run(std::stop_token stopToken, std::wstring path, HWND notifyWnd, WPARAM token) {
    HANDLE dir = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return;

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent || !ioEvent) {
        if (stopEvent) CloseHandle(stopEvent);
        if (ioEvent) CloseHandle(ioEvent);
        CloseHandle(dir);
        return;
    }

    {
        // I/O can reset its completion event. Use a separate stop event,
        // and unregister the callback before closing that event's handle.
        std::stop_callback wakeOnStop(stopToken, [stopEvent] { SetEvent(stopEvent); });
        const HANDLE events[]{stopEvent, ioEvent};
        alignas(DWORD) BYTE buffer[8192];
        while (!stopToken.stop_requested()) {
            OVERLAPPED ov{};
            ov.hEvent = ioEvent;
            ResetEvent(ioEvent);

            const BOOL issued = ReadDirectoryChangesW(
                dir, buffer, sizeof(buffer), FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
                nullptr, &ov, nullptr);
            if (!issued && GetLastError() != ERROR_IO_PENDING) break;

            DWORD transferred = 0;
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                CancelIoEx(dir, &ov);
                // CancelIoEx only requests cancellation; keep ov and the
                // buffer alive until the filesystem finishes with them.
                GetOverlappedResult(dir, &ov, &transferred, TRUE);
                break;
            }
            if (!GetOverlappedResult(dir, &ov, &transferred, FALSE) || stopToken.stop_requested()) break;
            PostMessageW(notifyWnd, WM_APP_DIR_CHANGED, token, 0);
        }
    }

    CloseHandle(ioEvent);
    CloseHandle(stopEvent);
    CloseHandle(dir);
}
