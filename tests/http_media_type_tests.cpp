#include "luil/net/http_media_type.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
    [[nodiscard]] std::span<const std::uint8_t> bytes_of(const auto& fixture) noexcept
    {
        return std::span<const std::uint8_t> { fixture.data(), fixture.size() };
    }

    [[nodiscard]] luil::net::http_body_kind kind_of(const std::u8string_view content_type)
    {
        return luil::net::classify_media_type(luil::net::parse_media_type(content_type));
    }

    [[nodiscard]] bool sniffs(const std::u8string_view content_type)
    {
        return luil::net::media_type_invites_sniffing(luil::net::parse_media_type(content_type));
    }
} // namespace

TEST_CASE("A media type without parameters keeps its essence", "[net][media]")
{
    const luil::net::http_media_type parsed { luil::net::parse_media_type(u8"application/json") };
    REQUIRE(parsed.valid);
    REQUIRE(parsed.essence == u8"application/json");
    REQUIRE(parsed.charset.empty());
}

TEST_CASE("A media type is lowered before it is compared", "[net][media]")
{
    const luil::net::http_media_type parsed { luil::net::parse_media_type(u8"TEXT/HTML; CHARSET=UTF-8") };
    REQUIRE(parsed == luil::net::http_media_type { u8"text/html", u8"utf-8", true });
}

TEST_CASE("A quoted charset loses its quotes", "[net][media]")
{
    const luil::net::http_media_type parsed { luil::net::parse_media_type(u8"text/plain;charset=\"euc-kr\"") };
    REQUIRE(parsed.essence == u8"text/plain");
    REQUIRE(parsed.charset == u8"euc-kr");
}

TEST_CASE("Whitespace, unknown parameters and empty ones are stepped over", "[net][media]")
{
    // 매개변수가 여럿이고 우리가 쓰는 것은 하나뿐이다.
    REQUIRE(luil::net::parse_media_type(u8"multipart/form-data; boundary=xyz; charset=utf-8").charset == u8"utf-8");
    // `=` 둘레의 빈칸과 줄 앞뒤의 빈칸.
    REQUIRE(luil::net::parse_media_type(u8"  text/plain ;  charset = utf-8  ").charset == u8"utf-8");
    // 값 없는 `;`와 이어진 `;;`는 그냥 넘어간다.
    REQUIRE(luil::net::parse_media_type(u8"text/plain;;charset=utf-8").charset == u8"utf-8");
    REQUIRE(luil::net::parse_media_type(u8"text/plain;").valid);
    REQUIRE(luil::net::parse_media_type(u8"text/plain;").charset.empty());
    // 이름만 있고 값이 없는 매개변수도 줄 전체를 버리지 않는다.
    REQUIRE(luil::net::parse_media_type(u8"application/json; profile").valid);
    // 따옴표 안의 `;`는 구분자가 아니다 — 자르면 뒤가 이름 없는 매개변수로 보인다.
    REQUIRE(luil::net::parse_media_type(u8"text/plain; name=\"a;b\"; charset=utf-8").charset == u8"utf-8");
}

TEST_CASE("The first charset wins when a header repeats it", "[net][media]")
{
    REQUIRE(luil::net::parse_media_type(u8"text/plain; charset=utf-8; charset=euc-kr").charset == u8"utf-8");
}

TEST_CASE("An empty first charset still takes the seat", "[net][media]")
{
    // 값이 비었다고 자리를 비워 두면 서버가 덧붙인 줄이 그 자리를 차지한다 —
    // 앞엣것이 이긴다는 규칙은 값의 내용을 보지 않는다.
    const luil::net::http_media_type parsed { luil::net::parse_media_type(u8"text/plain; charset=; charset=euc-kr") };
    REQUIRE(parsed.valid);
    REQUIRE(parsed.essence == u8"text/plain");
    REQUIRE(parsed.charset.empty());
}

TEST_CASE("A header that is not type/subtype is not valid", "[net][media]")
{
    REQUIRE(luil::net::parse_media_type(u8"").valid == false);
    REQUIRE(luil::net::parse_media_type(u8"   ").valid == false);
    REQUIRE(luil::net::parse_media_type(u8"json").valid == false);
    REQUIRE(luil::net::parse_media_type(u8";;;").valid == false);
    REQUIRE(luil::net::parse_media_type(u8"/json").valid == false);
    REQUIRE(luil::net::parse_media_type(u8"application/").valid == false);
    // 못 읽은 줄은 essence도 비운다 — 그래야 냄새를 맡을지 묻는 쪽이 한 가지만 본다.
    REQUIRE(luil::net::parse_media_type(u8"json").essence.empty());
}

TEST_CASE("Every json spelling classifies as json", "[net][media]")
{
    REQUIRE(kind_of(u8"application/json") == luil::net::http_body_kind::json);
    REQUIRE(kind_of(u8"text/json") == luil::net::http_body_kind::json);
    REQUIRE(kind_of(u8"application/problem+json") == luil::net::http_body_kind::json);
    REQUIRE(kind_of(u8"application/ld+json") == luil::net::http_body_kind::json);
    REQUIRE(kind_of(u8"application/json; charset=utf-8") == luil::net::http_body_kind::json);
}

TEST_CASE("Image media types classify as image", "[net][media]")
{
    REQUIRE(kind_of(u8"image/png") == luil::net::http_body_kind::image);
    REQUIRE(kind_of(u8"image/gif") == luil::net::http_body_kind::image);
    REQUIRE(kind_of(u8"image/webp") == luil::net::http_body_kind::image);
    REQUIRE(kind_of(u8"IMAGE/JPEG") == luil::net::http_body_kind::image);
}

TEST_CASE("An svg is bytes because this build has no svg codec", "[net][media]")
{
    // image라 부르면 모든 svg가 "디코드 실패"로 오고, 그것은 서버 잘못이 아니다.
    // `+xml`로도 새면 안 된다 — 글로 풀린 svg는 아무도 쓰지 않는다.
    REQUIRE(kind_of(u8"image/svg+xml") == luil::net::http_body_kind::bytes);
}

TEST_CASE("html is decided before the text family", "[net][media]")
{
    REQUIRE(kind_of(u8"text/html") == luil::net::http_body_kind::html);
    REQUIRE(kind_of(u8"text/html; charset=utf-8") == luil::net::http_body_kind::html);
    REQUIRE(kind_of(u8"application/xhtml+xml") == luil::net::http_body_kind::html);
}

TEST_CASE("The text family covers text/* and the xml spellings", "[net][media]")
{
    REQUIRE(kind_of(u8"text/plain") == luil::net::http_body_kind::text);
    REQUIRE(kind_of(u8"text/csv") == luil::net::http_body_kind::text);
    REQUIRE(kind_of(u8"application/xml") == luil::net::http_body_kind::text);
    REQUIRE(kind_of(u8"text/foo+xml") == luil::net::http_body_kind::text);
    REQUIRE(kind_of(u8"application/javascript") == luil::net::http_body_kind::text);
    REQUIRE(kind_of(u8"application/x-www-form-urlencoded") == luil::net::http_body_kind::text);
}

TEST_CASE("The json suffix is read before the xml suffix", "[net][media]")
{
    // 차례가 뒤집히면 `application/problem+json`이 xml 갈래로 샌다.
    REQUIRE(kind_of(u8"application/soap+xml") == luil::net::http_body_kind::text);
}

TEST_CASE("What we do not parse is bytes", "[net][media]")
{
    REQUIRE(kind_of(u8"application/octet-stream") == luil::net::http_body_kind::bytes);
    REQUIRE(kind_of(u8"application/zip") == luil::net::http_body_kind::bytes);
    REQUIRE(kind_of(u8"video/mp4") == luil::net::http_body_kind::bytes);
    REQUIRE(kind_of(u8"") == luil::net::http_body_kind::bytes);
}

TEST_CASE("Only a missing, empty or octet-stream media type invites sniffing", "[net][media]")
{
    REQUIRE(sniffs(u8""));
    REQUIRE(sniffs(u8"garbage"));
    REQUIRE(sniffs(u8"application/octet-stream"));
    REQUIRE(sniffs(u8"application/octet-stream; charset=utf-8"));
    REQUIRE(luil::net::media_type_invites_sniffing(luil::net::http_media_type {}));
}

TEST_CASE("A declared media type is never second-guessed", "[net][media]")
{
    // 이 한 줄이 sniffing 정책 전체를 잠근다 — 서버가 구체적인 형식을 말했으면
    // 바이트를 보지 않는다.
    REQUIRE(sniffs(u8"text/plain") == false);
    REQUIRE(sniffs(u8"application/json") == false);
    REQUIRE(sniffs(u8"image/png") == false);
}

TEST_CASE("Sniffing knows the five signatures this build decodes", "[net][media]")
{
    constexpr std::array<std::uint8_t, 8> png { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    constexpr std::array<std::uint8_t, 4> jpeg { 0xFFu, 0xD8u, 0xFFu, 0xE0u };
    constexpr std::array<std::uint8_t, 6> gif87a { 'G', 'I', 'F', '8', '7', 'a' };
    constexpr std::array<std::uint8_t, 6> gif89a { 'G', 'I', 'F', '8', '9', 'a' };
    constexpr std::array<std::uint8_t, 12> webp { 'R', 'I', 'F', 'F', 0x20u, 0x00u, 0x00u, 0x00u, 'W', 'E', 'B', 'P' };
    constexpr std::array<std::uint8_t, 4> bmp { 'B', 'M', 0x36u, 0x00u };

    REQUIRE(luil::net::sniff_body_kind(bytes_of(png)) == luil::net::http_body_kind::image);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(jpeg)) == luil::net::http_body_kind::image);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(gif87a)) == luil::net::http_body_kind::image);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(gif89a)) == luil::net::http_body_kind::image);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(webp)) == luil::net::http_body_kind::image);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(bmp)) == luil::net::http_body_kind::image);
}

TEST_CASE("Sniffing does not read past a short buffer", "[net][media]")
{
    constexpr std::array<std::uint8_t, 12> png_prefix { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au, 0x00u, 0x00u, 0x00u, 0x0Du };
    constexpr std::array<std::uint8_t, 11> webp_prefix { 'R', 'I', 'F', 'F', 0x20u, 0x00u, 0x00u, 0x00u, 'W', 'E', 'B' };

    REQUIRE(luil::net::sniff_body_kind({}) == luil::net::http_body_kind::bytes);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(png_prefix).first(1)) == luil::net::http_body_kind::bytes);
    REQUIRE(luil::net::sniff_body_kind(bytes_of(png_prefix).first(3)) == luil::net::http_body_kind::bytes);
    // 열한 바이트짜리 webp는 `WEBP`의 마지막 글자가 모자란다 — 여덟 바이트 뒤를
    // 검사도 없이 읽으면 여기서 넘어간다.
    REQUIRE(luil::net::sniff_body_kind(bytes_of(webp_prefix)) == luil::net::http_body_kind::bytes);
}

TEST_CASE("Sniffing refuses json, lookalike RIFF and ico", "[net][media]")
{
    constexpr std::array<std::uint8_t, 7> json { '{', '"', 'a', '"', ':', '1', '}' };
    constexpr std::array<std::uint8_t, 12> wave { 'R', 'I', 'F', 'F', 0x20u, 0x00u, 0x00u, 0x00u, 'W', 'A', 'V', 'E' };
    constexpr std::array<std::uint8_t, 6> ico { 0x00u, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u };

    // `{`는 서명이 아니다. 그것으로 갈래를 정하면 아무 글이나 json이 된다.
    REQUIRE(luil::net::sniff_body_kind(bytes_of(json)) == luil::net::http_body_kind::bytes);
    // `RIFF`만 보고 넘기면 wav가 그림이 된다.
    REQUIRE(luil::net::sniff_body_kind(bytes_of(wave)) == luil::net::http_body_kind::bytes);
    // ico는 libpng 갈래에서만 열린다 — 답이 빌드 구성에 따라 달라지는 냄새는
    // 함정이지 편의가 아니다.
    REQUIRE(luil::net::sniff_body_kind(bytes_of(ico)) == luil::net::http_body_kind::bytes);
}
