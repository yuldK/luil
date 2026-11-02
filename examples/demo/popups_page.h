#pragma once

// 메뉴·팝업 페이지다: popup 창을 실제로 쓰는 세 가지 예다.
//  - 드롭다운(select): 닫힌 칸을 누르면 아래에 popup 목록이 뜬다.
//    목록 위의 검색 칸은 **popup 안의 텍스트 입력**이다 — popup은 keyboard
//    focus를 받지 못하지만 앵커(여기서는 주 창)의 IME session이 대신 봐서
//    조합까지 된다 (popup-ime-design.md).
//  - 컨텍스트 메뉴: 카드를 오른쪽 클릭하면 그 자리에 popup 메뉴가 뜬다.
//  - 분할 버튼("저장")과 드롭다운 단추("보내기"): 본체를 누르면 동작이 나가고 화살표를 누르면 필드 아래에
//    메뉴가 뜬다. 자리는 `menu_popup_config::below`가 정하고 아래가 모자라면 위로 뒤집힌다
//    (split-button-design.md).
// 떠 있는 popup은 한 번에 하나다. 어느 필드의 것인지를 `open_popup_` 하나가 들고 있어 둘이 함께 열릴 수 없다.
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
    {
    };

    // 컨텍스트 메뉴에서 항목을 골랐다.
    // 페이지는 메뉴를 닫고, 셸이 토스트 알림으로 잇는다.
    struct card_menu_select_intent
    {
        std::u8string key {};
    };

    struct popups_scroll_intent
    {
        float delta { 0.0f };
    };

    // 분할 버튼("저장")이나 드롭다운 단추("보내기")의 메뉴를 이 상태로 하라는 절대 메시지다. 누름과 보조 기술의
    // 펼치기·접기, 키(Alt+↓·F4), 바깥 누름 닫기가 모두 이 하나로 간다.
    struct field_menu_intent
    {
        std::u8string owner {};
        bool open { false };
    };

    // 필드 메뉴에서 항목을 골랐다. 메뉴를 닫는 것은 `menu_config::after_select`가 따로 보낸다.
    struct field_menu_select_intent
    {
        std::u8string owner {};
        std::u8string key {};
    };

    // 분할 버튼의 본체("저장")를 눌렀다.
    struct split_invoke_intent
    {
    };

    // popup 모양을 골랐다 (`setting`은 "corner"·"border", `value`는 고른 값).
    struct popup_shape_intent
    {
        std::u8string setting {};
        std::u8string value {};
    };

    class popups_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 떠 있는 드롭다운·컨텍스트 메뉴·필드 메뉴를 popup으로 만든다.
        // 같은 frame에서 build가 만든 element의 자리(bounds)를 쓰므로 build 뒤에 불러야 한다.
        // `surface`는 앵커 창의 client 사각형이다 (물리 픽셀). 필드 메뉴가 이 안에서 위아래를 고른다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale, const luil::rect_f& surface) const;

        // 떠 있는 popup을 닫는다 (페이지 이동·Esc·바깥 클릭).
        void close_popups() noexcept
        {
            open_popup_.clear();
            search_ = {};
            composition_.reset();
        }

    private:
        // 떠 있는 popup의 임자를 바꾼다. 빈 값이면 모두 닫는다.
        // 한 번에 하나만 띄운다. 임자가 바뀌면 검색 상태도 함께 비워, 다음에 드롭다운을 열 때 목록이 온전히 보이게 한다.
        void show_popup(std::u8string owner);
        // 그 임자의 popup이 떠 있을 때만 닫는다. 늦게 온 닫기 메시지가 다른 필드의 popup을 닫지 않는다.
        void close_popup(std::u8string_view owner);

        // 이 페이지의 popup에 거는 모서리와 경계다. 기본은 OS 기본(Windows 11은 작게 둥글다)과 경계 있음이다.
        luil::popup_corner corner_ { luil::popup_corner::system };
        bool popup_border_ { true };
        std::u8string presentation_ { u8"자세히" };
        bool pinned_ { false };
        bool locked_ { true };
        bool deleted_ { false };
        bool preview_ { false };
        float scroll_ { 0.0f };
        // 드롭다운 popup 안 검색 칸의 상태다.
        // 여느 텍스트 칸과 똑같이 앱이 들고 있다 — popup이라고 다르지 않다.
        luil::text::text_edit_state search_ {};
        std::optional<luil::text_composition_event> composition_ {};
        // 떠 있는 popup을 연 필드의 owner다 ("presentation"·"card"·"save"·"send"). 비어 있으면 없다.
        std::u8string open_popup_ {};
        // 분할 버튼 페이지가 마지막으로 낸 동작이다. 화면의 label이 보이고 UIA가 되읽는다.
        std::u8string last_action_ {};
        // 컨텍스트 메뉴를 연 자리다 (물리 픽셀; popup을 만들 때 논리로 되돌린다).
        float menu_x_ { 0.0f };
        float menu_y_ { 0.0f };
        // 이번 build가 만든 드롭다운 칸이다.
        // popup의 자리 계산에만 쓰고 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* dropdown_ { nullptr };
        // 이번 build가 만든 분할 버튼과 드롭다운 단추다. 같은 규칙으로 frame이 게시되기 전까지만 유효하다.
        const luil::ui_element* split_ { nullptr };
        const luil::ui_element* send_ { nullptr };
    };
}
