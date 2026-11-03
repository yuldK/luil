#include "mobile_demo/toasts_page.h"

#include "luil/ui/dialog_elements.h"
#include "luil/ui/info_bar_element.h"
#include "luil/ui/layer_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/teaching_tip_element.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace mobile_demo {
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
                return { u8"success", u8"성공", u8"저장했다", u8"변경한 내용을 서버에 올렸다. 다른 기기에서도 곧 볼 수 있다.", u8"열기" };
            case luil::ui_severity::warning:
                return { u8"warning", u8"경고", u8"저장 공간이 얼마 남지 않았다", u8"남은 공간이 1GB 아래다. 오래된 파일을 정리하거나 저장 공간을 늘려라.", u8"정리" };
            case luil::ui_severity::error:
                return { u8"error", u8"오류", u8"동기화하지 못했다", u8"서버에 닿지 않는다. 연결을 확인한 뒤 다시 시도해라.", u8"다시 시도" };
            case luil::ui_severity::info:
                break;
            }
            return { u8"info", u8"정보", u8"새 버전이 있다", u8"지금 설치하면 작업 중인 문서를 저장한 뒤 다시 연다. 설치하는 동안에는 앱을 쓸 수 없다.", u8"설치" };
        }

        [[nodiscard]] std::size_t bar_index(const luil::ui_severity severity) noexcept
        {
            return static_cast<std::size_t>(severity);
        }

        // 손가락으로 누르므로 단추를 터치 영역으로 키운다. 막대의 최소 높이도 따라 오른다.
        // 풍선이 닫힌 계기의 낱말이다. 상태 설명에 적힌다.
        [[nodiscard]] std::u8string_view dismiss_word(const luil::teaching_tip_dismiss_reason reason) noexcept
        {
            switch (reason)
            {
            case luil::teaching_tip_dismiss_reason::outside:
                return u8"바깥 탭";
            case luil::teaching_tip_dismiss_reason::escape:
                return u8"Esc";
            case luil::teaching_tip_dismiss_reason::back:
                return u8"뒤로 가기";
            case luil::teaching_tip_dismiss_reason::close_button:
                break;
            }
            return u8"닫기 단추";
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
            bar.control_size = touch_height;
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
        if (const auto* const bar { message.get<info_bar_intent>() }; bar != nullptr)
        {
            const bar_text text { bar_text_of(bar->severity) };
            last_bar_action_ = std::u8string { text.word } + u8" 막대 " + std::u8string { bar->close ? std::u8string_view { u8"닫기" } : text.action };
            // 닫는 것은 막대가 아니라 앱이다. 다음 frame에 이 막대를 짓지 않는다.
            if (bar->close)
                bars_open_[bar_index(bar->severity)] = false;
            return true;
        }
        if (const auto* const open { message.get<tip_open_intent>() }; open != nullptr)
        {
            tip_open_ = true;
            tip_modal_ = open->modal;
            tip_opener_ = open->opener;
            tip_opened_at_ = std::chrono::steady_clock::now();
            return true;
        }
        if (const auto* const dismiss { message.get<tip_dismiss_intent>() }; dismiss != nullptr)
        {
            finish_tip(std::u8string { dismiss_word(dismiss->reason) });
            return true;
        }
        if (const auto* const action { message.get<tip_action_intent>() }; action != nullptr)
        {
            // 행동 단추는 풍선을 닫지 않는다. 닫는 것은 이 앱의 선택이다.
            finish_tip(action->primary ? u8"써 보기" : u8"나중에");
            return true;
        }
        if (message.get<tip_target_intent>() != nullptr)
        {
            push(u8"새 기능을 눌렀다.", luil::toast_severity::info, toast_request_intent {}.duration);
            return true;
        }
        if (message.get<info_bar_reopen_intent>() != nullptr)
        {
            bars_open_.fill(true);
            last_bar_action_ = u8"막대 다시 열기";
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
        // 풍선 상태 설명이 이번 frame의 대상 자리와 다르면 지금 한 frame을 더 짓는다.
        if (tip_relabel_)
            return std::chrono::steady_clock::now();
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
        column.gap(24.0f);

        add_tip_section(column, width, scale);

        // 정보 막대다. 토스트와 달리 닫기 전까지 남고, 본문은 폭에서 줄을 바꾼다. 회전하면 폭이 바뀌어 높이가 달라지므로
        // 폭을 받아 매번 짓는 이 페이지가 다시 잰다. 페이지 열은 높이만 받아 막대 높이를 같은 폭에서 재서 담는다.
        column.heading(u8"info-bar-heading", u8"정보 막대");
        column.note(u8"info-bar-hint", u8"닫기 전까지 남는다. 글 폭이 좁으면 동작 단추가 글 아래로 내려간다.");
        column.gap(8.0f);
        const float bar_width { width - 2.0f * page_padding };
        for (const luil::ui_severity severity : bar_severities)
        {
            if (bars_open_[bar_index(severity)] == false)
                continue;
            luil::info_bar_config bar { make_info_bar(severity) };
            const float bar_height { luil::info_bar_element::measure(bar, luil::measure_context { .scale = scale }, bar_width).height };
            column.add(std::make_unique<luil::info_bar_element>(std::move(bar)), bar_height);
            column.gap(8.0f);
        }
        // 열림과 마지막 동작은 label로도 그려 보조 기술과 자동화가 되읽는다.
        bool any_closed { false };
        for (const luil::ui_severity severity : bar_severities)
        {
            const bar_text text { bar_text_of(severity) };
            const bool open { bars_open_[bar_index(severity)] };
            any_closed = any_closed || open == false;
            column.note(u8"info-bar-state-" + std::u8string { text.owner }, std::u8string { text.word } + u8" 막대: " + (open ? u8"열림" : u8"닫힘"));
        }
        column.note(u8"info-bar-last-action", std::u8string { u8"마지막 동작: " } + (last_bar_action_.empty() ? std::u8string { u8"없음" } : last_bar_action_));
        if (any_closed)
        {
            // 닫은 막대는 tree에 없다. 다시 열 길은 앱이 따로 둔다.
            auto reopen { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, u8"info-bar-reopen" }, luil::text_button_config { .text = u8"막대 다시 열기" }) };
            reopen->set_cursor(luil::ui_cursor::hand);
            reopen->set_action(luil::ui_trigger::left_click, luil::make_message_action(info_bar_reopen_intent {}));
            column.gap(8.0f);
            column.add(std::move(reopen), touch_height, std::min(width - 2.0f * page_padding, 260.0f));
        }
        return column.finish();
    }

    void toasts_page::add_tip_section(page_column& column, const float width, const float scale)
    {
        tip_built_ = true;
        column.heading(u8"tip-heading", u8"안내 풍선");
        column.wrapped_note(u8"tip-hint", u8"새 기능 단추를 꼬리로 가리킨다. 덮개는 바깥 탭과 뒤로 가기로도 닫히고, 비modal은 닫기 단추로만 닫힌다.", width, scale);
        column.gap(8.0f);
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"tip-buttons" }, row_config) };
        auto target { std::make_unique<luil::text_button_element>(tip_target_id(), luil::text_button_config { .text = u8"새 기능" }) };
        target->set_cursor(luil::ui_cursor::hand);
        target->set_action(luil::ui_trigger::left_click, luil::make_message_action(tip_target_intent {}));
        row->add_flexible(std::move(target));
        const auto add_opener = [&row](std::u8string owner, std::u8string label, const bool modal) {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_toast_trigger, std::move(owner) }, luil::text_button_config { .text = std::move(label) }) };
            button->set_cursor(luil::ui_cursor::hand);
            // 연 단추가 돌아갈 자리다. 액션이 자기 id를 받는다.
            button->set_action(luil::ui_trigger::left_click, [modal](const luil::ui_action_context& context) -> std::vector<luil::input_action> {
                return { luil::make_app_action(tip_open_intent { modal, context.element }) };
            });
            row->add_flexible(std::move(button));
        };
        add_opener(u8"tip-open-modal", u8"덮개", true);
        add_opener(u8"tip-open-modeless", u8"비modal", false);
        column.add(std::move(row), touch_height);
        column.gap(8.0f);

        // 열림, 방향, 마지막 닫힘을 설명으로 그려 되읽는다. 방향은 바라는 방향이고, 지난 frame에 대상을 찾지 못했으면 "대상 없음"이다.
        std::u8string state { u8"안내 풍선: " };
        if (tip_open_)
            state += tip_modal_ ? u8"열림(덮개)" : u8"열림(비modal)";
        else
            state += u8"닫힘";
        state += u8" · 방향: ";
        if (tip_open_ == false)
            state += u8"없음";
        else
            state += tip_target_found_ ? u8"아래" : u8"대상 없음";
        state += u8" · 마지막 닫힘: ";
        state += tip_last_close_.empty() ? std::u8string { u8"없음" } : tip_last_close_;
        tip_label_found_ = tip_target_found_;
        column.wrapped_note(u8"tip-state", std::move(state), width, scale);
        column.gap(24.0f);
    }

    void toasts_page::finish_tip(std::u8string reason)
    {
        // 늦게 온 닫기 메시지가 다시 연 풍선의 기록을 덮지 않는다.
        if (tip_open_ == false)
            return;
        tip_open_ = false;
        tip_last_close_ = std::move(reason);
    }

    void toasts_page::close_tip()
    {
        tip_open_ = false;
        tip_relabel_ = false;
    }

    bool toasts_page::tip_open() const noexcept
    {
        return tip_open_;
    }

    bool toasts_page::back_closes_tip() const noexcept
    {
        return tip_open_ && tip_modal_;
    }

    luil::input_action toasts_page::back_action() const
    {
        return luil::make_app_action(tip_dismiss_intent { luil::teaching_tip_dismiss_reason::back });
    }

    std::unique_ptr<luil::ui_element> toasts_page::make_overlay(const luil::ui_element* const scope)
    {
        prune();
        std::unique_ptr<luil::ui_element> tip { make_tip(scope) };
        std::unique_ptr<luil::ui_element> stack { make_toast_stack() };
        if (tip == nullptr)
            return stack;
        if (stack == nullptr)
            return tip;
        // 둘 다 창 전체를 받는다. 풍선이 뒤라 위에 그려지고 Tab 순서의 끝이다.
        auto layer { std::make_unique<luil::layer_element>(luil::ui_element_id { kind_layout, u8"toasts-overlay" }) };
        layer->add(std::move(stack));
        layer->add(std::move(tip));
        return layer;
    }

    std::unique_ptr<luil::ui_element> toasts_page::make_tip(const luil::ui_element* const scope)
    {
        // 이 frame에 페이지를 지었을 때만 설명을 다시 맞춘다. 다른 페이지에서는 설명이 보이지 않는다.
        const bool built { tip_built_ };
        tip_built_ = false;
        if (tip_open_ == false)
        {
            tip_relabel_ = false;
            return nullptr;
        }
        luil::teaching_tip_config config {};
        config.owner = std::u8string { tip_owner };
        if (scope != nullptr)
            config.target = luil::teaching_tip_target(*scope, tip_target_id());
        tip_target_found_ = config.target.has_value();
        tip_relabel_ = built && tip_target_found_ != tip_label_found_;
        config.side = luil::field_popup_side::below;
        config.modal = tip_modal_;
        config.title = u8"새 기능";
        config.body = u8"새 기능 단추로 바로 시작할 수 있다. 덮개 풍선은 바깥을 탭하거나 뒤로 가기로 닫힌다.";
        config.primary = luil::teaching_tip_button { u8"써 보기", luil::make_message_action(tip_action_intent { true }) };
        config.secondary = luil::teaching_tip_button { u8"나중에", luil::make_message_action(tip_action_intent { false }) };
        config.dismiss = [](const luil::teaching_tip_dismiss_reason reason) { return luil::make_app_action(tip_dismiss_intent { reason }); };
        config.focus_return = tip_opener_;
        config.opened_at = tip_opened_at_;
        // 손가락으로 누르므로 단추 높이와 닫기 한 변을 터치 영역으로 올린다.
        config.control_size = touch_height;
        return std::make_unique<luil::teaching_tip_element>(std::move(config));
    }

    std::unique_ptr<luil::ui_element> toasts_page::make_toast_stack() const
    {
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
}
