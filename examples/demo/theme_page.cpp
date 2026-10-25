#include "demo/theme_page.h"

#include "luil/ui/theme_picker_element.h"

#include <algorithm>
#include <utility>

namespace demo {
    bool theme_page::handle(const luil::app_message& message)
    {
        if (const auto* const scroll { message.get<theme_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
            return true;
        }
        if (const auto* const theme { message.get<theme_intent>() }; theme != nullptr)
        {
            appearance_.theme = theme->theme;
            return true;
        }
        if (const auto* const accent { message.get<accent_intent>() }; accent != nullptr)
        {
            appearance_.accent_id = accent->id;
            return true;
        }
        return false;
    }

    std::unique_ptr<luil::ui_element> theme_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(height);
        static_cast<void>(scale);
        // 카드 안의 element가 실제로 받는 폭이다. 테마 카드와 견본은 이 폭으로 줄 수를 정한다.
        const float content_width { page_layout::card_content_width(width) };
        page_layout page { u8"theme" };
        page.heading(u8"모양과 색", u8"고른 값은 화면 전체에 바로 적용됩니다.");

        page.begin_card(u8"테마");
        luil::theme_picker_config themes {};
        themes.owner = u8"theme";
        themes.selected = appearance_.theme;
        themes.accent_id = appearance_.accent_id;
        themes.select = [](const luil::theme_preference value) { return luil::make_app_action(theme_intent { value }); };
        const float themes_height { luil::theme_picker_element::height_for(themes, content_width) };
        page.add(std::make_unique<luil::theme_picker_element>(std::move(themes)), themes_height);
        page.gap(6.0f);
        page.add(make_label({ kind_text, u8"theme-system-help" }, u8"'시스템'을 고르면 Windows의 앱 모드를 따릅니다.", 11.0f, luil::label_color_role::dim), 18.0f);
        page.end_card();

        page.begin_card(u8"강조색 · " + std::u8string { luil::accent_for(appearance_.accent_id).label });
        luil::accent_picker_config accents {};
        accents.owner = u8"accent";
        accents.selected = appearance_.accent_id;
        accents.select = [](const std::u8string& key) { return luil::make_app_action(accent_intent { key }); };
        const float accents_height { luil::accent_picker_element::height_for(accents, content_width) };
        page.add(std::make_unique<luil::accent_picker_element>(std::move(accents)), accents_height);
        page.gap(6.0f);
        page.add(make_label({ kind_text, u8"accent-help" }, u8"강조색은 기본 단추 · 선택 · 초점 테에 함께 쓰입니다.", 11.0f,
                     luil::label_color_role::dim),
            18.0f);
        page.end_card();
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(theme_scroll_intent { delta }); });
    }
} // namespace demo
