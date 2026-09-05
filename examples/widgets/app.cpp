#include "widgets/app.h"

#include <string>
#include <utility>

namespace widgets {
    namespace {
        // 토스트가 화면에 머무는 시간이다.

        [[nodiscard]] luil::caption_config make_caption()
        {
            luil::caption_config caption {};
            caption.title = u8"luil widgets";
            caption.minimize_tooltip = u8"Minimize";
            caption.maximize_tooltip = u8"Maximize or restore";
            caption.close_tooltip = u8"Close";
            return caption;
        }

        [[nodiscard]] std::u8string to_u8(const int value)
        {
            const std::string digits { std::to_string(value) };
            return std::u8string { digits.begin(), digits.end() };
        }
    } // namespace

    void widgets_driver::handle(luil::app_message message)
    {
        if (message.get<close_intent>() != nullptr)
        {
            closed_.store(true);
            return;
        }
        if (const auto* const metrics { message.get<metrics_intent>() }; metrics != nullptr)
        {
            metrics_ = *metrics;
            return;
        }
        if (message.get<increment_intent>() != nullptr)
        {
            ++state_.clicks;
            return;
        }
        if (const auto* const edit { message.get<edit_intent>() }; edit != nullptr)
        {
            // 편집 적용은 라이브러리 도우미가 한다.
            // 명령(insert·backspace·이동·undo …)의 해석을 앱이 다시 쓸 필요가 없다.
            luil::apply_text_edit(state_.note, edit->request);
            return;
        }
        if (const auto* const composition { message.get<composition_intent>() }; composition != nullptr)
        {
            // 조합이 끝나면 표시 상태를 버린다.
            // 확정된 글은 별도의 편집 요청으로 이미 들어왔다.
            if (composition->event.composing == false)
                state_.composition.reset();
            else
                state_.composition = composition->event;
            return;
        }
        if (const auto* const fruit { message.get<choose_fruit_intent>() }; fruit != nullptr)
        {
            state_.fruit = fruit->value;
            return;
        }
        if (const auto* const view { message.get<choose_view_intent>() }; view != nullptr)
        {
            state_.view = view->value;
            return;
        }
        if (message.get<collapse_intent>() != nullptr)
        {
            state_.choices_collapsed = state_.choices_collapsed == false;
            return;
        }
        // 뒤집는 것은 앱이다. element는 어느 것인지만 말한다.
        if (const auto* const check { message.get<check_intent>() }; check != nullptr)
        {
            if (check->owner == u8"wrap")
                state_.wrap_lines = state_.wrap_lines == false;
            else if (check->owner == u8"metric")
                state_.use_metric = state_.use_metric == false;
            else if (check->owner == u8"dark")
                state_.dark_preview = state_.dark_preview == false;
            return;
        }
        // 다듬는 것은 앱이다. element는 변화량만 나르므로 범위 밖으로 끌면
        // 그만큼이 쌓여, 되돌아올 때 헛돈다 — 여기서 잘라 상태에 되돌려 쓴다.
        if (const auto* const slide { message.get<slide_intent>() }; slide != nullptr)
        {
            state_.progress += slide->delta;
            if (state_.progress < 0.0f)
                state_.progress = 0.0f;
            if (state_.progress > 1.0f)
                state_.progress = 1.0f;
            return;
        }
        // 보조 기술이 정한 절대 값이다. 다듬기는 같은 자리(앱)에서 한다.
        if (const auto* const slide_to { message.get<slide_to_intent>() }; slide_to != nullptr)
        {
            state_.progress = slide_to->value;
            if (state_.progress < 0.0f)
                state_.progress = 0.0f;
            if (state_.progress > 1.0f)
                state_.progress = 1.0f;
            return;
        }
    }

    std::shared_ptr<const luil::win32::ui_frame> widgets_driver::make_frame()
    {
        const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
        const float width { metrics_.width > 0.0f ? metrics_.width : 900.0f };
        const float height { metrics_.height > 0.0f ? metrics_.height : 640.0f };

        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

        const luil::caption_config caption_config { make_caption() };
        const float caption_height { luil::caption_element::height_for(caption_config) * scale };
        auto caption { std::make_unique<luil::caption_element>(caption_config) };
        caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
        root->add(std::move(caption));

        // 섹션들을 세로로 쌓는다.
        // stack은 측정 단계가 없어 각 섹션이 자기 높이를 함께 돌려준다.
        luil::stack_config column_config {};
        column_config.padding = luil::edge_insets::all(24.0f);
        column_config.spacing = 20.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"shell" }, column_config) };
        for (section (*build)(const app_state&) : { &build_controls_section, &build_inputs_section, &build_choices_section, &build_status_section })
        {
            section built { build(state_) };
            column->add(std::move(built.element), built.height);
        }
        column->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(column));

        // 토스트 오버레이는 섹션 위가 아니라 창 전체 위에 얹는다.

        auto frame { std::make_shared<luil::win32::ui_frame>() };
        frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
        return frame;
    }

    luil::app_message widgets_driver::make_close_message()
    {
        return luil::app_message { close_intent {} };
    }

    bool widgets_driver::shutdown_completed() const
    {
        return closed_.load();
    }



    std::optional<luil::text_input_target> widgets_policy::text_target_of(const luil::ui_element_kind kind) const
    {
        // 텍스트 박스가 늘면 여기 한 줄씩 는다.
        // 이 표에서 빠지면 그 박스는 클릭해도 초점을 받지 못한다.
        if (kind == kind_note_input)
            return target_note;
        return std::nullopt;
    }

    luil::input_action widgets_policy::make_text_edit_action(const luil::text_edit_request& request) const
    {
        // 라이브러리의 편집 요청을 앱 메시지 봉투에 담는다.
        // 실제 적용은 logic thread의 handle이 한다 — 여기는 순수 변환이다.
        return luil::make_app_action(edit_intent { request });
    }

    luil::input_action widgets_policy::make_text_composition_action(const luil::text_composition_event& event) const
    {
        return luil::make_app_action(composition_intent { event });
    }

    luil::app_message widgets_delegate::make_window_metrics_message(const float width, const float height, const float scale)
    {
        return luil::app_message { metrics_intent { width, height, scale } };
    }
} // namespace widgets
