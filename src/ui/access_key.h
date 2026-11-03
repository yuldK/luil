#pragma once

#include "luil/ui/ui_events.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 접근 키 글자(`menu_bar_item_config::access_key`, `menu_item_config::access_key`)를 다루는 규칙 한 벌이다.
    // 막대 요소, 메뉴 항목, controller의 메뉴 키 경로가 같은 판정을 쓴다.

    // 그리는 글자다. 영문자는 대문자로 올리고, 영문자·숫자가 아니면 0이다 (접근 키가 없다).
    [[nodiscard]] constexpr char32_t access_key_glyph(const char32_t access_key) noexcept
    {
        if (access_key >= U'a' && access_key <= U'z')
            return access_key - U'a' + U'A';
        if ((access_key >= U'A' && access_key <= U'Z') || (access_key >= U'0' && access_key <= U'9'))
            return access_key;
        return 0;
    }

    // 접근 키가 맞추는 이름 키다. 이름은 미국 배열 자리라 입력기가 켜져 있어도 같은 키가 맞는다. 없으면 none이다.
    [[nodiscard]] constexpr key_code access_key_code(const char32_t access_key) noexcept
    {
        const char32_t glyph { access_key_glyph(access_key) };
        return glyph == 0 ? key_code::none : alphanumeric_key_code(static_cast<std::uint32_t>(glyph));
    }

    // 그 키가 접근 키 자리(영문자·숫자 이름 키)인가.
    [[nodiscard]] constexpr bool access_key_candidate(const key_code key) noexcept
    {
        if (key == key_code::none || static_cast<std::uint32_t>(key) >= static_cast<std::uint32_t>(key_code::first_platform_key))
            return false;
        const std::uint32_t virtual_key { platform_key_of(key) };
        return (virtual_key >= 'A' && virtual_key <= 'Z') || (virtual_key >= '0' && virtual_key <= '9');
    }

    // 라벨 안에서 접근 키 글자가 처음 선 자리(바이트)다. 대소문자를 가리지 않는다. 없으면 nullopt다.
    //  - 접근 키는 ASCII라 UTF-8 여러 바이트 글자의 가운데와 맞을 일이 없다.
    [[nodiscard]] inline std::optional<std::size_t> access_key_offset(const std::u8string_view label, const char32_t access_key) noexcept
    {
        const char32_t glyph { access_key_glyph(access_key) };
        if (glyph == 0)
            return std::nullopt;
        for (std::size_t index = 0; index < label.size(); ++index)
        {
            const char32_t character { label[index] >= u8'a' && label[index] <= u8'z' ? static_cast<char32_t>(label[index] - u8'a' + u8'A') : static_cast<char32_t>(label[index]) };
            if (character == glyph)
                return index;
        }
        return std::nullopt;
    }

    // 그릴 글이다. 글자가 라벨에 없으면 뒤에 "(F)"를 붙인다. 접근 키가 없으면 라벨 그대로다.
    //  - 폭을 재는 자리와 그리는 자리가 같은 글을 쓴다.
    [[nodiscard]] inline std::u8string access_key_display(const std::u8string_view label, const char32_t access_key)
    {
        const char32_t glyph { access_key_glyph(access_key) };
        std::u8string text { label };
        if (glyph != 0 && access_key_offset(label, access_key).has_value() == false)
        {
            text += u8'(';
            text += static_cast<char8_t>(glyph);
            text += u8')';
        }
        return text;
    }

    // 그린 글 안에서 밑줄을 그을 글자의 자리(바이트)다. 라벨 안이 먼저이고 없으면 덧붙인 괄호 안이다.
    [[nodiscard]] inline std::optional<std::size_t> access_key_underline(const std::u8string_view label, const char32_t access_key) noexcept
    {
        if (const std::optional<std::size_t> offset { access_key_offset(label, access_key) }; offset.has_value())
            return offset;
        if (access_key_glyph(access_key) == 0)
            return std::nullopt;
        return label.size() + 1;
    }
}
