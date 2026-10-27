#include "demo/theme_page.h"

#include "luil/ui/choice_group_element.h"
#include "luil/ui/theme_picker_element.h"

#include <algorithm>
#include <utility>

namespace demo {
    namespace {
        [[nodiscard]] std::u8string backdrop_key(const luil::window_backdrop backdrop)
        {
            switch (backdrop)
            {
            case luil::window_backdrop::mica:
                return u8"mica";
            case luil::window_backdrop::mica_alt:
                return u8"mica_alt";
            case luil::window_backdrop::acrylic:
                return u8"acrylic";
            default:
                return u8"none";
            }
        }

        [[nodiscard]] luil::window_backdrop backdrop_from_key(const std::u8string_view key)
        {
            if (key == u8"mica")
                return luil::window_backdrop::mica;
            if (key == u8"mica_alt")
                return luil::window_backdrop::mica_alt;
            if (key == u8"acrylic")
                return luil::window_backdrop::acrylic;
            return luil::window_backdrop::none;
        }

        // 라이브러리가 알린 상태를 사람의 말로 옮긴다. 서지 않은 까닭은 사용자가 할 수 있는 일과 함께 적는다.
        [[nodiscard]] std::u8string backdrop_status_text(const std::optional<luil::win32::backdrop_report>& report)
        {
            if (report.has_value() == false)
                return u8"상태: 아직 알림이 없습니다.";
            switch (report->status)
            {
            case luil::win32::backdrop_status::off:
                return u8"상태: 효과를 쓰지 않습니다.";
            case luil::win32::backdrop_status::applied:
                return u8"상태: 효과가 섰습니다.";
            case luil::win32::backdrop_status::unsupported_presentation:
                return u8"상태: 레이어드 표시에서는 이 효과를 쓸 수 없습니다. Acrylic은 됩니다.";
            case luil::win32::backdrop_status::unsupported_system:
                return u8"상태: 이 Windows는 이 효과를 지원하지 않습니다.";
            case luil::win32::backdrop_status::transparency_disabled:
                return u8"상태: Windows 설정의 투명 효과가 꺼져 있습니다.";
            case luil::win32::backdrop_status::high_contrast:
                return u8"상태: 고대비에서는 효과를 깔지 않습니다.";
            case luil::win32::backdrop_status::remote_session:
                return u8"상태: 원격 데스크톱에서는 효과가 그려지지 않습니다.";
            }
            return {};
        }
    } // namespace

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
        if (const auto* const backdrop { message.get<backdrop_intent>() }; backdrop != nullptr)
        {
            backdrop_ = backdrop->backdrop;
            return true;
        }
        if (const auto* const report { message.get<backdrop_report_intent>() }; report != nullptr)
        {
            report_ = report->report;
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

        // 주 창 뒤의 배경 효과다. 효과가 선 창만 바탕과 캡션이 옅어지고, 서지 않으면 까닭을 라이브러리가 알린다.
        page.begin_card(u8"창 배경 효과", u8"창 뒤에 효과를 깝니다. 표면과 입력칸은 그대로 불투명합니다.");
        luil::choice_group_config backdrops {};
        backdrops.owner = u8"backdrop";
        backdrops.name = u8"창 배경 효과";
        backdrops.style = luil::choice_style::toggle;
        backdrops.items = { { u8"none", u8"없음" }, { u8"mica", u8"Mica" }, { u8"mica_alt", u8"Mica Alt" }, { u8"acrylic", u8"Acrylic" } };
        backdrops.selected = backdrop_key(backdrop_);
        backdrops.select = [](const std::u8string& value) { return luil::make_app_action(backdrop_intent { backdrop_from_key(value) }); };
        page.add(std::make_unique<luil::choice_group_element>(std::move(backdrops)), luil::choice_toggle_height);
        page.gap(6.0f);
        page.add(make_label({ kind_text, u8"backdrop-status" }, backdrop_status_text(report_), 11.0f, luil::label_color_role::dim), 18.0f);
        page.end_card();
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(theme_scroll_intent { delta }); });
    }
} // namespace demo
