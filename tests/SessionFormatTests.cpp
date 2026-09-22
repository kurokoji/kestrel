#include "doctest.h"

#include "SessionFormat.h"

TEST_CASE("splitTab splits on tabs, including empty trailing fields") {
    CHECK(SessionFormat::splitTab(L"a\tb\tc") == std::vector<std::wstring>{L"a", L"b", L"c"});
    CHECK(SessionFormat::splitTab(L"a\t\tc") == std::vector<std::wstring>{L"a", L"", L"c"});
    CHECK(SessionFormat::splitTab(L"solo") == std::vector<std::wstring>{L"solo"});
}

TEST_CASE("serialize then parseContent round-trips a full SessionData") {
    SessionData data;
    data.windowX = 10;
    data.windowY = 20;
    data.windowW = 800;
    data.windowH = 600;
    data.maximized = true;
    data.treeWidth = 150;
    data.leftWidth = 300;
    data.previewHeight = 180;
    data.activePane = 1;
    data.singlePane = true;
    data.leftTabs = {L"C:\\foo", L"C:\\bar"};
    data.leftActiveTab = 1;
    data.rightTabs = {L"D:\\baz"};
    data.rightActiveTab = 0;
    data.leftColumnWidths = {100, 50, 60, 90};
    data.rightColumnWidths = {200, 55, 65, 95};
    data.fontFamily = L"Consolas";
    data.fontSize = 11;
    data.fontBold = true;
    data.defaultSortColumn = 3;
    data.defaultSortAscending = false;

    const std::wstring serialized = SessionFormat::serialize(data);
    const SessionData parsed = SessionFormat::parseContent(serialized);

    CHECK(parsed.windowX == 10);
    CHECK(parsed.windowY == 20);
    CHECK(parsed.windowW == 800);
    CHECK(parsed.windowH == 600);
    CHECK(parsed.maximized == true);
    CHECK(parsed.treeWidth == 150);
    CHECK(parsed.leftWidth == 300);
    CHECK(parsed.previewHeight == 180);
    CHECK(parsed.activePane == 1);
    CHECK(parsed.singlePane == true);
    CHECK(parsed.leftTabs == std::vector<std::wstring>{L"C:\\foo", L"C:\\bar"});
    CHECK(parsed.leftActiveTab == 1);
    CHECK(parsed.rightTabs == std::vector<std::wstring>{L"D:\\baz"});
    CHECK(parsed.rightActiveTab == 0);
    CHECK(parsed.leftColumnWidths == std::array<int, 4>{100, 50, 60, 90});
    CHECK(parsed.rightColumnWidths == std::array<int, 4>{200, 55, 65, 95});
    CHECK(parsed.fontFamily == L"Consolas");
    CHECK(parsed.fontSize == 11);
    CHECK(parsed.fontBold == true);
    CHECK(parsed.defaultSortColumn == 3);
    CHECK(parsed.defaultSortAscending == false);
}

TEST_CASE("parseContent falls back to default sort column/direction when absent") {
    const SessionData data = SessionFormat::parseContent(L"T\t0\tC:\\ok\n");
    CHECK(data.defaultSortColumn == 0);
    CHECK(data.defaultSortAscending == true);
}

TEST_CASE("parseContent falls back to default column widths when absent") {
    const SessionData data = SessionFormat::parseContent(L"T\t0\tC:\\ok\n");
    CHECK(data.leftColumnWidths == std::array<int, 4>{220, 70, 80, 130});
    CHECK(data.rightColumnWidths == std::array<int, 4>{220, 70, 80, 130});
}

TEST_CASE("parseContent falls back to no font override when absent") {
    const SessionData data = SessionFormat::parseContent(L"T\t0\tC:\\ok\n");
    CHECK(data.fontFamily == L"");
    CHECK(data.fontSize == 0);
    CHECK(data.fontBold == false);
}

TEST_CASE("parseContent tolerates CRLF line endings") {
    const SessionData data = SessionFormat::parseContent(L"T\t0\tC:\\one\r\nT\t1\tC:\\two\r\n");
    CHECK(data.leftTabs == std::vector<std::wstring>{L"C:\\one"});
    CHECK(data.rightTabs == std::vector<std::wstring>{L"C:\\two"});
}

TEST_CASE("parseContent skips malformed lines instead of failing the whole parse") {
    const SessionData data = SessionFormat::parseContent(L"W\tnot-a-number\tx\ty\tz\tw\nT\t0\tC:\\ok\n");
    CHECK(data.leftTabs == std::vector<std::wstring>{L"C:\\ok"});
}

TEST_CASE("parseContent ignores unknown record types") {
    const SessionData data = SessionFormat::parseContent(L"X\tunknown\tstuff\nT\t0\tC:\\ok\n");
    CHECK(data.leftTabs == std::vector<std::wstring>{L"C:\\ok"});
}

TEST_CASE("parseContent of empty content yields empty tabs") {
    const SessionData data = SessionFormat::parseContent(L"");
    CHECK(data.leftTabs.empty());
    CHECK(data.rightTabs.empty());
}
