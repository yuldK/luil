#include "luil/net/http_body.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("Android decodes Korean extended characters and Japanese with JVM charsets", "[android][net][charset]")
{
    const auto convert { luil::net::codepage_text_transcoder() };
    std::u8string error {};
    const std::vector<std::uint8_t> korean { 0xC7, 0xD1, 0xB1, 0xDB, 0x81, 0x41 };
    REQUIRE(convert(korean, u8" EUC-KR ", error) == u8"한글갂");
    REQUIRE(error.empty());
    const std::vector<std::uint8_t> japanese { 0x93, 0xFA, 0x96, 0x7B };
    REQUIRE(convert(japanese, u8"shift_jis", error) == u8"日本");
    REQUIRE(error.empty());
}

TEST_CASE("Android Latin-1 has the Windows-1252 interpretation and keeps embedded NUL", "[android][net][charset]")
{
    std::u8string error {};
    const std::vector<std::uint8_t> bytes { 0x80, 0x00, 0xE9 };
    const std::u8string result { luil::net::codepage_text_transcoder()(bytes, u8"iso-8859-1", error) };
    REQUIRE(result == std::u8string { u8"€\0é", 6 });
    REQUIRE(error.empty());
}

TEST_CASE("Android charset decoding rejects incomplete input and recovers for the next body", "[android][net][charset]")
{
    const auto convert { luil::net::codepage_text_transcoder() };
    std::u8string error {};
    const std::vector<std::uint8_t> broken { 0x81 };
    REQUIRE(convert(broken, u8"cp949", error).empty());
    REQUIRE(error.empty() == false);
    error.clear();
    const std::vector<std::uint8_t> good { 0xE9 };
    REQUIRE(convert(good, u8"windows-1252", error) == u8"é");
    REQUIRE(error.empty());
}
