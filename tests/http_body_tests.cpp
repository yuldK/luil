#include "luil/net/http_body.h"

#include "net/http_text_transcode.h"
#include "sample_image_bytes.h"
#include "luil/net/http_media_type.h"
#include "luil/net/http_message.h"
#include "luil/text/utf8_text.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using luil::net::http_body;
    using luil::net::http_body_kind;
    using luil::net::http_body_kind_source;
    using luil::net::http_body_parse_options;
    using luil::net::http_media_type;

    [[nodiscard]] std::vector<std::uint8_t> bytes_of(const auto& fixture)
    {
        return std::vector<std::uint8_t> { fixture.begin(), fixture.end() };
    }

    [[nodiscard]] std::vector<std::uint8_t> text_bytes(const std::u8string_view text)
    {
        return luil::net::http_text_body(text);
    }

    [[nodiscard]] http_media_type media_type(const std::u8string_view header)
    {
        return luil::net::parse_media_type(header);
    }

    [[nodiscard]] bool mentions(const std::u8string_view text, const std::u8string_view needle) noexcept
    {
        return text.find(needle) != std::u8string_view::npos;
    }

    // 깊이만 있는 json이다 (`[[[…]]]`).
    [[nodiscard]] std::vector<std::uint8_t> nested_array_bytes(const std::size_t depth)
    {
        std::u8string text {};
        text.reserve(depth * 2u);
        text.append(depth, u8'[');
        text.append(depth, u8']');
        return text_bytes(text);
    }

    [[nodiscard]] std::u8string number_text(std::size_t value)
    {
        if (value == 0)
            return std::u8string { u8"0" };

        std::u8string text {};
        while (value != 0)
        {
            text.insert(text.begin(), static_cast<char8_t>(static_cast<std::size_t>(u8'0') + value % 10u));
            value /= 10u;
        }
        return text;
    }

    // 깊이는 얕고 **너비**만 큰 json이다 (`{"k000000000000":0,…}`).
    // 이름을 일정한 폭으로 채우는 것은 문서 크기를 멤버 수로 정하기 위해서다.
    [[nodiscard]] std::vector<std::uint8_t> wide_object_bytes(const std::size_t members)
    {
        constexpr std::size_t name_digits { 12 };
        std::u8string text { u8"{" };
        text.reserve(members * 24u);
        for (std::size_t index { 0 }; index < members; ++index)
        {
            if (index != 0)
                text += u8",";
            const std::u8string number { number_text(index) };
            text += u8"\"k";
            text.append(name_digits - number.size(), u8'0');
            text += number;
            text += u8"\":";
            text += number;
        }
        text += u8"}";
        return text_bytes(text);
    }
} // namespace

TEST_CASE("An empty body is empty whatever the header says", "[net][body]")
{
    const http_body body { luil::net::decode_http_body({}, media_type(u8"application/json"), {}) };
    REQUIRE(body.kind == http_body_kind::empty);
    REQUIRE(body.kind_source == http_body_kind_source::absent);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.data().empty());
    REQUIRE(body.json == nullptr);
}

TEST_CASE("A declared json body parses into a value", "[net][body]")
{
    const http_body body { luil::net::decode_http_body(text_bytes(u8"{\"a\":1,\"b\":[2,3]}"), media_type(u8"application/json; charset=utf-8"), {}) };
    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.kind_source == http_body_kind_source::declared);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.json != nullptr);
    REQUIRE((*body.json)["a"] == 1);
    REQUIRE((*body.json)["b"][1] == 3);
    REQUIRE(body.parsed());
    // 바이트는 어느 경로에서도 그대로 실린다.
    REQUIRE(body.data().size() == 17u);
}

TEST_CASE("A broken json body keeps its kind and reports where it broke", "[net][body]")
{
    const std::vector<std::uint8_t> sent { text_bytes(u8"{\"a\":x}") };
    const http_body body { luil::net::decode_http_body(sent, media_type(u8"application/json"), {}) };
    // **갈래를 낮추지 않는다** — 갈래는 서버가 한 말이고 `parse_error`는 우리에게
    // 일어난 일이다.
    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.json == nullptr);
    REQUIRE(body.parsed() == false);
    // 자리가 남아 있어야 사람이 서버를 고칠 수 있다 — `allow_exceptions = false`를
    // 거절한 이유가 이 한 줄이다.
    REQUIRE(mentions(body.parse_error, u8"column"));
    // 실패를 사람에게 보일 때 필요한 것이 정확히 이 바이트다.
    REQUIRE(std::vector<std::uint8_t> { body.data().begin(), body.data().end() } == sent);
}

TEST_CASE("A json body may start with a byte order mark", "[net][body]")
{
    std::vector<std::uint8_t> sent { 0xEFu, 0xBBu, 0xBFu };
    const std::vector<std::uint8_t> payload { text_bytes(u8"{\"a\":1}") };
    sent.insert(sent.end(), payload.begin(), payload.end());

    const http_body body { luil::net::decode_http_body(sent, media_type(u8"application/json"), {}) };
    REQUIRE(body.json != nullptr);
    REQUIRE((*body.json)["a"] == 1);
    // 표식은 넘겼을 뿐 바이트에서 지우지 않았다.
    REQUIRE(body.data().size() == sent.size());
}

TEST_CASE("A json body larger than the parse limit is not parsed", "[net][body]")
{
    http_body_parse_options options {};
    options.max_json_bytes = 4u;

    const http_body body { luil::net::decode_http_body(text_bytes(u8"{\"a\":1}"), media_type(u8"application/json"), options) };
    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.json == nullptr);
    REQUIRE(body.parse_error.empty() == false);
    // 64 MiB를 받는 것은 되어도 64 MiB를 DOM으로 펴는 것은 아니다 — 바이트는 남는다.
    REQUIRE(body.data().size() == 7u);
}

TEST_CASE("A json body nested deeper than the limit is refused, not built", "[net][body]")
{
    const std::vector<std::uint8_t> deep { nested_array_bytes(300u) };

    http_body_parse_options refused {};
    refused.max_json_depth = 256;
    const http_body rejected { luil::net::decode_http_body(deep, media_type(u8"application/json"), refused) };
    // 짓지 않았으므로 버릴 것도 없다 — 상한이 파서가 아니라 **파괴자** 때문에 있다.
    REQUIRE(rejected.kind == http_body_kind::json);
    REQUIRE(rejected.json == nullptr);
    REQUIRE(rejected.parse_error.empty() == false);

    http_body_parse_options allowed {};
    allowed.max_json_depth = 512;
    const http_body accepted { luil::net::decode_http_body(deep, media_type(u8"application/json"), allowed) };
    REQUIRE(accepted.json != nullptr);
    REQUIRE(accepted.parse_error.empty());
}

TEST_CASE("A wide json document does not pay for the depth check", "[net][body]")
{
    // 멤버 2만 개짜리 평평한 객체다 (~420 KB). 깊이 검사가 문서를 한 바퀴 도는
    // O(n)이 아니라 값마다 되돌아보는 꼴이면 **너비**에 상한이 걸려 이 문서가
    // 초 단위로 늘어진다 (nlohmann의 `parser_callback_t`를 버린 이유가 그것이다).
    //  - 재는 것은 차수이지 절대 시각이 아니라 상한을 아주 넉넉히 잡는다.
    //    느린 CI 기계에서 흔들리는 값으로는 무엇도 잠글 수 없다.
    const std::vector<std::uint8_t> wide { wide_object_bytes(20000u) };
    REQUIRE(wide.size() > 400u * 1024u);

    const auto started { std::chrono::steady_clock::now() };
    const http_body body { luil::net::decode_http_body(wide, media_type(u8"application/json"), {}) };
    const auto elapsed { std::chrono::steady_clock::now() - started };

    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.json != nullptr);
    REQUIRE(body.json->size() == 20000u);
    REQUIRE(elapsed < std::chrono::seconds { 2 });
}

TEST_CASE("Brackets inside a json string are not nesting", "[net][body]")
{
    http_body_parse_options options {};
    options.max_json_depth = 3;

    // 따옴표 안의 `[`를 세면 성한 문서 하나가 문자열 때문에 거절된다 — 상한이
    // 잡으려는 것은 파괴자를 재귀로 모는 **구조**이지 글자가 아니다.
    const http_body body { luil::net::decode_http_body(text_bytes(u8"{\"s\":\"[[[[[[[[[[\"}"), media_type(u8"application/json"), options) };
    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.json != nullptr);
    REQUIRE((*body.json)["s"] == "[[[[[[[[[[");
}

TEST_CASE("An empty text from the transcoder is a value, not a failure", "[net][body]")
{
    // BOM만 든 utf-16 몸통이 이 자리다. 이유 없이 빈 글이면 갈고리가 정말 빈 글을
    // 답한 것이라 슬롯이 서야 한다 — 비워 두면 "풀지 못했는데 이유도 없다"가 되고
    // 화면은 그 둘을 가르지 못한다.
    const auto hook = [](const std::span<const std::uint8_t>, const std::u8string_view, std::u8string&) { return std::u8string {}; };

    const std::vector<std::uint8_t> byte_order_mark { 0xFFu, 0xFEu };
    const http_body body { luil::net::decode_http_body(byte_order_mark, media_type(u8"text/plain; charset=utf-16"), {}, hook) };
    REQUIRE(body.kind == http_body_kind::text);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.text != nullptr);
    REQUIRE(body.as_text().empty());
    REQUIRE(body.parsed());
}

TEST_CASE("A declared png decodes into a single frame image", "[net][body]")
{
    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_quadrant_png), media_type(u8"image/png"), {}) };
    REQUIRE(body.kind == http_body_kind::image);
    REQUIRE(body.kind_source == http_body_kind_source::declared);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.image.valid());
    REQUIRE(body.image.frame_count() == 1u);
    REQUIRE(body.image.animated() == false);
    REQUIRE(body.still_image().width() == 2);
    REQUIRE(body.still_image().height() == 2);
}

TEST_CASE("An animated gif decodes into every frame", "[net][body]")
{
    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_animated_gif), media_type(u8"image/gif"), {}) };
    REQUIRE(body.image.valid());
    REQUIRE(body.image.frame_count() == 3u);
    REQUIRE(body.image.animated());
}

TEST_CASE("An animated gif decodes into one frame when the app asks for stills", "[net][body]")
{
    http_body_parse_options options {};
    options.image.animated = false;

    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_animated_gif), media_type(u8"image/gif"), options) };
    REQUIRE(body.image.valid());
    REQUIRE(body.image.frame_count() == 1u);
    REQUIRE(body.image.animated() == false);
    REQUIRE(body.still_image().valid());
}

TEST_CASE("The decode limits reach the image decoder", "[net][body]")
{
    http_body_parse_options options {};
    options.image.decode.max_width = 8;

    // 64×32짜리 견본이다. 상한이 흘러가지 않으면 원본 크기가 그대로 나온다.
    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_solid_png), media_type(u8"image/png"), options) };
    REQUIRE(body.image.valid());
    REQUIRE(body.still_image().width() == 8);
    REQUIRE(body.still_image().height() == 4);
}

TEST_CASE("A broken image keeps its kind and carries the decoder's reason", "[net][body]")
{
    constexpr std::array<std::uint8_t, 8> garbage { 0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u };

    const http_body body { luil::net::decode_http_body(bytes_of(garbage), media_type(u8"image/png"), {}) };
    REQUIRE(body.kind == http_body_kind::image);
    REQUIRE(body.image.valid() == false);
    REQUIRE(body.parsed() == false);
    REQUIRE(body.parse_error.empty() == false);
    REQUIRE(body.data().size() == 8u);
}

TEST_CASE("A utf-8 text body is read as it stands", "[net][body]")
{
    const http_body body { luil::net::decode_http_body(text_bytes(u8"한글 본문"), media_type(u8"text/plain; charset=utf-8"), {}) };
    REQUIRE(body.kind == http_body_kind::text);
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.as_text() == u8"한글 본문");
    REQUIRE(body.parsed());
}

TEST_CASE("A byte order mark leaves the text but stays in the bytes", "[net][body]")
{
    std::vector<std::uint8_t> sent { 0xEFu, 0xBBu, 0xBFu };
    const std::vector<std::uint8_t> payload { text_bytes(u8"hello") };
    sent.insert(sent.end(), payload.begin(), payload.end());

    const http_body body { luil::net::decode_http_body(sent, media_type(u8"text/plain"), {}) };
    REQUIRE(body.as_text() == u8"hello");
    REQUIRE(body.data().size() == 8u);
    REQUIRE(body.data()[0] == 0xEFu);
}

TEST_CASE("Invalid utf-8 is replaced instead of thrown away", "[net][body]")
{
    // 0xFF는 UTF-8의 어느 자리에도 오지 않는 바이트다.
    const std::vector<std::uint8_t> sent { 'a', 0xFFu, 'b' };

    const http_body body { luil::net::decode_http_body(sent, media_type(u8"text/plain; charset=utf-8"), {}) };
    REQUIRE(body.kind == http_body_kind::text);
    // 빈 글로 돌려주면 화면에서 "빈 응답"과 구별되지 않는다.
    REQUIRE(body.as_text().empty() == false);
    REQUIRE(luil::text::utf8_is_valid(body.as_text()));
    REQUIRE(mentions(body.as_text(), luil::text::utf8_encode(luil::text::utf8_replacement_character)));
    REQUIRE(body.parse_error.empty() == false);
    REQUIRE(body.data().size() == 3u);
}

TEST_CASE("An unknown charset without a transcoder names itself and keeps the bytes", "[net][body]")
{
    constexpr std::array<std::uint8_t, 4> korean { 0xC7u, 0xD1u, 0xB1u, 0xDBu };

    const http_body body { luil::net::decode_http_body(bytes_of(korean), media_type(u8"text/plain; charset=euc-kr"), {}) };
    REQUIRE(body.kind == http_body_kind::text);
    REQUIRE(body.as_text().empty());
    REQUIRE(mentions(body.parse_error, u8"euc-kr"));
    // 바이트가 온전히 남으므로 앱이 자기 규칙으로 읽을 수 있다.
    REQUIRE(body.data().size() == 4u);
}

TEST_CASE("The transcoder hook receives the declared charset", "[net][body]")
{
    constexpr std::array<std::uint8_t, 4> korean { 0xC7u, 0xD1u, 0xB1u, 0xDBu };

    std::u8string seen {};
    std::size_t seen_bytes { 0 };
    const auto hook = [&seen, &seen_bytes](const std::span<const std::uint8_t> bytes, const std::u8string_view charset, std::u8string&) {
        seen = std::u8string { charset };
        seen_bytes = bytes.size();
        return std::u8string { u8"옮긴 글" };
    };

    const http_body body { luil::net::decode_http_body(bytes_of(korean), media_type(u8"text/plain; charset=EUC-KR"), {}, hook) };
    REQUIRE(seen == u8"euc-kr");
    REQUIRE(seen_bytes == 4u);
    REQUIRE(body.as_text() == u8"옮긴 글");
    REQUIRE(body.parse_error.empty());
}

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

TEST_CASE("The default transcoder reads utf-16 by hand", "[net][body]")
{
    // `MultiByteToWideChar`가 1200·1201을 받지 않아 이 경로만 우리 코드다.
    // "한" (U+D55C)을 BOM 뒤에 little endian으로 적었다.
    const std::vector<std::uint8_t> little { 0xFFu, 0xFEu, 0x5Cu, 0xD5u };
    const http_body decoded { luil::net::decode_http_body(little, media_type(u8"text/plain; charset=utf-16"), {}, luil::net::codepage_text_transcoder()) };
    REQUIRE(decoded.parse_error.empty());
    REQUIRE(decoded.as_text() == u8"한");

    // 바이트 차례를 말하지 않은 이름에서는 **BOM이 표를 이긴다.**
    const std::vector<std::uint8_t> big { 0xFEu, 0xFFu, 0xD5u, 0x5Cu };
    const http_body swapped { luil::net::decode_http_body(big, media_type(u8"text/plain; charset=utf-16"), {}, luil::net::codepage_text_transcoder()) };
    REQUIRE(swapped.as_text() == u8"한");

    // 홀수 길이는 잘린 것이라 거절한다 — 반쪽을 버리고 넘어가면 "원래 그런 답"으로
    // 보인다.
    const std::vector<std::uint8_t> truncated { 0x5Cu, 0xD5u, 0x00u };
    const http_body broken { luil::net::decode_http_body(truncated, media_type(u8"text/plain; charset=utf-16le"), {}, luil::net::codepage_text_transcoder()) };
    REQUIRE(broken.as_text().empty());
    REQUIRE(broken.parse_error.empty() == false);
}

TEST_CASE("The default transcoder refuses a charset it does not know", "[net][body]")
{
    constexpr std::array<std::uint8_t, 3> some { 'a', 'b', 'c' };

    const http_body body { luil::net::decode_http_body(bytes_of(some), media_type(u8"text/plain; charset=x-made-up"), {}, luil::net::codepage_text_transcoder()) };
    REQUIRE(body.as_text().empty());
    REQUIRE(mentions(body.parse_error, u8"x-made-up"));
}

TEST_CASE("html keeps its bytes and charset and is not turned into text", "[net][body]")
{
    const http_media_type declared { media_type(u8"text/html; charset=euc-kr") };
    const http_body body { luil::net::decode_http_body(text_bytes(u8"<p>hi</p>"), declared, {}) };
    REQUIRE(body.kind == http_body_kind::html);
    REQUIRE(body.data().empty() == false);
    // webview가 받을 것은 바이트와 charset이다. 미리 옮겨 두면 파서가 쓸 charset을
    // 버리는 셈이고 문서가 메모리에서 두 벌이 된다.
    REQUIRE(body.as_text().empty());
    REQUIRE(body.parse_error.empty());
    REQUIRE(body.parsed() == false);
    REQUIRE(declared.charset == u8"euc-kr");
}

TEST_CASE("html fills the text only when the app asks to look at it", "[net][body]")
{
    http_body_parse_options options {};
    options.html_as_text = true;

    const http_body body { luil::net::decode_http_body(text_bytes(u8"<p>안녕</p>"), media_type(u8"text/html; charset=utf-8"), options) };
    REQUIRE(body.kind == http_body_kind::html);
    REQUIRE(body.as_text() == u8"<p>안녕</p>");
}

TEST_CASE("A body with no media type is sniffed", "[net][body]")
{
    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_quadrant_png), media_type(u8""), {}) };
    REQUIRE(body.kind == http_body_kind::image);
    REQUIRE(body.kind_source == http_body_kind_source::sniffed);
    REQUIRE(body.image.valid());
}

TEST_CASE("A declared media type is obeyed even when the bytes disagree", "[net][body]")
{
    // png 바이트에 `text/plain`이라 적은 서버는 그렇게 하기로 한 것이다. 그 말을
    // 뒤집는 것이 브라우저들이 십수 년 값을 치른 길이다.
    const http_body body { luil::net::decode_http_body(bytes_of(luil::testing::sample_quadrant_png), media_type(u8"text/plain"), {}) };
    REQUIRE(body.kind == http_body_kind::text);
    REQUIRE(body.kind_source == http_body_kind_source::declared);
    REQUIRE(body.image.valid() == false);
}

TEST_CASE("assume_kind beats both the header and the bytes", "[net][body]")
{
    http_body_parse_options options {};
    options.assume_kind = http_body_kind::json;

    // json을 `text/plain`으로 내는 API가 흔하다 — 라이브러리가 몰래 고치는 것과
    // 앱이 시켜서 하는 것은 성격이 아주 다르다.
    const http_body body { luil::net::decode_http_body(text_bytes(u8"{\"a\":1}"), media_type(u8"text/plain"), options) };
    REQUIRE(body.kind == http_body_kind::json);
    REQUIRE(body.kind_source == http_body_kind_source::assumed);
    REQUIRE(body.json != nullptr);
    REQUIRE((*body.json)["a"] == 1);
}

TEST_CASE("A default constructed body answers without dangling", "[net][body]")
{
    const http_body body {};
    REQUIRE(body.kind == http_body_kind::empty);
    REQUIRE(body.kind_source == http_body_kind_source::absent);
    REQUIRE(body.data().empty());
    REQUIRE(body.as_text().empty());
    REQUIRE(body.parsed() == false);
    // 빈 이미지를 답하는 계약이라 부르는 쪽이 범위를 검사하지 않는다.
    REQUIRE(body.still_image().valid() == false);
    REQUIRE(body.still_image().width() == 0);
}
