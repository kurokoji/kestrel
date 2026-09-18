#include "Session.h"
#include "SessionFormat.h"

#include <shlobj.h>

#include <vector>

namespace {

std::wstring sessionFilePath() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        dir = appData;
        CoTaskMemFree(appData);
    }
    if (dir.empty()) return L"";

    dir += L"\\Kestrel";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\session.ini";
}

}  // namespace

namespace Session {

std::optional<SessionData> load() {
    const std::wstring path = sessionFilePath();
    if (path.empty()) return std::nullopt;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return std::nullopt;

    LARGE_INTEGER size{};
    GetFileSizeEx(hFile, &size);
    std::vector<BYTE> buf(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(hFile);
    if (!ok || buf.size() < 2) return std::nullopt;

    size_t offset = 0;
    if (buf[0] == 0xFF && buf[1] == 0xFE) offset = 2;  // UTF-16LE BOM
    if ((buf.size() - offset) % 2 != 0) return std::nullopt;
    const std::wstring content(reinterpret_cast<const wchar_t*>(buf.data() + offset), (buf.size() - offset) / 2);

    SessionData data = SessionFormat::parseContent(content);
    if (data.leftTabs.empty() && data.rightTabs.empty()) return std::nullopt;
    return data;
}

void save(const SessionData& data) {
    const std::wstring path = sessionFilePath();
    if (path.empty()) return;

    const std::wstring out = SessionFormat::serialize(data);

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(hFile, &bom, sizeof(bom), &written, nullptr);
    WriteFile(hFile, out.data(), static_cast<DWORD>(out.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);
}

}  // namespace Session
