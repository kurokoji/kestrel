#include "doctest.h"
#include "WindowLayout.h"
#include <initializer_list>

using namespace WindowLayout;

static Input normal() { return {1000, 700, 200, 22, 20, 200, 350, 160, 0, false, 0}; }

TEST_CASE("Dual panes retain the existing geometry") {
    const auto r = calculate(normal());
    CHECK(r.toolbar == Rect{0, 3, 200, 25});
    CHECK(r.address == Rect{210, 3, 994, 25});
    CHECK(r.tree == Rect{0, 28, 200, 516});
    CHECK(r.preview == Rect{0, 520, 200, 680});
    CHECK(r.splitter1 == Rect{200, 28, 204, 680});
    CHECK(r.splitter2 == Rect{554, 28, 558, 680});
    CHECK(r.splitter3 == Rect{0, 516, 200, 520});
    CHECK(r.leftOuter == Rect{204, 28, 554, 680});
    CHECK(r.leftInner == Rect{206, 30, 552, 678});
    CHECK(r.rightOuter == Rect{558, 28, 1000, 680});
    CHECK(r.rightInner == Rect{560, 30, 998, 678});
}

TEST_CASE("Single pane occupies remaining space for either active pane") {
    for (int active : {0, 1}) {
        auto input = normal(); input.singlePane = true; input.activePane = active;
        const auto r = calculate(input);
        CHECK((active == 0 ? r.leftOuter : r.rightOuter) == Rect{204, 28, 1000, 680});
        CHECK((active == 0 ? r.rightOuter : r.leftOuter) == Rect{});
        CHECK(r.splitter2 == Rect{});
        CHECK(r.leftWidth == 350);
    }
}

TEST_CASE("Resize scales left width but splitter changes do not") {
    auto input = normal(); input.previousWidth = 800;
    CHECK(calculate(input).leftWidth == 438);
    input.previousWidth = 1000;
    CHECK(calculate(input).leftWidth == 350);
    CHECK(calculate(input).treeWidth == 200);
}

TEST_CASE("Small windows clamp splitters using existing minimums") {
    auto input = normal(); input.width = 300; input.height = 200;
    const auto r = calculate(input);
    CHECK(r.treeWidth == 132);
    CHECK(r.leftWidth == 80);
    CHECK(r.previewHeight == 88);
    CHECK(r.rightOuter == Rect{220, 28, 300, 180});
    CHECK(r.tree == Rect{0, 28, 132, 88});
}

TEST_CASE("Toolbar measurement fallback and tall toolbar") {
    auto input = normal(); input.toolbarWidth = 0; input.toolbarHeight = 0;
    CHECK(calculate(input).toolbar == Rect{0, 3, 200, 25});
    input.toolbarHeight = 40;
    CHECK(calculate(input).toolbar == Rect{0, 3, 200, 43});
    CHECK(calculate(input).address == Rect{210, 12, 994, 34});
    CHECK(calculate(input).contentTop == 46);
}
