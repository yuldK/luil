#include "mobile_demo/sheets_page.h"

#include "luil/ui/check_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/wrap_element.h"
#include "mobile_demo/device_page.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace mobile_demo {
    namespace {
        // 이 폭부터 아래 시트가 오른쪽 옆 시트로 선다 (논리 픽셀). 폭 등급 medium이 시작하는 자리다.
        constexpr float side_sheet_width { 600.0f };
        constexpr int row_count { 30 };
        constexpr float row_height { 44.0f };
        // 시트 안 머리(설명 한 줄과 단추 줄)의 높이다.
        constexpr float sheet_header_height { note_line_height + touch_height + 3.0f * 8.0f };
        // 아래 시트에는 놓기를 되풀이하는 줄이 하나 더 있다.
        constexpr float sheet_motion_row_height { touch_height + 8.0f };
        // "튕기기" 단추가 놓는 속도다 (초당 논리 픽셀). 손으로 힘껏 튕기는 정도다.
        constexpr float fling_speed { 1800.0f };

        struct sheet_choice
        {
            std::u8string_view key {};
            std::u8string_view label {};
            luil::sheet_kind kind {};
            bool modal { true };
        };

        constexpr std::array<sheet_choice, 4> sheet_choices {
            sheet_choice { u8"bottom", u8"아래 시트", luil::sheet_kind::bottom, true },
            sheet_choice { u8"bottom-plain", u8"아래 (비modal)", luil::sheet_kind::bottom, false },
            sheet_choice { u8"end", u8"옆 시트", luil::sheet_kind::end, true },
            sheet_choice { u8"start", u8"서랍", luil::sheet_kind::start, true },
        };

        [[nodiscard]] std::u8string reason_name(const luil::sheet_dismiss_reason reason)
        {
            switch (reason)
            {
            case luil::sheet_dismiss_reason::scrim:
                return u8"scrim";
            case luil::sheet_dismiss_reason::back:
                return u8"뒤로 가기";
            case luil::sheet_dismiss_reason::escape:
                return u8"Esc";
            case luil::sheet_dismiss_reason::drag:
                return u8"끌어 내림";
            }
            return u8"";
        }

        // 같은 놓기를 되풀이하는 release다. 멈춤점이 반이면 위로, 전체면 아래로 튕기고, 멈출 자리는 손으로 놓았을 때와 같은 규칙
        // (`settle_sheet`)으로 고른다.
        [[nodiscard]] luil::sheet_release make_fling(const luil::sheet_host_config& sheet, const float height)
        {
            const float rising { sheet.detent == luil::sheet_detent::half ? fling_speed : -fling_speed };
            const luil::sheet_settle settle { luil::settle_sheet(sheet, height, -rising) };
            const float target { settle.dismiss ? 0.0f : luil::sheet_detent_height(sheet, settle.detent, height) };
            return luil::sheet_release { settle, luil::sheet_visible_height(sheet, height), target, rising };
        }

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_button(const std::u8string& key, const std::u8string_view label, luil::ui_action action)
        {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"sheet-" + key }, luil::text_button_config { .text = std::u8string { label } }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            return button;
        }
    }

    float sheets_page::offset_at(const std::chrono::steady_clock::time_point now) const noexcept
    {
        return drag_offset_ + luil::spring_value(motion_, now);
    }

    void sheets_page::close(const luil::sheet_dismiss_reason reason)
    {
        open_.reset();
        drag_offset_ = 0.0f;
        motion_ = luil::spring_at_rest(0.0f);
        last_dismiss_ = reason;
    }

    void sheets_page::apply_release(const luil::sheet_release& release, const std::chrono::steady_clock::time_point now)
    {
        if (release.settle.dismiss)
        {
            close(luil::sheet_dismiss_reason::drag);
            return;
        }
        detent_ = release.settle.detent;
        drag_offset_ = 0.0f;
        target_height_ = release.target_height;
        // 시트가 오르면 끄는 양이 줄어든다. 그래서 끄는 양의 속도는 높이의 속도와 부호가 반대다.
        motion_ = luil::spring_release(release.target_height - release.visible_height, -release.velocity, 0.0f, now, luil::spring_snappy, reduce_motion_);
    }

    void sheets_page::set_reduce_motion(const bool reduce_motion) noexcept
    {
        reduce_motion_ = reduce_motion;
        // 도는 중에 켜지면 멈춤점에 붙인다. 두면 남은 시간만큼 더 돌고 끝난다.
        if (reduce_motion)
            motion_ = luil::spring_at_rest(0.0f);
    }

    std::optional<std::chrono::steady_clock::time_point> sheets_page::next_update(const std::chrono::steady_clock::time_point now) const noexcept
    {
        return luil::spring_next_update(motion_, now);
    }

    bool sheets_page::handle(const luil::app_message& message, const std::chrono::steady_clock::time_point now)
    {
        if (const auto* const open { message.get<sheet_open_intent>() }; open != nullptr)
        {
            open_ = open_sheet { open->kind, open->modal };
            detent_ = luil::sheet_detent::half;
            drag_offset_ = 0.0f;
            motion_ = luil::spring_at_rest(0.0f);
            list_offset_ = 0.0f;
            return true;
        }
        if (const auto* const drag { message.get<sheet_drag_intent>() }; drag != nullptr)
        {
            // 놓은 뒤 도는 중에 다시 잡으면 지금 끄는 양에서 이어 끈다. 스프링은 거둔다.
            drag_offset_ = offset_at(now) + drag->delta;
            motion_ = luil::spring_at_rest(0.0f);
            return true;
        }
        if (const auto* const settle { message.get<sheet_settle_intent>() }; settle != nullptr)
        {
            detent_ = settle->detent;
            drag_offset_ = 0.0f;
            motion_ = luil::spring_at_rest(0.0f);
            return true;
        }
        if (const auto* const release { message.get<sheet_release_intent>() }; release != nullptr)
        {
            apply_release(release->release, now);
            return true;
        }
        if (const auto* const dismiss { message.get<sheet_dismiss_intent>() }; dismiss != nullptr)
        {
            close(dismiss->reason);
            return true;
        }
        if (const auto* const scroll { message.get<sheet_scroll_intent>() }; scroll != nullptr)
        {
            list_offset_ += scroll->delta;
            return true;
        }
        if (const auto* const adaptive { message.get<sheet_adaptive_intent>() }; adaptive != nullptr)
        {
            adaptive_ = adaptive->adaptive;
            return true;
        }
        return false;
    }

    page_content sheets_page::build(const float width, const float scale)
    {
        static_cast<void>(scale);
        page_column column { u8"sheets" };
        column.note(u8"sheets-note-1", u8"열린 시트·멈춤점·목록 스크롤은 앱 상태다.");
        column.note(u8"sheets-note-2", u8"돌리거나 화면을 나눠도 그대로 남는다.");
        column.gap(12.0f);

        luil::wrap_config grid {};
        grid.item_width = 120.0f;
        grid.item_height = touch_height;
        grid.spacing = 8.0f;
        grid.line_spacing = 8.0f;
        auto buttons { std::make_unique<luil::wrap_element>(luil::ui_element_id { kind_layout, u8"sheet-buttons" }, grid) };
        for (const sheet_choice& choice : sheet_choices)
            buttons->add(make_button(std::u8string { choice.key }, choice.label, luil::make_message_action(sheet_open_intent { choice.kind, choice.modal })));
        column.add(std::move(buttons), luil::wrap_height_for(grid, width - 2.0f * page_padding, sheet_choices.size()));
        column.gap(12.0f);

        luil::check_config adaptive {};
        adaptive.owner = u8"sheet-adaptive";
        adaptive.style = luil::check_style::toggle_switch;
        adaptive.label = u8"600dp부터 아래 시트를 옆 시트로";
        adaptive.checked = adaptive_;
        adaptive.toggle = luil::make_message_action(sheet_adaptive_intent { adaptive_ == false });
        column.add(std::make_unique<luil::check_element>(std::move(adaptive)), touch_height, std::min(width - 2.0f * page_padding, 360.0f));
        column.gap(12.0f);
        column.body(u8"sheets-dismissed", u8"마지막으로 닫은 계기: " + (last_dismiss_.has_value() ? reason_name(*last_dismiss_) : std::u8string { u8"없음" }));
        return column.finish();
    }

    luil::sheet_host_config sheets_page::config(const std::chrono::steady_clock::time_point now) const
    {
        luil::sheet_host_config config {};
        config.owner = u8"demo-sheet";
        config.name = u8"시트";
        config.kind = open_.has_value() ? open_->kind : luil::sheet_kind::bottom;
        config.modal = open_.has_value() ? open_->modal : true;
        if (adaptive_ && config.kind == luil::sheet_kind::bottom)
            config.side_from_width = side_sheet_width;
        config.detent = detent_;
        config.drag_offset = offset_at(now);
        config.dismiss = [](const luil::sheet_dismiss_reason reason) { return luil::make_app_action(sheet_dismiss_intent { reason }); };
        config.drag = [](const float delta) { return luil::make_app_action(sheet_drag_intent { delta }); };
        // 놓기는 settle이 아니라 release로 나간다. 닫는 놓기는 전처럼 바로 거두고, 그 밖에는 놓은 자리에서 멈춤점까지 스프링으로 이어 간다.
        config.release = [](const luil::sheet_release& release) {
            if (release.settle.dismiss)
                return luil::make_app_action(sheet_dismiss_intent { luil::sheet_dismiss_reason::drag });
            return luil::make_app_action(sheet_release_intent { release });
        };
        return config;
    }

    luil::input_action sheets_page::back_action() const
    {
        return luil::make_app_action(sheet_dismiss_intent { luil::sheet_dismiss_reason::back });
    }

    void sheets_page::add_overlay(luil::root_element& root, const float width, const float height, const float scale, const luil::rect_f& surface, const std::chrono::steady_clock::time_point now)
    {
        if (open_.has_value() == false)
            return;
        luil::sheet_host_config sheet { config(now) };
        const float logical_height { height / scale };
        const luil::sheet_kind kind { luil::resolve_sheet_kind(sheet, width / scale) };
        // 옆 시트에는 끌기 종료 콜백이 없다. 아래 시트에서 끄던 양과 도는 스프링은 여기서 거두고 멈춤점과 목록 위치는 지킨다.
        if (kind != luil::sheet_kind::bottom)
        {
            drag_offset_ = 0.0f;
            motion_ = luil::spring_at_rest(0.0f);
            sheet.drag_offset = 0.0f;
        }
        // 시트 안 자리의 높이다. 아래 시트는 지금 보이는 높이에서 손잡이(24)를 뺀다.
        const float inner_height { kind == luil::sheet_kind::bottom ? std::max(0.0f, luil::sheet_visible_height(sheet, logical_height) - 24.0f) : logical_height };

        luil::stack_config column_config {};
        column_config.spacing = 8.0f;
        column_config.padding = luil::edge_insets::symmetric(page_padding, 8.0f);
        auto content { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"sheet-content" }, column_config) };
        content->add(make_label({ kind_text, u8"sheet-help" }, u8"목록이 맨 위일 때 아래로 끌면 시트가 내려간다.", note_text_size, luil::label_color_role::dim), note_line_height);

        // 회전을 시스템 설정 없이 시험하는 단추다. 방향 요청은 기기 페이지의 상태다.
        luil::stack_config row_config {};
        row_config.direction = luil::stack_direction::row;
        row_config.spacing = 8.0f;
        auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"sheet-row" }, row_config) };
        if (luil::current_ui_platform().capabilities.screen_orientation)
        {
            row->add(make_button(u8"landscape", u8"가로로", luil::make_message_action(orientation_intent { luil::screen_orientation::landscape })), 68.0f);
            row->add(make_button(u8"portrait", u8"세로로", luil::make_message_action(orientation_intent { luil::screen_orientation::portrait })), 68.0f);
            row->add(make_button(u8"system", u8"시스템", luil::make_message_action(orientation_intent { luil::screen_orientation::system })), 68.0f);
        }
        row->add(make_button(u8"close", u8"닫기", luil::make_message_action(sheet_dismiss_intent { luil::sheet_dismiss_reason::scrim })), 68.0f);
        content->add(std::move(row), touch_height);

        // 아래 시트만이다 (옆 시트와 서랍에는 멈춤점이 없다). 단추가 같은 놓기를 되풀이하고, 글이 스프링의 상태를 말한다.
        if (kind == luil::sheet_kind::bottom)
        {
            const bool moving { luil::spring_finished(motion_, now) == false };
            std::u8string state { u8"멈춤" };
            if (moving)
                state = u8"도는 중 · 목표 높이 " + to_u8(static_cast<int>(std::lround(target_height_)));
            luil::stack_config motion_config {};
            motion_config.direction = luil::stack_direction::row;
            motion_config.spacing = 8.0f;
            auto motion_row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"sheet-motion-row" }, motion_config) };
            motion_row->add(make_button(u8"fling", u8"튕기기", luil::make_message_action(sheet_release_intent { make_fling(sheet, logical_height) })), 80.0f);
            motion_row->add_flexible(make_label({ kind_text, u8"sheets-motion" }, std::move(state), note_text_size, luil::label_color_role::dim));
            content->add(std::move(motion_row), touch_height);
        }

        // 시트 안의 세로 목록이다. 위치는 이 페이지의 상태이고, 맨 위인지를 함께 싣는다.
        page_column rows { u8"sheet-rows", 0.0f };
        for (int index { 0 }; index < row_count; ++index)
            rows.add(make_label({ kind_text, u8"sheet-row-" + to_u8(index) }, u8"항목 " + to_u8(index + 1), body_text_size, luil::label_color_role::primary), row_height);
        page_content list { rows.finish() };
        const float viewport { std::max(0.0f, inner_height - sheet_header_height - (kind == luil::sheet_kind::bottom ? sheet_motion_row_height : 0.0f)) };
        list_offset_ = luil::clamp_scroll(list.height, viewport, list_offset_);
        auto view { std::make_unique<luil::scroll_view_element>(luil::ui_element_id { kind_page_scroll, u8"sheet-list" }, luil::scroll_view_config { list.height, list_offset_ }) };
        view->set_content(std::move(list.element));
        view->set_scroll_source(luil::scroll_source {
            .scroll = [](const float delta) { return luil::make_app_action(sheet_scroll_intent { delta }); },
            .scale = scale,
            .at_start = list_offset_ <= 0.0f,
        });
        content->add_flexible(std::move(view));

        auto host { std::make_unique<luil::sheet_host_element>(sheet) };
        host->set_content(std::move(content));
        host->arrange({ .slot = { 0.0f, 0.0f, width, height }, .scale = scale, .surface = surface });
        root.add(std::move(host));
    }
}
