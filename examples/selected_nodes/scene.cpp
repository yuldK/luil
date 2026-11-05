#include "selected_nodes/scene.h"

namespace selected_nodes {
    namespace {
        std::unique_ptr<luil::label_element> label(std::u8string text)
        {
            return std::make_unique<luil::label_element>(luil::ui_element_id {}, luil::label_config { .text = std::move(text), .font_size = 14.0f, .color = luil::label_color_role::primary, .wrap = true });
        }
    }

    std::unique_ptr<luil::ui_element> make_scene(const scene_state& state)
    {
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id {}, luil::stack_config { .spacing = 12.0f, .padding = luil::edge_insets::all(20.0f) }) };
        column->add(label(u8"선택 노드 · 도킹 / 페이지 / 시·분 / 합자 caret"), 30.0f);
        luil::dock_config dock {};
        dock.owner = u8"selected";
        dock.layout = state.dock;
        dock.panels = { { u8"editor", u8"편집기" }, { u8"inspector", u8"속성" }, { u8"console", u8"콘솔" }, { u8"notes", u8"메모" } };
        dock.change = [](luil::dock_command command) { return luil::make_app_action(std::move(command)); };
        dock.content = [&state](const std::u8string& panel) -> std::unique_ptr<luil::ui_element> {
            if (panel == u8"editor")
                return std::make_unique<luil::text_input_element>(luil::ui_element_id { kind_sample }, luil::make_text_input_view(state.sample, state.composition, target_sample), luil::text_input_config {});
            if (panel == u8"inspector")
                return label(u8"탭을 끌어 합치거나 패널 가장자리에 놓아 분할합니다.");
            if (panel == u8"notes")
                return label(u8"떠 있는 패널입니다. 탭을 다른 그룹에 놓으면 다시 붙습니다.");
            return label(u8"합자 caret: LUIL_ENABLE_TEXT_SHAPING을 켠 글꼴의 GDEF 좌표를 사용합니다.");
        };
        column->add_flexible(std::make_unique<luil::dock_element>(std::move(dock)));
        luil::time_picker_config time {};
        time.owner = u8"time";
        time.hour = luil::make_text_input_view(state.hour, state.composition, target_hour);
        time.minute = luil::make_text_input_view(state.minute, state.composition, target_minute);
        time.invalid = luil::read_time_picker(state.hour.text, state.minute.text).has_value() == false;
        const auto replace = [](const luil::text_input_target target, std::u8string text) {
            const auto end { text.size() };
            return luil::make_app_action(luil::text_edit_request { .target = target, .command = luil::text::text_edit_command::replace_all, .offset = end, .text = std::move(text) });
        };
        time.hour_to = [replace](std::u8string text) { return replace(target_hour, std::move(text)); };
        time.minute_to = [replace](std::u8string text) { return replace(target_minute, std::move(text)); };
        column->add(std::make_unique<luil::time_picker_element>(std::move(time)), { .length = 28.0f, .cross_length = 148.0f });
        auto pages { std::make_unique<luil::stack_element>(luil::ui_element_id {}, luil::stack_config { .direction = luil::stack_direction::row, .spacing = 8.0f }) };
        for (const int direction : { -1, 1 })
        {
            if (direction == 1)
            {
                luil::pager_config config {};
                config.offset = state.page;
                config.change = [](luil::pager_command command) { return luil::make_app_action(command); };
                config.name = u8"넘기는 페이지";
                auto pager { std::make_unique<luil::pager_element>(luil::ui_element_id { luil::ui_element_kind::pager, u8"pages" }, std::move(config)) };
                for (const auto* text : { u8"첫 번째 장 — 터치로 넘기세요", u8"두 번째 장 — ← / → 키로도 넘깁니다", u8"세 번째 장 — 손을 떼면 페이지에 정착합니다" })
                    pager->add_page(label(text));
                pages->add_flexible(std::move(pager));
            }
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { luil::application_element_kind(501), direction < 0 ? u8"previous" : u8"next" }, luil::text_button_config { .text = direction < 0 ? u8"이전" : u8"다음" }) };
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(luil::pager_command { .operation = luil::pager_operation::step, .delta = static_cast<float>(direction) }));
            pages->add(std::move(button), 72.0f);
        }
        column->add(std::move(pages), 68.0f);
        column->add(label(u8"WebSocket과 Windows 미디어 재생 API의 사용법은 selected-nodes 문서를 참고하세요. 이 화면에서는 외부 연결이나 재생을 시작하지 않습니다."), 44.0f);
        return column;
    }
}
