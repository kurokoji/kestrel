#pragma once

#include <windows.h>

// Which UI language is active. Kept separate from Strings.h so the pure
// detection logic (languageFromLangId) can be unit-tested without pulling
// in the whole string table.
enum class Language { En, Ja };

// Maps a Win32 LANGID (as from GetUserDefaultUILanguage()) to one of our
// supported languages. Anything that isn't Japanese falls back to English -
// we only ship two translations, not a "closest match" system.
Language languageFromLangId(LANGID langId);

// Process-wide current language, defaulting to the OS UI language at first
// use (see Language.cpp). Tools > Options can override it via
// setLanguage(); that override is what Session persists, not the raw OS
// detection.
Language currentLanguage();
void setLanguage(Language lang);
