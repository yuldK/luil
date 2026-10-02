#include "net/winhttp/http_text_transcode.h"

#include "luil/net/http_body.h"
#include "luil/net/http_media_type.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// Windows의 코드 페이지 변환이다. Android는 utf-8과 utf-16만 옮긴다 (http_body_tests.cpp에 그 둘이 있다).
namespace {
    using luil::net::http_body;
    using luil::net::http_media_type;

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const auto& fixture)
    {
        return std::vector<std::uint8_t> { fixture.begin(), fixture.end() };
    }

    [[nodiscard]] http_media_type media_type(const std::u8string_view header)
    {
        return luil::net::parse_media_type(header);
    }
} // namespace

TEST_CASE("The default transcoder really reads euc-kr", "[net][body]")
{
    // 손으로 적은 EUC-KR "한글"이다 (C7 D1 B1 DB).
    constexpr std::array<std::uint8_t, 4> korean { 0xC7u, 0xD1u, 0xB1u, 0xDBu };

    const http_body body { luil::net::decode_http_body(bytes_of(korean), media_type(u8"text/plain; charset=euc-kr"), {}, luil::net::codepage_text_transcoder()) };
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.as_text() == u8"한글");
}

TEST_CASE("The charset table answers the names servers actually send", "[net][body]")
{
    REQUIRE(luil::net::charset_code_page(u8"euc-kr") == 949u);
    REQUIRE(luil::net::charset_code_page(u8"KS_C_5601-1987") == 949u);
    REQUIRE(luil::net::charset_code_page(u8"shift_jis") == 932u);
    REQUIRE(luil::net::charset_code_page(u8"big5") == 950u);
    REQUIRE(luil::net::charset_code_page(u8"windows-1251") == 1251u);
    REQUIRE(luil::net::charset_code_page(u8"iso-8859-5") == 28595u);
    // 이름은 ISO지만 실제로 오는 바이트는 windows-1252다 (0x80~0x9F에 따옴표가
    // 든다). 28591로 읽으면 그 자리가 C1 제어 문자가 되어 **올바른 UTF-8인 채로**
    // 못 읽을 글이 나온다.
    REQUIRE(luil::net::charset_code_page(u8"iso-8859-1") == 1252u);
    REQUIRE(luil::net::charset_code_page(u8"latin1") == 1252u);
    REQUIRE(luil::net::charset_code_page(u8"utf-16le") == luil::net::utf16le_code_page);
    REQUIRE(luil::net::charset_code_page(u8"x-made-up") == 0u);
    REQUIRE(luil::net::charset_code_page(u8"") == 0u);
}
