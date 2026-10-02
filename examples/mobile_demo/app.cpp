#include "mobile_demo/app.h"

#include "luil/generated/codicons.h"
#include "luil/ui/app_bar_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/glyph_element.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_platform.h"

#include <algorithm>
#include <array>
#include <utility>

namespace mobile_demo {
    namespace {
        // 이 폭부터 목록과 페이지를 나란히 둔다 (논리 픽셀). Material의 "펼친" 창 크기 구간이 시작하는
        // 자리다 — 휴대폰 가로와 태블릿이 여기 든다.
        constexpr float two_pane_width { 600.0f };
        // 넓은 화면에서 왼쪽 목록의 폭이다.
        constexpr float navigation_width { 260.0f };
        // 목록 한 칸의 높이다. 제목과 설명 두 줄에 손가락이 넉넉히 닿는다.
        constexpr float entry_height { 64.0f };

        struct page_entry
        {
            std::u8string_view key {};
            std::u8string_view title {};
            std::u8string_view summary {};
            char32_t icon { 0 };
        };

        constexpr std::array<page_entry, 8> page_entries {
            page_entry { page_basics, u8"기본", u8"단추, 글 칸, 그림, dialog", luil::codicons::icon_home },
            page_entry { page_lists, u8"목록", u8"순서 바꾸기, tree, 가상 목록", luil::codicons::icon_list_unordered },
            page_entry { page_tabs, u8"탭", u8"쓸어 넘기기, 넘침 메뉴", luil::codicons::icon_files },
            page_entry { page_groups, u8"그룹", u8"접는 섹션, 선택 묶음", luil::codicons::icon_group_by_ref_type },
            page_entry { page_toasts, u8"토스트", u8"알림과 실행 취소", luil::codicons::icon_bell },
            page_entry { page_popups, u8"메뉴·팝업", u8"드롭다운, 길게 눌러 메뉴", luil::codicons::icon_menu },
            page_entry { page_zoom, u8"확대 보기", u8"두 손가락 확대와 이동", luil::codicons::icon_zoom_in },
            page_entry { page_theme, u8"테마", u8"밝기, 키 컬러, 시스템 색", luil::codicons::icon_symbol_color },
        };

        [[nodiscard]] std::u8string page_title(const std::u8string_view page)
        {
            for (const page_entry& entry : page_entries)
                if (entry.key == page)
                    return std::u8string { entry.title };
            return std::u8string { u8"luil mobile demo" };
        }

        // 목록 한 칸이다: 아이콘, 제목, 한 줄 설명. 넓은 화면에서 고른 칸은 목록의 고른 행과 같은 바탕이다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_entry(const page_entry& entry, const bool selected, const bool chevron)
        {
            luil::panel_config panel_config {};
            if (selected)
                panel_config.background = [](const luil::ui_color_palette& palette) { return palette.row_selection_background; };
            else
                panel_config.background = [](const luil::ui_color_palette& palette) { return palette.window_background; };
            panel_config.corner_radius = selected ? 8.0f : 0.0f;
            const std::u8string key { entry.key };
            auto panel { std::make_unique<luil::panel_element>(luil::ui_element_id { kind_page_entry, key }, std::move(panel_config)) };
            panel->set_cursor(luil::ui_cursor::hand);
            panel->set_action(luil::ui_trigger::left_click, luil::make_message_action(navigate_intent { key }));

            luil::stack_config row_config {};
            row_config.direction = luil::stack_direction::row;
            row_config.spacing = 16.0f;
            row_config.padding = luil::edge_insets::symmetric(16.0f, 10.0f);
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"entry-" + key }, row_config) };
            row->add(std::make_unique<luil::glyph_element>(luil::ui_element_id { kind_text, u8"entry-icon-" + key }, luil::glyph_config { .glyph = entry.icon, .font_size = 22.0f }), 24.0f);

            luil::stack_config text_config {};
            auto text { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"entry-text-" + key }, text_config) };
            text->add(make_label(luil::ui_element_id { kind_text, u8"entry-title-" + key }, std::u8string { entry.title }, body_text_size, luil::label_color_role::primary), body_line_height);
            text->add(make_label(luil::ui_element_id { kind_text, u8"entry-summary-" + key }, std::u8string { entry.summary }, note_text_size, luil::label_color_role::dim), note_line_height);
            row->add_flexible(std::move(text));
            // 좁은 화면에서는 누르면 다음 장으로 넘어간다는 표시를 둔다.
            if (chevron)
                row->add(std::make_unique<luil::glyph_element>(
                             luil::ui_element_id { kind_text, u8"entry-chevron-" + key }, luil::glyph_config { .glyph = luil::codicons::icon_chevron_right, .font_size = 18.0f }),
                    20.0f);
            panel->set_content(std::move(row));
            return panel;
        }
    } // namespace

    void mobile_driver::handle(luil::app_message message)
    {
        if (message.get<close_intent>() != nullptr)
        {
            closed_.store(true);
            return;
        }
        if (const auto* const metrics { message.get<window_metrics_intent>() }; metrics != nullptr)
        {
            metrics_ = *metrics;
            return;
        }
        if (const auto* const navigate { message.get<navigate_intent>() }; navigate != nullptr)
        {
            page_ = navigate->page;
            // 페이지를 떠나면 그 페이지의 popup도 닫는다.
            close_popups();
            return;
        }
        if (message.get<back_intent>() != nullptr)
        {
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
        if (basics_.handle(message) || lists_.handle(message) || tabs_.handle(message) || groups_.handle(message) || toasts_.handle(message) || popups_.handle(message) || zoom_.handle(message)
            || theme_.handle(message))
            return;
    }

    void mobile_driver::close_popups()
    {
        tabs_.close_popups();
        popups_.close_popups();
    }

    std::shared_ptr<const luil::ui_frame> mobile_driver::make_frame()
    {
        const float scale { metrics_.scale > 0.0f ? metrics_.scale : 1.0f };
        const float width { metrics_.width > 0.0f ? metrics_.width : 480.0f };
        const float height { metrics_.height > 0.0f ? metrics_.height : 800.0f };
        const bool wide { width / scale >= two_pane_width };
        // 좁은 화면에서 페이지를 열었으면 뒤로 갈 자리가 있다.
        const bool nested { wide == false && page_.empty() == false };
        const std::u8string shown { page_.empty() && wide ? std::u8string { page_basics } : page_ };

        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width, height }, scale });

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
        app_bar->arrange({ { 0.0f, top, width, bar_height }, scale });
        root->add(std::move(app_bar));
        top += bar_height;

        const float body_width { width / scale };
        const float body_height { (height - top) / scale };
        std::unique_ptr<luil::ui_element> body {};
        if (wide)
        {
            luil::stack_config row_config {};
            row_config.direction = luil::stack_direction::row;
            auto row { std::make_unique<luil::stack_element>(luil::ui_element_id { kind_layout, u8"two-pane" }, row_config) };
            row->add(make_navigation(navigation_width, body_height, scale, true), navigation_width);
            row->add_flexible(make_page(shown, body_width - navigation_width, body_height, scale));
            body = std::move(row);
        }
        else if (page_.empty())
        {
            body = make_navigation(body_width, body_height, scale, false);
        }
        else
        {
            body = make_page(page_, body_width, body_height, scale);
        }
        body->arrange({ { 0.0f, top, width, height - top }, scale });
        root->add(std::move(body));

        // 오버레이들: dialog는 화면을 덮고 토스트는 아래쪽에 쌓인다.
        if (shown == page_basics)
            basics_.add_overlay(*root, width, height, scale);
        if (auto toasts { toasts_.make_overlay() }; toasts != nullptr)
        {
            toasts->arrange({ { 0.0f, 0.0f, width, height }, scale });
            root->add(std::move(toasts));
        }

        auto frame { std::make_shared<luil::ui_frame>() };
        frame->tree = std::make_shared<const luil::ui_tree>(std::move(root));
        frame->appearance = theme_.appearance();
        // popup은 이번 frame에서 배치된 element의 자리를 쓴다. 보이는 페이지의 것만 단다.
        if (shown == page_tabs)
            frame->popups = tabs_.make_popups(scale);
        if (shown == page_popups)
            frame->popups = popups_.make_popups(scale);
        // 뒤로 가기는 열린 dialog를 먼저 닫고, 그다음 좁은 화면의 페이지를 닫는다. 맨 앞 화면이면 비워
        // 두어 플랫폼의 기본 동작(앱을 떠나기)을 받는다. popup은 host가 그보다 먼저 닫는다.
        if (shown == page_basics && basics_.dialog_open())
            frame->back = [] { return luil::make_app_action(dialog_intent { false }); };
        else if (nested)
            frame->back = [] { return luil::make_app_action(back_intent {}); };
        return frame;
    }

    std::unique_ptr<luil::ui_element> mobile_driver::make_navigation(const float width, const float height, const float scale, const bool highlight)
    {
        page_column column { u8"navigation", 0.0f };
        column.gap(8.0f);
        for (const page_entry& entry : page_entries)
        {
            const bool selected { highlight && (page_.empty() ? entry.key == page_basics : entry.key == page_) };
            column.add(make_entry(entry, selected, highlight == false), entry_height, width);
        }
        column.gap(8.0f);
        return wrap_scroll(u8"navigation", column.finish(), height, scale);
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
        // 위에서 아래로 흘려 보는 페이지다.
        if (page == page_groups)
            return wrap_scroll(page, groups_.build(width, scale), height, scale);
        if (page == page_toasts)
            return wrap_scroll(page, toasts_.build(width, scale), height, scale);
        if (page == page_popups)
            return wrap_scroll(page, popups_.build(width, scale), height, scale);
        if (page == page_theme)
            return wrap_scroll(page, theme_.build(width, scale), height, scale);
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
        return toasts_.next_expiry();
    }

    void mobile_driver::tick(std::chrono::steady_clock::time_point)
    {
        toasts_.prune();
    }

    std::optional<luil::text_input_target> mobile_policy::text_target_of(const luil::ui_element_kind kind) const
    {
        if (kind == kind_number_input)
            return target_number;
        if (kind == kind_note_input)
            return target_note;
        // popup 안의 검색 칸도 여느 텍스트 칸과 같이 등록한다 (popup-ime-design.md).
        if (kind == kind_dropdown_search_input)
            return target_dropdown_search;
        return std::nullopt;
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
        return luil::route_wheel(tree, event.x, event.y, scroll_delta);
    }

    std::vector<luil::input_action> mobile_policy::on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        if (auto actions { lists_page::route_reveal(tree, focused) }; actions.empty() == false)
            return actions;
        if (auto actions { tabs_page::route_reveal(tree, focused) }; actions.empty() == false)
            return actions;
        return luil::route_reveal(tree, focused);
    }

    luil::app_message make_metrics_message(const float width, const float height, const float scale)
    {
        return luil::app_message { window_metrics_intent { width, height, scale } };
    }
} // namespace mobile_demo
