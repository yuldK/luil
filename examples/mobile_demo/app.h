#pragma once

// mobile demo의 셸이다: 페이지 목록, 앱 바, 좁은 화면의 한 장씩 보기와 넓은 화면의 두 판.
//
// 데스크톱 demo가 사이드바와 캡션을 두는 자리에 휴대폰의 꼴을 둔다.
//  - 좁은 화면(휴대폰 세로)에서는 목록을 누르면 페이지가 들어서고, 앱 바의 뒤로 단추와 Android의
//    뒤로 가기가 목록으로 돌아간다 (`ui_frame::back`).
//  - 넓은 화면(태블릿, 휴대폰 가로)에서는 왼쪽에 목록, 오른쪽에 고른 페이지가 선다.
//  - 페이지의 상태는 셸이 아니라 페이지가 든다. 화면을 돌려 꼴이 바뀌어도 그대로다.

#include "mobile_demo/basics_page.h"
#include "mobile_demo/common.h"
#include "mobile_demo/groups_page.h"
#include "mobile_demo/lists_page.h"
#include "mobile_demo/popups_page.h"
#include "mobile_demo/tabs_page.h"
#include "mobile_demo/theme_page.h"
#include "mobile_demo/toasts_page.h"
#include "mobile_demo/zoom_page.h"

#include "luil/app/app_delegate.h"
#include "luil/app/app_host.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace mobile_demo {
    // 흘리는 창 하나를 끈 만큼 옮긴다 (논리 픽셀, +가 아래로). 어느 창인지는 owner가 말한다
    // (페이지 키, 또는 목록의 `navigation`).
    struct scroll_intent
    {
        std::u8string owner {};
        float delta { 0.0f };
    };

    // --- logic thread ---
    class mobile_driver final : public luil::logic_driver
    {
    public:
        void handle(luil::app_message message) override;
        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override;
        [[nodiscard]] luil::app_message make_close_message() override;
        [[nodiscard]] bool shutdown_completed() const override;
        // 토스트 만료다. 다음 만료 시각을 예고하면 메시지가 없어도 그때 tick이 온다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_tick() override;
        void tick(std::chrono::steady_clock::time_point now) override;

    private:
        void close_popups();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_navigation(float width, float height, float scale, bool highlight);
        // 페이지 하나를 그 자리에 맞춰 짓는다. 흘려 보는 페이지는 scroll view에 담는다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_page(const std::u8string& page, float width, float height, float scale);
        [[nodiscard]] std::unique_ptr<luil::ui_element> wrap_scroll(std::u8string owner, page_content content, float height, float scale);

        basics_page basics_ {};
        lists_page lists_ {};
        tabs_page tabs_ {};
        groups_page groups_ {};
        toasts_page toasts_ {};
        popups_page popups_ {};
        zoom_page zoom_ {};
        theme_page theme_ {};

        // 좁은 화면에서 열어 둔 페이지다. 비어 있으면 목록이다. 넓은 화면은 비어 있으면 첫 페이지를 보인다.
        std::u8string page_ {};
        // 흘리는 창마다의 위치다 (논리 픽셀). 범위는 frame을 지을 때 다듬는다.
        std::map<std::u8string, float> scrolls_ {};
        window_metrics_intent metrics_ {};
        std::atomic<bool> closed_ { false };
    };

    // --- input thread ---
    class mobile_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(luil::ui_element_kind kind) const override;
        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override;
        [[nodiscard]] luil::input_action make_text_composition_action(const luil::text_composition_event& event) const override;
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override;
        [[nodiscard]] std::vector<luil::input_action> close_menu() const override;
        [[nodiscard]] std::vector<luil::input_action> on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, float scroll_delta) override;
        [[nodiscard]] std::vector<luil::input_action> on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused) override;
    };

    // --- UI thread ---
    // 화면 크기와 배율을 앱 메시지로 옮긴다. 플랫폼마다 넓힌 delegate가 이것을 부른다.
    [[nodiscard]] luil::app_message make_metrics_message(float width, float height, float scale);
} // namespace mobile_demo
