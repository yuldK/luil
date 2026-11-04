#include "luil/app/menu_popup.h"

#include "luil/ui/ui_tree.h"

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace luil {
    ui_popup make_menu_popup(menu_config menu, menu_popup_config config)
    {
        const float scale { std::isfinite(config.scale) && config.scale > 0.0f ? config.scale : 1.0f };
        ui_popup popup {};
        popup.id = std::move(config.id);
        popup.anchor = std::move(config.anchor);
        popup.x = config.x;
        popup.y = config.y;
        const float font_size { config.font_size.value_or(default_ui_style().metrics.body_font_size) };
        const font_weight weight { config.weight.value_or(default_ui_style().metrics.font_weights.body_text) };
        // 폭은 아래에서 tree를 배치하는 `scale`과 같은 배율로 잰다. 항목은 그 배율의 물리 글자 크기로 그려지기 때문이다.
        popup.width = std::isfinite(config.width) && config.width > 0.0f
            ? config.width
            : menu_element::width_for(menu, config.measurer ? config.measurer : styled_text_measurer({ weight }), font_size, scale);
        popup.height = menu_element::height_for(menu);
        // 크기를 안 뒤에 필드 아래의 자리를 정한다. 아래가 모자라면 위로 뒤집힌다.
        if (config.below.has_value())
        {
            const field_popup_position position { place_field_popup(*config.below, { popup.width, popup.height }, scale) };
            popup.x = position.x;
            popup.y = position.y;
        }
        popup.dismiss = std::move(config.dismiss);
        const popup_corner corner { config.corner.value_or(default_ui_style().popup.corner) };
        popup.corner = corner;
        popup.border = config.border;
        popup.backdrop = config.backdrop;
        // 둥근 창이면 메뉴의 사각 선이 모서리에서 튀어나온다. 경계는 OS 테두리가 긋는다.
        menu.border = config.border && popup_corner_radius(corner) <= 0.0f;
        auto root { std::make_unique<menu_element>(std::move(menu)) };
        popup.tree = std::make_shared<const ui_tree>(make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * scale, popup.height * scale }, scale));
        return popup;
    }

    ui_popup make_menu_bar_popup(const menu_bar_element& bar, menu_config menu, menu_popup_config popup, const rect_f limit)
    {
        const menu_bar_state state { bar.config().state };
        const std::function<input_action(const menu_bar_state&)>& set_state { bar.config().set_state };
        // 열린 메뉴의 이름이 곧 항목 키다. controller가 막대 메뉴가 바뀐 것(←/→)을 이 owner로 안다.
        menu.owner = state.current;
        if (set_state != nullptr)
        {
            if (menu.after_select == nullptr)
                menu.after_select = [set_state](const ui_action_context&) { return std::vector<input_action> { set_state(menu_bar_state {}) }; };
            // Esc는 메뉴만 닫고 막대는 선 채 남긴다. 그 밖의 닫힘(바깥 누름·창 비활성·이동)은 막대도 쉬게 한다.
            popup.dismiss = [set_state, state](const popup_dismiss_reason reason) {
                return set_state(reason == popup_dismiss_reason::escape_key ? menu_bar_state { state.current, false, state.keyboard } : menu_bar_state {});
            };
        }
        if (const std::optional<rect_f> item { bar.item_bounds(state.current) }; item.has_value())
            popup.below = field_popup_anchor { .field = *item, .limit = limit, .gap = 0.0f };
        return make_menu_popup(std::move(menu), std::move(popup));
    }
}
