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
#include <utility>

namespace mobile_demo {
    namespace {
        // 이 폭부터 아래 시트가 오른쪽 옆 시트로 선다 (논리 픽셀). 폭 등급 medium이 시작하는 자리다.
        constexpr float side_sheet_width { 600.0f };
        constexpr int row_count { 30 };
        constexpr float row_height { 44.0f };
        // 시트 안 머리(설명 한 줄과 단추 줄)의 높이다.
        constexpr float sheet_header_height { note_line_height + touch_height + 3.0f * 8.0f };

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

        [[nodiscard]] std::unique_ptr<luil::ui_element> make_button(const std::u8string& key, const std::u8string_view label, luil::ui_action action)
        {
            auto button { std::make_unique<luil::text_button_element>(luil::ui_element_id { kind_layout, u8"sheet-" + key }, luil::text_button_config { .text = std::u8string { label } }) };
            button->set_cursor(luil::ui_cursor::hand);
            button->set_action(luil::ui_trigger::left_click, std::move(action));
            return button;
        }
    } // namespace

    bool sheets_page::handle(const luil::app_message& message)
    {
        if (const auto* const open { message.get<sheet_open_intent>() }; open != nullptr)
        {
            open_ = open_sheet { open->kind, open->modal };
            detent_ = luil::sheet_detent::half;
            drag_offset_ = 0.0f;
            list_offset_ = 0.0f;
            return true;
        }
        if (const auto* const drag { message.get<sheet_drag_intent>() }; drag != nullptr)
        {
            drag_offset_ += drag->delta;
            return true;
        }
        if (const auto* const settle { message.get<sheet_settle_intent>() }; settle != nullptr)
        {
            detent_ = settle->detent;
            drag_offset_ = 0.0f;
            return true;
        }
        if (const auto* const dismiss { message.get<sheet_dismiss_intent>() }; dismiss != nullptr)
        {
            open_.reset();
            drag_offset_ = 0.0f;
            last_dismiss_ = dismiss->reason;
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

    luil::sheet_host_config sheets_page::config() const
    {
        luil::sheet_host_config config {};
        config.owner = u8"demo-sheet";
        config.name = u8"시트";
        config.kind = open_.has_value() ? open_->kind : luil::sheet_kind::bottom;
        config.modal = open_.has_value() ? open_->modal : true;
        if (adaptive_ && config.kind == luil::sheet_kind::bottom)
            config.side_from_width = side_sheet_width;
        config.detent = detent_;
        config.drag_offset = drag_offset_;
        config.dismiss = [](const luil::sheet_dismiss_reason reason) { return luil::make_app_action(sheet_dismiss_intent { reason }); };
        config.drag = [](const float delta) { return luil::make_app_action(sheet_drag_intent { delta }); };
        config.settle = [](const luil::sheet_detent detent) { return luil::make_app_action(sheet_settle_intent { detent }); };
        return config;
    }

    luil::input_action sheets_page::back_action() const
    {
        return luil::make_app_action(sheet_dismiss_intent { luil::sheet_dismiss_reason::back });
    }

    void sheets_page::add_overlay(luil::root_element& root, const float width, const float height, const float scale)
    {
        if (open_.has_value() == false)
            return;
        const luil::sheet_host_config sheet { config() };
        const float logical_height { height / scale };
        const luil::sheet_kind kind { luil::resolve_sheet_kind(sheet, width / scale) };
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

        // 시트 안의 세로 목록이다. 위치는 이 페이지의 상태이고, 맨 위인지를 함께 싣는다.
        page_column rows { u8"sheet-rows", 0.0f };
        for (int index { 0 }; index < row_count; ++index)
            rows.add(make_label({ kind_text, u8"sheet-row-" + to_u8(index) }, u8"항목 " + to_u8(index + 1), body_text_size, luil::label_color_role::primary), row_height);
        page_content list { rows.finish() };
        const float viewport { std::max(0.0f, inner_height - sheet_header_height) };
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
        host->arrange({ { 0.0f, 0.0f, width, height }, scale });
        root.add(std::move(host));
    }
} // namespace mobile_demo
