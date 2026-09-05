#pragma once

// 메뉴·팝업 페이지다: popup 창을 실제로 쓰는 두 가지 예다.
//  - 드롭다운(select): 닫힌 칸을 누르면 아래에 popup 목록이 뜬다.
//    목록 위의 검색 칸은 **popup 안의 텍스트 입력**이다 — popup은 keyboard
//    focus를 받지 못하지만 앵커(여기서는 주 창)의 IME session이 대신 봐서
//    조합까지 된다 (popup-ime-design.md).
//  - 컨텍스트 메뉴: 카드를 오른쪽 클릭하면 그 자리에 popup 메뉴가 뜬다.
// popup은 창 경계를 넘을 수 있고, 닫힘 계기는 **이유와 함께** 온다: 컨텍스트
// 메뉴는 어느 계기에도 닫히지만, 검색 중인 드롭다운은 창 이동·활성 전환에는
// 남는다 — 잠깐 다른 창을 봤다고 치던 글이 사라지면 안 되기 때문이다.

#include "demo/common.h"
#include "luil/win32/app_host.h"

#include <vector>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct dropdown_toggle_intent
    {
        bool open { false };
    };

    struct dropdown_select_intent
    {
        std::u8string value {};
    };

    // 카드의 오른쪽 클릭 자리다 (주 창 client 물리 픽셀).
    struct card_menu_intent
    {
        float x { 0.0f };
        float y { 0.0f };
    };

    struct card_menu_close_intent
    {};

    // 컨텍스트 메뉴에서 항목을 골랐다.
    // 페이지는 메뉴를 닫고, 셸이 토스트 알림으로 잇는다.
    struct card_menu_select_intent
    {
        std::u8string key {};
    };

    class popups_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 떠 있는 드롭다운·컨텍스트 메뉴를 popup으로 만든다.
        // 같은 frame에서 build가 만든 element의 자리(bounds)를 쓰므로 build 뒤에 불러야 한다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale) const;

        // 떠 있는 popup을 닫는다 (페이지 이동·Esc·바깥 클릭).
        void close_popups() noexcept
        {
            close_dropdown();
            menu_open_ = false;
        }

    private:
        // 드롭다운을 닫고 검색 상태도 함께 비운다.
        // 다음에 열 때 목록이 온전히 보여야 한다.
        void close_dropdown() noexcept;

        std::u8string renderer_ { u8"자동" };
        // 드롭다운 popup 안 검색 칸의 상태다.
        // 여느 텍스트 칸과 똑같이 앱이 들고 있다 — popup이라고 다르지 않다.
        luil::text::text_edit_state search_ {};
        std::optional<luil::text_composition_event> composition_ {};
        bool dropdown_open_ { false };
        bool menu_open_ { false };
        // 컨텍스트 메뉴를 연 자리다 (물리 픽셀; popup을 만들 때 논리로 되돌린다).
        float menu_x_ { 0.0f };
        float menu_y_ { 0.0f };
        // 이번 build가 만든 드롭다운 칸이다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* dropdown_ { nullptr };
    };
} // namespace demo
