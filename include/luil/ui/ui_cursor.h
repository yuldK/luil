#pragma once

#include <cstdint>

namespace luil {
    // 포인터 모양이다.
    // element가 자기 자리의 모양을 알려 주고, platform이 실제 커서로 옮긴다.
    //
    // 라이브러리는 흔한 모양만 예약하고 64 이상은 앱이 정의한다
    // (`ui_element_kind`와 같은 규약이다). 앱이 정의한 값의 실제 커서는
    // 소비자가 `window_config::resolve_cursor`로 준다.
    enum class ui_cursor : std::uint32_t
    {
        // 모양을 정하지 않았다는 뜻이다.
        // 위쪽 element나 창의 기본 모양을 따른다.
        inherit = 0,
        arrow,
        // 글자를 고를 수 있는 자리다.
        text,
        // 누를 수 있는 자리다.
        hand,
        // 잡아 끌 수 있는 자리다.
        grab,
        // 지금 잡고 끄는 중이다.
        grabbing,
        resize_horizontal,
        resize_vertical,
        not_allowed,
        wait,
        // 앱이 정의하는 모양은 이 값부터다.
        // 라이브러리가 예약 대역을 넓혀도 기존 앱 상수가 밀리지 않도록 여유를 둔다.
        first_application_cursor = 64,
    };

    [[nodiscard]] constexpr ui_cursor application_cursor(const std::uint32_t index) noexcept
    {
        return static_cast<ui_cursor>(static_cast<std::uint32_t>(ui_cursor::first_application_cursor) + index);
    }
} // namespace luil
