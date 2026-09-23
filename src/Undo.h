#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

// Edit > 元に戻す (Ctrl+Z) for file operations Kestrel itself performed.
// Explorer's own undo history isn't reachable from outside, so this keeps
// its own: what each operation actually produced (as reported by the
// shell, including conflict renames) and how to reverse it. Operations the
// shell performs on our behalf without telling us the outcome - drag and
// drop, shell context-menu commands - aren't recorded. Pure logic only;
// FileOperations records and executes.
namespace Undo {

enum class Kind { Rename, NewFolder, Copy, Move, Recycle };

struct Change {
    std::wstring source;  // path before (empty for a new folder)
    std::wstring result;  // path after; for Recycle, the recycled file (C:\$Recycle.Bin\<SID>\$R...)
};

struct Record {
    Kind kind;
    std::vector<Change> changes;
};

struct Step {
    enum class Action { Rename, Recycle, MoveTo, Restore };
    Action action;
    std::wstring target;   // item to act on
    std::wstring destDir;  // MoveTo only
    std::wstring newName;  // Rename / MoveTo
};

// The steps that reverse `record`.
std::vector<Step> plan(const Record& record);

// "名前の変更", "コピー", ... for the Edit menu's "元に戻す: ..." label.
std::wstring describe(Kind kind);

// Most-recent-first history, capped at `limit` records.
class Stack {
public:
    explicit Stack(size_t limit) : limit_(limit) {}
    void push(Record record);  // records with no changes are ignored
    std::optional<Record> pop();
    const Record* top() const { return records_.empty() ? nullptr : &records_.back(); }
    bool empty() const { return records_.empty(); }

private:
    size_t limit_;
    std::deque<Record> records_;
};

}  // namespace Undo
