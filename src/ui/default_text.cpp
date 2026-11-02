#include "ui/default_text.h"

namespace luil::default_text {
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

    std::u8string_view text_or_default(const std::u8string_view configured, const std::u8string_view fallback) noexcept
    {
        return configured.empty() ? fallback : configured;
    }
}
