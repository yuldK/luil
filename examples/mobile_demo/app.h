#pragma once

// mobile demo의 셸이다: 페이지 목록, 앱 바, 좁은 화면의 한 장씩 보기와 넓은 화면의 두 판.
//
// 데스크톱 demo가 사이드바와 캡션을 두는 자리에 휴대폰의 꼴을 둔다.
//  - 좁은 화면(휴대폰 세로)에서는 목록을 누르면 페이지가 들어서고, 앱 바의 뒤로 단추와 Android의
//    뒤로 가기가 목록으로 돌아간다 (`ui_frame::back`).
//  - 넓은 화면(태블릿, 휴대폰 가로)에서는 왼쪽에 목록, 오른쪽에 고른 페이지가 선다.
//  - 페이지의 상태는 셸이 아니라 페이지가 든다. 화면을 돌려 꼴이 바뀌어도 그대로다.

#include "mobile_demo/assets.h"
#include "mobile_demo/basics_page.h"
#include "mobile_demo/chart_page.h"
#include "mobile_demo/common.h"
#include "mobile_demo/device_page.h"
#include "mobile_demo/files_page.h"
#include "mobile_demo/groups_page.h"
#include "mobile_demo/ink_page.h"
#include "mobile_demo/inspector_page.h"
#include "mobile_demo/lists_page.h"
#include "mobile_demo/popups_page.h"
#include "mobile_demo/sheets_page.h"
#include "mobile_demo/smb_page.h"
#include "mobile_demo/tabs_page.h"
#include "mobile_demo/theme_page.h"
#include "mobile_demo/toasts_page.h"
#include "mobile_demo/zoom_page.h"

#include "luil/app/app_delegate.h"
#include "luil/app/app_host.h"
#include "luil/ui/page_transition.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mobile_demo {
    // 흘리는 창 하나를 끈 만큼 옮긴다 (논리 픽셀, +가 아래로). 어느 창인지는 owner가 말한다
    // (페이지 키, 또는 목록의 `navigation`).
    struct scroll_intent
    {
        std::u8string owner {};
        float delta { 0.0f };
    };

    // logic thread가 클립보드 요청을 넣을 host다. delegate가 시작할 때 한 번 보낸다. host는 logic thread보다 오래 산다.
    //  - 서식 칸의 복사는 글과 HTML을 logic thread에서 짓는다 (rich-edit-exchange-design.md). 요청은 host만 넣는다.
    struct clipboard_host_intent
    {
        luil::app_host* host { nullptr };
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
        // SMB client와 파일 감시자, 핫 리로드 도우미를 멈춘다. 답이 앱 host를 거쳐 오므로 host보다 먼저 멈춘다.
        void stop_workers() override;

    private:
        void close_popups();
        // 지금 본문에서 `next` 페이지(빈 글은 목록)로 가는 전환이다. 떠나는 본문을 지난 frame의 자리와 배율로 따로 짓는다.
        [[nodiscard]] luil::page_transition make_page_motion(const std::u8string& next, luil::page_transition_direction direction);
        // 좁은 화면은 목록 또는 페이지 하나, 넓은 화면은 오른쪽 판의 페이지다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_body_page(const std::u8string& page, bool wide, float width, float height, float scale);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_navigation(float width, float height, float scale, bool highlight);
        // 고른 언어 칸을 라이브러리 언어로 잇는다. "system"이면 OS가 준 목록이다.
        void apply_text_language(const std::u8string& choice);
        // 페이지 하나를 그 자리에 맞춰 짓는다. 흘려 보는 페이지는 scroll view에 담는다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_page(const std::u8string& page, float width, float height, float scale);
        [[nodiscard]] std::unique_ptr<luil::ui_element> wrap_scroll(std::u8string owner, page_content content, float height, float scale);

        basics_page basics_ {};
        lists_page lists_ {};
        tabs_page tabs_ {};
        groups_page groups_ {};
        toasts_page toasts_ {};
        // 오늘은 시작할 때 한 번 읽어 날짜 칸에 준다 (라이브러리는 시계를 읽지 않는다).
        popups_page popups_ { popups_page::local_today() };
        zoom_page zoom_ {};
        ink_page ink_ {};
        inspector_page inspector_ {};
        theme_page theme_ { std::u8string { style_json() }, std::u8string { markup_xml() } };
        device_page device_ {};
        sheets_page sheets_ {};
        smb_page smb_ {};
        files_page files_ {};
        chart_page chart_ {};

        // 기기 페이지에서 고른 언어 칸과 OS 표시 언어다. 고른 적이 없으면 비었고 라이브러리 언어를 정하지 않는다.
        std::u8string text_language_ {};
        std::vector<std::u8string> system_languages_ {};

        // 좁은 화면에서 열어 둔 페이지다. 비어 있으면 목록이다. 넓은 화면은 비어 있으면 첫 페이지를 보인다.
        std::u8string page_ {};
        // 흘리는 창마다의 위치다 (논리 픽셀). 범위는 frame을 지을 때 다듬는다.
        std::map<std::u8string, float> scrolls_ {};
        window_metrics_intent metrics_ {};
        // 마지막으로 지은 frame의 시각이다. 시간이 흘러야 바뀌는 상태의 다음 예고를 이 시각에서 센다.
        std::chrono::steady_clock::time_point frame_time_ {};
        // 페이지 전환이다 (앱 상태). 감싼 자리(좁은 화면은 본문 전체, 넓은 화면은 오른쪽 판)와 배율, 그때의 꼴을 지난 frame에서 적어 둔다.
        luil::page_transition page_motion_ {};
        // 지은 전환의 시작 시각을 아직 정하지 않았는가다. 들어오는 본문을 지은 뒤 그 frame에서 시작 시각을 적는다.
        //  - 처음 가는 무거운 페이지는 짓는 데 전환 길이보다 오래 걸려, 메시지 시각에서 세면 첫 그림에 이미 끝나 있다.
        bool page_motion_pending_ { false };
        luil::rect_f motion_slot_ {};
        float motion_scale_ { 1.0f };
        bool motion_wide_ { false };
        std::atomic<bool> closed_ { false };
        // 클립보드 요청을 넣을 host다 (`clipboard_host_intent`). 받기 전에는 요청을 버린다.
        luil::app_host* host_ { nullptr };
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
        // 앱이 받겠다고 한 볼륨 키를 기기 페이지에 알린다.
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree* tree, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot) override;
    };

    // --- UI thread ---
    // 화면 크기·배율·폭 등급을 앱 메시지로 옮긴다. 플랫폼마다 넓힌 delegate가 이것을 부른다.
    [[nodiscard]] luil::app_message make_metrics_message(const luil::window_metrics& metrics);
}
