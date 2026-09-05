// 선택 그룹·접이식 그룹 섹션이다.
//
// 다루는 element
//   choice_group_element — 배타 선택. radio(세로 목록)와 toggle(가로 묶음) 두 모양.
//   check_element        — 낱개 컨트롤. checkbox·radio·toggle_switch 세 모양이고
//                          셋은 그리는 모양만 다르다 (상태를 받아 그리고 메시지만 낸다).
//   group_element        — 제목 있는 접이식 섹션. 접히면 내용이 tree에 아예 없다.
//
// 공통 규칙: "상태는 앱이, 표시는 element가."
//   어느 값이 선택인지(selected)·접혔는지(collapsed)는 전부 app_state다.
//   element는 그 값을 설정으로 받아 그리고, "바꾸자"는 메시지만 낸다.
//   그래서 이 파일에는 상태 변경 코드가 한 줄도 없다 — handle(app.cpp)에 있다.

#include "widgets/app.h"

#include <utility>

namespace widgets {
    section build_choices_section(const app_state& state)
    {
        // 라디오 목록: 접이식 그룹의 내용으로 넣는다.
        luil::choice_group_config fruit {};
        fruit.owner = u8"fruit";
        fruit.style = luil::choice_style::radio;
        fruit.items = {
            { u8"apple", u8"사과" },
            { u8"grape", u8"포도" },
            { u8"peach", u8"복숭아" },
        };
        fruit.selected = state.fruit;
        // 선택 메시지 factory는 고른 값(value)을 받아 메시지를 만든다.
        fruit.select = [](const std::u8string& value) { return luil::make_app_action(choose_fruit_intent { value }); };
        // 높이는 정적 사이저가 알려 준다 (논리 픽셀). 배치 길이에 같은 값을 쓴다.
        const float fruit_height { luil::choice_group_element::height_for(fruit) };
        auto fruit_group { std::make_unique<luil::choice_group_element>(fruit) };

        // 접이식 그룹: 접힘 상태와 내용 높이를 설정으로 받는다.
        // 접혀 있으면 내용이 tree에 들어가지 않아 보이지도 눌리지도 않는다.
        luil::group_config advanced {};
        advanced.owner = u8"choices";
        advanced.title = state.choices_collapsed ? u8"과일 고르기 (접힘 — 눌러서 펼친다)" : u8"과일 고르기";
        advanced.collapsed = state.choices_collapsed;
        advanced.toggle = luil::make_message_action(collapse_intent {});
        advanced.content_height = fruit_height;
        const float group_height { luil::group_element::height_for(advanced) };
        auto group { std::make_unique<luil::group_element>(advanced, state.choices_collapsed ? nullptr : std::move(fruit_group)) };

        // 토글 묶음: 같은 choice_group을 모양만 바꿔 쓴다.
        luil::choice_group_config view {};
        view.owner = u8"view";
        view.style = luil::choice_style::toggle;
        view.items = {
            { u8"list", u8"목록" },
            { u8"grid", u8"바둑판" },
            { u8"detail", u8"자세히" },
        };
        view.selected = state.view;
        view.select = [](const std::u8string& value) { return luil::make_app_action(choose_view_intent { value }); };
        const float view_height { luil::choice_group_element::height_for(view) };

        // 낱개 컨트롤 셋: 같은 element를 style만 바꿔 쓴다.
        // 상태는 앱 것이고 element는 "뒤집자"는 메시지만 낸다 — 스스로 바뀌지 않는다.
        const auto make_check = [](std::u8string owner, const luil::check_style style, std::u8string label, const bool checked) {
            luil::check_config config {};
            config.owner = owner;
            config.style = style;
            config.label = std::move(label);
            config.checked = checked;
            config.toggle = luil::make_message_action(check_intent { std::move(owner) });
            return std::make_unique<luil::check_element>(std::move(config));
        };

        luil::stack_config checks_config {};
        auto checks { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"checks" }, checks_config) };
        checks->add(make_check(u8"wrap", luil::check_style::checkbox, u8"줄 바꿈 (checkbox)", state.wrap_lines), luil::check_row_height);
        checks->add(make_check(u8"metric", luil::check_style::radio, u8"미터법 (radio)", state.use_metric), luil::check_row_height);
        checks->add(make_check(u8"dark", luil::check_style::toggle_switch, u8"어두운 미리보기 (switch)", state.dark_preview), luil::check_row_height);
        const float checks_height { 3.0f * luil::check_row_height };

        luil::stack_config column_config {};
        column_config.spacing = 12.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"choices" }, column_config) };
        column->add(std::move(group), group_height);
        column->add(std::make_unique<luil::choice_group_element>(view), view_height);
        column->add(std::move(checks), checks_height);

        return { std::move(column), group_height + 12.0f + view_height + 12.0f + checks_height };
    }
} // namespace widgets
