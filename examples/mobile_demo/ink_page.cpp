#include "mobile_demo/ink_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"

#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float row_spacing { 8.0f };
    }

    bool ink_page::handle(const luil::app_message& message)
    {
        return sketch_.handle(message);
    }

    const demo::ink_sketch& ink_page::sketch() const noexcept
    {
        return sketch_;
    }

    std::unique_ptr<luil::ui_element> ink_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        const luil::stack_config column_config { .spacing = row_spacing, .padding = luil::edge_insets::all(page_padding) };
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-page" }, column_config) };
        column->add(make_label({ kind_text, u8"ink-help" }, u8"스타일러스로 그리고 지우개 끝으로 지운다", note_text_size, luil::label_color_role::dim), note_line_height);
        column->add(make_label({ kind_text, u8"ink-count" }, sketch_.count_text(), body_text_size, luil::label_color_role::primary), body_line_height);
        column->add(make_label({ kind_text, u8"ink-tool" }, sketch_.tool_text(), body_text_size, luil::label_color_role::primary), body_line_height);
        column->add(make_label({ kind_text, u8"ink-last" }, sketch_.last_text(), body_text_size, luil::label_color_role::primary), body_line_height);

        // 손가락으로 누르는 단추 줄과 켜는 칸 줄이다. 견본 단추는 캔버스가 내는 것과 같은 메시지를 낸다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = row_spacing;
        auto buttons { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-controls" }, row_config) };
        const auto add_button = [&buttons](std::u8string owner, std::u8string text, luil::ui_action action) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            buttons->add_flexible(std::move(button));
        };
        add_button(u8"ink-tool-toggle", u8"도구", luil::make_message_action(demo::ink_command_intent { demo::ink_command::toggle_tool }));
        add_button(u8"ink-clear", u8"모두 지우기", luil::make_message_action(demo::ink_command_intent { demo::ink_command::clear }));
        add_button(u8"ink-sample", u8"견본 획", [actions = sketch_.sample_actions()](const luil::ui_action_context&) { return actions; });
        column->add(std::move(buttons), touch_height);

        auto checks { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-checks" }, row_config) };
        const auto add_check = [&checks](std::u8string owner, std::u8string label, const bool checked, const demo::ink_command command) {
            checks->add_flexible(std::make_unique<luil::check_element>(
                luil::check_config { .owner = std::move(owner), .label = std::move(label), .checked = checked, .toggle = luil::make_message_action(demo::ink_command_intent { command }) }));
        };
        add_check(u8"ink-mouse", u8"마우스로", sketch_.mouse(), demo::ink_command::toggle_mouse);
        add_check(u8"ink-touch", u8"손가락으로", sketch_.touch(), demo::ink_command::toggle_touch);
        column->add(std::move(checks), touch_height);

        // 캔버스는 투명하다. 판(바탕·테두리)은 앱이 깐다. 페이지를 흘리지 않아 손가락을 켜도 스크롤과 다투지 않는다.
        luil::panel_config board_config {};
        board_config.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        board_config.corner_radius = 8.0f;
        board_config.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto board { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"ink-board" }, std::move(board_config)) };
        board->set_content(std::make_unique<luil::ink_canvas_element>(sketch_.canvas_config()));
        column->add_flexible(std::move(board));
        return column;
    }
}
