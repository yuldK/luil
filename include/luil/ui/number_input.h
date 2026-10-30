#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 숫자 칸의 범위와 눈금이다 (number-input-design.md).
    struct number_input_range
    {
        double minimum { std::numeric_limits<double>::lowest() };
        double maximum { std::numeric_limits<double>::max() };
        // ↑↓·스핀 단추의 걸음이다. 눈금은 최솟값에서 센다 (최솟값이 정해지지 않았으면 0에서).
        double step { 1.0 };
        // PageUp·PageDown의 걸음이다. 0이면 `step`의 10배다.
        double page_step { 0.0 };
        // 소수 자릿수다. 증감한 글을 이 자릿수로 쓰고, 이보다 자세한 글은 `too_precise`다.
        int decimals { 0 };
    };

    // 글이 숫자 칸의 값으로 서지 못하는 까닭이다.
    enum class number_input_problem
    {
        none,
        empty,
        not_a_number,
        too_precise,
        below_minimum,
        above_maximum,
    };

    struct number_input_check
    {
        number_input_problem problem { number_input_problem::empty };
        // 읽은 값이다. 범위를 벗어났어도 읽었으면 있다.
        std::optional<double> value {};

        [[nodiscard]] bool valid() const noexcept
        {
            return problem == number_input_problem::none;
        }

        [[nodiscard]] bool operator==(const number_input_check&) const = default;
    };

    // 글을 숫자로 읽고 범위를 잰다.
    //  - 앞뒤 공백을 무시하고, 전각 숫자·전각 마침표·전각 빼기·U+2212를 ASCII로 읽는다 (IME 전각 확정, 복사한 "−5").
    //  - 천 단위 쉼표는 받지 않는다. 쉼표를 소수점으로 쓰는 지역과 섞이면 값이 조용히 천 배가 된다.
    //  - 끝의 0은 소수 자릿수로 세지 않는다 ("1.50"은 소수 한 자리 칸에서 1.5다).
    [[nodiscard]] number_input_check check_number_input(std::u8string_view text, const number_input_range& range);

    // 값을 `decimals` 자리 글로 쓴다. 범위로 접지 않는다.
    [[nodiscard]] std::u8string format_number_input(double value, const number_input_range& range);

    // `step` 한 걸음 뒤의 글이다. 이미 같은 글이면(끝에 닿았다) nullopt다.
    //  - 지금 값이 눈금 사이면 늘리기는 위 눈금, 줄이기는 아래 눈금으로 간다. 결과는 범위 안으로 접는다.
    //  - 비었거나 읽을 수 없는 글에서는 걸음 없이 0을 범위로 접은 값이다.
    //  - `minimum`·`maximum`은 그 끝이 정해져 있을 때만 간다.
    [[nodiscard]] std::optional<std::u8string> stepped_number_input(std::u8string_view text, const number_input_range& range, value_step step);

    // 넣는 글(타자·붙여넣기·IME 확정)에서 그 범위의 숫자에 나올 수 있는 글자만 남기는 거르기다 (`apply_text_edit`).
    //  - 숫자, 최솟값이 음수면 부호, `decimals`가 있으면 마침표를 남긴다. 전각 글자는 ASCII로 바꾼다.
    //  - 조각만 보므로 이어 붙여 틀린 글("1..2")은 막지 못한다 — 그것은 `check_number_input`이 잡는다.
    [[nodiscard]] text_insert_filter number_input_filter(const number_input_range& range);
}
