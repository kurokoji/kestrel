#include "Undo.h"

namespace Undo {
namespace {

// "C:\a\x.txt" -> {"C:\a", "x.txt"}; a drive root keeps its separator ("C:\").
std::pair<std::wstring, std::wstring> splitParent(const std::wstring& path) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {std::wstring{}, path};
    std::wstring dir = path.substr(0, slash);
    if (dir.size() == 2 && dir[1] == L':') dir += L'\\';
    return {dir, path.substr(slash + 1)};
}

}  // namespace

std::vector<Step> plan(const Record& record) {
    std::vector<Step> steps;
    steps.reserve(record.changes.size());
    for (const auto& change : record.changes) {
        switch (record.kind) {
            case Kind::Rename:
                steps.push_back({Step::Action::Rename, change.result, {}, splitParent(change.source).second});
                break;
            case Kind::NewFolder:
            case Kind::Copy:
                steps.push_back({Step::Action::Recycle, change.result, {}, {}});
                break;
            case Kind::Move: {
                auto [dir, name] = splitParent(change.source);
                steps.push_back({Step::Action::MoveTo, change.result, std::move(dir), std::move(name)});
                break;
            }
            case Kind::Recycle:
                steps.push_back({Step::Action::Restore, change.result, {}, {}});
                break;
        }
    }
    return steps;
}

std::wstring describe(Kind kind) {
    switch (kind) {
        case Kind::Rename: return L"名前の変更";
        case Kind::NewFolder: return L"新しいフォルダー";
        case Kind::Copy: return L"コピー";
        case Kind::Move: return L"移動";
        case Kind::Recycle: return L"削除";
    }
    return {};
}

void Stack::push(Record record) {
    if (record.changes.empty()) return;
    records_.push_back(std::move(record));
    while (records_.size() > limit_) records_.pop_front();
}

std::optional<Record> Stack::pop() {
    if (records_.empty()) return std::nullopt;
    Record record = std::move(records_.back());
    records_.pop_back();
    return record;
}

}  // namespace Undo
