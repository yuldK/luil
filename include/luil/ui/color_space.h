#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/ui_interaction.h"

#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 색 고르기가 쓰는 색 계산이다 (docs/color-space-design.md). 모두 플랫폼을 모르는 순수 함수다.
    //
    // 앱은 고르는 동안의 색을 `hsv_color`(실수 넷)로 들고, 8비트 색(`ui_color`)은 내보낼 때만 `color_from_hsv`로 짓는다.
    //  - 왕복 약속은 한 방향이다. 모든 `ui_color`에서 `color_from_hsv(hsv_from_color(c, k)) == c`다. 반대 방향(HSV에서 8비트를
    //    거쳐 HSV)은 양자화로 어긋난다. 그래서 상태를 8비트로 들면 끄는 동안 손잡이가 흔들린다.
    //  - 식은 sRGB 부호 값 위의 표준 HSV다. 선형 빛으로 바꾸지 않는다. 지각 균일 공간이 아니다.

    // HSV 색이다. 범위 밖의 값은 `clamp_hsv`가 자른다.
    struct hsv_color
    {
        // 색상이다 (도, [0, 360]). 360은 0과 같은 색이다.
        float hue { 0.0f };
        // 채도다 ([0, 1]).
        float saturation { 0.0f };
        // 명도다 ([0, 1]).
        float value { 0.0f };
        // 불투명도다 ([0, 1]).
        float alpha { 1.0f };

        [[nodiscard]] bool operator==(const hsv_color&) const = default;
    };

    // 범위로 자른다. NaN은 0이고 alpha의 NaN은 1이다 (보이지 않는 색이 조용히 생기지 않게).
    [[nodiscard]] hsv_color clamp_hsv(hsv_color color) noexcept;
    // 8비트 sRGB 색을 짓는다. 먼저 `clamp_hsv`로 자르고 색상 360을 0으로 접은 뒤 채널마다 lround(x * 255)다. 알파도 같다.
    [[nodiscard]] ui_color color_from_hsv(hsv_color color) noexcept;
    // 8비트 색에서 HSV를 짓는다.
    //  - 채도가 0(회색)이면 색상이 정해지지 않아 `keep.hue`를 쓴다. 명도가 0(검정)이면 색상과 채도가 정해지지 않아
    //    `keep.hue`·`keep.saturation`을 쓴다. 앱은 지금 상태를 `keep`으로 넘겨 회색을 넣어도 색상 막대가 0으로 튀지 않게 한다.
    //  - `keep`도 범위로 잘라 쓴다.
    [[nodiscard]] hsv_color hsv_from_color(ui_color color, const hsv_color& keep = {}) noexcept;

    // 16진 색 글이 색으로 서지 못하는 까닭이다. 오류 글의 문구는 앱의 말이다 (숫자 칸과 같다).
    enum class hex_color_problem
    {
        none,
        // 공백뿐이거나 비었다.
        empty,
        // 16진 밖의 글자가 있다 (가운데 공백, 둘째 `#` 포함).
        not_hex,
        // 자릿수가 3·6(알파면 4·8도)이 아니다.
        wrong_length,
    };

    struct hex_color_check
    {
        hex_color_problem problem { hex_color_problem::empty };
        // 읽은 색이다. 읽었으면 있다.
        std::optional<ui_color> color {};

        [[nodiscard]] bool valid() const noexcept
        {
            return problem == hex_color_problem::none;
        }

        [[nodiscard]] bool operator==(const hex_color_check&) const = default;
    };

    // 16진 색 글을 읽는다.
    //  - 앞뒤 공백을 무시하고 앞의 `#`는 있어도 없어도 된다. 전각 숫자·전각 영문·전각 `#`는 ASCII로 읽는다 (IME 전각 확정).
    //  - 자릿수는 3·6이고 `alpha`가 참이면 4·8도 받는다. 3·4자리는 자리마다 두 번 늘린다 (`#abc`는 `#AABBCC`).
    //  - 알파 자리가 없으면 불투명(FF)이다. 순서는 `#RRGGBBAA`다 (CSS와 스타일 파일과 같다. `ui_color` 리터럴과는 다르다).
    //  - 대소문자를 가리지 않는다.
    [[nodiscard]] hex_color_check check_hex_color(std::u8string_view text, bool alpha);
    // `#`와 대문자 여섯 자리다 (`#3366FF`). `alpha`가 참이면 알파를 뒤에 붙인 여덟 자리다 (`#3366FF80`).
    // 같은 색은 늘 같은 글이라 test와 UIA 되읽기가 글로 비교할 수 있다.
    [[nodiscard]] std::u8string format_hex_color(ui_color color, bool alpha);
    // 넣는 글(타자·붙여넣기·IME 확정)에서 `0-9`·`a-f`·`A-F`·`#`만 남기는 거르기다 (`apply_text_edit`). 전각은 ASCII로 바꾼다.
    //  - 조각만 보므로 이어 붙인 틀린 글("##12")은 막지 못한다. 그것은 `check_hex_color`가 잡는다.
    [[nodiscard]] text_insert_filter hex_color_filter();
}
