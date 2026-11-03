#include "mobile_demo/app.h"

#include "demo/readonly_undo_keys.h"
#include "luil/generated/codicons.h"
#include "luil/ui/app_bar_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/page_transition_element.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_platform.h"
#include "mobile_demo/page_list.h"

#include <algorithm>
#include <utility>

namespace mobile_demo {
    namespace {
        // 넓은 화면에서 왼쪽 목록의 폭이다.
        constexpr float navigation_width { 260.0f };
    }

    void mobile_driver::handle(luil::app_message message)
    {
        if (message.get<close_intent>() != nullptr)
        {
            closed_.store(true);
            return;
        }
        if (const auto* const metrics { message.get<window_metrics_intent>() }; metrics != nullptr)
        {
            // 전환은 움직임 줄이기가 켜지거나 창 크기·배율·폭 등급이 바뀌면(회전·화면 분할) 놓는다 (page-transition-design.md 결정 5).
            if ((metrics->reduce_motion && metrics_.reduce_motion == false) || metrics->width != metrics_.width || metrics->height != metrics_.height
                || metrics->scale != metrics_.scale || metrics->width_class != metrics_.width_class)
                page_motion_ = {};
            metrics_ = *metrics;
            // 시트 페이지는 metrics 메시지를 보지 못하므로 셸이 움직임 줄이기만 넘긴다.
            sheets_.set_reduce_motion(metrics->reduce_motion);
            return;
        }
        if (const auto* const clipboard_host { message.get<clipboard_host_intent>() }; clipboard_host != nullptr)
        {
            host_ = clipboard_host->host;
            return;
        }
        if (const auto* const navigate { message.get<navigate_intent>() }; navigate != nullptr)
        {
            // 떠나는 본문은 `page_`가 아직 옛 값일 때 짓는다. 목록에서 페이지로 들어가는 것은 앞으로다.
            //  - 안내 풍선은 대상이 있는 페이지에만 선다. 떠나는 본문을 짓기 전에 닫아 그 본문의 설명도 닫힘을 그린다.
            if (navigate->page != page_)
            {
                toasts_.close_tip();
                page_motion_ = make_page_motion(navigate->page, luil::page_transition_direction::forward);
            }
            page_ = navigate->page;
            // 페이지를 떠나면 그 페이지의 popup도 닫는다.
            close_popups();
            return;
        }
        if (message.get<back_intent>() != nullptr)
        {
            // 앱 바의 뒤로 단추와 시스템 뒤로 가기는 뒤로다.
            if (page_.empty() == false)
            {
                toasts_.close_tip();
                page_motion_ = make_page_motion({}, luil::page_transition_direction::backward);
            }
            page_.clear();
            close_popups();
            return;
        }
        if (const auto* const scroll { message.get<scroll_intent>() }; scroll != nullptr)
        {
            scrolls_[scroll->owner] += scroll->delta;
            return;
        }
        if (message.get<popup_close_intent>() != nullptr)
        {
            close_popups();
            return;
        }
        // 컨텍스트 메뉴 선택은 페이지가 메뉴를 닫고 셸이 토스트로 잇는다.
        if (const auto* const select { message.get<card_menu_select_intent>() }; select != nullptr)
        {
            static_cast<void>(popups_.handle(message));
            static_cast<void>(toasts_.handle(luil::app_message { toast_request_intent { u8"메뉴: " + select->key, luil::toast_severity::success } }));
            return;
        }
        // 페이지들의 메시지다. 타입이 겹치지 않아 처음 받는 쪽이 임자다. 편집 메시지만 타입이 같고
        // target으로 나뉜다 — 남의 target이면 handle이 거짓을 돌려준다.
        // 기본 페이지의 서식 칸이 쥔 클립보드 요청은 받은 자리에서 host에 넣는다.
        if (basics_.handle(message))
        {
            if (std::optional<luil::clipboard_request> request { basics_.take_clipboard_request() }; request.has_value() && host_ != nullptr)
                host_->post_clipboard_request(std::move(*request));
            return;
        }
        if (lists_.handle(message) || tabs_.handle(message) || groups_.handle(message) || toasts_.handle(message) || popups_.handle(message) || zoom_.handle(message) || ink_.handle(message)
            || inspector_.handle(message) || theme_.handle(message) || device_.handle(message) || sheets_.handle(message) || smb_.handle(message)
            || files_.handle(message))
            return;
    }

    void mobile_driver::stop_workers()
    {
        smb_.stop();
        files_.stop();
    }

    void mobile_driver::close_popups()
    {
        tabs_.close_popups();
        popups_.close_popups();
    }

    luil::page_transition mobile_driver::make_page_motion(const std::u8string& next, const luil::page_transition_direction direction)
    {
        if (device_.page_transition_enabled() == false || metrics_.reduce_motion || motion_slot_.width <= 0.0f || motion_slot_.height <= 0.0f)
            return {};
        const bool wide { metrics_.width_class != luil::window_width_class::compact };
        // 지난 frame과 꼴이 다르면(회전 직후) 자리가 맞지 않아 짓지 않는다.
        if (wide != motion_wide_)
            return {};
        // 넓은 화면의 오른쪽 판은 비어 있으면 첫 페이지를 보인다. 같은 페이지로 가는 것이면 바뀌는 것이 없다.
        const std::u8string shown { page_.empty() && wide ? std::u8string { page_basics } : page_ };
        const std::u8string coming { next.empty() && wide ? std::u8string { page_basics } : next };
        if (shown == coming)
            return {};
        const float scale { motion_scale_ };
        auto prior { make_body_page(shown, wide, motion_slot_.width / scale, motion_slot_.height / scale, scale) };
        auto tree { std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(prior), motion_slot_, scale, {}, nullptr, theme_.style().get())) };
        // 좁은 화면은 목록 → 페이지를 가로로 민다. 넓은 화면의 오른쪽 판은 자리가 그대로라 옅어짐이다.
        luil::page_transition_style style {};
        style.effect = wide ? luil::page_transition_effect::fade : luil::page_transition_effect::slide;
        style.axis = luil::page_transition_axis::horizontal;
        style.direction = direction;
        return luil::start_page_transition(std::move(tree), style, std::chrono::steady_clock::now(), metrics_.reduce_motion);
    }

    std::unique_ptr<luil::ui_element> mobile_driver::make_body_page(const std::u8string& page, const bool wide, const float width, const float height, const float scale)
    {
        if (wide == false && page.empty())
            return make_navigation(width, height, scale, false);
        return make_page(page, width, height, scale);
    }

    std::shared_ptr<const luil::ui_frame> mobile_driver::make_frame()
    {
        // 이 frame을 지은 시각이다. 놓은 뒤 도는 시트의 지금 값과 다음 예고(`next_tick`)가 같은 시각에서 센다.
        frame_time_ = std::chrono::steady_clock::now();
        const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
        const float width { metrics_.width > 0.0f ? metrics_.width : 480.0f };
        const float height { metrics_.height > 0.0f ? metrics_.height : 800.0f };
        // 폭 등급이 compact를 넘으면(600dp부터) 목록과 페이지를 나란히 둔다. 기기 방향이 아니라 창 폭으로 고르므로
        // 휴대폰 가로·화면 분할·접는 기기·태블릿이 한 규칙이다.
        const bool wide { metrics_.width_class != luil::window_width_class::compact };
        // 좁은 화면에서 페이지를 열었으면 뒤로 갈 자리가 있다.
        const bool nested { wide == false && page_.empty() == false };
        const std::u8string shown { page_.empty() && wide ? std::u8string { page_basics } : page_ };

        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });
        // 테마 페이지가 읽은 스타일이다 (비면 내장 스타일). 앱 바와 본문을 이 스타일로 배치하고 frame에도 같은 포인터를 싣는다.
        const luil::ui_style* const style { theme_.style().get() };

        // 데스크톱 창으로 띄우면 창 caption이 맨 위에 선다. 앱 바는 그 아래에서 같은 일을 한다.
        float top { 0.0f };
        if (luil::current_ui_platform().window_caption)
        {
            luil::caption_config caption {};
            caption.title = u8"luil mobile demo";
            caption.minimize_tooltip = u8"Minimize";
            caption.maximize_tooltip = u8"Maximize or restore";
            caption.close_tooltip = u8"Close";
            const float caption_height { luil::caption_element::height_for(caption) * scale };
            auto element { std::make_unique<luil::caption_element>(std::move(caption)) };
            element->arrange({ { 0.0f, 0.0f, width, caption_height }, scale });
            root->add(std::move(element));
            top = caption_height;
        }

        luil::app_bar_config bar {};
        bar.title = nested ? page_title(page_) : std::u8string { u8"luil mobile demo" };
        if (nested)
            bar.navigation = luil::app_bar_button { .glyph = luil::codicons::icon_arrow_left, .label = u8"뒤로", .action = luil::make_message_action(back_intent {}) };
        const float bar_height { luil::app_bar_element::height_for(bar) * scale };
        auto app_bar { std::make_unique<luil::app_bar_element>(std::move(bar)) };
        app_bar->arrange({ .slot = { 0.0f, top, width, bar_height }, .scale = scale, .style = style });
        root->add(std::move(app_bar));
        top += bar_height;

        const float body_width { width / scale };
        const float body_height { (height - top) / scale };
        // 기기 페이지의 전환 label이 읽는 상태다. 도는 동안은 tree를 다시 짓지 않으므로 시작 frame의 값이 끝 tick까지 남는다.
        device_.set_page_transition_turning(page_motion_.prior != nullptr && luil::page_transition_finished(page_motion_, frame_time_) == false);
        // 바뀌는 자리는 늘 화면 전환 요소로 감싼다. 떠나는 화면이 없으면 새 화면만 그리는 껍질이다.
        const auto wrap = [this](std::unique_ptr<luil::ui_element> content) {
            return std::make_unique<luil::page_transition_element>(luil::page_transition_config { u8"page", page_motion_ }, std::move(content));
        };
        std::unique_ptr<luil::ui_element> body {};
        const luil::ui_element* moving { nullptr };
        if (wide)
        {
            luil::stack_config row_config {};
            row_config.direction = luil::stack_direction::row;
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"two-pane" }, row_config) };
            row->add(make_navigation(navigation_width, body_height, scale, true), navigation_width);
            auto pane { wrap(make_page(shown, body_width - navigation_width, body_height, scale)) };
            moving = pane.get();
            row->add_flexible(std::move(pane));
            body = std::move(row);
        }
        else
        {
            body = wrap(make_body_page(page_, false, body_width, body_height, scale));
            moving = body.get();
        }
        body->arrange({ .slot = { 0.0f, top, width, height - top }, .scale = scale, .style = style });
        // 다음 이동에서 떠나는 화면을 이 자리와 배율로 짓는다.
        motion_slot_ = moving->bounds();
        motion_scale_ = scale;
        motion_wide_ = wide;
        root->add(std::move(body));

        // 오버레이들: dialog는 화면을 덮고 토스트는 아래쪽에 쌓인다.
        //  - scrim과 시트 바탕은 안전 영역 밖(상태 표시줄·제스처 막대 자리)까지 칠한다.
        if (shown == page_basics)
            basics_.add_overlay(*root, width, height, scale, metrics_.surface);
        // 시트는 창 전체에 겹친다. 열림은 페이지의 상태라 회전·화면 분할 뒤에도 다시 선다.
        if (shown == page_sheets)
            sheets_.add_overlay(*root, width, height, scale, metrics_.surface, frame_time_);
        // 안내 풍선은 이번 frame에 배치한 root에서 대상의 보이는 자리를 찾는다.
        if (auto toasts { toasts_.make_overlay(root.get()) }; toasts != nullptr)
        {
            // 풍선은 배치 때 글을 잰다. frame에 싣는 스타일과 같은 포인터라야 잰 줄과 그린 글이 맞는다.
            toasts->arrange({ .slot = { 0.0f, 0.0f, width, height }, .scale = scale, .style = style });
            root->add(std::move(toasts));
        }

        auto frame { std::make_shared<luil::ui_frame>() };
        frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
        frame->appearance = theme_.appearance();
        // 위의 앱 바·본문 배치에 준 것과 같은 포인터다. 팔레트·그리기 치수는 이쪽에서 읽는다.
        frame->style = theme_.style();
        // 기기 요청은 앱 상태라 어느 페이지를 보든 실린다.
        frame->device = device_.request();
        // 검사기도 앱 상태 하나로 켠다. 켠 채 다른 페이지를 들여다볼 수 있다.
        frame->inspector = inspector_.config();
        // popup은 이번 frame에서 배치된 element의 자리를 쓴다. 보이는 페이지의 것만 단다.
        if (shown == page_tabs)
            frame->popups = tabs_.make_popups(scale);
        if (shown == page_popups)
            frame->popups = popups_.make_popups(scale, { 0.0f, 0.0f, width, height });
        // 날짜 칸 popup의 초점 요청이다. 이 페이지만 요청을 내므로 번호를 그대로 싣는다.
        if (shown == page_popups)
            frame->focus = popups_.make_focus_request();
        // 뒤로 가기는 열린 dialog·시트·안내 풍선을 먼저 닫고, 그다음 좁은 화면의 페이지를 닫는다. 맨 앞 화면이면 비워
        // 두어 플랫폼의 기본 동작(앱을 떠나기)을 받는다. popup은 host가 그보다 먼저 닫는다.
        if (shown == page_basics && basics_.dialog_open())
            frame->back = [action = basics_.back_action()] { return action; };
        else if (shown == page_sheets && sheets_.sheet_open())
            frame->back = [action = sheets_.back_action()] { return action; };
        else if (shown == page_toasts && toasts_.tip_open())
            frame->back = [action = toasts_.back_action()] { return action; };
        else if (nested)
            frame->back = [] { return luil::make_app_action(back_intent {}); };
        return frame;
    }

    std::unique_ptr<luil::ui_element> mobile_driver::make_navigation(const float width, const float height, const float scale, const bool highlight)
    {
        // 넓은 화면은 고른 페이지(없으면 첫 페이지)의 칸을 칠하고, 좁은 화면은 칠하지 않고 다음 장 표시를 둔다.
        std::u8string_view selected {};
        if (highlight)
            selected = page_.empty() ? page_basics : std::u8string_view { page_ };
        return wrap_scroll(u8"navigation", make_page_list(width, selected, highlight == false), height, scale);
    }

    std::unique_ptr<luil::ui_element> mobile_driver::make_page(const std::u8string& page, const float width, const float height, const float scale)
    {
        // 화면을 채우는 페이지다. 안에서 저마다 흘린다 (목록, 탭 판, 확대 보기).
        if (page == page_lists)
            return lists_.build(width, height, scale);
        if (page == page_tabs)
            return tabs_.build(width, height, scale);
        if (page == page_zoom)
            return zoom_.build(width, height, scale);
        if (page == page_ink)
            return ink_.build(width, height, scale);
        // 위에서 아래로 흘려 보는 페이지다.
        if (page == page_groups)
            return wrap_scroll(page, groups_.build(width, scale), height, scale);
        if (page == page_toasts)
            return wrap_scroll(page, toasts_.build(width, scale), height, scale);
        if (page == page_popups)
            return wrap_scroll(page, popups_.build(width, scale), height, scale);
        if (page == page_inspector)
            return wrap_scroll(page, inspector_.build(width, scale), height, scale);
        if (page == page_theme)
            return wrap_scroll(page, theme_.build(width, scale), height, scale);
        if (page == page_sheets)
            return wrap_scroll(page, sheets_.build(width, scale), height, scale);
        if (page == page_device)
            return wrap_scroll(page, device_.build(width, scale, metrics_), height, scale);
        if (page == page_smb)
            return wrap_scroll(page, smb_.build(width, scale), height, scale);
        if (page == page_files)
            return wrap_scroll(page, files_.build(width, scale), height, scale);
        return wrap_scroll(page, basics_.build(width, scale), height, scale);
    }

    std::unique_ptr<luil::ui_element> mobile_driver::wrap_scroll(std::u8string owner, page_content content, const float height, const float scale)
    {
        // 내용이 화면보다 짧으면 흘리지 않는다. 다듬은 값을 상태에 되돌려 다음 쓸기가 범위 밖에서
        // 시작하지 않게 한다.
        float& offset { scrolls_[owner] };
        offset = luil::clamp_scroll(content.height, height, offset);
        auto view { std::make_unique<luil::scroll_view_element>(luil::ui_element_id { kind_page_scroll, owner }, luil::scroll_view_config { content.height, offset }) };
        view->set_content(std::move(content.element));
        // 휠과 터치 쓸기가 같은 메시지로 온다.
        view->set_scroll_source(luil::scroll_source {
            .scroll = [owner](const float delta) { return luil::make_app_action(scroll_intent { owner, delta }); },
            .scale = scale,
        });
        return view;
    }

    luil::app_message mobile_driver::make_close_message()
    {
        return luil::app_message { close_intent {} };
    }

    bool mobile_driver::shutdown_completed() const
    {
        return closed_.load();
    }

    std::optional<std::chrono::steady_clock::time_point> mobile_driver::next_tick()
    {
        std::optional<std::chrono::steady_clock::time_point> next { toasts_.next_expiry() };
        // 놓은 뒤 도는 시트다. 마지막으로 지은 frame의 시각에서 센다. 그 frame이 도는 중이었다면 한 frame 뒤에 하나를 더 지어
        // 멈춘 모습으로 끝낸다 (지금 시각에서 세면 그 사이에 끝난 스프링이 "도는 중" 표시를 남긴 채 잠든다).
        if (const auto sheet { sheets_.next_update(frame_time_) }; sheet.has_value() && (next.has_value() == false || *sheet < *next))
            next = sheet;
        // 페이지 전환은 끝 시각 하나를 싣는다. 도는 동안의 frame은 요소의 예고가 다시 그린다.
        if (const auto end { luil::page_transition_end(page_motion_) }; end.has_value() && (next.has_value() == false || *end < *next))
            next = end;
        return next;
    }

    void mobile_driver::tick(const std::chrono::steady_clock::time_point now)
    {
        toasts_.prune();
        // 끝난 전환은 놓는다. 다음 frame은 prior 없는 tree라 덮개가 걷혀 손가락이 풀린다.
        if (page_motion_.prior != nullptr && luil::page_transition_finished(page_motion_, now))
            page_motion_ = {};
    }

    // 표는 page test가 함께 보도록 `common.cpp`에 둔다.
    std::optional<luil::text_input_target> mobile_policy::text_target_of(const luil::ui_element_kind kind) const
    {
        return text_target_of_kind(kind);
    }

    luil::input_action mobile_policy::make_text_edit_action(const luil::text_edit_request& request) const
    {
        return luil::make_app_action(edit_intent { request });
    }

    luil::input_action mobile_policy::make_text_composition_action(const luil::text_composition_event& event) const
    {
        return luil::make_app_action(composition_intent { event });
    }

    std::optional<luil::menu_kinds> mobile_policy::menu() const
    {
        return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
    }

    std::vector<luil::input_action> mobile_policy::close_menu() const
    {
        return { luil::make_app_action(popup_close_intent {}) };
    }

    std::vector<luil::input_action> mobile_policy::on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float scroll_delta)
    {
        // 표를 든 페이지가 먼저다. 표 없는 짝은 좌표를 덮는 아무 흘리는 창이나 답해, 순서를 뒤집으면
        // 표가 이름 댄 것을 감싼 페이지 창이 휠을 먼저 삼킨다.
        if (auto actions { lists_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
            return actions;
        if (auto actions { tabs_page::route_wheel(tree, event, scroll_delta) }; actions.empty() == false)
            return actions;
        return luil::route_wheel(tree, event, scroll_delta);
    }

    std::vector<luil::input_action> mobile_policy::on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        if (auto actions { lists_page::route_reveal(tree, focused) }; actions.empty() == false)
            return actions;
        if (auto actions { tabs_page::route_reveal(tree, focused) }; actions.empty() == false)
            return actions;
        return luil::route_reveal(tree, focused);
    }

    std::vector<luil::input_action> mobile_policy::on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot)
    {
        // 읽기 전용 안내 칸에 초점이 있는 채 닿은 Ctrl+Z·Ctrl+Y다. 칸이 삼키지 않고 넘긴 키를 기본 페이지가 센다.
        if (const auto undo { demo::readonly_undo_key_of(event, snapshot, kind_readonly_input) }; undo.has_value())
            return { luil::make_app_action(*undo) };
        if (event.key == luil::key_code::volume_up || event.key == luil::key_code::volume_down)
            return { luil::make_app_action(volume_key_intent { event.key == luil::key_code::volume_up, event.repeat }) };
        return {};
    }

    luil::app_message make_metrics_message(const luil::window_metrics& metrics)
    {
        return luil::app_message { window_metrics_intent { metrics.width, metrics.height, metrics.scale, metrics.width_class, luil::window_surface(metrics), metrics.reduce_motion } };
    }
}
