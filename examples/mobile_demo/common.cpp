#include "mobile_demo/common.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <utility>

namespace mobile_demo {
    std::u8string to_u8(const int value)
    {
        const std::string digits { std::to_string(value) };
        return std::u8string { reinterpret_cast<const char8_t*>(digits.c_str()) };
    }

    std::optional<luil::text_input_target> text_target_of_kind(const luil::ui_element_kind kind) noexcept
    {
        if (kind == kind_number_input)
            return target_number;
        if (kind == kind_note_input)
            return target_note;
        if (kind == kind_secret_input)
            return target_secret;
        if (kind == kind_memo_input)
            return target_memo;
        if (kind == kind_code_input)
            return target_code;
        if (kind == kind_line_break_input)
            return target_line_break;
        if (kind == kind_readonly_input)
            return target_readonly;
        if (kind == kind_color_hex_input)
            return target_color_hex;
        if (kind == kind_rich_editor_input)
            return target_rich_editor;
        // popup 안의 검색 칸도 여느 텍스트 칸과 같이 등록한다 (popup-ime-design.md).
        if (kind == kind_dropdown_search_input)
            return target_dropdown_search;
        if (kind == kind_smb_address_input)
            return target_smb_address;
        if (kind == kind_smb_user_input)
            return target_smb_user;
        if (kind == kind_smb_password_input)
            return target_smb_password;
        return std::nullopt;
    }

    luil::text_input_config make_secret_input_config(std::u8string placeholder, const bool reveal, luil::ui_action toggle)
    {
        // 단추의 글리프는 누르면 무엇이 되는지를 말한다 — 보이는 중이면 감은 눈이다.
        luil::text_input_config config { .placeholder = std::move(placeholder) };
        config.secret = true;
        config.reveal = reveal;
        config.clear = std::move(toggle);
        config.clear_glyph = reveal ? luil::codicons::icon_eye_closed : luil::codicons::icon_eye;
        config.clear_tooltip = reveal ? u8"암호 가리기" : u8"암호 보이기";
        return config;
    }

    bool search_popup_stays(const luil::popup_dismiss_reason reason) noexcept
    {
        // 휴대폰에는 창이 움직이는 계기가 없다. 알림 창을 내려 보는 것만 남는다.
        return reason == luil::popup_dismiss_reason::activation_changed;
    }

    page_column::page_column(std::u8string owner, const float padding)
        : padding_ { padding }
        , height_ { 2.0f * padding }
    {
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(padding);
        column_ = std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, std::move(owner) }, config);
    }

    void page_column::add(std::unique_ptr<luil::ui_element> child, const float height)
    {
        column_->add(std::move(child), height);
        height_ += height;
    }

    void page_column::add(std::unique_ptr<luil::ui_element> child, const float height, const float width)
    {
        column_->add(std::move(child), { .length = height, .cross_length = width });
        height_ += height;
    }

    void page_column::gap(const float height)
    {
        column_->add_gap(height);
        height_ += height;
    }

    void page_column::note(std::u8string owner, std::u8string text)
    {
        add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(text), note_text_size, luil::label_color_role::dim),
            luil::text_line_height(note_text_size, note_line_height - note_text_size));
    }

    void page_column::wrapped_note(std::u8string owner, std::u8string text, const float page_width, const float scale, const luil::text_measurer* const measurer)
    {
        luil::label_config config {};
        config.text = std::move(text);
        config.font_size = note_text_size;
        config.color = luil::label_color_role::dim;
        config.wrap = true;
        // 줄 나눔 규칙을 스타일에 맡기면 여기서 잴 때와 배치할 때의 규칙이 갈려 줄 수가 달라질 수 있다.
        config.line_break = luil::text::line_break_policy::hangul_words;
        // 한 줄이면 한 줄 `note`와 같은 높이를 지켜 위아래 간격이 그대로다.
        const float one_line { luil::text_line_height(note_text_size, note_line_height - note_text_size) };
        const float measured { luil::label_element::measure_wrap(config, luil::measure_context { .scale = scale, .measurer = measurer }, page_width - 2.0f * padding_).height };
        add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, std::move(owner) }, std::move(config)), std::max(one_line, measured));
    }

    void page_column::body(std::u8string owner, std::u8string text)
    {
        add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(text), body_text_size, luil::label_color_role::primary),
            luil::text_line_height(body_text_size, body_line_height - body_text_size));
    }

    void page_column::heading(std::u8string owner, std::u8string text)
    {
        add(make_label(luil::ui_element_id { kind_text, std::move(owner) }, std::move(text), heading_text_size, luil::label_color_role::primary),
            luil::text_line_height(heading_text_size, heading_line_height - heading_text_size));
    }

    page_content page_column::finish()
    {
        return page_content { std::move(column_), height_ };
    }

    std::unique_ptr<luil::label_element> make_label(luil::ui_element_id id, std::u8string text, const float font_size, const luil::label_color_role color)
    {
        luil::label_config config {};
        config.text = std::move(text);
        config.font_size = font_size;
        config.color = color;
        return std::make_unique<luil::label_element>(std::move(id), std::move(config));
    }

    float search_menu_height(const luil::menu_config& menu) noexcept
    {
        // 둘레 여백 두 번 + 검색 칸 + 칸과 메뉴 사이 한 번.
        return 3.0f * search_menu_padding + search_menu_field_height + luil::menu_element::height_for(menu);
    }

    std::vector<luil::menu_item_config> narrow_menu_items(std::vector<luil::menu_item_config> items, const luil::text_input_view& search)
    {
        // 이 앱의 매칭 정책이다: 이름에 그 글이 들어 있으면 맞다.
        // 빈 질의는 전부 통과시킨다.
        const auto matches = [](const std::u8string_view label, const std::u8string_view query) { return query.empty() || label.find(query) != std::u8string_view::npos; };
        // 그 질의로 맞는 것이 하나라도 있는지만 답한다.
        // 조합 중인 글자를 어떻게 다룰지는 라이브러리가 이 답으로 정한다.
        const auto any_match = [&items, &matches](const std::u8string_view candidate) {
            for (const luil::menu_item_config& item : items)
                if (matches(item.label, candidate))
                    return true;
            return false;
        };
        const std::u8string_view query { luil::search_query(search, any_match) };

        std::vector<luil::menu_item_config> narrowed {};
        for (luil::menu_item_config& item : items)
            if (matches(item.label, query))
                narrowed.push_back(std::move(item));
        if (narrowed.empty())
        {
            luil::menu_item_config empty {};
            empty.key = u8"none";
            empty.label = u8"일치하는 항목 없음";
            empty.enabled = false;
            narrowed.push_back(std::move(empty));
        }
        return narrowed;
    }

    std::shared_ptr<const luil::ui_tree> make_search_menu_tree(
        luil::menu_config menu, const luil::ui_element_kind search_kind, luil::text_input_view search, std::u8string placeholder, const float width, const float scale)
    {
        const float height { search_menu_height(menu) };
        const float menu_height { luil::menu_element::height_for(menu) };
        std::u8string owner { menu.owner };

        luil::stack_config config {};
        config.padding = luil::edge_insets::all(search_menu_padding);
        config.spacing = search_menu_padding;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"search-" + owner }, config) };
        luil::text_input_config search_config {};
        search_config.placeholder = std::move(placeholder);
        column->add(std::make_unique<luil::text_input_element>(luil::ui_element_id { search_kind }, std::move(search), std::move(search_config)), search_menu_field_height);
        column->add(std::make_unique<luil::menu_element>(std::move(menu)), menu_height);

        // popup 창 전체를 칠한다. 메뉴는 자기 배경과 테두리를 그 위에 그린다.
        luil::panel_config background {};
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_page_panel, u8"search-" + std::move(owner) }, std::move(background)) };
        // 여백을 눌러도 아래 element로 새지 않는다 (메뉴와 같은 규칙이다).
        panel->set_hit_opaque(true);
        panel->set_content(std::move(column));
        // root가 자식까지 배치하는 tree라 표준 입구를 쓴다.
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(panel), { 0.0f, 0.0f, width * scale, height * scale }, scale));
    }
}
