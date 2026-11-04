#include "demo/inspector_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_inspect.h"

#include <utility>

namespace demo {
    namespace {
        // 단추 줄의 높이와 줄 사이 간격이다 (논리 픽셀).
        constexpr float controls_height { 32.0f };
        constexpr float row_spacing { 8.0f };

        // 검사기가 앱 kind에 붙이는 이름이다. 라이브러리 kind는 이름표가 알고 있다.
        [[nodiscard]] std::vector<luil::inspector_kind_name> demo_kind_names()
        {
            return { { kind_layout, u8"layout" }, { kind_text, u8"text" } };
        }
    }

    bool inspector_page::handle(const luil::app_message& message)
    {
        const auto* const intent { message.get<inspector_command_intent>() };
        if (intent == nullptr)
            return false;
        // 선택 이동은 방금 지은 이 페이지의 tree 위에서 움직인다.
        if (inspector_state::moves_selection(intent->command))
        {
            const luil::ui_tree tree { page_tree() };
            state_.apply(intent->command, &tree);
        }
        else
            state_.apply(intent->command, nullptr);
        return true;
    }

    luil::inspector_config inspector_page::config() const
    {
        return state_.config(demo_kind_names());
    }

    const inspector_state& inspector_page::state() const noexcept
    {
        return state_;
    }

    luil::ui_tree inspector_page::page_tree() const
    {
        return luil::make_arranged_tree(assemble(), { 0.0f, 0.0f, width_ * scale_, height_ * scale_ }, scale_);
    }

    std::unique_ptr<luil::ui_element> inspector_page::build(const float width, const float height, const float scale)
    {
        width_ = width;
        height_ = height;
        scale_ = scale > 0.0f ? scale : 1.0f;
        // 요소 수는 이 페이지의 tree를 지어 센 값이다. 상태 글이 달라져도 요소 수는 같으므로 한 번 센 뒤 다시 짓는다.
        state_.note_elements(luil::inspect_nodes(page_tree()).size());
        return assemble();
    }

    std::unique_ptr<luil::ui_element> inspector_page::assemble() const
    {
        const luil::inspector_config names { config() };
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-page" }, luil::stack_config { .spacing = row_spacing, .padding = luil::edge_insets::all(16.0f) }) };
        column->add(make_label({ kind_text, u8"inspector-help" }, u8"Ctrl+Shift+I로 켠다 / 마우스를 올린 요소에 윤곽과 정보 상자가 얹힌다 / 단추로 조상과 선택을 옮긴다", 12.0f, luil::label_color_role::dim), 20.0f);

        luil::stack_config status_config {};
        status_config.direction = luil::stack_direction::row;
        status_config.spacing = 16.0f;
        auto status { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-status" }, status_config) };
        status->add(make_label({ kind_text, u8"inspector-state" }, state_.state_text(), 14.0f, luil::label_color_role::primary), 112.0f);
        status->add(make_label({ kind_text, u8"inspector-pick" }, state_.pick_text(), 14.0f, luil::label_color_role::primary), 136.0f);
        status->add(make_label({ kind_text, u8"inspector-levels" }, state_.levels_text(), 14.0f, luil::label_color_role::primary), 128.0f);
        status->add(make_label({ kind_text, u8"inspector-count" }, state_.count_text(), 14.0f, luil::label_color_role::primary), 96.0f);
        column->add(std::move(status), 24.0f);
        column->add(make_label({ kind_text, u8"inspector-selected" }, state_.selected_text(names), 14.0f, luil::label_color_role::primary), 24.0f);

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        const auto add_button = [](luil::stack_element& row, std::u8string owner, std::u8string text, const inspector_command command, const float button_width) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(inspector_command_intent { command }));
            row.add(std::move(button), button_width);
        };
        auto switches { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-switches" }, row_config) };
        add_button(*switches, u8"inspector-toggle", u8"검사기 켜기·끄기", inspector_command::toggle, 128.0f);
        add_button(*switches, u8"inspector-up", u8"조상 위로", inspector_command::levels_up, 96.0f);
        add_button(*switches, u8"inspector-down", u8"조상 아래로", inspector_command::levels_down, 96.0f);
        add_button(*switches, u8"inspector-outline", state_.outline_text(), inspector_command::toggle_outline, 128.0f);
        add_button(*switches, u8"inspector-palette", state_.palette_text(), inspector_command::toggle_palette, 144.0f);
        column->add(std::move(switches), controls_height);

        auto moves { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-moves" }, row_config) };
        add_button(*moves, u8"inspector-parent", u8"부모", inspector_command::select_parent, 80.0f);
        add_button(*moves, u8"inspector-child", u8"첫 자식", inspector_command::select_child, 80.0f);
        add_button(*moves, u8"inspector-prev", u8"이전", inspector_command::select_previous, 80.0f);
        add_button(*moves, u8"inspector-next", u8"다음", inspector_command::select_next, 80.0f);
        column->add(std::move(moves), controls_height);

        // 들여다볼 견본이다. 안쪽 여백(padding)과 간격(spacing)이 있는 stack이라 안쪽 여백 띠와 inset 수치가 보인다.
        luil::panel_config sample_config {};
        sample_config.corner_radius = 6.0f;
        sample_config.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto sample { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"inspector-sample" }, std::move(sample_config)) };
        luil::stack_config sample_stack_config {};
        sample_stack_config.spacing = 8.0f;
        sample_stack_config.padding = luil::edge_insets::symmetric(12.0f, 8.0f);
        auto sample_stack { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-sample-stack" }, sample_stack_config) };
        sample_stack->add(make_label({ kind_text, u8"inspector-sample-a" }, u8"견본 첫째 줄", 14.0f, luil::label_color_role::primary), 22.0f);
        sample_stack->add(make_label({ kind_text, u8"inspector-sample-b" }, u8"견본 둘째 줄", 14.0f, luil::label_color_role::dim), 22.0f);
        auto sample_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"inspector-sample-button" }, luil::text_button_config { .text = u8"견본 단추" }) };
        // 아무 일도 하지 않는 단추다. 눌러서 검사 대상의 상태가 달라지지 않게 하면서도 hover와 초점을 받는다.
        sample_button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        sample_stack->add(std::move(sample_button), 28.0f);
        sample->set_content(std::move(sample_stack));
        // 안쪽 여백 위아래 8, 줄 22와 22, 단추 28, 간격 8 둘이라 높이는 104다.
        column->add(std::move(sample), 104.0f);
        return column;
    }
}
