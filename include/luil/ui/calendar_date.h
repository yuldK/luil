#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 달력이 쓰는 순수 날짜 계산이다 (docs/calendar-date-design.md). 값은 시간대가 없는 그레고리력 날짜이고, 표준
    // `std::chrono::year_month_day`·`year_month`·`weekday`를 그대로 쓴다 (결정 1). 시계와 OS 지역은 읽지 않는다 (결정 2).
    //  - 날짜는 1년부터 9999년까지다. 범위의 빈 끝은 그 끝이다.
    //  - `ok()`가 아닌 값은 없는 것으로 본다. 표준 형만으로는 1월 31일에 한 달을 더하면 2월 31일(`ok()` 거짓)이 되어 앱마다
    //    말일 접기를 짓게 되므로 접는 함수를 여기 둔다.

    // 고를 수 있는 날의 최소·최대다. 둘 다 그 날을 포함한다.
    //  - 비어 있거나 `ok()`가 아니면 그 끝(0001-01-01, 9999-12-31)이다. 끝 밖의 값은 끝으로 자른다.
    //  - 최소가 최대보다 늦으면 최대를 최소로 본다 (범위가 그 날 하루다).
    struct calendar_bounds
    {
        std::optional<std::chrono::year_month_day> minimum {};
        std::optional<std::chrono::year_month_day> maximum {};
    };

    // 커서를 옮기는 한 걸음이다. 달력의 키 표가 이 열 가지를 쓴다 (docs/date-picker-design.md).
    enum class calendar_move
    {
        previous_day,
        next_day,
        previous_week,
        next_week,
        previous_month,
        next_month,
        previous_year,
        next_year,
        // 그 달의 첫날·끝날이다 (Home·End, 날짜 고르기 결정 5).
        month_start,
        month_end,
    };

    // 한 달을 6주 x 7일로 편 격자다. 첫 칸은 그 달 1일과 같거나 그 앞의 가장 가까운 `first_day`다.
    //  - 6주로 고정해 높이가 달마다 같다. 앞뒤 칸은 이웃 달의 날이고, 0001년 1월의 앞 칸과 9999년 12월의 뒤 칸은 범위 밖이다.
    //  - `month`가 `ok()`가 아니면 1970년 1월로, `first_day`가 `ok()`가 아니면 일요일로 본다.
    [[nodiscard]] std::array<std::chrono::year_month_day, 42> calendar_month_grid(std::chrono::year_month month, std::chrono::weekday first_day) noexcept;
    // 달을 더하고 날을 그 달 말일로 접는다. 해는 12달이다 (1월 31일 + 1달은 2월 28·29일, 2월 29일 + 12달은 2월 28일).
    //  - 1~9999년 밖으로 나가도 그대로 돌려준다. 범위로 접는 것은 `clamp_date`의 몫이다. `ok()`가 아닌 값은 그대로다.
    [[nodiscard]] std::chrono::year_month_day add_months_clamped(std::chrono::year_month_day day, int months) noexcept;
    // 범위 안으로 접는다. `ok()`가 아닌 날은 범위의 최소다.
    [[nodiscard]] std::chrono::year_month_day clamp_date(std::chrono::year_month_day day, const calendar_bounds& bounds) noexcept;
    // `ok()`이고 범위 안인가.
    [[nodiscard]] bool calendar_contains(const calendar_bounds& bounds, std::chrono::year_month_day day) noexcept;
    // 달력을 열 때 커서를 둘 날이다. 값, 없으면 오늘, 없으면 범위의 최소, 그것도 없으면 1970-01-01이고 범위로 접는다.
    [[nodiscard]] std::chrono::year_month_day calendar_start_cursor(
        std::optional<std::chrono::year_month_day> value, std::optional<std::chrono::year_month_day> today, const calendar_bounds& bounds) noexcept;
    // 커서를 한 걸음 옮긴 날이다. 일·주는 `sys_days` 산술, 달·해는 `add_months_clamped`, 처음·끝은 그 달 1일과 말일이고 범위로
    // 접는다. 범위 끝에서 더 갈 곳이 없으면 커서와 같은 날이 나온다. `ok()`가 아닌 커서는 범위의 최소에서 센다.
    [[nodiscard]] std::chrono::year_month_day calendar_moved(std::chrono::year_month_day cursor, calendar_move move, const calendar_bounds& bounds) noexcept;
    // "2026-11-03" 꼴의 ISO 글이다. 네 자리 해와 두 자리 달·날을 숫자 글자로 직접 지어 로케일에 달리지 않는다.
    //  - 날 칸의 owner, test, 앱 저장에 쓰는 글이고 화면 글이 아니다 (화면 글은 기본 문구 "2026년 11월 3일").
    [[nodiscard]] std::u8string format_iso_date(std::chrono::year_month_day day);
    // 정확히 "YYYY-MM-DD" 꼴만 읽는다. 1~9999년이고 `ok()`인 날짜만이다. 공백·한 자리 달·2월 30일은 `std::nullopt`다.
    [[nodiscard]] std::optional<std::chrono::year_month_day> parse_iso_date(std::u8string_view text) noexcept;
}
