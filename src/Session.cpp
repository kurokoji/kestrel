#include "Session.h"

#include <shlobj.h>

#include <format>
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

std::vector<std::wstring> splitTab(const std::wstring& line) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        const size_t pos = line.find(L'\t', start);
        if (pos == std::wstring::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
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

    SessionData data;
    data.leftTabs.clear();
    data.rightTabs.clear();

    size_t lineStart = 0;
    while (lineStart <= content.size()) {
        const size_t lineEnd = content.find(L'\n', lineStart);
        std::wstring line =
            (lineEnd == std::wstring::npos) ? content.substr(lineStart) : content.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        if (!line.empty()) {
            try {
                auto f = splitTab(line);
                if (f[0] == L"W" && f.size() >= 5) {
                    data.windowX = std::stoi(f[1]);
                    data.windowY = std::stoi(f[2]);
                    data.windowW = std::stoi(f[3]);
                    data.windowH = std::stoi(f[4]);
                    if (f.size() >= 6) data.maximized = (f[5] == L"1");
                } else if (f[0] == L"S" && f.size() >= 4) {
                    data.treeWidth = std::stoi(f[1]);
                    data.leftWidth = std::stoi(f[2]);
                    data.previewHeight = std::stoi(f[3]);
                } else if (f[0] == L"A" && f.size() >= 2) {
                    data.activePane = std::stoi(f[1]);
                    if (f.size() >= 3) data.singlePane = (f[2] == L"1");
                } else if (f[0] == L"P" && f.size() >= 3) {
                    const int paneId = std::stoi(f[1]);
                    const int activeIdx = std::stoi(f[2]);
                    if (paneId == 0) data.leftActiveTab = activeIdx;
                    else data.rightActiveTab = activeIdx;
                } else if (f[0] == L"T" && f.size() >= 3) {
                    const int paneId = std::stoi(f[1]);
                    if (paneId == 0) data.leftTabs.push_back(f[2]);
                    else data.rightTabs.push_back(f[2]);
                }
            } catch (...) {
                // Malformed line (hand-edited file, corruption, a future
                // format) - just skip it rather than failing the whole load.
            }
        }

        if (lineEnd == std::wstring::npos) break;
        lineStart = lineEnd + 1;
    }

    if (data.leftTabs.empty() && data.rightTabs.empty()) return std::nullopt;
    return data;
}

void save(const SessionData& data) {
    const std::wstring path = sessionFilePath();
    if (path.empty()) return;

    std::wstring out;
    out += std::format(L"W\t{}\t{}\t{}\t{}\t{}\n", data.windowX, data.windowY, data.windowW, data.windowH,
                        data.maximized ? 1 : 0);
    out += std::format(L"S\t{}\t{}\t{}\n", data.treeWidth, data.leftWidth, data.previewHeight);
    out += std::format(L"A\t{}\t{}\n", data.activePane, data.singlePane ? 1 : 0);

    out += std::format(L"P\t0\t{}\n", data.leftActiveTab);
    for (const auto& t : data.leftTabs) out += std::format(L"T\t0\t{}\n", t);
    out += std::format(L"P\t1\t{}\n", data.rightActiveTab);
    for (const auto& t : data.rightTabs) out += std::format(L"T\t1\t{}\n", t);

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    const WORD bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(hFile, &bom, sizeof(bom), &written, nullptr);
    WriteFile(hFile, out.data(), static_cast<DWORD>(out.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(hFile);
}

}  // namespace Session
