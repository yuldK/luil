// 상태를 보여 주는 것들의 섹션이다.
//
// 다루는 element
//   slider_element   — 끌거나 눌러서 값을 바꾼다. 값이 아니라 변화량을 나른다.
//   progress_element — 진행률 막대. 표시뿐이라 액션도 커서도 없다.
//   badge_element    — 알약 모양의 작은 상태 표시. tone이 색과 글리프를 함께 정한다.
//
// 공통 규칙
//   - 값은 언제나 앱(app_state)이 소유한다. element는 받은 값을 그릴 뿐이다.
//     여기서는 slider가 정하고 progress가 보여 주는 **하나의 값**을 쓴다 —
//     컨트롤과 표시가 같은 상태를 나눠 보는 것이 이 섹션의 요점이다.
//   - 범위 다듬기도 앱의 몫이다. slider는 변화량만 나르므로 범위 밖으로 끌면
//     그만큼이 쌓이고, 그러면 되돌아올 때 헛돈다 (app.cpp의 handle이 자른다).
//   - 높이는 height_for가 알려 준다. stack에 측정 단계가 없으므로 담는 쪽이
//     길이를 함께 넣어야 한다.
//   - 배지는 **폭도** 담는 쪽이 준다. 배치 시점에는 글꼴이 없어 글자 폭을 잴 수
//     없기 때문이고, 그래서 배지 줄은 고정 폭 칸을 늘어놓는 가로 stack이다.

#include "widgets/app.h"

#include <string>
#include <utility>

namespace widgets {
    section build_status_section(const app_state& state)
    {
        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"status" }, column_config) };

        luil::label_config title {};
        title.text = u8"상태 표시";
        title.font_size = 13.0f;
        title.color = luil::label_color_role::primary;
        const float title_height { luil::label_element::height_for(title) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"status-title" }, title), title_height);

        // slider가 정하고 progress가 보여 주는 **하나의 값**이다.
        // 컨트롤과 표시가 같은 앱 상태를 나눠 보는 것이 이 줄의 전부다.
        luil::slider_config slider {};
        slider.value = state.progress;
        slider.change = [](const float delta) { return luil::make_app_action(slide_intent { delta }); };
        // 보조 기술의 SetValue는 목표 값을 절대 메시지로 나른다.
        slider.change_to = [](const float value) { return luil::make_app_action(slide_to_intent { value }); };
        auto slider_element { std::make_unique<luil::slider_element>(luil::ui_element_id { kind_slider }, std::move(slider)) };
        slider_element->set_tooltip(u8"끌거나 눌러서 진행률을 바꾼다");
        const float slider_height { luil::slider_element::height_for(luil::slider_config {}) };
        column->add(std::move(slider_element), { .length = slider_height, .cross_length = 220.0f });

        // 진행률 줄: 막대가 남는 폭을 갖고 오른쪽에 백분율 라벨을 둔다.
        const int steps { static_cast<int>(state.progress * 100.0f + 0.5f) };
        luil::progress_config progress {};
        progress.value = state.progress;
        progress.thickness = 6.0f;

        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 12.0f;
        row_config.cross_alignment = luil::stack_alignment::center;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"status-row" }, row_config) };
        row->add_flexible(std::make_unique<luil::progress_element>(luil::ui_element_id { kind_progress }, progress));

        luil::label_config percent {};
        const std::string digits { std::to_string(steps) };
        percent.text = std::u8string { digits.begin(), digits.end() } + u8"%";
        percent.font_size = 11.0f;
        percent.color = luil::label_color_role::dim;
        row->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"status-percent" }, percent), { .length = 40.0f });

        // 줄 높이는 라벨 쪽이 정한다 — 막대는 그 줄의 가운데에 눕는다.
        const float row_height { luil::label_element::height_for(percent) };
        column->add(std::move(row), row_height);

        // 배지 줄: tone 넷을 나란히 둔다.
        // 심각도 둘은 글리프가 딸려 오므로 고대비에서 색이 접혀도 서로 구분된다.
        luil::stack_config badges_config {};
        badges_config.direction = luil::stack_direction::row;
        badges_config.spacing = 8.0f;
        auto badges { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"status-badges" }, badges_config) };

        const struct
        {
            std::u8string_view text {};
            luil::badge_tone tone {};
            float width { 0.0f };
        } samples[] {
            { u8"진행 중", luil::badge_tone::neutral, 64.0f },
            { u8"새것", luil::badge_tone::accent, 52.0f },
            { u8"확인 필요", luil::badge_tone::warning, 88.0f },
            { u8"실패", luil::badge_tone::error, 64.0f },
        };
        float badge_height { 0.0f };
        for (const auto& sample : samples)
        {
            luil::badge_config badge {};
            badge.text = std::u8string { sample.text };
            badge.tone = sample.tone;
            badge_height = luil::badge_element::height_for(badge);
            badges->add(std::make_unique<luil::badge_element>(luil::ui_element_id { kind_badge, std::u8string { sample.text } }, std::move(badge)), sample.width);
        }
        badges->add_flexible_gap();
        column->add(std::move(badges), badge_height);

        return { std::move(column), title_height + 8.0f + slider_height + 8.0f + row_height + 8.0f + badge_height };
    }
} // namespace widgets
