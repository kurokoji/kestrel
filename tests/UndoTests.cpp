#include "doctest.h"

#include "Undo.h"
#include "Language.h"

using Undo::Kind;
using Undo::Record;
using Undo::Step;

TEST_CASE("undoing a rename renames the result back to the old name") {
    const Record r{Kind::Rename, {{L"C:\\d\\old.txt", L"C:\\d\\new.txt"}}};
    const auto steps = Undo::plan(r);
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].action == Step::Action::Rename);
    CHECK(steps[0].target == L"C:\\d\\new.txt");
    CHECK(steps[0].newName == L"old.txt");
}

TEST_CASE("undoing a new folder or a copy recycles what was created") {
    const auto folder = Undo::plan({Kind::NewFolder, {{L"", L"C:\\d\\新しいフォルダー"}}});
    REQUIRE(folder.size() == 1);
    CHECK(folder[0].action == Step::Action::Recycle);
    CHECK(folder[0].target == L"C:\\d\\新しいフォルダー");

    const auto copy = Undo::plan({Kind::Copy, {{L"C:\\a\\x.txt", L"C:\\b\\x.txt"}, {L"C:\\a\\y", L"C:\\b\\y - コピー"}}});
    REQUIRE(copy.size() == 2);
    CHECK(copy[1].action == Step::Action::Recycle);
    CHECK(copy[1].target == L"C:\\b\\y - コピー");
}

TEST_CASE("undoing a move puts each item back under its original folder and name") {
    const auto steps = Undo::plan({Kind::Move, {{L"C:\\a\\x.txt", L"D:\\b\\x (2).txt"}}});
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].action == Step::Action::MoveTo);
    CHECK(steps[0].target == L"D:\\b\\x (2).txt");
    CHECK(steps[0].destDir == L"C:\\a");
    CHECK(steps[0].newName == L"x.txt");
}

TEST_CASE("moving an item out of a drive root goes back to the root itself") {
    const auto steps = Undo::plan({Kind::Move, {{L"C:\\x.txt", L"D:\\x.txt"}}});
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].destDir == L"C:\\");
}

TEST_CASE("undoing a delete restores the recycled items") {
    const auto steps = Undo::plan({Kind::Recycle, {{L"C:\\a\\x.txt", L"::{645FF040}\\recycled-x"}}});
    REQUIRE(steps.size() == 1);
    CHECK(steps[0].action == Step::Action::Restore);
    CHECK(steps[0].target == L"::{645FF040}\\recycled-x");
}

TEST_CASE("describe names the operation for the Edit menu") {
    setLanguage(Language::Ja);
    CHECK(Undo::describe(Kind::Rename) == L"名前の変更");
    CHECK(Undo::describe(Kind::Recycle) == L"削除");
    setLanguage(Language::En);
    CHECK(Undo::describe(Kind::Rename) == L"Rename");
    CHECK(Undo::describe(Kind::Recycle) == L"Delete");
}

TEST_CASE("Stack pops the most recent record first and drops the oldest past its limit") {
    Undo::Stack stack(2);
    CHECK(stack.empty());
    stack.push({Kind::Rename, {{L"a", L"b"}}});
    stack.push({Kind::Copy, {{L"c", L"d"}}});
    stack.push({Kind::Move, {{L"e", L"f"}}});
    CHECK(stack.top()->kind == Kind::Move);
    CHECK(stack.pop()->kind == Kind::Move);
    CHECK(stack.pop()->kind == Kind::Copy);
    CHECK_FALSE(stack.pop().has_value());  // the Rename fell off the bottom
}

TEST_CASE("Stack ignores records where nothing actually happened") {
    Undo::Stack stack(5);
    stack.push({Kind::Copy, {}});  // e.g. every item skipped in a conflict dialog
    CHECK(stack.empty());
}
