#include "demo/ink_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"

#include <utility>

namespace demo {
    namespace {
        // 단추 줄의 높이와 줄 사이 간격이다 (논리 픽셀).
        constexpr float controls_height { 32.0f };
        constexpr float row_spacing { 8.0f };
    }

    bool ink_page::handle(const luil::app_message& message)
    {
        return sketch_.handle(message);
    }

    const ink_sketch& ink_page::sketch() const noexcept
    {
        return sketch_;
    }

    std::unique_ptr<luil::ui_element> ink_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-page" }, luil::stack_config { .spacing = row_spacing, .padding = luil::edge_insets::all(16.0f) }) };
        column->add(make_label({ kind_text, u8"ink-help" }, u8"펜으로 그린다 / 지우개 끝이나 지우개 도구로 닿은 획을 지운다 / 손가락은 켜기 전까지 그리지 않는다", 12.0f, luil::label_color_role::dim), 20.0f);

        luil::stack_config status_config {};
        status_config.direction = luil::stack_direction::row;
        status_config.spacing = 16.0f;
        auto status { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-status" }, status_config) };
        status->add(make_label({ kind_text, u8"ink-count" }, sketch_.count_text(), 14.0f, luil::label_color_role::primary), 96.0f);
        status->add(make_label({ kind_text, u8"ink-tool" }, sketch_.tool_text(), 14.0f, luil::label_color_role::primary), 112.0f);
        status->add_flexible(make_label({ kind_text, u8"ink-last" }, sketch_.last_text(), 14.0f, luil::label_color_role::primary));
        column->add(std::move(status), 24.0f);

        // 도구·지우기·되돌리기는 키보드가 닿는 단추다 (결정 9). 견본 단추는 캔버스가 내는 것과 같은 메시지를 낸다.
        luil::stack_config controls_config {};
        controls_config.direction = luil::stack_direction::row;
        controls_config.spacing = 8.0f;
        auto controls { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"ink-controls" }, controls_config) };
        const auto add_button = [&controls](std::u8string owner, std::u8string text, luil::ui_action action, const float button_width) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            controls->add(std::move(button), button_width);
        };
        add_button(u8"ink-tool-toggle", u8"도구 바꾸기", luil::make_message_action(ink_command_intent { ink_command::toggle_tool }), 96.0f);
        add_button(u8"ink-clear", u8"모두 지우기", luil::make_message_action(ink_command_intent { ink_command::clear }), 96.0f);
        add_button(u8"ink-sample", u8"견본 획", [actions = sketch_.sample_actions()](const luil::ui_action_context&) { return actions; }, 80.0f);
        const auto add_check = [&controls](std::u8string owner, std::u8string label, const bool checked, const ink_command command) {
            luil::check_config config { .owner = std::move(owner), .label = std::move(label), .checked = checked, .toggle = luil::make_message_action(ink_command_intent { command }) };
            const float check_width { 140.0f };
            controls->add(std::make_unique<luil::check_element>(std::move(config)), check_width);
        };
        add_check(u8"ink-mouse", u8"마우스로 그리기", sketch_.mouse(), ink_command::toggle_mouse);
        add_check(u8"ink-touch", u8"손가락으로 그리기", sketch_.touch(), ink_command::toggle_touch);
        column->add(std::move(controls), controls_height);

        // 캔버스는 투명하다. 판(바탕·테두리)은 앱이 깐다.
        luil::panel_config board_config {};
        board_config.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        board_config.corner_radius = 6.0f;
        board_config.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto board { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"ink-board" }, std::move(board_config)) };
        board->set_content(std::make_unique<luil::ink_canvas_element>(sketch_.canvas_config()));
        column->add_flexible(std::move(board));
        return column;
    }
}
