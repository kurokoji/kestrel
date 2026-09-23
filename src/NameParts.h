#pragma once

#include <string>
#include <vector>

// Pure file-name helpers for rename and new-folder creation, kept free of
// HWNDs so they can be unit tested.
namespace NameParts {

// Length of the part of `name` Explorer preselects on rename: everything
// before the last extension for files, the whole name for folders or for
// names without an extension (".gitignore" has none).
size_t renameSelectionEnd(const std::wstring& name, bool isDirectory);

// `base`, or "base (2)", "base (3)", ... - the first one not already in
// `existingNames` (compared case-insensitively, as the file system does).
std::wstring uniqueName(const std::vector<std::wstring>& existingNames, const std::wstring& base);

// Type column text when the shell has no registered type name: "ABC
// ファイル" for ".abc", as Explorer shows it.
std::wstring fallbackTypeName(const std::wstring& extension, bool isDirectory);

}  // namespace NameParts
