#include "luil/ui/color_space.h"

#include "luil/text/utf8_text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace luil {
    namespace {
        // 16진 자리로 읽을 수 없는 글자의 자리표다. 16진 글자 어느 것과도 겹치지 않는다.
        constexpr char8_t foreign_character { u8'?' };
        constexpr std::array<char8_t, 16> hex_digits { u8'0', u8'1', u8'2', u8'3', u8'4', u8'5', u8'6', u8'7', u8'8', u8'9', u8'A', u8'B', u8'C', u8'D', u8'E', u8'F' };

        [[nodiscard]] float clamp_unit(const float value, const float fallback) noexcept
        {
            if (std::isnan(value))
                return fallback;
            return std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] bool is_space(const char32_t value) noexcept
        {
            return value == U' ' || value == U'\t' || value == U'\r' || value == U'\n' || value == 0x00A0u || value == 0x3000u;
        }

        // 16진 꼴의 글자를 ASCII로 옮긴다. 공백은 공백, 그 밖은 읽을 수 없는 글자다.
        [[nodiscard]] char8_t ascii_of(const char32_t value) noexcept
        {
            if ((value >= U'0' && value <= U'9') || (value >= U'a' && value <= U'f') || (value >= U'A' && value <= U'F') || value == U'#')
                return static_cast<char8_t>(value);
            // 전각 숫자·전각 영문 대소문자·전각 `#`다 (IME가 전각으로 확정한 글).
            if (value >= 0xFF10u && value <= 0xFF19u)
                return static_cast<char8_t>(u8'0' + (value - 0xFF10u));
            if (value >= 0xFF21u && value <= 0xFF26u)
                return static_cast<char8_t>(u8'A' + (value - 0xFF21u));
            if (value >= 0xFF41u && value <= 0xFF46u)
                return static_cast<char8_t>(u8'a' + (value - 0xFF41u));
            if (value == 0xFF03u)
                return u8'#';
            if (is_space(value))
                return u8' ';
            return foreign_character;
        }

        // 글 전체를 한 글자 한 바이트의 ASCII 꼴로 옮긴다. 깨진 바이트는 읽을 수 없는 글자다.
        [[nodiscard]] std::u8string ascii_form(const std::u8string_view text)
        {
            std::u8string result {};
            result.reserve(text.size());
            for (std::size_t at { 0 }; at < text.size();)
            {
                const text::utf8_decoded decoded { text::utf8_decode(text, at) };
                result += ascii_of(decoded.codepoint);
                at += decoded.length;
            }
            return result;
        }

        [[nodiscard]] int digit_of(const char8_t value) noexcept
        {
            if (value >= u8'0' && value <= u8'9')
                return value - u8'0';
            if (value >= u8'a' && value <= u8'f')
                return value - u8'a' + 10;
            if (value >= u8'A' && value <= u8'F')
                return value - u8'A' + 10;
            return -1;
        }

        [[nodiscard]] std::uint8_t channel_of(const double unit) noexcept
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(unit, 0.0, 1.0) * 255.0));
        }

        void append_byte(std::u8string& text, const std::uint32_t value)
        {
            text += hex_digits[(value >> 4u) & 0x0Fu];
            text += hex_digits[value & 0x0Fu];
        }
    }

    hsv_color clamp_hsv(const hsv_color color) noexcept
    {
        hsv_color result {};
        result.hue = std::isnan(color.hue) ? 0.0f : std::clamp(color.hue, 0.0f, 360.0f);
        result.saturation = clamp_unit(color.saturation, 0.0f);
        result.value = clamp_unit(color.value, 0.0f);
        result.alpha = clamp_unit(color.alpha, 1.0f);
        return result;
    }

    ui_color color_from_hsv(const hsv_color color) noexcept
    {
        const hsv_color clamped { clamp_hsv(color) };
        // 계산은 double이다. float로 하면 8비트 왕복의 가운데 값에서 반올림이 한 칸 어긋날 수 있다.
        const double hue { clamped.hue >= 360.0f ? 0.0 : static_cast<double>(clamped.hue) };
        const double saturation { clamped.saturation };
        const double value { clamped.value };
        // 표준 식이다. 채널 n(빨강 5, 초록 3, 파랑 1)마다 k = (n + h / 60) mod 6이고 값은 v - v s max(0, min(k, 4 - k, 1))다.
        const auto channel = [hue, saturation, value](const double n) {
            const double k { std::fmod(n + hue / 60.0, 6.0) };
            return value - value * saturation * std::max(0.0, std::min({ k, 4.0 - k, 1.0 }));
        };
        return make_ui_color(channel_of(channel(5.0)), channel_of(channel(3.0)), channel_of(channel(1.0)), channel_of(clamped.alpha));
    }

    hsv_color hsv_from_color(const ui_color color, const hsv_color& keep) noexcept
    {
        const hsv_color kept { clamp_hsv(keep) };
        const double red { static_cast<double>((color >> 16u) & 0xFFu) / 255.0 };
        const double green { static_cast<double>((color >> 8u) & 0xFFu) / 255.0 };
        const double blue { static_cast<double>(color & 0xFFu) / 255.0 };
        const double maximum { std::max({ red, green, blue }) };
        const double minimum { std::min({ red, green, blue }) };
        const double range { maximum - minimum };

        hsv_color result {};
        result.value = static_cast<float>(maximum);
        result.alpha = static_cast<float>(static_cast<double>((color >> 24u) & 0xFFu) / 255.0);
        // 검정은 색상과 채도가 정해지지 않는다. 회색은 색상만 정해지지 않는다. 둘 다 지금 값을 지킨다.
        if (maximum <= 0.0)
        {
            result.hue = kept.hue;
            result.saturation = kept.saturation;
            return result;
        }
        result.saturation = static_cast<float>(range / maximum);
        if (range <= 0.0)
        {
            result.hue = kept.hue;
            return result;
        }
        double hue { 0.0 };
        if (maximum == red)
            hue = std::fmod((green - blue) / range + 6.0, 6.0);
        else if (maximum == green)
            hue = (blue - red) / range + 2.0;
        else
            hue = (red - green) / range + 4.0;
        result.hue = static_cast<float>(hue * 60.0);
        return result;
    }

    hex_color_check check_hex_color(const std::u8string_view text, const bool alpha)
    {
        const std::u8string ascii { ascii_form(text) };
        const std::size_t first { ascii.find_first_not_of(u8' ') };
        if (first == std::u8string::npos)
            return { hex_color_problem::empty, std::nullopt };
        const std::size_t last { ascii.find_last_not_of(u8' ') };
        std::u8string_view digits { std::u8string_view { ascii }.substr(first, last - first + 1u) };
        if (digits.front() == u8'#')
            digits.remove_prefix(1u);
        // 16진 밖 글자가 자릿수보다 먼저다. "#12G"는 자릿수가 맞아도 쓸 수 없는 글이다.
        if (std::ranges::any_of(digits, [](const char8_t value) { return digit_of(value) < 0; }))
            return { hex_color_problem::not_hex, std::nullopt };
        const std::size_t length { digits.size() };
        const bool short_form { length == 3u || (alpha && length == 4u) };
        const bool long_form { length == 6u || (alpha && length == 8u) };
        if (short_form == false && long_form == false)
            return { hex_color_problem::wrong_length, std::nullopt };

        // 채널은 빨강·초록·파랑·알파 순이다. 짧은 꼴은 자리마다 두 번 늘린다.
        std::array<std::uint32_t, 4> channels { 0u, 0u, 0u, 0xFFu };
        const std::size_t count { short_form ? length : length / 2u };
        for (std::size_t index { 0 }; index < count; ++index)
        {
            if (short_form)
            {
                const auto digit { static_cast<std::uint32_t>(digit_of(digits[index])) };
                channels[index] = digit * 16u + digit;
            }
            else
                channels[index] = static_cast<std::uint32_t>(digit_of(digits[index * 2u]) * 16 + digit_of(digits[index * 2u + 1u]));
        }
        const ui_color color { channels[3] << 24u | channels[0] << 16u | channels[1] << 8u | channels[2] };
        return { hex_color_problem::none, color };
    }

    std::u8string format_hex_color(const ui_color color, const bool alpha)
    {
        std::u8string text { u8"#" };
        append_byte(text, color >> 16u);
        append_byte(text, color >> 8u);
        append_byte(text, color);
        if (alpha)
            append_byte(text, color >> 24u);
        return text;
    }

    text_insert_filter hex_color_filter()
    {
        return [](const std::u8string_view text) {
            std::u8string kept {};
            for (const char8_t value : ascii_form(text))
                if (value != u8' ' && value != foreign_character)
                    kept += value;
            return kept;
        };
    }
}
