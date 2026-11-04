#pragma once

#include "win32/utf8.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luil::win32 {
    // OS가 준 값을 바꾸는 순수 함수다. test가 PC의 지역 설정과 무관하게 친다 (docs/i18n-adoption-design.md).

    // `LOCALE_IFIRSTDAYOFWEEK`의 값을 요일로 바꾼다. 0이 월요일, 6이 일요일이다. 그 밖의 값은 빈 값이다.
    [[nodiscard]] inline std::optional<std::chrono::weekday> weekday_from_locale_first_day(const std::uint32_t value) noexcept
    {
        if (value > 6u)
            return std::nullopt;
        // `weekday`의 생성자는 0을 일요일로 읽는다.
        return std::chrono::weekday { (value + 1u) % 7u };
    }

    // `GetUserPreferredUILanguages`의 목록(이름마다 NUL, 끝에 NUL 하나 더)을 태그로 나눈다. 앞이 먼저다.
    //  - 빈 이름에서 멈춘다. UTF-8로 바꾸지 못한 이름은 건너뛴다.
    [[nodiscard]] inline std::vector<std::u8string> language_tags_from_multi_string(const std::wstring_view list)
    {
        std::vector<std::u8string> tags {};
        std::size_t begin { 0 };
        while (begin < list.size())
        {
            const std::size_t end { list.find(L'\0', begin) };
            const std::wstring_view name { end == std::wstring_view::npos ? list.substr(begin) : list.substr(begin, end - begin) };
            if (name.empty())
                break;
            if (auto converted { utf16_to_utf8(name).value }; converted.has_value())
                tags.push_back(std::move(*converted));
            if (end == std::wstring_view::npos)
                break;
            begin = end + 1u;
        }
        return tags;
    }
}
