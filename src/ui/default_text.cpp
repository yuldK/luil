#include "ui/default_text.h"

#include "text/library_text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace luil::default_text {
    namespace {
        using library_text::key;

        // 생성 enum은 이름 글자 차례라 달과 요일의 차례가 아니다. 달 번호와 `c_encoding`(일요일이 0)으로 찾도록 표를 둔다.
        constexpr std::array<key, 12> month_names {
            key::calendar_month_name_january
            , key::calendar_month_name_february
            , key::calendar_month_name_march
            , key::calendar_month_name_april
            , key::calendar_month_name_may
            , key::calendar_month_name_june
            , key::calendar_month_name_july
            , key::calendar_month_name_august
            , key::calendar_month_name_september
            , key::calendar_month_name_october
            , key::calendar_month_name_november
            , key::calendar_month_name_december
        };
        constexpr std::array<key, 7> weekday_short_names {
            key::calendar_weekday_short_sunday
            , key::calendar_weekday_short_monday
            , key::calendar_weekday_short_tuesday
            , key::calendar_weekday_short_wednesday
            , key::calendar_weekday_short_thursday
            , key::calendar_weekday_short_friday
            , key::calendar_weekday_short_saturday
        };
        constexpr std::array<key, 7> weekday_long_names {
            key::calendar_weekday_long_sunday
            , key::calendar_weekday_long_monday
            , key::calendar_weekday_long_tuesday
            , key::calendar_weekday_long_wednesday
            , key::calendar_weekday_long_thursday
            , key::calendar_weekday_long_friday
            , key::calendar_weekday_long_saturday
        };

        // 자리 값이 없는 낱말이다. 표는 정적 저장소라 언어가 바뀌어도 돌려준 view가 산다.
        [[nodiscard]] std::u8string_view word(const key value) noexcept
        {
            return library_text::text(library_strings(), value);
        }

        [[nodiscard]] std::u8string sentence(const key value, const std::span<const string_argument> arguments)
        {
            return library_text::format(library_strings(), value, arguments);
        }

        // 십진수 글자를 덧붙인다. 로케일이 자릿점을 끼우지 못하게 직접 짓는다.
        void append_number(std::u8string& text, std::size_t number)
        {
            if (number == 0u)
            {
                text += u8'0';
                return;
            }

            char8_t digits[20] {};
            int length { 0 };
            for (; number != 0u; number /= 10u)
                digits[length++] = static_cast<char8_t>(u8'0' + number % 10u);
            while (length > 0)
                text += digits[--length];
        }

        // 해의 수다. 음수 해는 날짜 범위(1~9999년) 밖이라 0으로 본다.
        [[nodiscard]] std::int64_t year_number(const std::chrono::year year) noexcept
        {
            const int value { static_cast<int>(year) };
            return value > 0 ? std::int64_t { value } : 0;
        }

        // 달 이름이다. 1~12 밖의 달은 이름이 없어도 문장이 서도록 숫자 글자다.
        [[nodiscard]] std::u8string month_name(const std::chrono::month month)
        {
            if (month.ok())
                return std::u8string { word(month_names[static_cast<unsigned>(month) - 1u]) };

            std::u8string digits {};
            append_number(digits, static_cast<unsigned>(month));
            return digits;
        }

        // 날짜 문장 셋이 같은 인자를 받는다. 언어마다 쓰는 이름이 달라(ko는 달 번호, en은 달 이름) 늘 모두 넘긴다.
        //  - 문장에 없는 이름의 인자는 읽히지 않는다.
        [[nodiscard]] std::u8string date_sentence(const key value, const std::chrono::year_month_day day, const std::u8string_view weekday)
        {
            const std::u8string month { month_name(day.month()) };
            const std::array<string_argument, 5> arguments {
                {
                    { u8"year", year_number(day.year()) }
                    , { u8"month", std::int64_t { static_cast<unsigned>(day.month()) } }
                    , { u8"day", std::int64_t { static_cast<unsigned>(day.day()) } }
                    , { u8"month_name", std::u8string_view { month } }
                    , { u8"weekday", weekday }
                }
            };
            return sentence(value, arguments);
        }
    }

    std::u8string_view time_hour() noexcept { return word(key::time_hour); }

    std::u8string_view time_minute() noexcept { return word(key::time_minute); }

    std::u8string_view time_am() noexcept { return word(key::time_am); }

    std::u8string_view time_pm() noexcept { return word(key::time_pm); }

    std::u8string_view time_hour_increase() noexcept { return word(key::time_hour_increase); }

    std::u8string_view time_hour_decrease() noexcept { return word(key::time_hour_decrease); }

    std::u8string_view time_minute_increase() noexcept { return word(key::time_minute_increase); }

    std::u8string_view time_minute_decrease() noexcept { return word(key::time_minute_decrease); }

    std::u8string_view info_bar_close() noexcept
    {
        return word(key::info_bar_close);
    }

    std::u8string_view severity_name(const ui_severity severity) noexcept
    {
        switch (severity)
        {
        case ui_severity::success:
            return word(key::severity_name_success);
        case ui_severity::warning:
            return word(key::severity_name_warning);
        case ui_severity::error:
            return word(key::severity_name_error);
        case ui_severity::info:
            break;
        }
        return word(key::severity_name_info);
    }

    std::u8string split_button_arrow_name(const std::u8string_view body_name)
    {
        const std::array<string_argument, 1> arguments { { { u8"label", body_name } } };
        return sentence(key::split_button_arrow_name, arguments);
    }

    std::u8string_view zoom_decrease() noexcept
    {
        return word(key::zoom_decrease);
    }

    std::u8string_view zoom_increase() noexcept
    {
        return word(key::zoom_increase);
    }

    std::u8string_view zoom_fit() noexcept
    {
        return word(key::zoom_fit);
    }

    std::u8string_view theme_picker_name() noexcept
    {
        return word(key::theme_picker_name);
    }

    std::u8string_view theme_picker_label(const theme_preference preference) noexcept
    {
        switch (preference)
        {
        case theme_preference::light:
            return word(key::theme_picker_label_light);
        case theme_preference::dark:
            return word(key::theme_picker_label_dark);
        case theme_preference::system:
            break;
        }
        return word(key::theme_picker_label_system);
    }

    std::u8string_view accent_picker_name() noexcept
    {
        return word(key::accent_picker_name);
    }

    std::u8string_view combo_box_loading() noexcept
    {
        return word(key::combo_box_loading);
    }

    std::u8string_view message_box_ok_label() noexcept
    {
        return word(key::message_box_ok_label);
    }

    std::u8string_view message_box_cancel_label() noexcept
    {
        return word(key::message_box_cancel_label);
    }

    std::u8string_view message_box_yes_label() noexcept
    {
        return word(key::message_box_yes_label);
    }

    std::u8string_view message_box_no_label() noexcept
    {
        return word(key::message_box_no_label);
    }

    std::u8string_view rating_name() noexcept
    {
        return word(key::rating_name);
    }

    std::u8string rating_value_name(const float value, const std::size_t count)
    {
        // NaN과 음수는 0이고 최댓값 위는 최댓값이다. 호출부가 이미 자른 값이지만 이 함수가 어떤 값에도 글을 낸다.
        const float bounded { value > 0.0f ? std::min(value, static_cast<float>(count)) : 0.0f };
        const std::size_t tenths { static_cast<std::size_t>(std::lround(bounded * 10.0f)) };
        // 표의 수 자리는 정수뿐이라 소수가 끼는 값은 글로 지어 넘긴다.
        std::u8string shown {};
        append_number(shown, tenths / 10u);
        if (tenths % 10u != 0u)
        {
            shown += u8'.';
            append_number(shown, tenths % 10u);
        }
        const std::array<string_argument, 2> arguments {
            {
                { u8"value", std::u8string_view { shown } }
                , { u8"maximum", static_cast<std::int64_t>(count) }
            }
        };
        return sentence(key::rating_value_name, arguments);
    }

    std::u8string_view undo_command_undo() noexcept
    {
        return word(key::undo_command_undo);
    }

    std::u8string_view undo_command_redo() noexcept
    {
        return word(key::undo_command_redo);
    }

    std::u8string_view ink_canvas_name() noexcept
    {
        return word(key::ink_canvas_name);
    }

    std::u8string ink_canvas_summary(const std::size_t strokes)
    {
        const std::array<string_argument, 1> arguments { { { u8"count", static_cast<std::int64_t>(strokes) } } };
        return sentence(key::ink_canvas_summary, arguments);
    }

    std::u8string_view color_picker_name() noexcept
    {
        return word(key::color_picker_name);
    }

    std::u8string_view color_picker_plane_name() noexcept
    {
        return word(key::color_picker_plane_name);
    }

    std::u8string color_picker_plane_value(const float saturation, const float value)
    {
        // NaN은 "0보다 크다"가 거짓이라 0이다. 1 위는 100%다.
        const auto percent = [](const float unit) { return std::int64_t { std::lround((unit > 0.0f ? std::min(unit, 1.0f) : 0.0f) * 100.0f) }; };
        const std::array<string_argument, 2> arguments {
            {
                { u8"saturation", percent(saturation) }
                , { u8"value", percent(value) }
            }
        };
        return sentence(key::color_picker_plane_value, arguments);
    }

    std::u8string_view color_picker_hue_name() noexcept
    {
        return word(key::color_picker_hue_name);
    }

    std::u8string_view color_picker_alpha_name() noexcept
    {
        return word(key::color_picker_alpha_name);
    }

    std::u8string_view color_picker_hex_name() noexcept
    {
        return word(key::color_picker_hex_name);
    }

    std::u8string_view rich_text_bold() noexcept
    {
        return word(key::rich_text_bold);
    }

    std::u8string_view rich_text_italic() noexcept
    {
        return word(key::rich_text_italic);
    }

    std::u8string_view rich_text_underline() noexcept
    {
        return word(key::rich_text_underline);
    }

    std::u8string_view rich_text_code() noexcept
    {
        return word(key::rich_text_code);
    }

    std::u8string_view rich_text_link() noexcept
    {
        return word(key::rich_text_link);
    }

    std::u8string_view rich_text_clear_format() noexcept
    {
        return word(key::rich_text_clear_format);
    }

    std::u8string_view menu_bar_name() noexcept
    {
        return word(key::menu_bar_name);
    }

    std::u8string_view date_field_name() noexcept
    {
        return word(key::date_field_name);
    }

    std::u8string_view date_field_placeholder() noexcept
    {
        return word(key::date_field_placeholder);
    }

    std::u8string date_field_text(const std::chrono::year_month_day day)
    {
        return date_sentence(key::date_field_text, day, {});
    }

    std::u8string_view calendar_name() noexcept
    {
        return word(key::calendar_name);
    }

    std::u8string_view calendar_previous_month() noexcept
    {
        return word(key::calendar_previous_month);
    }

    std::u8string_view calendar_next_month() noexcept
    {
        return word(key::calendar_next_month);
    }

    std::u8string calendar_month_title(const std::chrono::year_month month)
    {
        return date_sentence(key::calendar_month_title, month / std::chrono::day { 1 }, {});
    }

    std::u8string_view calendar_weekday_short(const std::chrono::weekday day) noexcept
    {
        return day.ok() ? word(weekday_short_names[day.c_encoding()]) : std::u8string_view {};
    }

    std::u8string calendar_day_name(const std::chrono::year_month_day day)
    {
        // 틀린 날은 요일을 붙이지 않는다. 요일 계산이 틀린 날에는 뜻이 없다.
        if (day.ok() == false)
            return date_sentence(key::date_field_text, day, {});
        return date_sentence(key::calendar_day_name, day, word(weekday_long_names[std::chrono::weekday { std::chrono::sys_days { day } }.c_encoding()]));
    }

    std::u8string_view calendar_today() noexcept
    {
        return word(key::calendar_today);
    }

    std::u8string_view calendar_unavailable() noexcept
    {
        return word(key::calendar_unavailable);
    }

    std::u8string_view teaching_tip_name() noexcept
    {
        return word(key::teaching_tip_name);
    }

    std::u8string_view teaching_tip_close() noexcept
    {
        return word(key::teaching_tip_close);
    }

    std::u8string_view text_or_default(const std::u8string_view configured, const std::u8string_view fallback) noexcept
    {
        return configured.empty() ? fallback : configured;
    }
}
