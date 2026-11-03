#pragma once

#include "luil/ui/field_popup.h"
#include "luil/ui/image_element.h"
#include "luil/ui/ui_element.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace luil {
    // 안내 풍선의 치수다 (논리 픽셀).
    inline constexpr float teaching_tip_default_width { 320.0f };
    // 꼬리의 밑변과 길이다. 꼬리 끝과 대상 사이는 2를 더 띄운다.
    inline constexpr float teaching_tip_tail_width { 16.0f };
    inline constexpr float teaching_tip_tail_length { 8.0f };
    // 풍선이 root 자리 가장자리에서 띄우는 여백이다.
    inline constexpr float teaching_tip_screen_margin { 8.0f };

    // 풍선을 닫자는 계기다. 무엇을 닫을지는 앱이 정한다.
    enum class teaching_tip_dismiss_reason
    {
        // 닫기 단추를 눌렀다.
        close_button,
        // modal일 때 덮개를 눌렀다.
        outside,
        // modal일 때 Esc를 눌렀다.
        escape,
        // 요소가 아니라 앱이 `ui_frame::back`에서 이 값으로 부른다 (`sheet_dismiss_reason::back`과 같다).
        back,
    };

    // 행동 단추 하나다. 정보 막대의 `action_label`·`action` 쌍과 같은 규칙으로 둘 다 있어야 단추가 선다.
    struct teaching_tip_button
    {
        std::u8string text {};
        ui_action action {};
    };

    struct teaching_tip_config
    {
        // root·덮개·몸·그림의 owner다. 단추의 owner는 이 값, 단위 구분자(U+001F), `close`·`primary`·`secondary`다.
        std::u8string owner {};
        // 가리킬 자리다. 물리 픽셀, tree 좌표(`ui_element::bounds()`와 같다)다.
        //  - 비거나 root 자리와 겹치지 않으면 대상이 없다. 풍선은 자리 가운데에 꼬리 없이 선다 (설계 문서 결정 2).
        //  - 같은 frame에 먼저 배치한 가지에서 `teaching_tip_target`으로 보이는 자리를 얻어 넣는다 (결정 1).
        std::optional<rect_f> target {};
        // 바라는 방향이다. 모자라면 반대, 남은 두 방향 순으로 뒤집는다 (`place_field_popup_at_side`).
        field_popup_side side { field_popup_side::below };
        // 참이면 덮개다. 투명한 덮개가 뒤를 막고 초점을 가두며 덮개 누름·Esc가 `dismiss`를 부른다.
        // 거짓이면 비modal이다. 뒤 화면을 그대로 쓰고 초점을 옮기지 않으며 Esc를 받지 않는다 (결정 4). 닫기 단추로만 닫힌다.
        bool modal { true };
        // 제목과 본문이다. 둘 다 풍선 폭에서 줄을 바꾼다. 자리가 모자라면 본문의 줄 수를 줄이고 마지막 줄을 줄인다.
        std::u8string title {};
        std::u8string body {};
        // 풍선 위쪽의 그림이다. 폭은 안쪽 폭이고 높이는 그림 비율이다 (최대 160). 설명이 있을 때만 읽힌다.
        std::optional<image_config> image {};
        // 행동 단추 둘이다 (결정 7). primary는 accent, secondary는 normal이다. 누름은 앱 액션이고 풍선을 닫지 않는다.
        std::optional<teaching_tip_button> primary {};
        std::optional<teaching_tip_button> secondary {};
        // 닫자는 메시지다. 비면 닫기 단추가 없다. 덮개는 누름만 흡수하고 Esc는 앱으로 흐른다
        // (가둠에 dismiss 액션이 없을 때의 controller 규칙이다).
        std::function<input_action(teaching_tip_dismiss_reason)> dismiss {};
        // 접근 이름이다. 비면 `title`, 둘 다 비면 기본 문구(안내)다.
        std::u8string name {};
        // 닫기 단추의 툴팁이자 이름이다. 비면 기본 문구(닫기)다.
        std::u8string close_tooltip {};
        // modal이 닫힌 뒤 초점이 돌아갈 바깥 자리다 (`modal_host_config::focus_return`과 같다). 비modal에서는 쓰이지 않는다.
        ui_element_id focus_return {};
        // 연 시각이다. 비면(시계의 처음) 나타남 움직임이 없다 (결정 5).
        std::chrono::steady_clock::time_point opened_at {};
        // 풍선의 폭이다. 자리 폭에서 여백 둘을 뺀 값보다 넓으면 그 값으로 줄인다.
        float width { teaching_tip_default_width };
        // 행동 단추의 높이와 닫기 단추의 한 변이다. 0이면 `text_button_height`와 24다. 터치 앱이 올린다.
        float control_size { 0.0f };
    };

    // 풍선이 설 자리다 (물리 픽셀). 배치·그리기·test가 같은 답을 쓴다.
    struct teaching_tip_placement
    {
        // 꼬리를 뺀 둥근 몸통의 사각형이다.
        rect_f bubble {};
        // 꼬리가 선 방향이다. 풍선이 대상의 아래에 서면 `below`이고 꼬리는 풍선의 위 변에 선다. 비면 꼬리가 없다.
        std::optional<field_popup_side> side {};
        // 꼬리 밑변 가운데의 다른 축 좌표다 (아래·위면 x, 왼쪽·오른쪽이면 y).
        float tail_center { 0.0f };
    };

    // 화면의 한 요소를 꼬리로 가리키는 상주 안내다 (docs/teaching-tip-design.md).
    // 제목, 본문, 선택적 그림, 행동 단추 0~2개, 닫기 단추를 담는다. 주 tree 안에 겹쳐 그리고 `ui_frame::popups`에 싣지 않는다.
    //
    // root는 앱이 준 자리 전체를 bounds로 받는다. 앱은 modal host처럼 창 전체로 배치해 root 끝에 담는다.
    //  - 자식은 차례로 덮개(`teaching_tip_scrim`, modal일 때만)와 몸(`teaching_tip_body`)이다. 몸이 풍선·꼬리·제목·본문을
    //    그리고 자식으로 그림, 닫기, 행동 단추를 둔다. Tab 순서는 닫기, primary, secondary다.
    //  - modal이면 초점 가둠·Esc·진입·복귀를 modal host와 같은 부품으로 단다. 진입은 primary, 없으면 secondary, 없으면 닫기다.
    //  - 열림, 대상 사각형, 연 시각은 앱 상태다. 매 frame 다시 놓으므로 창 크기·배율·스크롤을 따라간다.
    class teaching_tip_element final : public ui_element
    {
    public:
        explicit teaching_tip_element(teaching_tip_config config);

        // 그 자리(`slot`, 물리 픽셀)에서 풍선이 설 자리다. 높이는 `context`의 측정기로 잰 글과 단추의 합이다.
        [[nodiscard]] static teaching_tip_placement place(const teaching_tip_config& config, const measure_context& context, const rect_f& slot);
        [[nodiscard]] static ui_element_id close_id(std::u8string_view owner);
        [[nodiscard]] static ui_element_id primary_id(std::u8string_view owner);
        [[nodiscard]] static ui_element_id secondary_id(std::u8string_view owner);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // modal이면 `dialog`, 아니면 `pane`이다. 이름은 `name`, `title`, 기본 문구 순이다.
        [[nodiscard]] access_info accessibility() const override;
        // 나타나는 동안만 한 frame 뒤를 청한다. 움직임 줄이기면 처음부터 끝 모습이라 청하지 않는다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;

    private:
        class body;

        teaching_tip_config config_ {};
        ui_element* scrim_ { nullptr };
        body* body_ { nullptr };
    };

    // 배치가 끝난 가지 `root`에서 `id`의 보이는 자리를 찾는다 (물리 픽셀, tree 좌표). 같은 frame에 대상을 먼저 배치한 앱이
    // `teaching_tip_config::target`에 넣는다 (결정 1).
    //  - 숨은 가지(`visible()`이 거짓)는 건너뛴다. `ui_tree::visible_bounds`는 숨김을 보지 않으므로 여기서만 nullopt다.
    //  - 지나온 자르는 조상(`clip_children`)과 겹친 자리만 남긴다. 규칙은 `ui_tree::visible_bounds`와 같다.
    //  - 없거나 다 잘렸으면 nullopt다. 앱은 그것을 보고 풍선을 닫을지 정한다.
    [[nodiscard]] std::optional<rect_f> teaching_tip_target(const ui_element& root, const ui_element_id& id);
}
