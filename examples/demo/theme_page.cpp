#include "demo/theme_page.h"

#include "luil/app/menu_popup.h"
#include "luil/generated/codicons.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
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

        // 고르기 묶음마다 키 앞에 `window-`·`popup-` 같은 머리를 붙인다 (`backdrop_choice`). 머리는 떼고 읽는다.
        [[nodiscard]] luil::window_backdrop backdrop_from_key(std::u8string_view key)
        {
            if (const std::size_t prefix { key.rfind(u8'-') }; prefix != std::u8string_view::npos)
                key.remove_prefix(prefix + 1);
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

        // 견본 보조 창의 id·처음 크기다. 상태 보고의 `surface`가 이 id다.
        constexpr char8_t sample_window_id[] { u8"backdrop-sample" };
        constexpr float sample_window_width { 360.0f };
        constexpr float sample_window_height { 220.0f };
        constexpr float sample_menu_width { 200.0f };

        [[nodiscard]] luil::caption_config make_sample_caption()
        {
            luil::caption_config caption {};
            caption.title = u8"배경 효과 견본";
            caption.buttons = { .minimize = false, .maximize = false, .close = true };
            caption.close_tooltip = u8"Close";
            return caption;
        }

        // 효과 고르기 묶음이다. 항목의 접근성 id는 키에서 나오므로, 한 페이지의 묶음끼리 키가 겹치지 않게 머리를 붙인다
        // (`prefix`, 주 창 묶음은 비운다 — 지금까지의 id다).
        [[nodiscard]] luil::choice_group_config backdrop_choice(std::u8string owner, std::u8string name, const std::u8string& prefix, const luil::window_backdrop selected)
        {
            luil::choice_group_config choice {};
            choice.owner = std::move(owner);
            choice.name = std::move(name);
            choice.style = luil::choice_style::toggle;
            choice.items = { { prefix + u8"none", u8"없음" }, { prefix + u8"mica", u8"Mica" }, { prefix + u8"mica_alt", u8"Mica Alt" }, { prefix + u8"acrylic", u8"Acrylic" } };
            choice.selected = prefix + backdrop_key(selected);
            return choice;
        }
    }

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
            if (report->report.surface.empty())
                report_ = report->report;
            else if (report->report.surface == sample_window_id)
                sample_report_ = report->report;
            return true;
        }
        if (const auto* const backdrop { message.get<sample_window_backdrop_intent>() }; backdrop != nullptr)
        {
            sample_window_backdrop_ = backdrop->backdrop;
            return true;
        }
        if (const auto* const backdrop { message.get<sample_popup_backdrop_intent>() }; backdrop != nullptr)
        {
            sample_popup_backdrop_ = backdrop->backdrop;
            return true;
        }
        if (const auto* const toggle { message.get<sample_window_toggle_intent>() }; toggle != nullptr)
        {
            sample_window_open_ = toggle->open;
            // 창을 다시 만들면 상태도 처음부터 다시 온다.
            if (sample_window_open_ == false)
            {
                sample_report_.reset();
                sample_metrics_ = {};
            }
            return true;
        }
        if (const auto* const metrics { message.get<sample_window_metrics_intent>() }; metrics != nullptr)
        {
            sample_metrics_ = *metrics;
            return true;
        }
        if (const auto* const toggle { message.get<sample_menu_toggle_intent>() }; toggle != nullptr)
        {
            sample_menu_open_ = toggle->open;
            return true;
        }
        return false;
    }

    std::vector<luil::win32::ui_window> theme_page::make_windows() const
    {
        std::vector<luil::win32::ui_window> windows {};
        if (sample_window_open_ == false)
            return windows;
        luil::win32::ui_window window {};
        window.id = sample_window_id;
        window.caption = make_sample_caption();
        window.x = 80.0f;
        window.y = 80.0f;
        window.width = sample_window_width;
        window.height = sample_window_height;
        window.minimum_width = 240.0f;
        window.minimum_height = 160.0f;
        window.tree = make_sample_tree();
        window.metrics = [](const float width, const float height, const float scale) { return luil::app_message { sample_window_metrics_intent { width, height, scale } }; };
        window.close = [] { return luil::make_app_action(sample_window_toggle_intent { false }); };
        window.backdrop = sample_window_backdrop_;
        windows.push_back(std::move(window));
        return windows;
    }

    std::shared_ptr<const luil::ui_tree> theme_page::make_sample_tree() const
    {
        // metrics가 오기 전(창이 만들어지는 그 frame)에는 요청 크기로 배치한다.
        const float scale { sample_metrics_.scale > 0.0f ? sample_metrics_.scale : 1.0f };
        const float width { sample_metrics_.width > 0.0f ? sample_metrics_.width : sample_window_width * scale };
        const float height { sample_metrics_.height > 0.0f ? sample_metrics_.height : sample_window_height * scale };
        auto root { std::make_unique<luil::root_element>(sample_window_id) };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

        const luil::caption_config caption_config { make_sample_caption() };
        const float caption_height { luil::caption_element::height_for(caption_config) * scale };
        auto caption { std::make_unique<luil::caption_element>(caption_config) };
        caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
        root->add(std::move(caption));

        // 창 바탕은 효과를 비추고, 입력칸 같은 표면은 그대로 불투명하다.
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(16.0f);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"backdrop-sample" }, config) };
        column->add(make_label({ kind_text, u8"backdrop-sample-hint" }, u8"창 바탕이 효과를 비춥니다. 아래 칸은 불투명합니다.", 11.0f, luil::label_color_role::dim), 18.0f);
        column->add_gap(8.0f);
        column->add(make_label({ kind_text, u8"backdrop-sample-status" }, backdrop_status_text(sample_report_), 11.0f, luil::label_color_role::dim), 18.0f);
        column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(column));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    std::vector<luil::win32::ui_popup> theme_page::make_popups(const float scale) const
    {
        std::vector<luil::win32::ui_popup> popups {};
        if (sample_menu_open_ == false || sample_menu_button_ == nullptr)
            return popups;
        luil::menu_config menu {};
        menu.owner = u8"backdrop-menu";
        menu.items = {
            { u8"first", u8"메뉴 몸통이 효과를 비춥니다", luil::codicons::icon_color_mode },
            { u8"second", u8"항목은 그대로 고를 수 있습니다", luil::codicons::icon_check },
            { u8"close", u8"닫기", luil::codicons::icon_close },
        };
        menu.select = [](const std::u8string&) { return luil::make_app_action(sample_menu_toggle_intent { false }); };
        const luil::rect_f button { sample_menu_button_->bounds() };
        luil::menu_popup_config config {};
        config.id = u8"backdrop-menu";
        config.x = button.x / scale;
        config.y = (button.y + button.height) / scale + 2.0f;
        config.width = sample_menu_width;
        config.scale = scale;
        config.dismiss = [](const luil::win32::popup_dismiss_reason) { return luil::make_app_action(sample_menu_toggle_intent { false }); };
        config.backdrop = sample_popup_backdrop_;
        popups.push_back(luil::make_menu_popup(std::move(menu), std::move(config)));
        return popups;
    }

    std::unique_ptr<luil::ui_element> theme_page::build(const float width, const float height, const float scale)
    {
        sample_menu_button_ = nullptr;
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
        luil::choice_group_config backdrops { backdrop_choice(u8"backdrop", u8"창 배경 효과", {}, backdrop_) };
        backdrops.select = [](const std::u8string& value) { return luil::make_app_action(backdrop_intent { backdrop_from_key(value) }); };
        page.add(std::make_unique<luil::choice_group_element>(std::move(backdrops)), luil::choice_toggle_height);
        page.gap(6.0f);
        page.add(make_label({ kind_text, u8"backdrop-status" }, backdrop_status_text(report_), 11.0f, luil::label_color_role::dim), 18.0f);
        page.end_card();

        // 보조 창과 popup도 같은 값으로 고른다. 길은 표면마다 라이브러리가 정한다 — popup은 활성화되지 않아 Acrylic만 선다.
        page.begin_card(u8"보조 창 · popup 배경 효과", u8"견본 창과 견본 메뉴에 효과를 겁니다.");
        luil::choice_group_config windows { backdrop_choice(u8"backdrop-window", u8"보조 창 배경 효과", u8"window-", sample_window_backdrop_) };
        windows.select = [](const std::u8string& value) { return luil::make_app_action(sample_window_backdrop_intent { backdrop_from_key(value) }); };
        page.add(std::make_unique<luil::choice_group_element>(std::move(windows)), luil::choice_toggle_height);
        page.gap(6.0f);
        const std::u8string window_status { sample_window_open_ ? u8"견본 창 " + backdrop_status_text(sample_report_) : u8"견본 창을 열면 그 창의 상태가 나옵니다." };
        page.add(make_label({ kind_text, u8"backdrop-window-status" }, window_status, 11.0f, luil::label_color_role::dim), 18.0f);
        page.gap(8.0f);
        luil::choice_group_config popups { backdrop_choice(u8"backdrop-popup", u8"popup 배경 효과", u8"popup-", sample_popup_backdrop_) };
        popups.select = [](const std::u8string& value) { return luil::make_app_action(sample_popup_backdrop_intent { backdrop_from_key(value) }); };
        page.add(std::make_unique<luil::choice_group_element>(std::move(popups)), luil::choice_toggle_height);
        page.gap(6.0f);
        page.add(make_label({ kind_text, u8"backdrop-popup-help" }, u8"popup은 상태를 알리지 않습니다. Mica는 서지 않고 불투명합니다.", 11.0f, luil::label_color_role::dim), 18.0f);
        page.gap(10.0f);

        luil::text_button_config window_config {};
        window_config.text = sample_window_open_ ? u8"견본 창 닫기" : u8"견본 창 열기";
        auto window_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_toggle, u8"backdrop-window" }, std::move(window_config)) };
        window_button->set_cursor(luil::ui_cursor::hand);
        window_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(sample_window_toggle_intent { sample_window_open_ == false }));
        page.add(std::move(window_button), { .length = 26.0f, .cross_length = 140.0f });
        page.gap(8.0f);

        luil::text_button_config menu_config {};
        menu_config.text = u8"견본 메뉴 열기";
        menu_config.visual = sample_menu_open_ ? luil::text_button_visual::accent : luil::text_button_visual::normal;
        auto menu_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_tool_menu, u8"backdrop-menu" }, std::move(menu_config)) };
        menu_button->set_cursor(luil::ui_cursor::hand);
        menu_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(sample_menu_toggle_intent { sample_menu_open_ == false }));
        sample_menu_button_ = menu_button.get();
        page.add(std::move(menu_button), { .length = 26.0f, .cross_length = 140.0f });
        page.end_card();
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(theme_scroll_intent { delta }); });
    }
}
