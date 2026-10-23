#pragma once

#include "luil/ui/badge_element.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 탭 이름 뒤에 붙는 작은 표시다.
    //
    // 그림은 `badge_element`와 한 벌이다 — 테와 심각도 글리프가 고대비에서도 뜻을 남긴다.
    //  - 탭 폭은 고정(`tab_bar_config::tab_width`)이라 배지가 자리를 먼저 갖고 이름이 남는 자리에서 잘린다.
    //    배지까지 잘리면 개수가 틀린 값으로 읽히므로 이름 쪽을 줄인다.
    struct tab_badge
    {
        // 적을 글이다 ("3", "99+" 같은 것).
        //  - 비어 있으면 글 없는 상태 점이다. 심각도(`warning`·`error`)는 점 대신 글리프만 든 알약이다.
        std::u8string text {};
        badge_tone tone { badge_tone::accent };
        // 보조 기술이 읽을 말이다 ("읽지 않음 3개"). 탭 이름 뒤에 붙어 읽힌다.
        //  - 비어 있으면 `text`를 읽는다. 그것도 비면(상태 점) 읽을 말이 없다 — 점의 뜻은 앱만 알므로,
        //    보조 기술에 알리려면 여기에 적는다.
        std::u8string description {};
        // 글 앞에 붙일 앱의 글리프다 (codicon, 0이면 없다).
        //  - neutral·accent에서만 쓴다. warning·error는 심각도 글리프가 정한다 — 경고의 모양을 앱이 바꾸면 같은
        //    경고가 자리마다 다르게 보인다.
        char32_t glyph { 0 };
        // 그 글리프가 진행 중을 뜻해 돈다 (`tab_indicator_kind::busy`와 같은 규칙 — 움직임 줄이기면 멈춘다).
        bool spinning { false };

        [[nodiscard]] bool operator==(const tab_badge&) const = default;
    };

    // 탭 상태 표시의 모양이다.
    enum class tab_indicator_kind
    {
        // 채운 점이다 (저장하지 않은 변경 같은 것).
        dot,
        // 속 빈 고리다.
        ring,
        // 진행 중이다. 도는 호로 그린다 — 움직임 줄이기면 멈춘 호다.
        busy,
        // 앱이 고른 글리프다 (`tab_indicator::glyph`).
        glyph,
    };

    // 탭 하나에 다는 작은 상태 표시다. 배지와 별개이고 둘을 함께 달 수 있다.
    //  - 자리(아이콘 모서리·닫기 단추 자리)는 앱 스타일이 정한다 (`tab_bar_style::indicator_placement`).
    //  - 색만으로 뜻을 말하지 않도록 모양(점·고리·호·글리프)이 먼저이고 tone은 그 위의 색이다.
    struct tab_indicator
    {
        tab_indicator_kind kind { tab_indicator_kind::dot };
        badge_tone tone { badge_tone::accent };
        // `kind`가 `glyph`일 때의 글리프다 (codicon).
        char32_t glyph { 0 };
        // 보조 기술이 읽을 말이다. 배지의 `description`과 같은 규약으로 탭 이름 뒤에 붙어 읽힌다.
        //  - 비어 있으면 읽을 말이 없다 — 표시의 뜻은 앱만 안다.
        std::u8string description {};

        [[nodiscard]] bool operator==(const tab_indicator&) const = default;
    };

    // 탭 하나의 설정이다.
    struct tab_item
    {
        // 탭을 가리키는 앱 정의 키다 (ui_element_id::owner 규약).
        std::u8string key {};
        std::u8string label {};
        // 0이면 아이콘이 없다.
        char32_t icon { 0 };
        // 닫기 버튼은 이 값과 close factory가 모두 있어야 생긴다.
        bool closable { false };
        // 이름 뒤의 배지다 (개수·상태 점). 없으면 배지가 없다.
        std::optional<tab_badge> badge {};
        // 상태 표시다. 없으면 표시가 없다.
        std::optional<tab_indicator> indicator {};
    };

    // 넘칠 때 탭을 어떻게 넘기는가다.
    enum class tab_overflow_mode
    {
        // 띠를 흘린다 (휠·터치 끌기·초점 되살리기). 처음부터의 동작이다.
        scroll,
        // 양 끝에 앞·뒤 단추를 두고 한 탭씩 넘긴다. 흘리는 길(터치 끌기·초점 되살리기)은 그대로 남는다.
        step,
    };

    // 견본 막대다 (`tab_bar_config::preview`). 환경 설정에서 스타일과 상태 표시를 고를 때 그림만 보인다.
    //  - 누름·hover·초점·끌기·접근 tree에서 모두 빠진다. 접근 tree에는 `name` 하나(그림)로만 선다.
    //  - 상태를 키로 고정해 그린다 — 견본은 포인터가 없어도 "올라간 탭"을 보여야 한다.
    struct tab_bar_preview
    {
        // 보조 기술이 읽을 견본의 이름이다 ("이어진 탭 견본"). 비면 접근 tree에서 빠진다.
        std::u8string name {};
        // 포인터가 올라간 것처럼 그릴 탭이다.
        std::u8string hovered {};
        // 눌린 것처럼 그릴 탭이다.
        std::u8string pressed {};
        // 키보드 초점 테를 두른 것처럼 그릴 탭이다.
        std::u8string focused {};
        // 닫기 단추에 포인터가 올라간 것처럼 그릴 탭이다. 상태 표시가 닫기 단추 자리에 있으면 단추로 바뀐다.
        std::u8string close_hovered {};

        [[nodiscard]] bool operator==(const tab_bar_preview&) const = default;
    };

    // 탭 키 하나를 담은 메시지다 (선택·닫기).
    using tab_message_factory = std::function<input_action(const std::u8string& key)>;
    // 끌어 놓은 탭을 대상 탭 자리로 옮기자는 메시지다.
    using tab_reorder_message_factory = std::function<input_action(const std::u8string& moved, const std::u8string& target)>;
    // 끌어 놓은 탭을 대상 탭의 앞(`before`)이나 뒤(`after`)로 옮기자는 메시지다.
    using tab_move_message_factory = std::function<input_action(const std::u8string& moved, const std::u8string& target, drop_side side)>;
    // 다른 탭 막대(다른 창)에서 끌어 온 탭을 이 막대의 대상 탭 앞이나 뒤에 넣자는 메시지다.
    //  - `payload`는 출발 탭이 지은 그대로다. `dragged_owner`가 탭 키이고 `container`가 출발 막대다.
    //  - `target`이 비어 있으면 탭이 하나도 없는 막대다 (넣을 자리는 처음이자 끝이다).
    using tab_receive_message_factory = std::function<input_action(const drag_payload& payload, const std::u8string& target, drop_side side)>;
    // 끌던 탭을 받는 대상이 없는 자리에 놓았다는 메시지다 (`drag_source::on_unclaimed_drop`).
    //  - 탭 키는 `payload.dragged_owner`다. 잡은 지점(`grab_offset_x`·`grab_offset_y`)도 payload에 있어, 새 창을 세울 때 그
    //    탭이 놓은 점에 그대로 오게 할 수 있다 (`ui_window::screen_anchor`).
    using tab_release_message_factory = std::function<input_action(const drag_payload& payload, const drag_release& release)>;
    // 탭에 컨텍스트 메뉴를 열자는 메시지다.
    //  - 문맥의 좌표(물리 픽셀, 창 좌표)는 메뉴를 띄울 자리이고, 수식 키는 그 순간의 것이다.
    using tab_context_message_factory = std::function<input_action(const std::u8string& key, const ui_action_context& context)>;

    struct tab_bar_config
    {
        // 같은 화면에 막대가 여럿일 때 구분하는 키다.
        std::u8string owner {};
        std::vector<tab_item> items {};
        // 선택된 탭의 키다 (앱 상태).
        std::u8string selected {};
        // 탭 한 개의 폭이다 (논리 픽셀).
        float tab_width { 160.0f };
        // 자리가 모자랄 때 줄일 수 있는 최소 폭이다 (논리 픽셀). 0이거나 `tab_width` 이상이면 줄이지 않는다.
        //  - 모자라면 모든 탭을 같은 폭으로 `tab_width`에서 이 값까지 줄이고, 그래도 넘칠 때만 넘침이 시작된다.
        //  - 줄어든 폭에서는 배지와 상태 표시가 먼저 자리를 갖고 이름이 말줄임된다.
        //  - 지금 실제 폭은 `tab_bar_element::tab_width()`로 묻는다.
        float min_tab_width { 0.0f };
        // 이 막대만의 스타일이다. 비어 있으면 앱 스타일(`ui_style::tab_bar`)을 따른다.
        //  - 앱 안의 모든 탭 막대를 한 모양으로 하려면 막대마다 적지 말고 `ui_style`에 한 번 적는다 (공용 스타일).
        //    여기는 견본 막대처럼 한 막대만 달라야 하는 자리다.
        std::optional<tab_bar_style> style {};
        // 가운데 버튼으로 탭을 닫는다 (`close` factory가 있고 닫을 수 있는 탭). 선택은 바뀌지 않는다.
        bool middle_click_close { true };
        // 넘칠 때 탭을 넘기는 방식이다.
        tab_overflow_mode overflow_mode { tab_overflow_mode::scroll };
        // 앞·뒤 단추의 tooltip이자 보조 기술이 읽는 이름이다 (`overflow_tooltip`과 같은 규약 — 비면 접근 tree에서 빠진다).
        //  - 단추는 `scroll` factory가 있어야 선다 (넘기는 것도 스크롤 메시지다).
        std::u8string step_back_tooltip {};
        std::u8string step_forward_tooltip {};
        // 휠(세로·가로) 한 칸에 탭 하나씩 넘긴다. 잘게 오는 휠은 쌓았다가 탭 경계에 맞춘다.
        //  - `scroll` factory가 있어야 한다. 넘기는 방식(`overflow_mode`)과 따로 켠다.
        bool wheel_steps { false };
        // 막대 끝에 따라붙는 꼬리 element다 (새 탭 단추 같은 것). 탭이 적으면 마지막 탭 바로 뒤에 서고, 넘치면 오른쪽
        // 끝에 고정된다.
        //  - factory다 — config를 값으로 복사할 수 있게 둔다. element는 막대를 지을 때 한 번 짓는다.
        //  - 꼬리는 막대의 초점 묶음(←/→) 안에 선다.
        std::function<std::unique_ptr<ui_element>()> trailing {};
        // 꼬리의 폭이다 (논리 픽셀).
        float trailing_width { 0.0f };
        // 있으면 견본 막대다 — 그림만 남고 동작은 하나도 서지 않는다 (`tab_bar_preview`).
        std::optional<tab_bar_preview> preview {};
        // 넘칠 때의 가로 스크롤 값이다 (논리 픽셀, 앱 상태).
        // 휠을 이 값으로 옮길지는 앱 정책이 정한다. 범위 밖 값은 arrange가 다듬는다.
        float scroll_offset { 0.0f };
        // 없는 factory의 동작은 아예 만들지 않는다.
        tab_message_factory select {};
        tab_message_factory close {};
        tab_reorder_message_factory reorder {};
        // 탭을 우클릭하면 낸다. 터치의 길게 누르기도 우클릭이라 이 하나로 함께 선다.
        //  - 탭의 닫기 버튼 위에서 눌러도 그 탭의 메뉴다 — 버튼은 탭의 일부로 보인다.
        //  - 메뉴의 내용과 열림 상태는 앱 몫이다 (통상 menu_element를 popup으로 띄운다).
        tab_context_message_factory context_menu {};
        // 탭이 넘칠 때 오른쪽 끝에 두는 넘침 메뉴 버튼의 액션이다
        // (통상 탭 목록 popup을 열자는 메시지 — 내용은 menu_element로 조립한다).
        // 없으면 버튼을 만들지 않고, 있어도 넘치지 않으면 보이지 않는다.
        ui_action overflow {};
        // 그 버튼의 tooltip이자 **보조 기술이 읽는 이름**이다.
        // 글리프뿐인 버튼이라 이름의 원천이 이것뿐이고, 비면 접근 tree에서
        // 통째로 빠진다 (이름 없는 단추는 소음이다 —
        // accessibility-action-design.md). caption 버튼의 tooltip과 같은
        // 자리·같은 이유다: 그 말은 앱의 것이라 라이브러리가 정할 수 없다.
        std::u8string overflow_tooltip {};
        // 가로 스크롤 값을 이만큼 옮기자는 메시지다 (논리 픽셀).
        // 있으면 넘칠 때 탭 레인이 가로 흘림의 임자로 선다 — 터치로 쓸어 넘기고,
        // 표 없는 휠·초점 되살리기도 레인을 흘린다. 휠을 막대 전체로 받으려면
        // 지금처럼 앱의 표가 이름 댄다.
        std::function<input_action(float delta)> scroll {};
        // 끌어 놓은 탭을 대상 탭의 앞이나 뒤로 옮기자는 메시지다. 있으면 `reorder` 대신 이것이 선다.
        //  - 놓는 자리는 대상 탭의 앞 반쪽이면 앞, 뒤 반쪽이면 뒤다. 마지막 탭 뒤의 빈 곳은 마지막 탭의 뒤다.
        //  - **순서가 그대로인 자리는 받지 않는다** — 자기 자신, 바로 앞 탭의 뒤 반쪽, 바로 뒤 탭의 앞 반쪽이다. 그
        //    자리에서는 강조도 서지 않고 놓아도 메시지가 없다.
        //  - 받는 자리는 탭 테두리 대신 그 틈에 accent 세로 막대로 보인다.
        tab_move_message_factory move {};
        // 다른 탭 막대(다른 창)에서 끌어 온 탭을 받는다 — 창의 탭을 다른 창으로 합치는 길이다.
        //  - 자리 규칙과 틈 표시는 `move`와 같다. 다른 막대에서 왔으니 그대로인 자리가 없어 어디든 받는다.
        //  - 받는 것은 탭(`ui_element_kind::tab`)뿐이다. 어느 막대에서 왔는지는 `payload.container`로 본다.
        tab_receive_message_factory receive {};
        // 끌던 탭을 받는 대상이 없는 자리에 놓았다 (창 밖 — 새 창으로 떼어 낼 자리, 또는 다른 창의 빈 곳).
        //  - Esc·캡처 상실로 취소된 끌기에는 오지 않는다. 자리와 화면 좌표는 `drag_release`에 있다.
        //  - 있으면 `reorder`·`move`가 없어도 탭을 끌 수 있다.
        tab_release_message_factory unclaimed_drop {};
        // 탭을 끌어 창 밖으로 나갔을 때의 포인터 모양이다 (`drag_payload::outside_cursor`).
        ui_cursor outside_cursor { ui_cursor::inherit };
    };

    // 넘침 메뉴 버튼의 폭이다 (논리 픽셀). 앞·뒤 단추도 같은 폭이다.
    inline constexpr float tab_overflow_width { 28.0f };

    // 한 탭씩 넘기는 스크롤 값이다 (논리 픽셀). 앞·뒤 단추와 칸 휠이 같은 식을 쓴다.
    //  - 앞으로(`steps` > 0)는 오른쪽 끝에 걸친 탭이 온전히 드러나는 자리, 뒤로는 왼쪽 끝에 걸친 탭이 온전히
    //    드러나는 자리다. 한 칸마다 탭 경계 하나를 넘는다.
    //  - `viewport`는 띠의 폭, `maximum`은 흘릴 수 있는 최대치다. 답은 [0, maximum] 안이다.
    [[nodiscard]] float tab_step_offset(float offset, float tab_width, float viewport, float maximum, int steps) noexcept;

    // 자리에 맞춘 탭 폭이다 (논리 픽셀). 다 들어가면 `tab_width`, 모자라면 같은 폭으로 줄이되 `min_tab_width` 밑으로는
    // 줄이지 않는다. `available`은 탭이 쓸 수 있는 폭이다 (꼬리를 뗀 막대 폭).
    [[nodiscard]] float fitted_tab_width(std::size_t count, float tab_width, float min_tab_width, float available) noexcept;

    // 가로로 탭을 늘어놓는 막대다.
    // 선택·닫기·순서·컨텍스트 메뉴는 앱 상태이고 element는 메시지만 낸다.
    // 넘치는 탭은 가로 스크롤로 가리고(잘린 탭은 눌리지도 않는다), 내용 영역은 앱 몫이다.
    //
    // 조립은 가로 한 줄이다: [앞] 흘리는 띠(`strip_element`) [뒤] [넘침 메뉴] [꼬리].
    // 띠 안에는 탭을 같은 폭으로 늘어놓는 레인이 있다.
    //  - 흘리기·잘라내기·다듬기는 띠의 몫이다 (stack-expressiveness-design.md).
    //  - **넘침 판정은 단추 폭을 떼기 전의 slot 폭으로** 한다. 뗀 뒤에 재면
    //    "버튼이 있어서 넘친다"가 되어 판정이 자기참조가 된다.
    //  - 탭 폭은 자리에 달려(`min_tab_width`) 배치에서 정한다. 그래서 레인은 고정 길이 stack이 아니라 막대가 폭을
    //    정해 주는 레인이다.
    //
    // 모양은 앱 스타일(`ui_style::tab_bar`)이나 `tab_bar_config::style`이 정한다 (tab-bar-style-design.md).
    class tab_bar_element final : public ui_element
    {
    public:
        explicit tab_bar_element(tab_bar_config config);

        // 이 막대에서 흘릴 수 있는 최대치다 (논리 픽셀).
        // arrange 뒤에 유효하며 앱이 스크롤 값을 다듬을 때 쓴다.
        [[nodiscard]] float maximum_scroll() const noexcept;
        // arrange가 범위 안으로 다듬은 스크롤 값이다 (논리 픽셀).
        [[nodiscard]] float scroll_offset() const noexcept;
        // 지금 실제 탭 폭이다 (논리 픽셀). arrange 뒤에 유효하다 (`min_tab_width`로 줄었을 수 있다).
        [[nodiscard]] float tab_width() const noexcept;
        // 넘치고 있는가다. arrange 뒤에 유효하다.
        [[nodiscard]] bool overflowing() const noexcept;
        // 한 탭씩 넘기는 스크롤 변화량이다 (논리 픽셀, `tab_step_offset`). arrange 뒤에 유효하다.
        [[nodiscard]] float step_delta(int steps) const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 마지막 탭의 오른쪽 끝이다 (물리 픽셀). 탭이 없으면 띠의 왼쪽 끝이다.
        [[nodiscard]] float end_gap_x() const noexcept;
        // 그 자리가 마지막 탭 뒤의 빈 틈인가 (`tab_bar_config::move`·`receive`).
        [[nodiscard]] bool end_gap_contains(float x, float y) const noexcept;

        tab_bar_config config_ {};
        strip_element* strip_ { nullptr };
        // 탭을 같은 폭으로 늘어놓는 레인이다.
        ui_element* lane_ { nullptr };
        // 마지막 탭이다. 탭이 없으면 nullptr다.
        const ui_element* last_tab_ { nullptr };
        // 넘침 액션이 없으면 nullptr다.
        ui_element* overflow_ { nullptr };
        // 한 탭씩 넘기는 앞·뒤 단추다. 넘기는 방식이 `step`이고 `scroll` factory가 있을 때만 선다.
        ui_element* step_back_ { nullptr };
        ui_element* step_forward_ { nullptr };
        // 꼬리 element다. 없으면 nullptr다.
        ui_element* trailing_ { nullptr };
        // 배치가 고른 스타일이다 (`config_.style` 또는 앱 스타일). 끝의 단추들이 이 자리를 가리킨다.
        tab_bar_style style_ {};
        float maximum_scroll_ { 0.0f };
        float tab_width_ { 0.0f };
        // 띠의 폭이다 (논리 픽셀).
        float viewport_ { 0.0f };
        bool overflowing_ { false };
    };
} // namespace luil
