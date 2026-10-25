#include "demo/toasts_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace demo {
    bool toasts_page::handle(const luil::app_message& message)
    {
        if (const auto* const request { message.get<toast_request_intent>() }; request != nullptr)
        {
            push(request->text, request->severity, request->duration);
            return true;
        }
        if (const auto* const scroll { message.get<toasts_scroll_intent>() }; scroll != nullptr)
        {
            scroll_ = std::max(0.0f, scroll_ + scroll->delta);
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

    std::unique_ptr<luil::ui_element> toasts_page::build(const float width, const float height, const float scale)
    {
        static_cast<void>(width);
        static_cast<void>(height);
        static_cast<void>(scale);
        page_layout page { u8"toasts" };
        page.heading(u8"토스트", u8"토스트는 오른쪽 아래에 쌓이고, 시간이 다 되면 흐려집니다.");

        // 심각도별 트리거다. 단추의 글리프가 토스트에 붙을 글리프와 같다.
        page.begin_card(u8"심각도", u8"바탕은 모두 같은 중립 판입니다. 심각도는 글리프와 그 색이 말합니다.");
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"toast-triggers" }, row_config) };
        const auto add_trigger = [&row](std::u8string owner, std::u8string label, const char32_t glyph, const luil::toast_severity severity) {
            luil::text_button_config config {};
            config.text = std::move(label);
            config.glyph = glyph;
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, std::move(owner) }, std::move(config)) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, [severity](const luil::ui_action_context&) -> std::vector<luil::input_action> {
                std::u8string text {};
                switch (severity)
                {
                case luil::toast_severity::success:
                    text = u8"저장했습니다.";
                    break;
                case luil::toast_severity::warning:
                    text = u8"저장 공간이 얼마 남지 않았습니다.";
                    break;
                case luil::toast_severity::error:
                    text = u8"저장하지 못했습니다.";
                    break;
                case luil::toast_severity::info:
                    text = u8"새 버전이 있습니다.";
                    break;
                }
                return { luil::make_app_action(toast_request_intent { std::move(text), severity }) };
            });
            row->add_fit(std::move(button));
        };
        add_trigger(u8"info", u8"정보", luil::codicons::icon_info, luil::toast_severity::info);
        add_trigger(u8"success", u8"성공", luil::codicons::icon_pass, luil::toast_severity::success);
        add_trigger(u8"warning", u8"경고", luil::codicons::icon_warning, luil::toast_severity::warning);
        add_trigger(u8"error", u8"오류", luil::codicons::icon_error, luil::toast_severity::error);
        page.add(std::move(row), luil::text_button_height);
        page.end_card();

        // 액션 버튼이 있는 계속 남는 토스트다 (duration 0).
        page.begin_card(u8"계속 남는 토스트", u8"동작 단추가 있는 토스트는 저절로 사라지지 않습니다.");
        auto sticky { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, u8"sticky" }, luil::text_button_config { .text = u8"실행 취소가 있는 토스트" }) };
        sticky->set_cursor(luil::ui_cursor::hand);
        sticky->set_action(luil::ui_trigger::left_click, luil::make_message_action(toast_request_intent { u8"항목을 지웠습니다.", luil::toast_severity::info, std::chrono::milliseconds { 0 } }));
        luil::stack_config sticky_row_config {};
        sticky_row_config.direction = luil::stack_direction::row;
        auto sticky_row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"toast-sticky-row" }, sticky_row_config) };
        sticky_row->add_fit(std::move(sticky));
        page.add(std::move(sticky_row), luil::text_button_height);
        page.end_card();
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(toasts_scroll_intent { delta }); });
    }

    std::unique_ptr<luil::ui_element> toasts_page::make_overlay()
    {
        prune();
        if (toasts_.empty())
            return nullptr;

        luil::toast_stack_config config {};
        config.owner = u8"toasts";
        config.corner = luil::toast_corner::bottom_right;
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
                        luil::make_app_action(toast_request_intent { u8"되돌렸습니다.", luil::toast_severity::success }),
                    };
                };
            }
            toasts.push_back(std::move(toast));
        }
        return std::make_unique<luil::toast_stack_element>(std::move(config), std::move(toasts));
    }
} // namespace demo
