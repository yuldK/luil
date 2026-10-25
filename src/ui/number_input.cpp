#include "luil/ui/number_input.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace luil {
    namespace {
        // 값은 `10^decimals` 배의 정수(unit)로 다룬다. 이 크기 안이면 double로 옮겨도 정수가 정확하다.
        constexpr long long unit_limit { 1'000'000'000'000'000LL };
        constexpr int maximum_decimals { 9 };
        // 읽을 수 없는 글자의 자리표다. 숫자 꼴의 어느 글자와도 겹치지 않는다.
        constexpr char8_t foreign_character { u8'?' };

        [[nodiscard]] int decimals_of(const number_input_range& range) noexcept
        {
            return std::clamp(range.decimals, 0, maximum_decimals);
        }

        [[nodiscard]] long long scale_of(const number_input_range& range) noexcept
        {
            long long scale { 1 };
            for (int index { 0 }; index < decimals_of(range); ++index)
                scale *= 10;
            return scale;
        }

        [[nodiscard]] long long to_units(const double value, const long long scale) noexcept
        {
            const double scaled { value * static_cast<double>(scale) };
            if (std::isnan(scaled))
                return 0;
            if (scaled >= static_cast<double>(unit_limit))
                return unit_limit;
            if (scaled <= -static_cast<double>(unit_limit))
                return -unit_limit;
            return std::llround(scaled);
        }

        [[nodiscard]] bool has_minimum(const number_input_range& range) noexcept
        {
            return range.minimum > std::numeric_limits<double>::lowest();
        }

        [[nodiscard]] bool has_maximum(const number_input_range& range) noexcept
        {
            return range.maximum < std::numeric_limits<double>::max();
        }

        [[nodiscard]] long long minimum_units(const number_input_range& range) noexcept
        {
            return has_minimum(range) ? to_units(range.minimum, scale_of(range)) : -unit_limit;
        }

        [[nodiscard]] long long maximum_units(const number_input_range& range) noexcept
        {
            return has_maximum(range) ? to_units(range.maximum, scale_of(range)) : unit_limit;
        }

        // UTF-8 한 글자를 읽고 그 길이만큼 `at`을 민다. 깨진 바이트는 한 바이트짜리 읽을 수 없는 글자다.
        [[nodiscard]] char32_t next_code_point(const std::u8string_view text, std::size_t& at) noexcept
        {
            const auto lead { static_cast<std::uint8_t>(text[at]) };
            std::size_t length { 1 };
            char32_t value { lead };
            if (lead >= 0xF0u)
            {
                length = 4;
                value = lead & 0x07u;
            }
            else if (lead >= 0xE0u)
            {
                length = 3;
                value = lead & 0x0Fu;
            }
            else if (lead >= 0xC0u)
            {
                length = 2;
                value = lead & 0x1Fu;
            }
            else if (lead >= 0x80u)
            {
                ++at;
                return 0xFFFDu;
            }
            if (at + length > text.size())
            {
                at = text.size();
                return 0xFFFDu;
            }
            for (std::size_t index { 1 }; index < length; ++index)
                value = (value << 6u) | (static_cast<std::uint8_t>(text[at + index]) & 0x3Fu);
            at += length;
            return value;
        }

        [[nodiscard]] bool is_space(const char32_t value) noexcept
        {
            return value == U' ' || value == U'\t' || value == U'\r' || value == U'\n' || value == 0x00A0u || value == 0x3000u;
        }

        // 숫자 꼴의 글자를 ASCII로 옮긴다. 공백은 공백, 그 밖은 읽을 수 없는 글자다.
        [[nodiscard]] char8_t ascii_of(const char32_t value) noexcept
        {
            if (value >= U'0' && value <= U'9')
                return static_cast<char8_t>(value);
            if (value >= 0xFF10u && value <= 0xFF19u)
                return static_cast<char8_t>(u8'0' + (value - 0xFF10u));
            if (value == U'.' || value == 0xFF0Eu)
                return u8'.';
            // 전각 빼기·수학 빼기(U+2212)·작은 빼기다.
            if (value == U'-' || value == 0xFF0Du || value == 0x2212u || value == 0xFE63u)
                return u8'-';
            if (value == U'+' || value == 0xFF0Bu)
                return u8'+';
            if (is_space(value))
                return u8' ';
            return foreign_character;
        }

        [[nodiscard]] std::u8string normalized(const std::u8string_view text)
        {
            std::u8string result {};
            result.reserve(text.size());
            for (std::size_t at { 0 }; at < text.size();)
                result.push_back(ascii_of(next_code_point(text, at)));
            const std::size_t first { result.find_first_not_of(u8' ') };
            if (first == std::u8string::npos)
                return {};
            const std::size_t last { result.find_last_not_of(u8' ') };
            return result.substr(first, last - first + 1);
        }

        // 읽은 글이다. `units`는 자릿수 안일 때만 정확하다 (`precise`).
        struct parsed_number
        {
            number_input_problem problem { number_input_problem::none };
            double value { 0.0 };
            long long units { 0 };
            bool precise { true };
            bool overflow { false };
        };

        [[nodiscard]] parsed_number parse(const std::u8string_view text, const number_input_range& range)
        {
            const std::u8string value { normalized(text) };
            if (value.empty())
                return { .problem = number_input_problem::empty };
            std::size_t at { 0 };
            const bool negative { value[at] == u8'-' };
            if (value[at] == u8'-' || value[at] == u8'+')
                ++at;
            std::u8string whole {};
            std::u8string fraction {};
            while (at < value.size() && value[at] >= u8'0' && value[at] <= u8'9')
                whole.push_back(value[at++]);
            if (at < value.size() && value[at] == u8'.')
            {
                ++at;
                while (at < value.size() && value[at] >= u8'0' && value[at] <= u8'9')
                    fraction.push_back(value[at++]);
            }
            if (at != value.size() || (whole.empty() && fraction.empty()))
                return { .problem = number_input_problem::not_a_number };

            while (fraction.empty() == false && fraction.back() == u8'0')
                fraction.pop_back();
            const std::size_t first_digit { whole.find_first_not_of(u8'0') };
            whole = first_digit == std::u8string::npos ? std::u8string {} : whole.substr(first_digit);

            parsed_number result {};
            const int decimals { decimals_of(range) };
            result.precise = fraction.size() <= static_cast<std::size_t>(decimals);
            // 정수 자리가 unit 한도를 넘는 글은 정수로 옮기지 않는다 — 값만 어림하고 넘침으로 둔다.
            result.overflow = whole.size() + static_cast<std::size_t>(decimals) > 15u;
            double approximate { 0.0 };
            for (const char8_t digit : whole)
                approximate = approximate * 10.0 + static_cast<double>(digit - u8'0');
            double place { 0.1 };
            for (const char8_t digit : fraction)
            {
                approximate += static_cast<double>(digit - u8'0') * place;
                place /= 10.0;
            }
            result.value = negative ? -approximate : approximate;
            if (result.overflow == false)
            {
                long long units { 0 };
                for (const char8_t digit : whole)
                    units = units * 10 + (digit - u8'0');
                for (int index { 0 }; index < decimals; ++index)
                    units = units * 10 + (static_cast<std::size_t>(index) < fraction.size() ? fraction[static_cast<std::size_t>(index)] - u8'0' : 0);
                result.units = negative ? -units : units;
                if (units > unit_limit)
                    result.overflow = true;
            }
            return result;
        }

        [[nodiscard]] std::u8string format_units(const long long units, const int decimals)
        {
            const bool negative { units < 0 };
            std::u8string digits {};
            for (long long rest { negative ? -units : units }; rest > 0 || digits.size() <= static_cast<std::size_t>(decimals); rest /= 10)
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + rest % 10));
            if (decimals > 0)
                digits.insert(digits.end() - decimals, u8'.');
            if (negative)
                digits.insert(digits.begin(), u8'-');
            return digits;
        }
    } // namespace

    number_input_check check_number_input(const std::u8string_view text, const number_input_range& range)
    {
        const parsed_number parsed { parse(text, range) };
        if (parsed.problem != number_input_problem::none)
            return { .problem = parsed.problem };
        number_input_check result { .problem = number_input_problem::none, .value = parsed.value };
        if (parsed.precise == false)
            result.problem = number_input_problem::too_precise;
        else if (parsed.overflow)
            result.problem = parsed.value < 0.0 ? number_input_problem::below_minimum : number_input_problem::above_maximum;
        else if (parsed.units < minimum_units(range))
            result.problem = number_input_problem::below_minimum;
        else if (parsed.units > maximum_units(range))
            result.problem = number_input_problem::above_maximum;
        return result;
    }

    std::u8string format_number_input(const double value, const number_input_range& range)
    {
        return format_units(to_units(value, scale_of(range)), decimals_of(range));
    }

    std::optional<std::u8string> stepped_number_input(const std::u8string_view text, const number_input_range& range, const value_step step)
    {
        const long long scale { scale_of(range) };
        const long long lowest { minimum_units(range) };
        const long long highest { std::max(lowest, maximum_units(range)) };
        const auto clamped = [lowest, highest](const long long units) { return std::clamp(units, lowest, highest); };
        const parsed_number parsed { parse(text, range) };

        std::optional<long long> target {};
        if (step == value_step::minimum)
        {
            if (has_minimum(range) == false)
                return std::nullopt;
            target = lowest;
        }
        else if (step == value_step::maximum)
        {
            if (has_maximum(range) == false)
                return std::nullopt;
            target = highest;
        }
        else if (parsed.problem != number_input_problem::none)
            target = clamped(0);
        else
        {
            const long long step_units { std::max(1LL, to_units(range.step, scale)) };
            const bool page { step == value_step::increase_page || step == value_step::decrease_page };
            const long long delta { page ? (range.page_step > 0.0 ? std::max(1LL, to_units(range.page_step, scale)) : step_units * 10) : step_units };
            const bool up { step == value_step::increase || step == value_step::increase_page };
            // 넘친 값은 그 끝에서 시작한다. 자릿수보다 자세한 값은 어림한 자리가 눈금 사이에 선다.
            const double current { parsed.overflow ? (parsed.value < 0.0 ? -static_cast<double>(unit_limit) : static_cast<double>(unit_limit))
                                                   : (parsed.precise ? static_cast<double>(parsed.units) : parsed.value * static_cast<double>(scale)) };
            const long long base { has_minimum(range) ? lowest : 0 };
            const double grid { (current - static_cast<double>(base)) / static_cast<double>(step_units) };
            const double nearest { std::round(grid) };
            long long next { 0 };
            if (std::abs(grid - nearest) < 1e-9)
                next = base + static_cast<long long>(nearest) * step_units + (up ? delta : -delta);
            else
                next = base + static_cast<long long>(up ? std::ceil(grid) : std::floor(grid)) * step_units;
            target = clamped(std::clamp(next, -unit_limit, unit_limit));
        }
        std::u8string result { format_units(*target, decimals_of(range)) };
        if (result == text)
            return std::nullopt;
        return result;
    }

    text_insert_filter number_input_filter(const number_input_range& range)
    {
        const bool signed_values { range.minimum < 0.0 };
        const bool fractions { decimals_of(range) > 0 };
        return [signed_values, fractions](const std::u8string_view text) {
            std::u8string kept {};
            for (std::size_t at { 0 }; at < text.size();)
            {
                const char8_t character { ascii_of(next_code_point(text, at)) };
                if ((character >= u8'0' && character <= u8'9') || (character == u8'.' && fractions) || ((character == u8'-' || character == u8'+') && signed_values))
                    kept.push_back(character);
            }
            return kept;
        };
    }
} // namespace luil
