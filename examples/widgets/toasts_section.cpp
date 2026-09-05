// 토스트 알림 섹션이다.
//
// 다루는 element: toast_stack_element (구석에 쌓이는 알림 더미).
//
// 토스트는 "시간이 흘러야 바뀌는 상태"의 대표라 tick 훅과 짝으로 배운다.
//   - 목록과 만료 판정은 앱(app_state.toasts)의 몫이다.
//   - 라이브러리는 shown_at·duration으로 끝날 무렵 흐려지게만 그린다.
//   - 실제로 목록에서 지우는 것은 driver의 next_tick/tick이다 (app.cpp):
//     다음 만료 시각을 예고하면 메시지가 없어도 그 시각에 tick이 온다.

#include "widgets/app.h"

#include <utility>
#include <vector>

namespace widgets {
    namespace {
        constexpr std::chrono::milliseconds toast_duration { 3000 };
    } // namespace

    section build_toasts_section(const app_state&)
    {
        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"toasts" }, column_config) };

        luil::label_config title {};
        title.text = u8"토스트 — 누르면 오른쪽 아래에 3초 동안 뜬다";
        title.font_size = 13.0f;
        title.color = luil::label_color_role::primary;
        const float title_height { luil::label_element::height_for(title) };
        column->add(std::make_unique<luil::label_element>(luil::ui_element_id { kind_text, u8"toasts-title" }, title), title_height);

        // 심각도별 트리거 버튼. 같은 kind를 owner로 구분한다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"toast-row" }, row_config) };
        const auto add_trigger = [&row](std::u8string owner, std::u8string label, std::u8string text, const luil::toast_severity severity) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_button, std::move(owner) }, luil::text_button_config { .text = std::move(label) }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(toast_intent { std::move(text), severity }));
            row->add(std::move(button), { .length = 88.0f });
        };
        add_trigger(u8"info", u8"info", u8"새 소식이 있다.", luil::toast_severity::info);
        add_trigger(u8"success", u8"success", u8"저장했다.", luil::toast_severity::success);
        add_trigger(u8"warning", u8"warning", u8"공간이 얼마 남지 않았다.", luil::toast_severity::warning);
        add_trigger(u8"error", u8"error", u8"저장에 실패했다.", luil::toast_severity::error);
        column->add(std::move(row), 28.0f);

        return { std::move(column), title_height + 8.0f + 28.0f };
    }

    std::unique_ptr<luil::ui_element> build_toast_overlay(const app_state& state)
    {
        // 토스트가 없으면 오버레이도 없다 ("없는 것은 두지 않는다").
        if (state.toasts.empty())
            return nullptr;

        luil::toast_stack_config config {};
        config.owner = u8"toasts";
        config.corner = luil::toast_corner::bottom_right;
        config.maximum_count = 4;

        std::vector<luil::toast_config> toasts {};
        for (const app_state::toast_entry& entry : state.toasts)
        {
            luil::toast_config toast {};
            toast.id = entry.id;
            toast.text = entry.text;
            toast.severity = entry.severity;
            // shown_at·duration으로 라이브러리가 끝날 무렵 흐려지게 그린다.
            // 목록에서 실제로 지우는 것은 driver의 tick이다.
            toast.shown_at = entry.shown_at;
            toast.duration = toast_duration;
            toasts.push_back(std::move(toast));
        }
        return std::make_unique<luil::toast_stack_element>(std::move(config), std::move(toasts));
    }
} // namespace widgets
