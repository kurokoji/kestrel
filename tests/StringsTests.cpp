#include "doctest.h"

#include "Strings.h"
#include "Language.h"

#include <string>

TEST_CASE("tr returns the table for the currently active language") {
    setLanguage(Language::En);
    CHECK(std::wstring(tr(StringId::ColumnName)) == L"Name");
    setLanguage(Language::Ja);
    CHECK(std::wstring(tr(StringId::ColumnName)) == L"名前");
}
