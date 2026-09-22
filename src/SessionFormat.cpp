#include "SessionFormat.h"

#include <array>
#include <format>

namespace SessionFormat {

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

SessionData parseContent(const std::wstring& content) {
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
                } else if (f[0] == L"K" && f.size() >= 6) {
                    const int paneId = std::stoi(f[1]);
                    std::array<int, 4> widths{std::stoi(f[2]), std::stoi(f[3]), std::stoi(f[4]), std::stoi(f[5])};
                    if (paneId == 0) data.leftColumnWidths = widths;
                    else data.rightColumnWidths = widths;
                } else if (f[0] == L"F" && f.size() >= 4) {
                    data.fontFamily = f[1];
                    data.fontSize = std::stoi(f[2]);
                    data.fontBold = (f[3] == L"1");
                } else if (f[0] == L"O" && f.size() >= 3) {
                    data.defaultSortColumn = std::stoi(f[1]);
                    data.defaultSortAscending = (f[2] == L"1");
                }
            } catch (...) {
                // Malformed line (hand-edited file, corruption, a future
                // format) - just skip it rather than failing the whole load.
            }
        }

        if (lineEnd == std::wstring::npos) break;
        lineStart = lineEnd + 1;
    }

    return data;
}

std::wstring serialize(const SessionData& data) {
    std::wstring out;
    out += std::format(L"W\t{}\t{}\t{}\t{}\t{}\n", data.windowX, data.windowY, data.windowW, data.windowH,
                        data.maximized ? 1 : 0);
    out += std::format(L"S\t{}\t{}\t{}\n", data.treeWidth, data.leftWidth, data.previewHeight);
    out += std::format(L"A\t{}\t{}\n", data.activePane, data.singlePane ? 1 : 0);

    out += std::format(L"P\t0\t{}\n", data.leftActiveTab);
    for (const auto& t : data.leftTabs) out += std::format(L"T\t0\t{}\n", t);
    out += std::format(L"P\t1\t{}\n", data.rightActiveTab);
    for (const auto& t : data.rightTabs) out += std::format(L"T\t1\t{}\n", t);

    out += std::format(L"K\t0\t{}\t{}\t{}\t{}\n", data.leftColumnWidths[0], data.leftColumnWidths[1],
                        data.leftColumnWidths[2], data.leftColumnWidths[3]);
    out += std::format(L"K\t1\t{}\t{}\t{}\t{}\n", data.rightColumnWidths[0], data.rightColumnWidths[1],
                        data.rightColumnWidths[2], data.rightColumnWidths[3]);

    if (!data.fontFamily.empty()) {
        out += std::format(L"F\t{}\t{}\t{}\n", data.fontFamily, data.fontSize, data.fontBold ? 1 : 0);
    }

    out += std::format(L"O\t{}\t{}\n", data.defaultSortColumn, data.defaultSortAscending ? 1 : 0);

    return out;
}

}  // namespace SessionFormat
