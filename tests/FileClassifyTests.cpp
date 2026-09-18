#include "doctest.h"

#include "FileClassify.h"

TEST_CASE("isImageExtension recognizes known image extensions and rejects others") {
    CHECK(FileClassify::isImageExtension(L".png"));
    CHECK(FileClassify::isImageExtension(L".jpeg"));
    CHECK_FALSE(FileClassify::isImageExtension(L".mp4"));
    CHECK_FALSE(FileClassify::isImageExtension(L".PNG"));  // caller is expected to lower-case first
}

TEST_CASE("isVideoExtension recognizes known video extensions and rejects others") {
    CHECK(FileClassify::isVideoExtension(L".mp4"));
    CHECK(FileClassify::isVideoExtension(L".mkv"));
    CHECK_FALSE(FileClassify::isVideoExtension(L".png"));
}

TEST_CASE("decodeTextContent treats an empty file as empty text") {
    CHECK(FileClassify::decodeTextContent({}) == L"");
}

TEST_CASE("decodeTextContent rejects bytes with an embedded NUL as binary") {
    std::vector<BYTE> bytes{'a', 'b', 0, 'c'};
    CHECK_FALSE(FileClassify::decodeTextContent(bytes).has_value());
}

TEST_CASE("decodeTextContent rejects a high ratio of control characters as binary") {
    std::vector<BYTE> bytes(100, 0x01);  // all control chars, no NULs
    CHECK_FALSE(FileClassify::decodeTextContent(bytes).has_value());
}

TEST_CASE("decodeTextContent decodes plain ASCII/UTF-8 text") {
    std::vector<BYTE> bytes{'h', 'i'};
    auto result = FileClassify::decodeTextContent(bytes);
    REQUIRE(result.has_value());
    CHECK(*result == L"hi");
}

TEST_CASE("decodeTextContent strips a UTF-8 BOM before decoding") {
    std::vector<BYTE> bytes{0xEF, 0xBB, 0xBF, 'h', 'i'};
    auto result = FileClassify::decodeTextContent(bytes);
    REQUIRE(result.has_value());
    CHECK(*result == L"hi");
}

TEST_CASE("decodeTextContent decodes UTF-16LE with a BOM") {
    std::vector<BYTE> bytes{0xFF, 0xFE, 'h', 0x00, 'i', 0x00};
    auto result = FileClassify::decodeTextContent(bytes);
    REQUIRE(result.has_value());
    CHECK(*result == L"hi");
}
