#include "luil/ui/calendar_date.h"

#include <algorithm>

namespace luil {
    namespace {
        using std::chrono::day;
        using std::chrono::days;
        using std::chrono::months;
        using std::chrono::sys_days;
        using std::chrono::weekday;
        using std::chrono::year;
        using std::chrono::year_month;
        using std::chrono::year_month_day;
        using std::chrono::year_month_day_last;

        constexpr year_month_day first_date { year { 1 }, std::chrono::January, day { 1 } };
        constexpr year_month_day last_date { year { 9999 }, std::chrono::December, day { 31 } };
        constexpr year_month_day epoch_date { year { 1970 }, std::chrono::January, day { 1 } };
        // 한 번에 더하는 달 수의 상한이다. 1~9999년을 다 넘는 수라 결과는 어차피 범위로 접힌다. 표준 해의 범위(±32767)를
        // 넘기지 않게 막는다.
        constexpr int month_limit { 12 * 10000 };

        [[nodiscard]] bool usable(const std::optional<year_month_day>& day) noexcept
        {
            return day.has_value() && day->ok();
        }

        // 범위의 실제 두 끝이다.
        struct span
        {
            year_month_day low { first_date };
            year_month_day high { last_date };
        };

        [[nodiscard]] span span_of(const calendar_bounds& bounds) noexcept
        {
            span result {};
            if (usable(bounds.minimum))
                result.low = std::clamp(*bounds.minimum, first_date, last_date);
            if (usable(bounds.maximum))
                result.high = std::clamp(*bounds.maximum, first_date, last_date);
            if (result.low > result.high)
                result.high = result.low;
            return result;
        }

        // 너비 `width`까지 앞에 0을 채운 십진수를 덧붙인다. 음수는 앞에 '-'를 둔다 (범위 밖 값을 그대로 보일 때뿐이다).
        void append_padded(std::u8string& text, const int value, const int width)
        {
            unsigned number { static_cast<unsigned>(value < 0 ? -value : value) };
            char8_t digits[12] {};
            int length { 0 };
            do
            {
                digits[length++] = static_cast<char8_t>(u8'0' + number % 10u);
                number /= 10u;
            } while (number != 0u);
            if (value < 0)
                text += u8'-';
            for (int pad { length }; pad < width; ++pad)
                text += u8'0';
            while (length > 0)
                text += digits[--length];
        }

        // 글 전체를 십진수로 읽는다. 숫자가 아닌 글자가 있으면 음수다. 부호와 공백도 숫자가 아니다.
        [[nodiscard]] int read_digits(const std::u8string_view text) noexcept
        {
            int value { 0 };
            for (const char8_t character : text)
            {
                if (character < u8'0' || character > u8'9')
                    return -1;
                value = value * 10 + (character - u8'0');
            }
            return value;
        }
    }

    std::array<year_month_day, 42> calendar_month_grid(year_month month, weekday first_day) noexcept
    {
        if (month.ok() == false)
            month = year_month { year { 1970 }, std::chrono::January };
        if (first_day.ok() == false)
            first_day = std::chrono::Sunday;
        // 1일의 요일에서 `first_day`를 뺀 날 수만큼 앞이 첫 칸이다. 요일 빼기는 늘 0~6이다.
        const sys_days first { month / day { 1 } };
        const sys_days start { first - (weekday { first } - first_day) };
        std::array<year_month_day, 42> cells {};
        for (std::size_t index { 0 }; index < cells.size(); ++index)
            cells[index] = year_month_day { start + days { static_cast<int>(index) } };
        return cells;
    }

    year_month_day add_months_clamped(const year_month_day day_value, const int months_value) noexcept
    {
        if (day_value.ok() == false)
            return day_value;
        const year_month moved { year_month { day_value.year(), day_value.month() } + months { std::clamp(months_value, -month_limit, month_limit) } };
        const day last { year_month_day_last { moved.year(), std::chrono::month_day_last { moved.month() } }.day() };
        return year_month_day { moved.year(), moved.month(), std::min(day_value.day(), last) };
    }

    year_month_day clamp_date(const year_month_day day_value, const calendar_bounds& bounds) noexcept
    {
        const span limits { span_of(bounds) };
        if (day_value.ok() == false)
            return limits.low;
        return std::clamp(day_value, limits.low, limits.high);
    }

    bool calendar_contains(const calendar_bounds& bounds, const year_month_day day_value) noexcept
    {
        if (day_value.ok() == false)
            return false;
        const span limits { span_of(bounds) };
        return limits.low <= day_value && day_value <= limits.high;
    }

    year_month_day calendar_start_cursor(const std::optional<year_month_day> value, const std::optional<year_month_day> today, const calendar_bounds& bounds) noexcept
    {
        if (usable(value))
            return clamp_date(*value, bounds);
        if (usable(today))
            return clamp_date(*today, bounds);
        if (usable(bounds.minimum))
            return clamp_date(*bounds.minimum, bounds);
        return clamp_date(epoch_date, bounds);
    }

    year_month_day calendar_moved(const year_month_day cursor, const calendar_move move, const calendar_bounds& bounds) noexcept
    {
        const year_month_day from { clamp_date(cursor, bounds) };
        const year_month month { from.year(), from.month() };
        year_month_day target { from };
        switch (move)
        {
        case calendar_move::previous_day:
            target = year_month_day { sys_days { from } - days { 1 } };
            break;
        case calendar_move::next_day:
            target = year_month_day { sys_days { from } + days { 1 } };
            break;
        case calendar_move::previous_week:
            target = year_month_day { sys_days { from } - days { 7 } };
            break;
        case calendar_move::next_week:
            target = year_month_day { sys_days { from } + days { 7 } };
            break;
        case calendar_move::previous_month:
            target = add_months_clamped(from, -1);
            break;
        case calendar_move::next_month:
            target = add_months_clamped(from, 1);
            break;
        case calendar_move::previous_year:
            target = add_months_clamped(from, -12);
            break;
        case calendar_move::next_year:
            target = add_months_clamped(from, 12);
            break;
        case calendar_move::month_start:
            target = month / day { 1 };
            break;
        case calendar_move::month_end:
            target = year_month_day { year_month_day_last { month.year(), std::chrono::month_day_last { month.month() } } };
            break;
        }
        // 0년이나 10000년으로 넘어간 날도 범위의 끝으로 접힌다.
        return clamp_date(target, bounds);
    }

    std::u8string format_iso_date(const year_month_day day_value)
    {
        std::u8string text {};
        text.reserve(10);
        append_padded(text, static_cast<int>(day_value.year()), 4);
        text += u8'-';
        append_padded(text, static_cast<int>(static_cast<unsigned>(day_value.month())), 2);
        text += u8'-';
        append_padded(text, static_cast<int>(static_cast<unsigned>(day_value.day())), 2);
        return text;
    }

    std::optional<year_month_day> parse_iso_date(const std::u8string_view text) noexcept
    {
        if (text.size() != 10u || text[4] != u8'-' || text[7] != u8'-')
            return std::nullopt;
        const int year_value { read_digits(text.substr(0, 4)) };
        const int month_value { read_digits(text.substr(5, 2)) };
        const int day_value { read_digits(text.substr(8, 2)) };
        if (year_value < 1 || month_value < 0 || day_value < 0)
            return std::nullopt;
        const year_month_day result { year { year_value }, std::chrono::month { static_cast<unsigned>(month_value) }, day { static_cast<unsigned>(day_value) } };
        if (result.ok() == false)
            return std::nullopt;
        return result;
    }
}
