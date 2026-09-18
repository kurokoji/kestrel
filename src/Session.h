#pragma once

#include <windows.h>
#include <optional>
#include <string>
#include <vector>

// What gets remembered across runs: window placement, the splitter
// positions, and each pane's open tabs. Stored as a small tab-delimited
// text file under %APPDATA%\Kestrel - no XML/JSON library, just enough
// structure for our own reader.
struct SessionData {
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowW = 1000;
    int windowH = 650;
    bool maximized = false;

    int treeWidth = 170;
    int leftWidth = 360;
    int previewHeight = 200;
    int activePane = 0;
    bool singlePane = false;

    std::vector<std::wstring> leftTabs;
    int leftActiveTab = 0;
    std::vector<std::wstring> rightTabs;
    int rightActiveTab = 0;
};

namespace Session {

// Returns nullopt if there's no saved session (first run) or it couldn't
// be read/parsed - callers should fall back to their own defaults, never
// treat that as an error.
std::optional<SessionData> load();

void save(const SessionData& data);

}  // namespace Session
