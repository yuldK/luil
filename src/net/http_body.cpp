#include "luil/net/http_body.h"

#include "luil/text/utf8_text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace luil::net {
    namespace {
        // 코덱과 nlohmann이 내주는 이유는 전부 ASCII다. 한 바이트씩 옮기면 그만이고,
        // 그것이 이 저장소가 이미 쓰는 길이다 (src/ui/image_decode.cpp의
        // `append_ascii`, src/win32/win32_error.cpp).
        //  - 밖에서 온 글이 섞일 수 있으므로 ASCII 밖은 `?`로 눕힌다. 이 글은
        //    `parse_error`를 거쳐 그리기까지 가고, 그리기는 깨진 UTF-8에 죽는다
        //    (text/utf8_text.h).
        void append_ascii(std::u8string& output, const std::string_view text)
        {
            for (const char character : text)
            {
                const auto byte { static_cast<std::uint8_t>(character) };
                output.push_back(byte >= 0x20u && byte < 0x7Fu ? static_cast<char8_t>(byte) : u8'?');
            }
        }

        void append_number(std::u8string& output, const std::size_t value)
        {
            std::array<char, 24> buffer {};
            const std::to_chars_result conversion { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            if (conversion.ec == std::errc {})
                append_ascii(output, std::string_view { buffer.data(), static_cast<std::size_t>(conversion.ptr - buffer.data()) });
        }

        constexpr std::array<std::uint8_t, 3> utf8_byte_order_mark { 0xEFu, 0xBBu, 0xBFu };

        // BOM은 값이 아니라 표식이라 어느 갈래에서든 넘긴다. **`bytes`에는 남는다** —
        // 받은 것을 그대로 들고 있는 것이 그 슬롯의 계약이다.
        [[nodiscard]] std::span<const std::uint8_t> without_bom(const std::span<const std::uint8_t> bytes) noexcept
        {
            if (bytes.size() < utf8_byte_order_mark.size())
                return bytes;
            for (std::size_t index { 0 }; index < utf8_byte_order_mark.size(); ++index)
                if (bytes[index] != utf8_byte_order_mark[index])
                    return bytes;
            return bytes.subspan(utf8_byte_order_mark.size());
        }

        [[nodiscard]] std::u8string_view text_view(const std::span<const std::uint8_t> bytes) noexcept
        {
            return std::u8string_view { reinterpret_cast<const char8_t*>(bytes.data()), bytes.size() };
        }

        // charset을 말하지 않았거나 UTF-8 갈래로 적었는가.
        //  - **옛 HTTP의 latin-1 기본값은 따르지 않는다.** 요즘 서버에서 charset을
        //    빠뜨린 몸은 거의 언제나 UTF-8이고, latin-1로 읽으면 한글 한 글자가
        //    세 글자로 늘어난 채 **올바른 UTF-8**이 되어 아무도 실패를 못 본다.
        [[nodiscard]] bool is_utf8_charset(const std::u8string_view charset) noexcept
        {
            return charset.empty() || charset == u8"utf-8" || charset == u8"utf8" || charset == u8"us-ascii" || charset == u8"ascii";
        }

        // 문서의 가장 깊은 중첩이다 — 문자열 안의 괄호는 세지 않는다 (`\"`·`\\`를
        // 건너뛰며 따옴표를 짝짓는다). 잘못된 JSON이어도 답을 낸다: 그것을 가리는 것은
        // 파서의 몫이고, 여기는 파서를 부를 만한 문서인지만 본다. O(n) 한 바퀴다.
        [[nodiscard]] std::size_t json_nesting_depth(const std::span<const std::uint8_t> bytes) noexcept
        {
            std::size_t depth { 0 };
            std::size_t deepest { 0 };
            bool in_string { false };
            bool escaped { false };
            for (const std::uint8_t byte : bytes)
            {
                if (in_string)
                {
                    if (escaped)
                        escaped = false;
                    else if (byte == '\\')
                        escaped = true;
                    else if (byte == '"')
                        in_string = false;
                    continue;
                }
                if (byte == '"')
                    in_string = true;
                else if (byte == '[' || byte == '{')
                    deepest = std::max(deepest, ++depth);
                else if ((byte == ']' || byte == '}') && depth > 0)
                    --depth;
            }
            return deepest;
        }

        void fill_text(http_body& body, const std::span<const std::uint8_t> bytes, const http_media_type& content_type, const http_text_transcoder& transcode)
        {
            if (is_utf8_charset(content_type.charset))
            {
                const std::u8string_view view { text_view(without_bom(bytes)) };
                if (text::utf8_is_valid(view))
                {
                    body.text = std::make_shared<const std::u8string>(view);
                    return;
                }
                // 깨진 것을 빈 글로 돌려주면 화면에서 "빈 응답"과 구별되지 않는다.
                // 글은 담고 원문과 다르다는 사실만 남긴다 (`http_body::parse_error`의
                // 계약이 정확히 이 경우를 적어 두었다).
                body.text = std::make_shared<const std::u8string>(text::utf8_replace_invalid(view));
                body.parse_error = u8"The body is not valid UTF-8; invalid bytes were replaced.";
                return;
            }

            if (transcode == nullptr)
            {
                // 갈고리가 없으면 옮기지 않고 이름만 적는다. 바이트는 온전히 남으므로
                // 앱이 자기 규칙으로 읽을 수 있다 (http-client-design.md).
                std::u8string message { u8"No transcoder for charset '" };
                for (const char8_t byte : content_type.charset)
                    message.push_back(byte >= 0x20u && byte < 0x7Fu ? byte : u8'?');
                message += u8"'.";
                body.parse_error = std::move(message);
                return;
            }

            std::u8string error {};
            std::u8string text { transcode(bytes, content_type.charset, error) };
            if (error.empty() == false)
                body.parse_error = std::move(error);
            // 빈 글은 이유가 있을 때만 실패다. 이유 없이 비었으면 갈고리가 정말 빈
            // 글을 답한 것이고(BOM만 든 utf-16 같은 것), 그것은 빈 글이라는 값이다 —
            // 슬롯을 비워 두면 "풀지 못했는데 이유도 없다"가 된다.
            if (text.empty() && body.parse_error.empty() == false)
                return;
            // 갈고리는 밖에서 오는 구현이라 계약을 믿되 검사한다 — 공개
            // `std::u8string`이 깨져 있으면 그리기가 죽는다.
            if (text::utf8_is_valid(text) == false)
            {
                text = text::utf8_replace_invalid(text);
                if (body.parse_error.empty())
                    body.parse_error = u8"The transcoder returned invalid UTF-8; invalid bytes were replaced.";
            }
            body.text = std::make_shared<const std::u8string>(std::move(text));
        }

        void fill_json(http_body& body, const std::span<const std::uint8_t> bytes, const http_body_parse_options& options)
        {
            if (bytes.size() > options.max_json_bytes)
            {
                // 64 MiB를 받는 것은 되어도 64 MiB를 DOM으로 펴는 것은 아니다.
                std::u8string message { u8"The JSON body is larger than the parse limit (" };
                append_number(message, bytes.size());
                message += u8" > ";
                append_number(message, options.max_json_bytes);
                message += u8" bytes).";
                body.parse_error = std::move(message);
                return;
            }

            const std::span<const std::uint8_t> payload { without_bom(bytes) };

            // **깊이 상한은 파서가 아니라 파괴자 때문에 있다.** nlohmann의 파싱은 제
            // 스택을 쓰지만 깊게 중첩된 값을 **버릴 때**는 재귀라, 십만 겹을 보낸
            // 서버가 우리 stack을 넘긴다. 그래서 짓기 **전에** 잰다 — 다 짓고 나서
            // 세는 검사로는 막지 못하는 자리다.
            //  - nlohmann의 `parser_callback_t`로 가지를 안 짓는 길은 버렸다. 그
            //    콜백을 걸면 파서가 사건마다 지은 값을 되돌아보아, 형제가 많은 보통
            //    문서에서 O(n²)이 된다 — 깊이가 아니라 **너비**에 걸리는 상한이었다.
            const std::size_t limit { static_cast<std::size_t>(options.max_json_depth < 0 ? 0 : options.max_json_depth) };
            if (json_nesting_depth(payload) > limit)
            {
                std::u8string message { u8"The JSON body is nested deeper than the limit (" };
                append_number(message, limit);
                message += u8" levels).";
                body.parse_error = std::move(message);
                return;
            }

            try
            {
                const auto* const first { reinterpret_cast<const char*>(payload.data()) };
                nlohmann::json value { nlohmann::json::parse(first, first + payload.size()) };
                body.json = std::make_shared<const nlohmann::json>(std::move(value));
            }
            // 예외가 값으로 바뀌는 **유일한 자리**다. `allow_exceptions = false`는 값은
            // 주지만 이유를 못 준다 ("22째 바이트"가 사라진다) — 그 한 줄이 사람이
            // 서버를 고칠 수 있는지를 가른다 (http-client-design.md).
            catch (const nlohmann::json::exception& failure)
            {
                std::u8string message { u8"Failed to parse the JSON body (" };
                append_ascii(message, failure.what());
                message += u8").";
                body.parse_error = std::move(message);
            }
            catch (const std::exception& failure)
            {
                std::u8string message { u8"Failed to parse the JSON body (" };
                append_ascii(message, failure.what());
                message += u8").";
                body.parse_error = std::move(message);
            }
            catch (...)
            {
                body.parse_error = u8"Failed to parse the JSON body.";
            }
        }

        void fill_image(http_body& body, const std::span<const std::uint8_t> bytes, const http_image_options& options)
        {
            image_decode_error error {};
            if (options.animated)
            {
                body.image = decode_animated_image_bytes(bytes, options.decode, error);
            }
            else
            {
                // 한 장짜리 값으로 담는다. 표시 시간은 0으로 넘겨 `make_animated_image`가
                // 다듬게 둔다 — 그 규칙이 사는 자리가 거기 하나여야 재생 판정이
                // 규칙을 다시 알 필요가 없다 (image_element.h).
                const ui_image single { decode_image_bytes(bytes, options.decode, error) };
                if (single.valid())
                {
                    const std::array<ui_image, 1> frames { single };
                    const std::array<std::chrono::milliseconds, 1> durations { std::chrono::milliseconds { 0 } };
                    body.image = make_animated_image(frames, durations, 1);
                }
            }
            // 실패해도 갈래는 `image`다 — 갈래는 서버가 한 말이고 `parse_error`는
            // 우리에게 일어난 일이다 (http-client-design.md).
            //  - **그림이 섰는지를 보고 가리지 않는다.** 디코더는
            //    `image_decode_options::incomplete`가 `accept`면 잘린 그림과 그
            //    사실을 함께 답하고(image_decode.h), 그 짝이 곧 이 슬롯의 계약이다
            //    ("비어 있지 않다고 값이 없는 것은 아니다" — http_body.h:118-123).
            //    갈래는 `image_decode_error::kind`가 들고 있으므로 여기서 잃는 것은
            //    없다: 몸은 언제나 바이트를 그대로 들고 있어 앱이 다시 풀 수 있다.
            if (error.empty() == false)
                body.parse_error = std::move(error.message);
        }

        [[nodiscard]] http_body_kind decide_kind(const http_body& body, const http_media_type& content_type, const http_body_parse_options& options, http_body_kind_source& source) noexcept
        {
            if (options.assume_kind.has_value())
            {
                source = http_body_kind_source::assumed;
                return *options.assume_kind;
            }
            if (media_type_invites_sniffing(content_type))
            {
                source = http_body_kind_source::sniffed;
                return sniff_body_kind(body.data());
            }
            source = http_body_kind_source::declared;
            return classify_media_type(content_type);
        }
    } // namespace

    http_body decode_http_body(std::vector<std::uint8_t> bytes, const http_media_type& content_type, const http_body_parse_options& options)
    {
        return decode_http_body(std::move(bytes), content_type, options, http_text_transcoder {});
    }

    http_body decode_http_body(std::vector<std::uint8_t> bytes, const http_media_type& content_type, const http_body_parse_options& options, const http_text_transcoder& transcode)
    {
        http_body body {};
        // **어느 경로에서도 바이트가 결과에 실린다.** 갈래를 정하기 전에 옮겨 두면
        // 이 뒤로 빠뜨릴 자리가 없다.
        body.bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));

        // 비어 있음이 형식 글보다 먼저다 (204·304·HEAD가 무엇이라 적혀 오든).
        if (body.bytes->empty())
            return body;

        body.kind = decide_kind(body, content_type, options, body.kind_source);
        switch (body.kind)
        {
        case http_body_kind::json:
            fill_json(body, body.data(), options);
            break;
        case http_body_kind::image:
            fill_image(body, body.data(), options.image);
            break;
        case http_body_kind::text:
            fill_text(body, body.data(), content_type, transcode);
            break;
        case http_body_kind::html:
            // 파싱하지 않고 `text`도 채우지 않는다. 남기는 것은 바이트와 charset뿐이고
            // 그 둘이 뒷날 webview가 받을 것이다 (http-client-design.md).
            if (options.html_as_text)
                fill_text(body, body.data(), content_type, transcode);
            break;
        case http_body_kind::empty:
        case http_body_kind::bytes:
            break;
        }
        return body;
    }
} // namespace luil::net
