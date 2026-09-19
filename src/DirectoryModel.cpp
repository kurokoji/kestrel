#include "DirectoryModel.h"
#include "Formatting.h"
#include "Messages.h"

#include <algorithm>
#include <memory>

DirectoryModel::~DirectoryModel() {
    cancel();
}

void DirectoryModel::cancel() {
    if (worker_.joinable()) {
        worker_.request_stop();
    }
}

uint64_t DirectoryModel::requestEnumeration(std::wstring path, HWND notifyWnd, WPARAM token) {
    const uint64_t requestId = nextRequestId_.fetch_add(1, std::memory_order_relaxed);
    // Assigning a new jthread requests-stop + joins the previous one first.
    worker_ = std::jthread(run, std::move(path), notifyWnd, token, requestId);
    return requestId;
}

void DirectoryModel::run(std::stop_token stopToken, std::wstring path, HWND notifyWnd, WPARAM token,
                          uint64_t requestId) {
    auto result = std::make_unique<EnumerationResult>();
    result->requestId = requestId;
    result->path = path;

    std::wstring searchPath = path;
    if (!searchPath.empty() && searchPath.back() != L'\\') {
        searchPath += L'\\';
    }
    searchPath += L'*';

    WIN32_FIND_DATAW findData{};
    HANDLE hFind = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic, &findData,
                                     FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (hFind == INVALID_HANDLE_VALUE) {
        result->success = false;
        result->errorCode = GetLastError();
        PostMessageW(notifyWnd, WM_APP_DIR_RESULT, token, reinterpret_cast<LPARAM>(result.release()));
        return;
    }

    result->entries.reserve(256);
    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        FileEntry entry;
        entry.name = findData.cFileName;
        entry.attributes = findData.dwFileAttributes;
        entry.modified = findData.ftLastWriteTime;
        if (!entry.isDirectory()) {
            entry.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32) | findData.nFileSizeLow;
            if (const size_t dot = entry.name.find_last_of(L'.');
                dot != std::wstring::npos && dot != 0) {
                entry.extension = entry.name.substr(dot);
                std::ranges::transform(entry.extension, entry.extension.begin(), ::towlower);
            }
            entry.formattedSize = Formatting::formatSize(entry.size);
        }
        entry.formattedModified = Formatting::formatFileTime(entry.modified);
        result->entries.push_back(std::move(entry));

        // Cooperative cancellation: bail without posting if a newer
        // navigation has already superseded this enumeration. Checked
        // every iteration (an atomic load) rather than batched, since
        // requestEnumeration() joins the previous worker synchronously on
        // the UI thread - on a slow (e.g. network) share, each
        // FindNextFileW call can itself take long enough that batching
        // this check made cancellation (and therefore that join) laggy.
        if (stopToken.stop_requested()) {
            FindClose(hFind);
            return;
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    if (stopToken.stop_requested()) {
        return;
    }

    result->success = true;
    PostMessageW(notifyWnd, WM_APP_DIR_RESULT, token, reinterpret_cast<LPARAM>(result.release()));
}
