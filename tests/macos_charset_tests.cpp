#include "luil/net/http_body.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

// macOS의 기본 변환기는 CoreFoundation의 표를 쓴다. 별칭 셋은 Windows와 같은 문자 집합으로 돌린다
// (net/macos/macos_text_transcode.cpp). Android의 같은 축은 android_charset_tests.cpp다.
TEST_CASE("macOS decodes Korean extended characters and Japanese with Windows code pages", "[macos][net][charset]")
{
    const auto convert { luil::net::codepage_text_transcoder() };
    std::u8string error {};
    // 0x81 0x41은 EUC-KR(KS X 1001)에 없는 확장 한글이다. CoreFoundation의 euc-kr 표로 읽으면 실패한다.
    const std::vector<std::uint8_t> korean { 0xC7, 0xD1, 0xB1, 0xDB, 0x81, 0x41 };
    REQUIRE(convert(korean, u8" EUC-KR ", error) == u8"한글갂");
    REQUIRE(error.empty());
    const std::vector<std::uint8_t> japanese { 0x93, 0xFA, 0x96, 0x7B };
    REQUIRE(convert(japanese, u8"shift_jis", error) == u8"日本");
    REQUIRE(error.empty());
}

TEST_CASE("macOS Latin-1 has the Windows-1252 interpretation and keeps embedded NUL", "[macos][net][charset]")
{
    std::u8string error {};
    const std::vector<std::uint8_t> bytes { 0x80, 0x00, 0xE9 };
    const std::u8string result { luil::net::codepage_text_transcoder()(bytes, u8"iso-8859-1", error) };
    REQUIRE(result == std::u8string { u8"€\0é", 6 });
    REQUIRE(error.empty());
}

TEST_CASE("macOS reads IANA names that are not in the alias table", "[macos][net][charset]")
{
    const auto convert { luil::net::codepage_text_transcoder() };
    std::u8string error {};
    const std::vector<std::uint8_t> chinese { 0xA4, 0xA4 };
    REQUIRE(convert(chinese, u8"Big5", error) == u8"中");
    const std::vector<std::uint8_t> cyrillic { 0xC4, 0xE0 };
    REQUIRE(convert(cyrillic, u8"windows-1251", error) == u8"Да");
    REQUIRE(error.empty());
}

TEST_CASE("macOS charset decoding rejects incomplete input and recovers for the next body", "[macos][net][charset]")
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
    REQUIRE(convert(good, u8"x-made-up", error).empty());
    REQUIRE(error.find(u8"x-made-up") != std::u8string::npos);
}
