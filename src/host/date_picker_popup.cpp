#include "luil/app/date_picker_popup.h"

#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_tree.h"

#include <cmath>
#include <memory>
#include <utility>

namespace luil {
    ui_popup make_date_picker_popup(calendar_config calendar, date_picker_popup_config config)
    {
        const float scale { std::isfinite(config.scale) && config.scale > 0.0f ? config.scale : 1.0f };
        const size_f body { calendar_element::size_for(calendar) };
        ui_popup popup {};
        popup.id = std::move(config.id);
        popup.anchor = std::move(config.anchor);
        popup.width = body.width + date_picker_popup_padding * 2.0f;
        popup.height = body.height + date_picker_popup_padding * 2.0f;
        // 크기를 안 뒤에 칸 아래의 자리를 정한다.
        const field_popup_position position { place_field_popup(config.below, { popup.width, popup.height }, scale) };
        popup.x = position.x;
        popup.y = position.y;
        popup.dismiss = std::move(config.dismiss);
        popup.corner = config.corner.value_or(default_ui_style().popup.corner);

        // 바탕 판이 popup 창 전체를 칠한다. 여백을 눌러도 아래 element로 새지 않는다 (메뉴·색 고르기와 같은 규칙이다).
        const std::u8string owner { popup.id };
        stack_config column {};
        column.padding = edge_insets::all(date_picker_popup_padding);
        auto stack { std::make_unique<stack_element>(ui_element_id { ui_element_kind::none, owner }, column) };
        stack->add(std::make_unique<calendar_element>(std::move(calendar)), stack_item { .length = body.height, .cross_length = body.width });
        auto panel { std::make_unique<panel_element>(ui_element_id { ui_element_kind::root, owner }, panel_config {}) };
        panel->set_hit_opaque(true);
        panel->set_content(std::move(stack));
        popup.tree = std::make_shared<const ui_tree>(make_arranged_tree(std::move(panel), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        return popup;
    }
}
