#pragma once

// 탭 페이지다: 선택·닫기·끌어서 순서 바꾸기·가로 스크롤과,
// 넘칠 때 나타나는 넘침 메뉴 버튼(popup으로 탭 목록을 띄운다).

#include "luil/app/app_host.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"
#include "mobile_demo/common.h"

#include <vector>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    struct tab_select_intent
    {
        std::u8string key {};
    };

    struct tab_close_intent
    {
        std::u8string key {};
    };

    struct tab_reorder_intent
    {
        std::u8string moved {};
        std::u8string target {};
    };

    struct tab_scroll_intent
    {
        float delta { 0.0f };
    };

    struct tab_overflow_intent
    {
        bool open { false };
    };

    class tabs_page
    {
    public:
        tabs_page();

        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 넘침 메뉴가 열려 있으면 탭 막대 오른쪽 아래에 popup을 단다.
        // 같은 frame에서 build가 만든 element의 자리(bounds)를 쓰므로 build 뒤에 불러야 한다.
        [[nodiscard]] std::vector<luil::ui_popup> make_popups(float scale) const;

        // 떠 있는 popup을 닫는다 (페이지 이동·Esc·바깥 클릭).
        void close_popups() noexcept
        {
            overflow_open_ = false;
        }

        [[nodiscard]] static std::vector<luil::input_action> route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, float delta);

        // Tab이 옮긴 초점을 탭 띠 안으로 들인다.
        [[nodiscard]] static std::vector<luil::input_action> route_reveal(const luil::ui_tree& tree, const luil::ui_element_id& focused);

    private:
        struct tab_entry
        {
            std::u8string key {};
            std::u8string label {};
        };

        std::vector<tab_entry> tabs_ {};
        std::u8string selected_ {};
        float scroll_ { 0.0f };
        bool overflow_open_ { false };
        int next_tab_ { 0 };
        // 이번 build가 만든 탭 막대다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* bar_ { nullptr };
    };
} // namespace mobile_demo
