#include "demo/toasts_page.h"

#include "luil/generated/codicons.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/info_bar_element.h"
#include "luil/ui/stack_element.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {
    namespace {
        constexpr std::array<luil::ui_severity, 4> bar_severities { luil::ui_severity::info, luil::ui_severity::success, luil::ui_severity::warning, luil::ui_severity::error };

        // 막대마다의 글이다. owner는 막대와 상태 label의 키이고, 낱말은 label에 적히는 심각도 이름이다.
        struct bar_text
        {
            std::u8string_view owner {};
            std::u8string_view word {};
            std::u8string_view title {};
            std::u8string_view message {};
            std::u8string_view action {};
        };

        [[nodiscard]] bar_text bar_text_of(const luil::ui_severity severity) noexcept
        {
            switch (severity)
            {
            case luil::ui_severity::success:
                return { u8"success", u8"성공", u8"저장했습니다", u8"변경한 내용을 서버에 올렸습니다. 다른 기기에서도 곧 볼 수 있습니다.", u8"열기" };
            case luil::ui_severity::warning:
                return { u8"warning", u8"경고", u8"저장 공간이 얼마 남지 않았습니다", u8"남은 공간이 1GB 아래입니다. 오래된 파일을 정리하거나 저장 공간을 늘리세요.", u8"정리" };
            case luil::ui_severity::error:
                return { u8"error", u8"오류", u8"동기화하지 못했습니다", u8"서버에 닿지 않습니다. 연결을 확인한 뒤 다시 시도하세요.", u8"다시 시도" };
            case luil::ui_severity::info:
                break;
            }
            return { u8"info", u8"정보", u8"새 버전이 있습니다", u8"지금 설치하면 작업 중인 문서를 저장한 뒤 다시 엽니다. 설치하는 동안에는 앱을 쓸 수 없습니다.", u8"설치" };
        }

        [[nodiscard]] std::size_t bar_index(const luil::ui_severity severity) noexcept
        {
            return static_cast<std::size_t>(severity);
        }

        [[nodiscard]] std::u8string availability_text(const std::optional<luil::notification_availability>& availability)
        {
            if (availability.has_value() == false)
                return u8"확인 전";
            switch (*availability)
            {
            case luil::notification_availability::available:
                return u8"사용 가능";
            case luil::notification_availability::blocked:
                return u8"꺼짐";
            case luil::notification_availability::permission_needed:
                return u8"권한 필요";
            case luil::notification_availability::unsupported:
                break;
            }
            return u8"지원 안 함";
        }

        // 답의 갈래 이름은 API 값 그대로 적는다. 실패 글은 OS가 준 진단이라 뒤에 붙인다.
        [[nodiscard]] std::u8string result_text(const std::optional<luil::platform_result>& result)
        {
            if (result.has_value() == false)
                return u8"없음";
            std::u8string text {};
            switch (result->status)
            {
            case luil::platform_status::ok:
                return u8"ok";
            case luil::platform_status::not_found:
                text = u8"not_found";
                break;
            case luil::platform_status::unsupported:
                text = u8"unsupported";
                break;
            case luil::platform_status::failed:
                text = u8"failed";
                break;
            }
            if (result->error.empty() == false)
                text += u8" · " + result->error;
            return text;
        }

        [[nodiscard]] std::u8string event_text(const std::optional<luil::notification_event>& event)
        {
            if (event.has_value() == false)
                return u8"없음";
            std::u8string text {};
            switch (event->kind)
            {
            case luil::notification_event_kind::activated:
                text = u8"눌림 " + event->id;
                if (event->launched)
                    text += u8" · 꺼진 앱에서";
                break;
            case luil::notification_event_kind::dismissed:
                text = u8"닫음 " + event->id;
                break;
            case luil::notification_event_kind::failed:
                text = u8"실패 " + event->id + u8" · " + event->error;
                break;
            }
            if (event->lost_before > 0)
                text += u8" · 앞서 버린 사건 " + to_u8(static_cast<int>(event->lost_before)) + u8"개";
            return text;
        }

        [[nodiscard]] luil::info_bar_config make_info_bar(const luil::ui_severity severity)
        {
            const bar_text text { bar_text_of(severity) };
            luil::info_bar_config bar {};
            bar.owner = std::u8string { text.owner };
            bar.severity = severity;
            bar.title = std::u8string { text.title };
            bar.message = std::u8string { text.message };
            bar.action_label = std::u8string { text.action };
            bar.action = luil::make_message_action(info_bar_intent { severity, false });
            bar.close = luil::make_message_action(info_bar_intent { severity, true });
            return bar;
        }
    }

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
        if (const auto* const bar { message.get<info_bar_intent>() }; bar != nullptr)
        {
            const bar_text text { bar_text_of(bar->severity) };
            last_bar_action_ = std::u8string { text.word } + u8" 막대 " + std::u8string { bar->close ? std::u8string_view { u8"닫기" } : text.action };
            // 닫는 것은 막대가 아니라 앱이다. 다음 frame에 이 막대를 짓지 않는다.
            if (bar->close)
                bars_open_[bar_index(bar->severity)] = false;
            return true;
        }
        if (const auto* const status { message.get<os_notification_status_intent>() }; status != nullptr)
        {
            os_status_ = status->availability;
            return true;
        }
        if (const auto* const result { message.get<os_notification_result_intent>() }; result != nullptr)
        {
            os_result_ = result->result;
            return true;
        }
        if (const auto* const event { message.get<os_notification_event_intent>() }; event != nullptr)
        {
            os_event_ = event->event;
            return true;
        }
        if (message.get<info_bar_reopen_intent>() != nullptr)
        {
            bars_open_.fill(true);
            last_bar_action_ = u8"막대 다시 열기";
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
        static_cast<void>(height);
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

        // 정보 막대다. 토스트와 달리 닫기 전까지 남고, 본문은 폭에서 줄을 바꾼다.
        // 높이가 폭에 달려 있어 맞춤 항목으로 담는다. 페이지 높이를 세려고 같은 폭에서 한 번 잰다.
        page.begin_card(u8"정보 막대", u8"닫기 전까지 남습니다. 본문은 폭에서 줄을 바꾸고, 글 폭이 좁으면 동작 단추가 글 아래로 내려갑니다.");
        const float bar_width { page_layout::card_content_width(width) };
        for (const luil::ui_severity severity : bar_severities)
        {
            if (bars_open_[bar_index(severity)] == false)
                continue;
            luil::info_bar_config bar { make_info_bar(severity) };
            const float bar_height { luil::info_bar_element::measure(bar, luil::measure_context { .scale = scale }, bar_width).height };
            page.add(std::make_unique<luil::info_bar_element>(std::move(bar)), luil::stack_item { .length = bar_height, .fit = true });
            page.gap(8.0f);
        }
        // 열림과 마지막 동작은 label로도 그려 보조 기술과 자동화가 되읽는다.
        bool any_closed { false };
        for (const luil::ui_severity severity : bar_severities)
        {
            const bar_text text { bar_text_of(severity) };
            const bool open { bars_open_[bar_index(severity)] };
            any_closed = any_closed || open == false;
            page.add(make_label({ kind_text, u8"info-bar-state-" + std::u8string { text.owner } }, std::u8string { text.word } + u8" 막대: " + (open ? u8"열림" : u8"닫힘"), 12.0f, luil::label_color_role::dim), 22.0f);
        }
        page.add(make_label({ kind_text, u8"info-bar-last-action" }, std::u8string { u8"마지막 동작: " } + (last_bar_action_.empty() ? std::u8string { u8"없음" } : last_bar_action_), 12.0f, luil::label_color_role::dim), 22.0f);
        if (any_closed)
        {
            // 닫은 막대는 tree에 없다. 다시 열 길은 앱이 따로 둔다.
            auto reopen { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, u8"info-bar-reopen" }, luil::text_button_config { .text = u8"막대 다시 열기" }) };
            reopen->set_cursor(luil::ui_cursor::hand);
            reopen->set_action(luil::ui_trigger::left_click, luil::make_message_action(info_bar_reopen_intent {}));
            luil::stack_config reopen_row_config {};
            reopen_row_config.direction = luil::stack_direction::row;
            auto reopen_row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"info-bar-reopen-row" }, reopen_row_config) };
            reopen_row->add_fit(std::move(reopen));
            page.gap(8.0f);
            page.add(std::move(reopen_row), luil::text_button_height);
        }
        page.end_card();

        add_os_notification_card(page);
        return page.finish(scroll_, [](const float delta) { return luil::make_app_action(toasts_scroll_intent { delta }); });
    }

    void toasts_page::add_os_notification_card(page_layout& page) const
    {
        page.begin_card(u8"OS 알림", u8"앱 창 밖 Windows 알림 센터에 올립니다. 알림을 누르면 데모가 앞으로 오고 사건 줄이 바뀝니다.");
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"os-notification-buttons" }, row_config) };
        const auto add_button = [&row](std::u8string owner, std::u8string label, const os_notification_action action) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, std::move(owner) }, luil::text_button_config { .text = std::move(label) }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, luil::make_message_action(os_notification_request_intent { action }));
            row->add_fit(std::move(button));
        };
        add_button(u8"os-notification-send", u8"OS 알림 보내기", os_notification_action::send);
        add_button(u8"os-notification-replace", u8"OS 알림 바꾸기", os_notification_action::replace);
        add_button(u8"os-notification-close", u8"OS 알림 거두기", os_notification_action::close);
        page.add(std::move(row), luil::text_button_height);
        page.gap(8.0f);
        page.add(make_label({ kind_text, u8"os-notification-status" }, u8"OS 알림 상태: " + availability_text(os_status_), 12.0f, luil::label_color_role::dim), 22.0f);
        page.add(make_label({ kind_text, u8"os-notification-result" }, u8"OS 알림 결과: " + result_text(os_result_), 12.0f, luil::label_color_role::dim), 22.0f);
        page.add(make_label({ kind_text, u8"os-notification-event" }, u8"OS 알림 사건: " + event_text(os_event_), 12.0f, luil::label_color_role::dim), 22.0f);
        // 데모는 시작할 때마다 앱 식별 `luil.Demo`를 HKCU에 쓴다. 지우는 길을 함께 적는다.
        page.add(make_label({ kind_text, u8"os-notification-hint" }, u8"앱 식별 luil.Demo를 HKCU에 씁니다. 지우려면 luil_demo.exe --unregister-app-identity로 띄웁니다.", 11.0f, luil::label_color_role::dim), 22.0f);
        page.end_card();
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
}
