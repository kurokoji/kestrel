#include "WindowLayout.h"

#include <algorithm>
#include <cmath>

namespace WindowLayout {
namespace {
Rect inset(Rect r) {
    return {r.left + activeFrameWidth, r.top + activeFrameWidth,
            r.right - activeFrameWidth, r.bottom - activeFrameWidth};
}
}

Result calculate(const Input& i) {
    Result r{};
    r.leftWidth = i.leftWidth;
    // Only window resizing scales the file pane split; tree width stays fixed.
    if (i.previousWidth > 0 && i.width != i.previousWidth) {
        r.leftWidth = static_cast<int>(std::lround(i.leftWidth *
            (static_cast<double>(i.width) / i.previousWidth)));
    }
    const int tbW = i.toolbarWidth > 0 ? i.toolbarWidth : 200;
    const int tbH = i.toolbarHeight > 0 ? i.toolbarHeight : 22;
    const int top = std::max(tbH, 22) + 6;
    r.toolbar = {0, (top - tbH) / 2, tbW, (top - tbH) / 2 + tbH};
    const int addrX = tbW + 10;
    r.address = {addrX, (top - 22) / 2, addrX + std::max(60, i.width - addrX - 6), (top - 22) / 2 + 22};
    const int workHeight = std::max(0, i.height - i.statusHeight - top);
    r.contentTop = top;
    r.contentHeight = workHeight;
    r.treeWidth = std::clamp(i.treeWidth, minPaneWidth,
        std::max(minPaneWidth, i.width - 2 * minPaneWidth - 2 * splitterWidth));
    r.previewHeight = std::clamp(i.previewHeight, 60, std::max(60, workHeight - 60 - splitterWidth));
    const int treeH = std::max(0, workHeight - r.previewHeight - splitterWidth);
    r.tree = {0, top, r.treeWidth, top + treeH};
    r.splitter3 = {0, top + treeH, r.treeWidth, top + treeH + splitterWidth};
    r.preview = {0, top + treeH + splitterWidth, r.treeWidth, top + workHeight};
    r.splitter1 = {r.treeWidth, top, r.treeWidth + splitterWidth, top + workHeight};
    int x = r.treeWidth + splitterWidth;
    if (i.singlePane) {
        const Rect outer{x, top, x + std::max(0, i.width - x), top + workHeight};
        if (i.activePane == 0) {
            r.leftOuter = outer;
            r.leftInner = inset(outer);
        } else {
            r.rightOuter = outer;
            r.rightInner = inset(outer);
        }
    } else {
        r.leftWidth = std::clamp(r.leftWidth, minPaneWidth,
            std::max(minPaneWidth, i.width - x - minPaneWidth - splitterWidth));
        r.leftOuter = {x, top, x + r.leftWidth, top + workHeight};
        r.leftInner = inset(r.leftOuter);
        x += r.leftWidth;
        r.splitter2 = {x, top, x + splitterWidth, top + workHeight};
        x += splitterWidth;
        r.rightOuter = {x, top, x + std::max(0, i.width - x), top + workHeight};
        r.rightInner = inset(r.rightOuter);
    }
    return r;
}
Result previewSplitter(Input input, int splitter, int x, int y) {
    input.previousWidth = input.width; // A drag is not a window resize.
    const auto current = calculate(input);
    if (splitter == 1) input.treeWidth = x;
    else if (splitter == 2) input.leftWidth = x - current.treeWidth - splitterWidth;
    else if (splitter == 3)
        input.previewHeight = current.contentTop + current.contentHeight - y - splitterWidth;
    return calculate(input);
}
}  // namespace WindowLayout
