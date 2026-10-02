#include "widgets/app.h"

#include <string>
#include <utility>

namespace widgets {
    namespace {
        // 토스트가 화면에 머무는 시간이다.
        constexpr std::chrono::milliseconds toast_duration { 3000 };

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
        if (message.get<toggle_record_intent>() != nullptr)
        {
            state_.recording = state_.recording == false;
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
        if (const auto* const scroll { message.get<shell_scroll_intent>() }; scroll != nullptr)
        {
            state_.scroll += scroll->delta;
            return;
        }
        if (const auto* const toast { message.get<toast_intent>() }; toast != nullptr)
        {
            app_state::toast_entry entry {};
            entry.id = u8"toast-" + to_u8(++state_.next_toast_id);
            entry.text = toast->text;
            entry.severity = toast->severity;
            entry.shown_at = std::chrono::steady_clock::now();
            state_.toasts.push_back(std::move(entry));
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

        // 맨 위 막대는 데스크톱이면 custom caption, 모바일이면 앱 바다 (hello와 같다).
        const luil::caption_config caption_config { make_caption() };
        const bool mobile { luil::current_ui_platform().form_factor == luil::ui_form_factor::mobile };
        float caption_height { 0.0f };
        if (luil::current_ui_platform().window_caption)
        {
            caption_height = luil::caption_element::height_for(caption_config) * scale;
            auto caption { std::make_unique<luil::caption_element>(caption_config) };
            caption->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(caption));
        }
        else
        {
            const luil::app_bar_config app_bar_config { .title = caption_config.title };
            caption_height = luil::app_bar_element::height_for(app_bar_config) * scale;
            auto app_bar { std::make_unique<luil::app_bar_element>(app_bar_config) };
            app_bar->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(app_bar));
        }

        // 섹션들을 세로로 쌓는다.
        // stack은 측정 단계가 없어 각 섹션이 자기 높이를 함께 돌려준다.
        //  - 휴대폰은 화면이 좁아 가장자리 여백을 줄인다.
        luil::stack_config column_config {};
        column_config.padding = luil::edge_insets::all(mobile ? 16.0f : 24.0f);
        column_config.spacing = 20.0f;
        auto column { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"shell" }, column_config) };
        float content_height { column_config.padding.top + column_config.padding.bottom };
        bool first { true };
        for (section (*build)(const app_state&) : { &build_controls_section, &build_inputs_section, &build_choices_section, &build_status_section, &build_toasts_section })
        {
            section built { build(state_) };
            content_height += built.height + (first ? 0.0f : column_config.spacing);
            first = false;
            column->add(std::move(built.element), built.height);
        }

        // 섹션이 창보다 길면(낮은 창, 가로로 돌린 휴대폰) 흘려 본다. 휠과 터치 쓸기가
        // 같은 `scroll_source`를 찾는다.
        const float viewport_height { (height - caption_height) / scale };
        state_.scroll = luil::clamp_scroll(content_height, viewport_height, state_.scroll);
        auto view { std::make_unique<luil::scroll_view_element>(luil::ui_element_id { kind_layout, u8"shell-view" }, luil::scroll_view_config { content_height, state_.scroll }) };
        view->set_content(std::move(column));
        view->set_scroll_source(luil::scroll_source { .scroll = [](const float delta) { return luil::make_app_action(shell_scroll_intent { delta }); }, .scale = scale });
        view->arrange({ { 0.0f, caption_height, width, height - caption_height }, scale });
        root->add(std::move(view));

        // 토스트 오버레이는 섹션 위가 아니라 창 전체 위에 얹는다.
        if (auto overlay { build_toast_overlay(state_) }; overlay != nullptr)
        {
            overlay->arrange({ { 0.0f, 0.0f, width, height }, scale });
            root->add(std::move(overlay));
        }

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

    std::optional<std::chrono::steady_clock::time_point> widgets_driver::next_tick()
    {
        // 다음에 만료되는 토스트의 시각을 예고한다.
        // 예고가 없으면 시간 경로는 완전히 잠잔다 (평소 비용 0).
        std::optional<std::chrono::steady_clock::time_point> next {};
        for (const app_state::toast_entry& entry : state_.toasts)
        {
            const std::chrono::steady_clock::time_point at { entry.shown_at + toast_duration };
            if (next.has_value() == false || at < *next)
                next = at;
        }
        return next;
    }

    void widgets_driver::tick(const std::chrono::steady_clock::time_point now)
    {
        // 예고한 시각이 지나면 메시지가 없어도 여기가 불리고 frame이 다시 게시된다.
        std::erase_if(state_.toasts, [now](const app_state::toast_entry& entry) { return now >= entry.shown_at + toast_duration; });
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

    std::vector<luil::input_action> widgets_policy::on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        // 섹션 창이 자기 스크롤 메시지를 들고 있어(`scroll_source`) 표가 필요 없다.
        return luil::route_reveal(tree, focused);
    }

    luil::app_message widgets_delegate::make_window_metrics_message(const float width, const float height, const float scale)
    {
        return luil::app_message { metrics_intent { width, height, scale } };
    }
} // namespace widgets
