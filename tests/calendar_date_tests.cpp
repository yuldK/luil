#include "luil/ui/calendar_date.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <string>

// 달력의 순수 날짜 계산이다 (docs/calendar-date-design.md). 시계를 읽지 않으므로 날은 모두 손으로 정한다.
namespace {
    using std::chrono::year_month_day;

    [[nodiscard]] year_month_day date(const int year, const unsigned month, const unsigned day)
    {
        return year_month_day { std::chrono::year { year }, std::chrono::month { month }, std::chrono::day { day } };
    }

    [[nodiscard]] std::chrono::year_month month_of(const int year, const unsigned month)
    {
        return std::chrono::year_month { std::chrono::year { year }, std::chrono::month { month } };
    }

    [[nodiscard]] std::chrono::weekday weekday_of(const year_month_day day)
    {
        return std::chrono::weekday { std::chrono::sys_days { day } };
    }

    // 범위가 없는 달력이다 (0001-01-01부터 9999-12-31까지).
    const luil::calendar_bounds open_bounds {};
}

TEST_CASE("Leap years follow the Gregorian rule and February ends on its last day", "[ui][calendar-date]")
{
    // 1900은 100의 배수라 아니고, 2000은 400의 배수라 맞고, 2024는 맞고, 2100은 아니다.
    REQUIRE(date(1900, 2, 29).ok() == false);
    REQUIRE(date(2000, 2, 29).ok());
    REQUIRE(date(2024, 2, 29).ok());
    REQUIRE(date(2100, 2, 29).ok() == false);
    REQUIRE(luil::calendar_moved(date(2024, 2, 10), luil::calendar_move::month_end, open_bounds) == date(2024, 2, 29));
    REQUIRE(luil::calendar_moved(date(2026, 2, 10), luil::calendar_move::month_end, open_bounds) == date(2026, 2, 28));
    REQUIRE(luil::calendar_moved(date(1900, 2, 1), luil::calendar_move::month_end, open_bounds) == date(1900, 2, 28));
}

TEST_CASE("Adding months folds the day into the last day of the month", "[ui][calendar-date]")
{
    REQUIRE(luil::add_months_clamped(date(2026, 1, 31), 1) == date(2026, 2, 28));
    REQUIRE(luil::add_months_clamped(date(2024, 1, 31), 1) == date(2024, 2, 29));
    REQUIRE(luil::add_months_clamped(date(2024, 2, 29), 12) == date(2025, 2, 28));
    REQUIRE(luil::add_months_clamped(date(2024, 2, 29), -12) == date(2023, 2, 28));
    REQUIRE(luil::add_months_clamped(date(2026, 3, 31), -1) == date(2026, 2, 28));
    // 12월에서 1월로 해를 넘기고, 1월에서 12월로 해를 거스른다.
    REQUIRE(luil::add_months_clamped(date(2026, 12, 15), 1) == date(2027, 1, 15));
    REQUIRE(luil::add_months_clamped(date(2026, 1, 15), -1) == date(2025, 12, 15));
    REQUIRE(luil::add_months_clamped(date(2026, 5, 31), 25) == date(2028, 6, 30));
    // 틀린 날은 그대로다.
    REQUIRE(luil::add_months_clamped(date(2026, 2, 30), 1) == date(2026, 2, 30));
}

TEST_CASE("The month grid starts on the first day at or before the first and is six weeks long", "[ui][calendar-date]")
{
    // 2026년 11월 1일은 일요일이다.
    const std::chrono::year_month november { month_of(2026, 11) };
    REQUIRE(weekday_of(date(2026, 11, 1)) == std::chrono::Sunday);
    for (unsigned first { 0 }; first < 7u; ++first)
    {
        const std::chrono::weekday first_day { first };
        const std::array<year_month_day, 42> cells { luil::calendar_month_grid(november, first_day) };
        // 첫 칸은 늘 `first_day`이고, 1일은 첫 주 안에 서며, 칸은 하루씩 늘어난다.
        REQUIRE(weekday_of(cells.front()) == first_day);
        std::size_t first_of_month { cells.size() };
        for (std::size_t index { 0 }; index < cells.size(); ++index)
        {
            if (cells[index] == date(2026, 11, 1))
                first_of_month = index;
            if (index > 0u)
                REQUIRE(std::chrono::sys_days { cells[index] } - std::chrono::sys_days { cells[index - 1u] } == std::chrono::days { 1 });
        }
        REQUIRE(first_of_month < 7u);
        REQUIRE(first_of_month == static_cast<std::size_t>((std::chrono::Sunday - first_day).count()));
    }
    // 1일이 `first_day`이면 첫 줄 첫 칸이 1일이다. 끝 칸은 12월 12일이다.
    const std::array<year_month_day, 42> sunday { luil::calendar_month_grid(november, std::chrono::Sunday) };
    REQUIRE(sunday.front() == date(2026, 11, 1));
    REQUIRE(sunday.back() == date(2026, 12, 12));
    // 월요일 시작이면 앞에 10월 26일부터 엿새가 선다.
    const std::array<year_month_day, 42> monday { luil::calendar_month_grid(november, std::chrono::Monday) };
    REQUIRE(monday.front() == date(2026, 10, 26));
    REQUIRE(monday[6] == date(2026, 11, 1));
    // 틀린 달과 요일은 1970년 1월과 일요일이다. 1970년 1월 1일은 목요일이다.
    const std::array<year_month_day, 42> fallback { luil::calendar_month_grid(month_of(2026, 13), std::chrono::weekday { 9 }) };
    REQUIRE(fallback.front() == date(1969, 12, 28));
    REQUIRE(fallback[4] == date(1970, 1, 1));
}

TEST_CASE("Bounds clamp dates, an inverted range is the minimum alone and empty ends are the calendar ends", "[ui][calendar-date]")
{
    const luil::calendar_bounds bounds { date(2026, 11, 1), date(2026, 11, 30) };
    REQUIRE(luil::clamp_date(date(2026, 10, 5), bounds) == date(2026, 11, 1));
    REQUIRE(luil::clamp_date(date(2026, 12, 5), bounds) == date(2026, 11, 30));
    REQUIRE(luil::clamp_date(date(2026, 11, 15), bounds) == date(2026, 11, 15));
    REQUIRE(luil::calendar_contains(bounds, date(2026, 11, 1)));
    REQUIRE(luil::calendar_contains(bounds, date(2026, 11, 30)));
    REQUIRE(luil::calendar_contains(bounds, date(2026, 12, 1)) == false);
    // 틀린 날은 범위 밖이고 접으면 최소다.
    REQUIRE(luil::calendar_contains(bounds, date(2026, 11, 31)) == false);
    REQUIRE(luil::clamp_date(date(2026, 11, 31), bounds) == date(2026, 11, 1));

    // 최소가 최대보다 늦으면 최대를 최소로 본다.
    const luil::calendar_bounds inverted { date(2026, 11, 20), date(2026, 11, 10) };
    REQUIRE(luil::clamp_date(date(2026, 11, 1), inverted) == date(2026, 11, 20));
    REQUIRE(luil::clamp_date(date(2026, 12, 1), inverted) == date(2026, 11, 20));
    REQUIRE(luil::calendar_contains(inverted, date(2026, 11, 20)));
    REQUIRE(luil::calendar_contains(inverted, date(2026, 11, 15)) == false);

    // 빈 끝과 틀린 끝은 0001-01-01과 9999-12-31이다. 끝 밖의 끝은 끝으로 자른다.
    REQUIRE(luil::clamp_date(date(0, 6, 1), open_bounds) == date(1, 1, 1));
    REQUIRE(luil::clamp_date(date(12000, 6, 1), open_bounds) == date(9999, 12, 31));
    const luil::calendar_bounds broken { date(2026, 2, 30), date(20000, 1, 1) };
    REQUIRE(luil::clamp_date(date(-5, 1, 1), broken) == date(1, 1, 1));
    REQUIRE(luil::clamp_date(date(15000, 1, 1), broken) == date(9999, 12, 31));
}

TEST_CASE("The start cursor is the value, else today, else the minimum, else 1970, folded into the range", "[ui][calendar-date]")
{
    const luil::calendar_bounds bounds { date(2026, 1, 1), date(2026, 12, 31) };
    REQUIRE(luil::calendar_start_cursor(date(2026, 5, 5), date(2026, 11, 3), bounds) == date(2026, 5, 5));
    REQUIRE(luil::calendar_start_cursor(std::nullopt, date(2026, 11, 3), bounds) == date(2026, 11, 3));
    REQUIRE(luil::calendar_start_cursor(std::nullopt, std::nullopt, bounds) == date(2026, 1, 1));
    REQUIRE(luil::calendar_start_cursor(std::nullopt, std::nullopt, open_bounds) == date(1970, 1, 1));
    // 범위 밖 값은 커서만 접는다.
    REQUIRE(luil::calendar_start_cursor(date(2030, 5, 5), date(2026, 11, 3), bounds) == date(2026, 12, 31));
    // `ok()`가 아닌 값과 오늘은 없는 것이다.
    REQUIRE(luil::calendar_start_cursor(date(2026, 2, 30), date(2026, 11, 3), bounds) == date(2026, 11, 3));
    REQUIRE(luil::calendar_start_cursor(date(2026, 2, 30), date(2026, 4, 31), bounds) == date(2026, 1, 1));
    // 1970년이 범위 밖이면 범위로 접는다.
    REQUIRE(luil::calendar_start_cursor(std::nullopt, std::nullopt, luil::calendar_bounds { std::nullopt, date(1960, 1, 1) }) == date(1960, 1, 1));
}

TEST_CASE("Each of the ten moves walks the cursor and stops at the calendar ends", "[ui][calendar-date]")
{
    const year_month_day cursor { date(2026, 11, 3) };
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::previous_day, open_bounds) == date(2026, 11, 2));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::next_day, open_bounds) == date(2026, 11, 4));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::previous_week, open_bounds) == date(2026, 10, 27));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::next_week, open_bounds) == date(2026, 11, 10));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::previous_month, open_bounds) == date(2026, 10, 3));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::next_month, open_bounds) == date(2026, 12, 3));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::previous_year, open_bounds) == date(2025, 11, 3));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::next_year, open_bounds) == date(2027, 11, 3));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::month_start, open_bounds) == date(2026, 11, 1));
    REQUIRE(luil::calendar_moved(cursor, luil::calendar_move::month_end, open_bounds) == date(2026, 11, 30));
    // 해를 넘는 일·주 이동과 말일 접기다.
    REQUIRE(luil::calendar_moved(date(2026, 12, 31), luil::calendar_move::next_day, open_bounds) == date(2027, 1, 1));
    REQUIRE(luil::calendar_moved(date(2027, 1, 3), luil::calendar_move::previous_week, open_bounds) == date(2026, 12, 27));
    REQUIRE(luil::calendar_moved(date(2026, 1, 31), luil::calendar_move::next_month, open_bounds) == date(2026, 2, 28));
    REQUIRE(luil::calendar_moved(date(2024, 2, 29), luil::calendar_move::next_year, open_bounds) == date(2025, 2, 28));

    // 0001-01-01 앞과 9999-12-31 뒤로는 가지 않는다.
    for (const luil::calendar_move move : { luil::calendar_move::previous_day, luil::calendar_move::previous_week, luil::calendar_move::previous_month, luil::calendar_move::previous_year })
        REQUIRE(luil::calendar_moved(date(1, 1, 1), move, open_bounds) == date(1, 1, 1));
    for (const luil::calendar_move move : { luil::calendar_move::next_day, luil::calendar_move::next_week, luil::calendar_move::next_month, luil::calendar_move::next_year })
        REQUIRE(luil::calendar_moved(date(9999, 12, 31), move, open_bounds) == date(9999, 12, 31));

    // 범위 끝도 같다. 범위 안의 가장 가까운 날에 선다.
    const luil::calendar_bounds bounds { date(2026, 11, 1), date(2026, 11, 20) };
    REQUIRE(luil::calendar_moved(date(2026, 11, 3), luil::calendar_move::previous_week, bounds) == date(2026, 11, 1));
    REQUIRE(luil::calendar_moved(date(2026, 11, 18), luil::calendar_move::month_end, bounds) == date(2026, 11, 20));
    REQUIRE(luil::calendar_moved(date(2026, 11, 20), luil::calendar_move::next_day, bounds) == date(2026, 11, 20));
    // `ok()`가 아닌 커서는 범위의 최소에서 센다.
    REQUIRE(luil::calendar_moved(date(2026, 11, 31), luil::calendar_move::next_day, bounds) == date(2026, 11, 2));
}

TEST_CASE("ISO text writes four digit years and two digit months and reads only that exact form", "[ui][calendar-date]")
{
    REQUIRE(luil::format_iso_date(date(2026, 11, 3)) == u8"2026-11-03");
    REQUIRE(luil::format_iso_date(date(1, 1, 1)) == u8"0001-01-01");
    REQUIRE(luil::format_iso_date(date(9999, 12, 31)) == u8"9999-12-31");
    for (const year_month_day day : { date(2026, 11, 3), date(1, 1, 1), date(9999, 12, 31), date(2024, 2, 29) })
        REQUIRE(luil::parse_iso_date(luil::format_iso_date(day)) == day);

    // 틀린 글이다. 없는 날, 한 자리 달, 앞뒤 공백, 0년, 부호, 다른 구분자다.
    const std::u8string_view wrong[] {
        u8"2026-02-30", u8"2026-1-3", u8" 2026-11-03", u8"2026-11-03 ", u8"0000-01-01", u8"+026-11-03", u8"2026/11/03", u8"2026-13-01", u8"2026-00-10", u8""
    };
    for (const std::u8string_view text : wrong)
        REQUIRE(luil::parse_iso_date(text).has_value() == false);
}
