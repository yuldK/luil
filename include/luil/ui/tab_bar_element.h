#pragma once

#include "luil/ui/badge_element.h"
#include "luil/ui/strip_element.h"
#include "luil/ui/ui_element.h"

#include <functional>
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

        [[nodiscard]] bool operator==(const tab_badge&) const = default;
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
    };

    // 탭 키 하나를 담은 메시지다 (선택·닫기).
    using tab_message_factory = std::function<input_action(const std::u8string& key)>;
    // 끌어 놓은 탭을 대상 탭 자리로 옮기자는 메시지다.
    using tab_reorder_message_factory = std::function<input_action(const std::u8string& moved, const std::u8string& target)>;
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
    };

    // 넘침 메뉴 버튼의 폭이다 (논리 픽셀).
    inline constexpr float tab_overflow_width { 28.0f };

    // 가로로 탭을 늘어놓는 막대다.
    // 선택·닫기·순서·컨텍스트 메뉴는 앱 상태이고 element는 메시지만 낸다.
    // 넘치는 탭은 가로 스크롤로 가리고(잘린 탭은 눌리지도 않는다), 내용 영역은 앱 몫이다.
    //
    // 조립은 **가로 stack 두 칸**이다: 흘리는 띠(`strip_element`) + 넘침 버튼.
    // 띠 안에는 탭을 늘어놓는 가로 `stack_element`가 있다.
    //  - 흘리기·잘라내기·다듬기를 막대가 스스로 하지 않으므로 그 넷을 한 자리에
    //    빠짐없이 갖춘다 (stack-expressiveness-design.md).
    //  - **넘침 판정은 버튼 폭을 떼기 전의 slot 폭으로** 한다. 뗀 뒤에 재면
    //    "버튼이 있어서 넘친다"가 되어 판정이 자기참조가 된다.
    class tab_bar_element final : public ui_element
    {
    public:
        explicit tab_bar_element(tab_bar_config config);

        // 이 막대에서 흘릴 수 있는 최대치다 (논리 픽셀).
        // arrange 뒤에 유효하며 앱이 스크롤 값을 다듬을 때 쓴다.
        [[nodiscard]] float maximum_scroll() const noexcept;
        // arrange가 범위 안으로 다듬은 스크롤 값이다 (논리 픽셀).
        [[nodiscard]] float scroll_offset() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        tab_bar_config config_ {};
        strip_element* strip_ { nullptr };
        // 넘침 액션이 없으면 nullptr다.
        ui_element* overflow_ { nullptr };
        float maximum_scroll_ { 0.0f };
    };
} // namespace luil
