#include "host/text_edit_menu.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <utility>

namespace luil {
    float text_edit_menu_height() noexcept
    {
        return 2.0f * std::max(48.0f, text_line_height(12.0f, 24.0f)) + 8.0f;
    }

    std::shared_ptr<const ui_tree> make_text_edit_menu(const text_input_snapshot& value, const bool korean, const float width, const float height, const float scale)
    {
        const bool selected { value.caret != value.anchor && value.composing == false };
        const std::u8string labels[] { korean ? u8"복사" : u8"Copy", korean ? u8"잘라내기" : u8"Cut", korean ? u8"붙여넣기" : u8"Paste", korean ? u8"전체 선택" : u8"Select all" };
        const std::u8string keys[] { u8"copy", u8"cut", u8"paste", u8"all" };
        const bool enabled[] { selected && value.secret == false, selected && value.secret == false && value.read_only == false, value.composing == false && value.read_only == false, value.text.empty() == false && value.composing == false };
        auto column { std::make_unique<stack_element>(ui_element_id { ui_element_kind::menu }, stack_config { .padding = { 4.0f, 4.0f, 4.0f, 4.0f } }) };
        for (int row { 0 }; row < 2; ++row)
        {
            auto buttons { std::make_unique<stack_element>(ui_element_id { ui_element_kind::menu, row == 0 ? u8"top" : u8"bottom" }, stack_config { .direction = stack_direction::row }) };
            for (int column_index { 0 }; column_index < 2; ++column_index)
            {
                const int index { row * 2 + column_index };
                auto button { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::menu_item, keys[index] }, text_button_config { .text = labels[index] }) };
                button->set_enabled(enabled[index]);
                // 입력은 host가 받으므로 액션이 없다. 그래도 자기 자리를 hit test해야 한다.
                button->set_hit_opaque(true);
                buttons->add_flexible(std::move(button));
            }
            // 키보드·작은 화면 때문에 높이가 줄어도 둘째 줄이 잘려 나가지 않는다.
            column->add_flexible(std::move(buttons));
        }
        auto panel { std::make_unique<panel_element>(ui_element_id { ui_element_kind::root }, panel_config {}) };
        panel->set_hit_opaque(true);
        panel->set_content(std::move(column));
        panel->arrange({ { 0.0f, 0.0f, width, height }, scale });
        return std::make_shared<const ui_tree>(std::move(panel));
    }
} // namespace luil
