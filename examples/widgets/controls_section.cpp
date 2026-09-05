// 버튼·라벨·panel 섹션이다.
//
// 다루는 element
//   label_element   — 텍스트 한 줄. 색은 역할(label_color_role)로 고른다.
//   button_element  — 아이콘(codicon 글리프) 버튼. hover·눌림 강조는 라이브러리가 그린다.
//   text_button_element — 글자 버튼. text_button_config의 visual이 그리는 모양을 정한다.
//   panel_element   — 배경을 칠하고 내용을 담는 컨테이너.
//
// 공통 규칙
//   - 액션은 set_action(trigger, ...)으로 등록하고 메시지만 반환한다.
//     가장 흔한 "메시지 하나" 꼴은 make_message_action(intent)가 줄여 준다.
//   - tooltip·커서는 set_tooltip / set_cursor로 붙인다.
//     붙이면 element가 hover 대상이 된다 (라이브러리가 hit 조건을 맞춘다).

#include "widgets/app.h"

#include "luil/generated/codicons.h"

#include <utility>

namespace widgets {
    section build_controls_section(const app_state& state)
    {
        // 제목 + 내용을 세로로 쌓는다.
        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"controls" }, column_config) };

        // 섹션 제목 라벨. 높이는 height_for가 글자 크기에서 계산한다.
        luil::label_config title {};
        title.text = u8"버튼과 라벨";
        title.font_size = 13.0f;
        title.color = luil::label_color_role::primary;
        const float title_height { luil::label_element::height_for(title) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"controls-title" }, title), title_height);

        // 버튼 줄: 아이콘 버튼 + 글자 버튼 + 결과 라벨을 가로로 쌓는다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 12.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"controls-row" }, row_config) };

        // 아이콘 버튼: 글리프는 내장 codicon에서 고른다.
        luil::button_config icon_config {};
        icon_config.glyph = luil::codicons::icon_add;
        icon_config.corner_radius = 3.0f;
        auto icon_button { std::make_unique<luil::button_element>(luil::ui_element_id { kind_icon_button }, icon_config) };
        icon_button->set_tooltip(u8"하나 더한다");
        icon_button->set_cursor(luil::ui_cursor::hand);
        icon_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(icon_button), 30.0f);

        // 글자 버튼: 같은 intent를 낸다. 어느 쪽을 눌러도 결과가 같다.
        auto text_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_counter_button }, luil::text_button_config { .text = u8"하나 더" }) };
        text_button->set_cursor(luil::ui_cursor::hand);
        text_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(text_button), { .length = 96.0f });

        // 아이콘+글자 버튼: 같은 element에 glyph를 더한 것뿐이다.
        // 아이콘 칸이 가져가는 폭은 icon_width_for가 알려 주므로 담는 쪽이 그만큼 더해 잡는다.
        luil::text_button_config both_config {};
        both_config.text = u8"하나 더";
        both_config.glyph = luil::codicons::icon_add;
        const float both_width { 96.0f + luil::text_button_element::icon_width_for(both_config) };
        auto both_button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_icon_text_button }, std::move(both_config)) };
        both_button->set_cursor(luil::ui_cursor::hand);
        both_button->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(both_button), { .length = both_width });

        // link: 같은 element에 visual만 바꾼 것이다. 바탕이 없어 글 흐름 안에 선다.
        // 커서는 담는 쪽이 준다 — 액션이 나중에 오므로 element가 미리 걸 수 없다.
        luil::text_button_config link_config {};
        link_config.text = u8"이것은 link다";
        link_config.visual = luil::text_button_visual::link;
        auto link { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_link }, std::move(link_config)) };
        link->set_cursor(luil::ui_cursor::hand);
        link->set_tooltip(u8"link도 액션 하나를 받아 실행한다");
        link->set_action(luil::ui_trigger::left_click, luil::make_message_action(increment_intent {}));
        row->add(std::move(link), { .length = 104.0f });

        // 상태를 보여 주는 라벨. 상태는 언제나 앱(app_state)이 소유한다.
        luil::label_config count {};
        const std::string digits { std::to_string(state.clicks) };
        count.text = u8"누른 횟수: " + std::u8string { digits.begin(), digits.end() };
        count.font_size = 12.0f;
        count.color = luil::label_color_role::dim;
        row->add_flexible(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"controls-count" }, count));
        column->add(std::move(row), 30.0f);

        // panel: 색 있는 바탕. 색은 구체 값이 아니라 팔레트의 역할로 고른다 —
        // 테마(밝음·어둠·고대비)가 바뀌어도 이 코드는 그대로다.
        luil::panel_config panel_config {};
        panel_config.background = [](const luil::ui_color_palette& palette) { return palette.surface_background; };
        panel_config.corner_radius = 6.0f;
        auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_panel, u8"controls" }, std::move(panel_config)) };

        luil::stack_config inner_config {};
        inner_config.padding = luil::edge_insets::all(12.0f);
        auto inner { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"controls-panel" }, inner_config) };
        luil::label_config note {};
        note.text = u8"이 상자가 panel_element다. 배경색은 팔레트에서 고른다.";
        note.font_size = 11.0f;
        const float note_height { luil::label_element::height_for(note) };
        inner->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"controls-note" }, note), note_height);
        panel->set_content(std::move(inner));
        const float panel_height { note_height + 24.0f };
        column->add(std::move(panel), panel_height);

        // 섹션 전체 높이 = 제목 + 간격 + 버튼 줄 + 간격 + panel.
        return { std::move(column), title_height + 8.0f + 30.0f + 8.0f + panel_height };
    }
} // namespace widgets
