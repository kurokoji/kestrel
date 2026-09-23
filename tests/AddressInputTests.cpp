#include "doctest.h"

#include "AddressInput.h"
#include "Types.h"

TEST_CASE("absolute paths pass through, trimmed and unquoted") {
    CHECK(AddressInput::resolve(L"  C:\\Windows  ", L"D:\\x") == L"C:\\Windows");
    CHECK(AddressInput::resolve(L"\"C:\\Program Files\"", L"D:\\x") == L"C:\\Program Files");
    CHECK(AddressInput::resolve(L"\\\\server\\share", L"D:\\x") == L"\\\\server\\share");
}

TEST_CASE("a bare drive letter means that drive's root") {
    CHECK(AddressInput::resolve(L"d:", L"C:\\x") == L"d:\\");
}

TEST_CASE("forward slashes are accepted as separators") {
    CHECK(AddressInput::resolve(L"C:/Users/me", L"D:\\x") == L"C:\\Users\\me");
}

TEST_CASE("relative input resolves against the current folder") {
    CHECK(AddressInput::resolve(L"sub", L"C:\\a\\b") == L"C:\\a\\b\\sub");
    CHECK(AddressInput::resolve(L"..", L"C:\\a\\b") == L"C:\\a");
    CHECK(AddressInput::resolve(L"..\\c\\.\\d", L"C:\\a\\b") == L"C:\\a\\c\\d");
    CHECK(AddressInput::resolve(L"..\\..\\..", L"C:\\a") == L"C:\\");  // can't climb above the root
}

TEST_CASE("dot segments in absolute paths are collapsed too") {
    CHECK(AddressInput::resolve(L"C:\\a\\..\\b\\", L"D:\\x") == L"C:\\b");
}

TEST_CASE("shell namespace input is left for the shell to parse") {
    CHECK(AddressInput::resolve(L"shell:Downloads", L"C:\\a") == L"shell:Downloads");
    CHECK(AddressInput::resolve(kThisPcPath, L"C:\\a") == kThisPcPath);
}

TEST_CASE("relative input has nothing to resolve against in a virtual folder") {
    CHECK(AddressInput::resolve(L"sub", kThisPcPath) == L"sub");
}

TEST_CASE("empty input stays empty") {
    CHECK(AddressInput::resolve(L"   ", L"C:\\a").empty());
}
