#pragma once

namespace WindowLayout {

inline constexpr int splitterWidth = 4;
inline constexpr int minPaneWidth = 80;
inline constexpr int activeFrameWidth = 2;

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const Rect&) const = default;
};

struct Input {
    int width, height;
    int toolbarWidth, toolbarHeight, statusHeight;
    int treeWidth, leftWidth, previewHeight, previousWidth;
    bool singlePane;
    int activePane;
};

struct Result {
    Rect toolbar, address, tree, preview;
    Rect splitter1, splitter2, splitter3;
    Rect leftOuter, rightOuter, leftInner, rightInner;
    int treeWidth, leftWidth, previewHeight;
    int contentTop, contentHeight;
};

// Caller skips minimized/zero-sized windows and supplies measured control sizes.
// Minimum sizes and resize scaling deliberately match the native layout.
Result calculate(const Input& input);

}  // namespace WindowLayout
