#include "doctest.h"

#include "Language.h"

TEST_CASE("languageFromLangId picks Japanese only for the Japanese primary language") {
    CHECK(languageFromLangId(MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)) == Language::Ja);
    CHECK(languageFromLangId(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == Language::En);
    CHECK(languageFromLangId(MAKELANGID(LANG_FRENCH, SUBLANG_DEFAULT)) == Language::En);
}

TEST_CASE("setLanguage/currentLanguage round-trip") {
    setLanguage(Language::Ja);
    CHECK(currentLanguage() == Language::Ja);
    setLanguage(Language::En);
    CHECK(currentLanguage() == Language::En);
}
