#pragma once

#include <windows.h>
#include <array>
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

    // Name/Type/Size/Modified column widths, in FilePane's column order.
    // Each pane's ListView carries its own widths since the two panes can
    // be resized independently already (native header drag).
    std::array<int, 4> leftColumnWidths = {220, 70, 80, 130};
    std::array<int, 4> rightColumnWidths = {220, 70, 80, 130};

    // The UI font, chosen via Tools > Options' ChooseFont dialog. An empty
    // fontFamily means "no override" - fall back to the OS default GUI
    // font, same as before this setting existed.
    std::wstring fontFamily;
    int fontSize = 0;
    bool fontBold = false;

    // Sort column/direction newly created tabs start out with (Tools >
    // Options). Column index matches FilePane's ListView order: 0=Name,
    // 1=Type, 2=Size, 3=Modified. Does not affect already-open tabs.
    int defaultSortColumn = 0;
    bool defaultSortAscending = true;

    // View > 隠しファイル. On by default: hidden files were always listed
    // before this setting existed.
    bool showHidden = true;

    // Tools > Options > 言語. -1 = follow the OS UI language (the
    // default before this setting existed and whenever "Auto" is picked);
    // 0 = English; 1 = Japanese.
    int languageOverride = -1;
};

namespace Session {

// Returns nullopt if there's no saved session (first run) or it couldn't
// be read/parsed - callers should fall back to their own defaults, never
// treat that as an error.
std::optional<SessionData> load();

void save(const SessionData& data);

}  // namespace Session
