#include "mobile_demo/inspector_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/stack_element.h"

#include <utility>

namespace mobile_demo {
    namespace {
        constexpr float row_spacing { 8.0f };

        // 검사기가 앱 kind에 붙이는 이름이다. 라이브러리 kind는 이름표가 알고 있다.
        [[nodiscard]] std::vector<luil::inspector_kind_name> demo_kind_names()
        {
            return { { kind_layout, u8"layout" }, { kind_text, u8"text" } };
        }
    }

    bool inspector_page::handle(const luil::app_message& message)
    {
        const auto* const intent { message.get<demo::inspector_command_intent>() };
        if (intent == nullptr)
            return false;
        // 선택 이동은 방금 지은 이 페이지의 tree 위에서 움직인다.
        if (demo::inspector_state::moves_selection(intent->command))
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

    const demo::inspector_state& inspector_page::state() const noexcept
    {
        return state_;
    }

    luil::ui_tree inspector_page::page_tree() const
    {
        page_content content { assemble() };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, width_ * scale_, height * scale_ }, scale_);
    }

    page_content inspector_page::build(const float width, const float scale)
    {
        width_ = width;
        scale_ = scale > 0.0f ? scale : 1.0f;
        // 요소 수는 이 페이지의 tree를 지어 센 값이다. 상태 글이 달라져도 요소 수는 같으므로 한 번 센 뒤 다시 짓는다.
        state_.note_elements(luil::inspect_nodes(page_tree()).size());
        return assemble();
    }

    page_content inspector_page::assemble() const
    {
        const luil::inspector_config names { config() };
        page_column column { u8"inspector-page" };
        column.note(u8"inspector-help", u8"켜고 칸을 탭하면 그 요소의 윤곽과 정보가 얹힌다");
        column.body(u8"inspector-state", state_.state_text());
        column.body(u8"inspector-pick", state_.pick_text());
        column.body(u8"inspector-levels", state_.levels_text());
        column.body(u8"inspector-selected", state_.selected_text(names));
        column.body(u8"inspector-count", state_.count_text());
        column.gap(row_spacing);

        // 손가락으로 누르는 단추 줄이다.
        //  - 탭해도 초점을 가져가지 않는다. 기본 집기(focused)는 초점 자리를 집으므로, 단추가 초점을 가져가면
        //    견본 단추를 탭한 뒤 누른 '조상 위로'가 견본이 아니라 방금 누른 단추에서 오른다.
        //  - Tab의 자리는 그대로라 키보드로는 단추에 갈 수 있다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = row_spacing;
        const auto add_button = [](luil::stack_element& row, std::u8string owner, std::u8string text, const demo::inspector_command command) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, std::move(owner) }, luil::text_button_config { .text = std::move(text) }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(demo::inspector_command_intent { command }));
            button->set_preserve_pointer_focus(true);
            row.add_flexible(std::move(button));
        };
        auto switches { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-switches" }, row_config) };
        add_button(*switches, u8"inspector-toggle", u8"켜기·끄기", demo::inspector_command::toggle);
        add_button(*switches, u8"inspector-up", u8"조상 위로", demo::inspector_command::levels_up);
        add_button(*switches, u8"inspector-down", u8"조상 아래로", demo::inspector_command::levels_down);
        column.add(std::move(switches), touch_height);
        column.gap(row_spacing);

        auto options { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-options" }, row_config) };
        add_button(*options, u8"inspector-outline", state_.outline_text(), demo::inspector_command::toggle_outline);
        add_button(*options, u8"inspector-palette", state_.palette_text(), demo::inspector_command::toggle_palette);
        column.add(std::move(options), touch_height);
        column.gap(row_spacing);

        auto moves { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-moves" }, row_config) };
        add_button(*moves, u8"inspector-parent", u8"부모", demo::inspector_command::select_parent);
        add_button(*moves, u8"inspector-child", u8"첫 자식", demo::inspector_command::select_child);
        add_button(*moves, u8"inspector-prev", u8"이전", demo::inspector_command::select_previous);
        add_button(*moves, u8"inspector-next", u8"다음", demo::inspector_command::select_next);
        column.add(std::move(moves), touch_height);
        column.gap(row_spacing);

        // 들여다볼 견본이다. 안쪽 여백(padding)과 간격(spacing)이 있는 stack이라 안쪽 여백 띠와 inset 수치가 보인다.
        luil::panel_config sample_config {};
        sample_config.corner_radius = 8.0f;
        sample_config.border = [](const luil::ui_color_palette& palette) { return palette.control_border; };
        auto sample { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_layout, u8"inspector-sample" }, std::move(sample_config)) };
        luil::stack_config sample_stack_config {};
        sample_stack_config.spacing = row_spacing;
        sample_stack_config.padding = luil::edge_insets::symmetric(12.0f, 8.0f);
        auto sample_stack { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"inspector-sample-stack" }, sample_stack_config) };
        sample_stack->add(make_label({ kind_text, u8"inspector-sample-a" }, u8"견본 첫째 줄", body_text_size, luil::label_color_role::primary), body_line_height);
        sample_stack->add(make_label({ kind_text, u8"inspector-sample-b" }, u8"견본 둘째 줄", body_text_size, luil::label_color_role::dim), body_line_height);
        auto sample_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"inspector-sample-button" }, luil::text_button_config { .text = u8"견본 단추" }) };
        // 아무 일도 하지 않는 단추다. 탭해도 검사 대상의 상태가 달라지지 않으면서 초점을 받는다.
        sample_button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        sample_stack->add(std::move(sample_button), touch_height);
        sample->set_content(std::move(sample_stack));
        // 안쪽 여백 위아래 8, 줄 둘과 단추, 간격 8 둘이다.
        column.add(std::move(sample), body_line_height * 2.0f + touch_height + row_spacing * 4.0f);
        return column.finish();
    }
}
