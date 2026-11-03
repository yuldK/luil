#include "ui/default_text.h"

#include <algorithm>
#include <cmath>

namespace luil::default_text {
    namespace {
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
    }

    std::u8string_view info_bar_close() noexcept
    {
        return u8"닫기";
    }

    std::u8string_view severity_name(const ui_severity severity) noexcept
    {
        switch (severity)
        {
        case ui_severity::success:
            return u8"성공";
        case ui_severity::warning:
            return u8"경고";
        case ui_severity::error:
            return u8"오류";
        case ui_severity::info:
            break;
        }
        return u8"정보";
    }

    std::u8string split_button_arrow_name(const std::u8string_view body_name)
    {
        std::u8string name { body_name };
        name += u8" 옵션";
        return name;
    }

    std::u8string_view zoom_decrease() noexcept
    {
        return u8"축소";
    }

    std::u8string_view zoom_increase() noexcept
    {
        return u8"확대";
    }

    std::u8string_view zoom_fit() noexcept
    {
        return u8"맞춤";
    }

    std::u8string_view theme_picker_name() noexcept
    {
        return u8"테마";
    }

    std::u8string_view theme_picker_label(const theme_preference preference) noexcept
    {
        switch (preference)
        {
        case theme_preference::light:
            return u8"밝게";
        case theme_preference::dark:
            return u8"어둡게";
        case theme_preference::system:
            break;
        }
        return u8"시스템";
    }

    std::u8string_view accent_picker_name() noexcept
    {
        return u8"강조색";
    }

    std::u8string_view combo_box_loading() noexcept
    {
        return u8"추천 항목을 불러오는 중";
    }

    std::u8string_view message_box_ok_label() noexcept
    {
        return u8"확인";
    }

    std::u8string_view message_box_cancel_label() noexcept
    {
        return u8"취소";
    }

    std::u8string_view message_box_yes_label() noexcept
    {
        return u8"예";
    }

    std::u8string_view message_box_no_label() noexcept
    {
        return u8"아니요";
    }

    std::u8string_view rating_name() noexcept
    {
        return u8"별점";
    }

    std::u8string rating_value_name(const float value, const std::size_t count)
    {
        // NaN과 음수는 0이고 최댓값 위는 최댓값이다. 호출부가 이미 자른 값이지만 이 함수가 어떤 값에도 글을 낸다.
        const float bounded { value > 0.0f ? std::min(value, static_cast<float>(count)) : 0.0f };
        const std::size_t tenths { static_cast<std::size_t>(std::lround(bounded * 10.0f)) };
        std::u8string name {};
        append_number(name, tenths / 10u);
        if (tenths % 10u != 0u)
        {
            name += u8'.';
            append_number(name, tenths % 10u);
        }
        name += u8" / ";
        append_number(name, count);
        return name;
    }

    std::u8string_view undo_command_undo() noexcept
    {
        return u8"실행 취소";
    }

    std::u8string_view undo_command_redo() noexcept
    {
        return u8"다시 실행";
    }

    std::u8string_view ink_canvas_name() noexcept
    {
        return u8"잉크 캔버스";
    }

    std::u8string ink_canvas_summary(const std::size_t strokes)
    {
        std::u8string summary { u8"획 " };
        append_number(summary, strokes);
        summary += u8"개";
        return summary;
    }

    std::u8string_view color_picker_name() noexcept
    {
        return u8"색";
    }

    std::u8string_view color_picker_plane_name() noexcept
    {
        return u8"채도와 명도";
    }

    std::u8string color_picker_plane_value(const float saturation, const float value)
    {
        // NaN은 "0보다 크다"가 거짓이라 0이다. 1 위는 100%다.
        const auto percent = [](const float unit) { return static_cast<std::size_t>(std::lround((unit > 0.0f ? std::min(unit, 1.0f) : 0.0f) * 100.0f)); };
        std::u8string text { u8"채도 " };
        append_number(text, percent(saturation));
        text += u8"%, 명도 ";
        append_number(text, percent(value));
        text += u8'%';
        return text;
    }

    std::u8string_view color_picker_hue_name() noexcept
    {
        return u8"색상";
    }

    std::u8string_view color_picker_alpha_name() noexcept
    {
        return u8"불투명도";
    }

    std::u8string_view color_picker_hex_name() noexcept
    {
        return u8"16진 색";
    }

    std::u8string_view rich_text_bold() noexcept
    {
        return u8"굵게";
    }

    std::u8string_view rich_text_italic() noexcept
    {
        return u8"기울임꼴";
    }

    std::u8string_view rich_text_underline() noexcept
    {
        return u8"밑줄";
    }

    std::u8string_view rich_text_code() noexcept
    {
        return u8"코드";
    }

    std::u8string_view rich_text_link() noexcept
    {
        return u8"링크";
    }

    std::u8string_view rich_text_clear_format() noexcept
    {
        return u8"서식 지우기";
    }

    std::u8string_view text_or_default(const std::u8string_view configured, const std::u8string_view fallback) noexcept
    {
        return configured.empty() ? fallback : configured;
    }
}
