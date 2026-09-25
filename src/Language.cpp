#include "Language.h"

namespace {
Language g_current = languageFromLangId(GetUserDefaultUILanguage());
}  // namespace

Language languageFromLangId(LANGID langId) {
    return PRIMARYLANGID(langId) == LANG_JAPANESE ? Language::Ja : Language::En;
}

Language currentLanguage() { return g_current; }

void setLanguage(Language lang) { g_current = lang; }
