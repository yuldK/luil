#include "demo/common.h"

#include "luil/ui/scroll_area_element.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <utility>

namespace demo {
    namespace {
        // 본문의 바깥 여백과 카드의 안쪽 여백이다 (논리 픽셀).
        constexpr luil::edge_insets page_padding { 40.0f, 32.0f, 40.0f, 32.0f };
        constexpr float card_padding { 16.0f };
        // 카드 사이 간격이다.
        constexpr float card_spacing { 16.0f };
    }

    page_layout::page_layout(std::u8string owner, const float max_width)
        : owner_ { std::move(owner) }
        , max_width_ { max_width }
    {
        luil::stack_config config {};
        config.padding = page_padding;
        column_ = std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, owner_ }, config);
        height_ = page_padding.top + page_padding.bottom;
    }

    float page_layout::content_width(const float width, const float max_width) noexcept
    {
        const float available { width - page_padding.left - page_padding.right - luil::scroll_area_bar_width };
        return std::max(0.0f, std::min(available, max_width));
    }

    float page_layout::card_content_width(const float width, const float max_width) noexcept
    {
        return std::max(0.0f, content_width(width, max_width) - card_padding * 2.0f);
    }

    luil::stack_element& page_layout::target() noexcept
    {
        return card_ != nullptr ? *card_ : *column_;
    }

    void page_layout::add(std::unique_ptr<luil::ui_element> element, const float height)
    {
        target().add(std::move(element), height);
        (card_ != nullptr ? card_height_ : height_) += height;
    }

    void page_layout::add(std::unique_ptr<luil::ui_element> element, const luil::stack_item item)
    {
        target().add(std::move(element), item);
        (card_ != nullptr ? card_height_ : height_) += item.length;
    }

    void page_layout::gap(const float height)
    {
        target().add_gap(height);
        (card_ != nullptr ? card_height_ : height_) += height;
    }

    void page_layout::heading(std::u8string title, std::u8string description)
    {
        const std::u8string key { owner_ + u8"/heading/" + to_u8(heading_++) };
        add(make_label({ kind_text, key }, std::move(title), 18.0f, luil::label_color_role::primary), 28.0f);
        add(make_label({ kind_text, key + u8"/description" }, std::move(description), 12.0f, luil::label_color_role::dim), 20.0f);
        gap(20.0f);
    }

    void page_layout::begin_card(std::u8string title, std::u8string description)
    {
        luil::stack_config config {};
        config.padding = luil::edge_insets::all(card_padding);
        const std::u8string key { owner_ + u8"/card/" + to_u8(cards_) };
        card_ = std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, key + u8"/body" }, config);
        card_height_ = card_padding * 2.0f;
        if (title.empty() == false)
            add(make_label({ kind_text, key + u8"/title" }, std::move(title), 14.0f, luil::label_color_role::primary), 22.0f);
        if (description.empty() == false)
            add(make_label({ kind_text, key + u8"/description" }, std::move(description), 11.0f, luil::label_color_role::dim), 18.0f);
        gap(10.0f);
    }

    void page_layout::end_card()
    {
        if (card_ == nullptr)
            return;

        // 앱 화면 규칙의 카드다: 전경색의 옅은 알파 바탕, 1px 구분선 테, 8px 모서리.
        luil::panel_config config {};
        config.background = [](const luil::ui_color_palette& palette) { return luil::with_alpha(palette.primary_foreground, 0.035f); };
        config.border = [](const luil::ui_color_palette& palette) { return palette.divider; };
        config.corner_radius = 8.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, owner_ + u8"/card/" + to_u8(cards_++) }, std::move(config)) };
        panel->set_content(std::move(card_));
        const float height { card_height_ };
        card_height_ = 0.0f;
        if (column_->children().empty() == false && cards_ > 1)
            gap(card_spacing);
        add(std::move(panel), height);
    }

    std::unique_ptr<luil::ui_element> page_layout::finish(const float offset, std::function<luil::input_action(float)> scroll)
    {
        end_card();
        // 본문을 최대 폭에 묶어 왼쪽에 둔다. 남는 자리는 비워 둔다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, owner_ + u8"/frame" }, row_config) };
        row->add(std::move(column_), luil::stack_item { .weight = 1.0f, .maximum = max_width_ + page_padding.left + page_padding.right });

        luil::scroll_area_config config {};
        config.owner = owner_;
        config.content_height = height_;
        config.scroll_offset = offset;
        config.scroll = std::move(scroll);
        config.bar = luil::scrollbar_visibility::always;
        auto area { std::make_unique<luil::scroll_area_element>(std::move(config)) };
        area->set_content(std::move(row));
        return area;
    }
    std::u8string to_u8(const int value)
    {
        const std::string digits { std::to_string(value) };
        return std::u8string { reinterpret_cast<const char8_t*>(digits.c_str()) };
    }

    bool search_popup_stays(const luil::win32::popup_dismiss_reason reason) noexcept
    {
        return reason == luil::win32::popup_dismiss_reason::surface_moved || reason == luil::win32::popup_dismiss_reason::activation_changed;
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
