#include "doctest.h"

#include "DropTargetPath.h"
#include "Types.h"

#include <string>
#include <vector>

using Paths = std::vector<std::wstring>;

TEST_CASE("joinPath inserts a single separator") {
    CHECK(DropTargetPath::joinPath(L"C:\\foo", L"bar") == L"C:\\foo\\bar");
    CHECK(DropTargetPath::joinPath(L"C:\\", L"bar") == L"C:\\bar");
}

TEST_CASE("joinPath returns This-PC entries (already drive roots) unchanged") {
    CHECK(DropTargetPath::joinPath(kThisPcPath, L"D:\\") == L"D:\\");
}

TEST_CASE("dropping on empty list space targets the current folder") {
    CHECK(DropTargetPath::candidates(L"C:\\foo", L"", false) == Paths{L"C:\\foo"});
}

TEST_CASE("dropping on a folder row targets that folder, falling back to the current folder") {
    CHECK(DropTargetPath::candidates(L"C:\\foo", L"sub", true) == Paths{L"C:\\foo\\sub", L"C:\\foo"});
}

TEST_CASE("dropping on a file row tries the file itself (exe/zip handlers) before the current folder") {
    CHECK(DropTargetPath::candidates(L"C:\\foo", L"a.zip", false) == Paths{L"C:\\foo\\a.zip", L"C:\\foo"});
}

TEST_CASE("This-PC view accepts drops only onto a drive row") {
    CHECK(DropTargetPath::candidates(kThisPcPath, L"", false).empty());
    CHECK(DropTargetPath::candidates(kThisPcPath, L"D:\\", true) == Paths{L"D:\\"});
}

TEST_CASE("anything dropped into the Recycle Bin view goes to the Recycle Bin itself") {
    CHECK(DropTargetPath::candidates(kRecycleBinPath, L"", false) == Paths{kRecycleBinPath});
    CHECK(DropTargetPath::candidates(kRecycleBinPath, L"old.txt", false) == Paths{kRecycleBinPath});
}

TEST_CASE("dropping a folder onto itself or its own descendant is rejected") {
    const Paths sources{L"C:\\foo\\sub", L"C:\\foo\\a.txt"};
    CHECK(DropTargetPath::isOntoSource(sources, L"C:\\foo\\sub", false));
    CHECK(DropTargetPath::isOntoSource(sources, L"c:\\FOO\\SUB", false));  // case-insensitive
    CHECK(DropTargetPath::isOntoSource(sources, L"C:\\foo\\sub\\deeper", false));
    CHECK_FALSE(DropTargetPath::isOntoSource(sources, L"C:\\foo\\subway", false));
}

TEST_CASE("dropping back into the source folder is rejected unless it is an explicit copy") {
    const Paths sources{L"C:\\foo\\a.txt"};
    CHECK(DropTargetPath::isOntoSource(sources, L"C:\\foo", false));
    CHECK_FALSE(DropTargetPath::isOntoSource(sources, L"C:\\foo", true));
    CHECK_FALSE(DropTargetPath::isOntoSource(sources, L"C:\\bar", false));
}

TEST_CASE("drive-root sources compare correctly against their parent") {
    const Paths sources{L"C:\\a.txt"};
    CHECK(DropTargetPath::isOntoSource(sources, L"C:\\", false));
}

TEST_CASE("tree nodes are draggable only when they are ordinary folders") {
    CHECK(DropTargetPath::isDraggableFolder(L"C:\\Users\\me"));
    CHECK_FALSE(DropTargetPath::isDraggableFolder(L""));
    CHECK_FALSE(DropTargetPath::isDraggableFolder(L"C:\\"));  // a drive root
    CHECK_FALSE(DropTargetPath::isDraggableFolder(kThisPcPath));
    CHECK_FALSE(DropTargetPath::isDraggableFolder(kRecycleBinPath));
}
