#include "demo/common.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <utility>

namespace demo {
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
} // namespace demo
