#pragma once

#include "luil/ui/scroll_view_element.h"
#include "luil/ui/scrollbar_element.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <string>
#include <vector>

namespace luil {
    // 줄이 가지인지 잎인지다 (tree).
    enum class list_expansion
    {
        // 삼각형이 없다 — 잎이거나, 접힘을 쓰지 않는 평평한 목록이다.
        none,
        collapsed,
        expanded,
    };

    // 목록의 줄 하나다.
    struct list_item
    {
        // 항목을 가리키는 앱 정의 키다 (ui_element_id::owner 규약).
        std::u8string key {};
        std::u8string label {};
        // 0이면 앞 글리프가 없다.
        char32_t icon { 0 };
        // tree의 깊이다. 0이면 뿌리 줄이다.
        // **앱이 평탄화해서 준다** — 접힌 가지의 자식은 `items`에 담지 않는다.
        // 그래서 라이브러리는 tree 모델을 갖지 않고, 지금 보이는 줄들이 곧 묶음의
        // 항목이 되어 화살표가 화면 순서 그대로 돈다 (list-view-design.md).
        int depth { 0 };
        list_expansion expansion { list_expansion::none };
        // 거짓이면 흐리게 그리고 고를 수 없다 (Tab도 서지 않는다).
        bool enabled { true };
    };

    // 항목 키 하나를 담은 메시지다 (선택·펼침).
    using list_message_factory = std::function<input_action(const std::u8string& key)>;
    // 끌어 놓은 항목을 대상 항목 자리로 옮기자는 메시지다.
    // `tab_reorder_message_factory`와 같은 모양이라 새 개념이 아니다.
    using list_reorder_message_factory = std::function<input_action(const std::u8string& moved, const std::u8string& target)>;

    struct list_config
    {
        // 같은 화면에 목록이 여럿일 때 구분하는 키다.
        std::u8string owner {};
        std::vector<list_item> items {};
        // 고른 항목의 키다 (앱 상태).
        // element는 이 값을 받아 그리고 고르자는 메시지만 낸다.
        // 묶음에 Tab으로 들어오면 이 행에 선다 (`focus_entry`).
        std::u8string selected {};
        // 줄 하나의 높이다 (논리 픽셀).
        float row_height { 24.0f };
        // 지금 흘러간 양이다 (논리 픽셀, 앱 상태).
        // 범위 밖 값은 `arrange`가 다듬고 `scroll_offset()`으로 되돌려 준다.
        float scroll_offset { 0.0f };
        // 없으면 행이 눌리지 않는다.
        //  - 그러면 행이 **Tab 자리가 아니게 되어** 목록에 키보드가 서지 않는다.
        //    `tab_stop`의 기본값("누를 수 있으면 자리")이 하는 일이라, 파생마다
        //    키보드를 켜고 끄는 규칙을 따로 두지 않는다 (list-view-design.md).
        list_message_factory select {};
        // 펼침 삼각형을 눌렀을 때다.
        //  - **없으면 삼각형을 만들지 않는다** (`set_expanded`가 대신 선다).
        //    `expansion`만 있고 factory가 없으면 아무 일도 하지 않는 삼각형이
        //    생긴다 (규약 5장).
        //  - 둘 중 하나라도 있으면 **모든 행이 삼각형 자리를 비운다.** 잎과
        //    가지의 글이 줄을 맞춰야 tree로 읽힌다.
        list_message_factory toggle {};
        // 그 가지의 펼침을 **이 상태로** 하라는 절대 메시지다. 없으면 보조 기술이
        // 펼치고 접을 수 없다 (UIA Expand·Collapse가 거절된다).
        //  - 토글로 흘리면 오래된 발행본을 본 같은 명령 둘이 두 번 뒤집는다 —
        //    그룹의 `set_collapsed`와 같은 갈래다.
        //  - 이것만 있으면 삼각형의 클릭도 여기서 나온다 (지금 상태의 반대를 담아).
        std::function<input_action(const std::u8string& key, bool expanded)> set_expanded {};
        // 끌어 놓아 순서를 바꿀 때다.
        //  - **없으면 잡는 손잡이도 drag도 drop도 만들지 않는다.** 손잡이를 따로
        //    켜는 플래그를 두면 "손잡이는 있는데 끌리지 않는" 조합이 생긴다.
        //  - 평평한 목록에서만 뜻이 하나다. tree에서 "저 자리로"는 앞·뒤·자식으로
        //    갈리므로 그 뜻은 앱이 정한다 (list-view-design.md).
        list_reorder_message_factory reorder {};
        // 행을 잡고 끄는 동안의 포인터 모양이다.
        // `inherit`이면 라이브러리가 drag 역할에서 고른다(`grabbing`).
        // `reorder`가 있을 때만 뜻이 있다 — 무엇이 자연스러운 모양인지는 목록이
        // 세로인지 격자인지에 달려 있어 앱이 정한다.
        ui_cursor row_active_cursor { ui_cursor::inherit };
        // 없으면 스크롤 막대를 만들지 않는다 (짧은 목록).
        std::function<input_action(float delta)> scroll {};
        // 스크롤을 **이 자리로** 하라는 절대 메시지다 (offset은 논리 픽셀).
        // 안에 서는 막대의 `scrollbar_config::scroll_to`로 그대로 이어진다 —
        // 없으면 보조 기술이 막대의 자리를 정할 수 없다.
        std::function<input_action(float offset)> scroll_to {};
        // 창의 위·아래 가장자리 표시다 (구분선과 흘린 쪽의 그림자).
        // `scroll_area_config::edges`와 같은 설정·같은 그림이다. 기본값은 전부 거짓이다.
        scroll_edges edges {};
    };

    // 목록 오른쪽에 서는 스크롤 막대의 폭이다 (논리 픽셀).
    // 막대의 보이는 폭에 좌우 여백을 더한 값이다.
    inline constexpr float list_scrollbar_width { scrollbar_visual_width + 8.0f };
    // 깊이 한 칸의 들여쓰기와 펼침 삼각형이 차지하는 폭이다 (논리 픽셀).
    inline constexpr float list_indent_step { 14.0f };
    inline constexpr float list_expander_width { 16.0f };
    // 잡는 손잡이가 차지하는 폭이다 (논리 픽셀).
    // 깊이 **밖**의 칸이다 — 손잡이는 행의 것이지 tree 계층의 것이 아니다.
    inline constexpr float list_handle_width { 20.0f };

    // 고르고 키보드로 훑는 목록이다.
    // 항목·고른 키·스크롤 값은 앱 상태고, element는 그 값을 받아 그리고 메시지만 낸다.
    //
    // 조립은 **가로 두 칸**이다: 흘리는 창(`scroll_view_element`) + 스크롤 막대.
    // 창 안에는 행을 늘어놓는 세로 `stack_element`가 있다.
    //  - 흘리기·잘라내기·다듬기를 목록이 스스로 하지 않으므로 그 셋을 한 자리에
    //    빠짐없이 갖춘다 (stack-expressiveness-design.md).
    //  - **막대를 안에 담는다.** `strip_element`가 "무엇을 옆에 둘지는 배치의
    //    문제"라며 밖에 둔 것과 갈리는데, 그것은 원시 도구의 판단이었다. 세로
    //    목록 옆에 서는 것은 언제나 세로 막대 하나라 고를 것이 없고, 밖에 두면
    //    소비자마다 내용·창 높이·스크롤 값 셋을 손으로 맞춰 넘겨야 한다
    //    (list-view-design.md).
    //
    // **모든 항목이 tree에 담긴다 — 창에 걸치지 않는 행도 그렇다.** 묶음의
    // 키보드(↑↓·Home/End·글자 탐색)가 전부 tree를 읽어 답하므로, 걸치는 것만
    // 담으면 ↓가 목록의 처음으로 돌고 End가 마지막으로 **보이는** 행으로 가며
    // 휠로 굴리는 것만으로 초점이 거둬진다 (list-view-design.md).
    class list_element final : public ui_element
    {
    public:
        explicit list_element(list_config config);

        // 항목 전체가 차지하는 높이다 (논리 픽셀).
        [[nodiscard]] float content_height() const noexcept;
        // 이 목록에서 흘릴 수 있는 최대치다 (논리 픽셀).
        // `arrange` 뒤에 유효하며 앱이 스크롤 값을 다듬을 때 쓴다.
        [[nodiscard]] float maximum_scroll() const noexcept;
        // `arrange`가 범위 안으로 다듬은 스크롤 값이다 (논리 픽셀).
        [[nodiscard]] float scroll_offset() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 안쪽 창이 답한다 — 다듬은 offset과 배율을 쥔 것이 그 창뿐이라,
        // 목록이 자기 bounds로 다시 재면 같은 식이 두 곳에 살고 언젠가 어긋난다.
        // 그래서 바깥인 목록을 이름 대도 옳은 값이 나온다 (`scroll_area_element`와
        // 같은 자리 — 기본값은 「나는 흘리지 않는다」라 재정의가 없으면 0이다).
        [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        list_config config_ {};
        scroll_view_element* view_ { nullptr };
        // `scroll` factory가 없으면 nullptr다.
        scrollbar_element* scrollbar_ { nullptr };
        float content_height_ { 0.0f };
        float maximum_scroll_ { 0.0f };
    };
} // namespace luil
