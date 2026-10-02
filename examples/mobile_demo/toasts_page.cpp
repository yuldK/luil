#include "mobile_demo/toasts_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include <utility>
#include <vector>

namespace mobile_demo {
    bool toasts_page::handle(const luil::app_message& message)
    {
        if (const auto* const request { message.get<toast_request_intent>() }; request != nullptr)
        {
            push(request->text, request->severity, request->duration);
            return true;
        }
        if (const auto* const undo { message.get<toast_undo_intent>() }; undo != nullptr)
        {
            for (auto position = toasts_.begin(); position != toasts_.end(); ++position)
            {
                if (position->id != undo->id)
                    continue;
                toasts_.erase(position);
                break;
            }
            return true;
        }
        return false;
    }

    void toasts_page::push(std::u8string text, const luil::toast_severity severity, const std::chrono::milliseconds duration)
    {
        prune();
        toast_entry entry {};
        entry.id = u8"toast-" + to_u8(++next_id_);
        entry.text = std::move(text);
        entry.severity = severity;
        entry.shown_at = std::chrono::steady_clock::now();
        entry.duration = duration;
        toasts_.push_back(std::move(entry));
    }

    void toasts_page::prune()
    {
        // 만료된 토스트를 지운다.
        // 만료 시각은 next_expiry가 driver의 next_tick으로 예고하므로
        // 메시지가 없어도 그 시각의 tick이 이 함수를 불러 목록이 실제로 준다.
        const std::chrono::steady_clock::time_point now { std::chrono::steady_clock::now() };
        std::erase_if(toasts_, [now](const toast_entry& entry) { return entry.duration.count() > 0 && now >= entry.shown_at + entry.duration; });
    }

    std::optional<std::chrono::steady_clock::time_point> toasts_page::next_expiry() const
    {
        std::optional<std::chrono::steady_clock::time_point> next {};
        for (const toast_entry& entry : toasts_)
        {
            if (entry.duration.count() == 0)
                continue;
            const std::chrono::steady_clock::time_point at { entry.shown_at + entry.duration };
            if (next.has_value() == false || at < *next)
                next = at;
        }
        return next;
    }

    page_content toasts_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        page_column column { u8"toasts" };
        column.note(u8"toasts-hint", u8"토스트는 아래쪽에 쌓인다.");
        column.note(u8"toasts-hint-2", u8"시간이 다 되면 흐려지며 사라진다.");
        column.gap(12.0f);

        // 심각도별 트리거다. 좁은 화면에서는 두 줄로 나눈다.
        const auto make_trigger = [](std::u8string owner, std::u8string label, const luil::toast_severity severity) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, std::move(owner) }, luil::text_button_config { .text = std::move(label) }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, [severity](const luil::ui_action_context&) -> std::vector<luil::input_action> {
                std::u8string text {};
                switch (severity)
                {
                case luil::toast_severity::success:
                    text = u8"저장했다.";
                    break;
                case luil::toast_severity::warning:
                    text = u8"저장 공간이 얼마 남지 않았다.";
                    break;
                case luil::toast_severity::error:
                    text = u8"저장에 실패했다.";
                    break;
                case luil::toast_severity::info:
                    text = u8"새 버전이 있다.";
                    break;
                }
                return { luil::make_app_action(toast_request_intent { std::move(text), severity }) };
            });
            return button;
        };
        const auto make_row = [](std::u8string owner) {
            luil::stack_config row_config {};
            row_config.direction = luil::stack_direction::row;
            row_config.spacing = 8.0f;
            return std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, std::move(owner) }, row_config);
        };
        const bool narrow { width < 480.0f };
        auto first { make_row(u8"toast-triggers") };
        first->add_flexible(make_trigger(u8"info", u8"info", luil::toast_severity::info));
        first->add_flexible(make_trigger(u8"success", u8"success", luil::toast_severity::success));
        auto second { narrow ? make_row(u8"toast-triggers-2") : nullptr };
        luil::stack_element& rest { narrow ? *second : *first };
        rest.add_flexible(make_trigger(u8"warning", u8"warning", luil::toast_severity::warning));
        rest.add_flexible(make_trigger(u8"error", u8"error", luil::toast_severity::error));
        column.add(std::move(first), touch_height);
        if (second != nullptr)
        {
            column.gap(8.0f);
            column.add(std::move(second), touch_height);
        }
        column.gap(16.0f);

        // 액션 버튼이 있는 계속 남는 토스트다 (duration 0).
        auto sticky { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, u8"sticky" }, luil::text_button_config { .text = u8"실행 취소가 있는 토스트" }) };
        sticky->set_cursor(luil::ui_cursor::hand);
        sticky->set_action(luil::ui_trigger::left_click, luil::make_message_action(toast_request_intent { u8"항목을 지웠다.", luil::toast_severity::info, std::chrono::milliseconds { 0 } }));
        column.add(std::move(sticky), touch_height, std::min(width - 2.0f * page_padding, 260.0f));
        return column.finish();
    }

    std::unique_ptr<luil::ui_element> toasts_page::make_overlay()
    {
        prune();
        if (toasts_.empty())
            return nullptr;

        luil::toast_stack_config config {};
        config.owner = u8"toasts";
        config.corner = luil::toast_corner::bottom_right;
        // 휴대폰에서 오른쪽 아래는 엄지가 닿는 자리다. 토스트가 내용 대신 그 자리를 덮는다.
        config.maximum_count = 4;
        std::vector<luil::toast_config> toasts {};
        for (const toast_entry& entry : toasts_)
        {
            luil::toast_config toast {};
            toast.id = entry.id;
            toast.text = entry.text;
            toast.severity = entry.severity;
            toast.shown_at = entry.shown_at;
            toast.duration = entry.duration;
            // 계속 남는 토스트만 액션 버튼을 갖는다.
            //  - 흐려져 보이지 않는 토스트의 버튼이 눌리는 유령을 만들지 않는다.
            if (entry.duration.count() == 0)
            {
                const std::u8string id { entry.id };
                toast.action_label = u8"실행 취소";
                toast.action = [id](const luil::ui_action_context&) -> std::vector<luil::input_action> {
                    return {
                        luil::make_app_action(toast_undo_intent { id }),
                        luil::make_app_action(toast_request_intent { u8"되돌렸다.", luil::toast_severity::success }),
                    };
                };
            }
            toasts.push_back(std::move(toast));
        }
        return std::make_unique<luil::toast_stack_element>(std::move(config), std::move(toasts));
    }
} // namespace mobile_demo
