#pragma once

// 목록 페이지다: 끌어서 순서를 바꾸는 스크롤 목록(막대 포함)과,
// 고르고 접는 tree, 만 줄이 넘는 모델을 창에 걸치는 만큼만 세우는 가상 목록,
// 그리고 그룹 머리행이 스크롤 중 위에 고정되는 목록.

#include "luil/ui/stack_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"
#include "mobile_demo/common.h"

#include <vector>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    // 목록을 논리 픽셀만큼 흘린다. 휠과 스크롤 막대가 같은 메시지를 만든다.
    struct list_scroll_intent
    {
        float delta { 0.0f };
    };

    // 목록을 이 자리로 흘린다 (보조 기술의 SetValue — 절대 offset).
    struct list_scroll_to_intent
    {
        float offset { 0.0f };
    };

    struct grouped_scroll_intent
    {
        float delta { 0.0f };
    };

    // 끌어 놓은 항목을 대상 항목 자리로 옮긴다.
    struct reorder_intent
    {
        std::u8string moved {};
        std::u8string target {};
    };

    // tree view의 세 가지 앱 상태 변화다.
    // 무엇을 고르고 무엇을 펼칠지가 앱의 것이라 element는 키만 실어 보낸다.
    struct tree_select_intent
    {
        std::u8string key {};
    };

    // 그 가지의 목표 펼침 상태다. 절대 상태를 실으면 factory 하나로 삼각형
    // 클릭(지금 상태의 반대)과 보조 기술의 Expand·Collapse가 함께 선다.
    struct tree_expand_intent
    {
        std::u8string key {};
        bool expanded { false };
    };

    struct tree_scroll_intent
    {
        float delta { 0.0f };
    };

    struct tree_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // 가상 목록의 앱 상태 변화 넷이다.
    //
    // **고름과 커서를 갈라 싣는다.** 어느 쪽을 언제 옮길지가 앱마다 다르므로
    // element는 키만 실어 보내고, 훑는 것(커서)과 고르는 것을 어떻게 이을지는
    // 이 페이지의 `handle`이 정한다 (virtual-list-design.md).
    struct log_select_intent
    {
        std::u8string key {};
    };

    struct log_cursor_intent
    {
        std::u8string key {};
    };

    struct log_scroll_intent
    {
        float delta { 0.0f };
    };

    struct log_scroll_to_intent
    {
        float offset { 0.0f };
    };

    // 두 목록을 나눈 자리를 논리 픽셀만큼 옮긴다.
    // +가 아래 판이 넓어지는 쪽이다 (부호는 손잡이가 맞춘다).
    // 좁은 화면에서 위 판에 보일 목록을 고른다 (순서 목록·tree·가상 목록).
    struct list_view_intent
    {
        std::u8string value {};
    };

    struct split_intent
    {
        float delta { 0.0f };
    };

    class lists_page
    {
    public:
        lists_page();

        bool handle(const luil::app_message& message);
        // width·height는 내용 영역의 논리 픽셀 크기다.
        // 배율은 스크롤 막대처럼 물리 값을 미리 받는 element에만 쓴다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 휠 라우팅이다: 목록 위에서 굴린 휠만 그 목록을 흘린다.
        // 셸의 정책이 페이지마다 차례로 물어본다.
        [[nodiscard]] static std::vector<luil::input_action> route_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, float delta);

        // 키보드가 옮긴 초점을 목록 창 안으로 들인다.
        // 휠과 같은 분업이다 — 표만 여기 적고 얼마나 흘릴지는 창이 답한다.
        [[nodiscard]] static std::vector<luil::input_action> route_reveal(const luil::ui_tree& tree, const luil::ui_element_id& focused);

    private:
        [[nodiscard]] static std::unique_ptr<luil::ui_element> make_split_handle();
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_top_row(float width, float viewport_height);
        // 좁은 화면에서 위 판의 목록을 고르는 토글 묶음이다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_view_choice() const;
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_list_panel(float viewport_height);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_tree_panel(float viewport_height);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_log_panel(float viewport_height);
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_grouped_section(float viewport_height);
        // 그 키가 펼쳐진 가지인가.
        [[nodiscard]] bool is_expanded(const std::u8string& key) const;

        std::vector<std::u8string> items_ {};
        float scroll_ { 0.0f };
        // tree view의 앱 상태다: 고른 줄, 펼친 가지들, 흘러간 양.
        // 라이브러리는 이 셋을 받아 그리고 바꾸자는 메시지만 낸다.
        std::u8string tree_selected_ {};
        std::vector<std::u8string> tree_expanded_ {};
        float tree_scroll_ { 0.0f };
        // 가상 목록의 앱 상태다: 모델 전체·고른 항목·커서·흘러간 양.
        //  - 모델은 **값**이라 만 줄이 넘어도 tree에 서는 것은 창에 걸치는 몇 줄뿐이다.
        //    이 벡터를 element로 들고 있으면 그 순간 element가 만 개다.
        std::vector<luil::virtual_list_item> log_items_ {};
        std::u8string log_selected_ {};
        std::u8string log_cursor_ {};
        float log_scroll_ { 0.0f };
        float grouped_scroll_ { 0.0f };
        // 아래 판(그룹 목록)의 높이다 (논리 픽셀). 나눈 자리가 이 값이고, 위 판은
        // 나머지를 갖는다 — 창이 커지면 늘어난 만큼은 위 판이 먹는다.
        //  - 한계가 담긴 자리에 달려 있어 `handle`이 아니라 `build`가 다듬는다.
        float grouped_height_ { 170.0f };
        // 좁은 화면에서 위 판에 보이는 목록이다. 넓은 화면은 셋을 나란히 둔다.
        std::u8string view_ { u8"list" };
    };
} // namespace mobile_demo
