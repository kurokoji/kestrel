#pragma once

#include "Session.h"

#include <string>
#include <vector>

// Pure parse/serialize logic for the session file format, pulled out of
// Session.cpp's load()/save() so it's testable without touching
// %APPDATA%. Session.cpp still owns the file path, BOM and raw I/O.
namespace SessionFormat {

std::vector<std::wstring> splitTab(const std::wstring& line);

// Parses the tab-delimited session content (BOM already stripped, line
// endings may be \n or \r\n). Malformed lines are skipped rather than
// failing the whole parse. Returns a SessionData with empty leftTabs/
// rightTabs if nothing usable was found - callers decide whether that
// means "no saved session".
SessionData parseContent(const std::wstring& content);

// Serializes to the same tab-delimited format `parseContent` reads back
// (no BOM - Session::save prepends that itself before writing).
std::wstring serialize(const SessionData& data);

}  // namespace SessionFormat
