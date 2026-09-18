#pragma once

#include <windows.h>
#include <optional>
#include <string>
#include <vector>

// Pure classification/decoding logic pulled out of PreviewPane so it can be
// unit tested without a preview HWND or real file I/O.
namespace FileClassify {

// `ext` must be lower-cased and include the leading dot (as PreviewPane's
// loadFor already produces).
bool isImageExtension(const std::wstring& ext);
bool isVideoExtension(const std::wstring& ext);

// Mirrors PreviewPane's loadTextFileContent binary/encoding heuristics:
// embedded NUL or >5% control chars => treated as binary (nullopt).
// Otherwise decodes UTF-8 BOM, UTF-16LE BOM, or falls back to UTF-8 then
// the system ANSI code page.
std::optional<std::wstring> decodeTextContent(const std::vector<BYTE>& bytes);

}  // namespace FileClassify
