#pragma once

#include "luil/ui/color_space.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 색 고르기의 치수다 (논리 픽셀). 그릴 때 배율을 곱한다.
    // 채도·명도 판의 한 변이다.
    inline constexpr float color_picker_plane_size { 200.0f };
    // 색상 막대와 알파 막대의 높이다.
    inline constexpr float color_picker_bar_height { 16.0f };
    // 부분 사이의 간격이다. 견본 칸 사이도 같다.
    inline constexpr float color_picker_gap { 8.0f };
    // 지금 색을 보이는 견본의 한 변이다.
    inline constexpr float color_picker_swatch_size { 32.0f };
    // 미리 정한 견본 칸의 한 변이다.
    inline constexpr float color_picker_preset_size { 24.0f };
    // 16진 칸의 폭이다. 판이 좁으면 남는 자리까지 줄인다.
    inline constexpr float color_picker_hex_width { 96.0f };
    // 16진 칸의 높이다. 견본 줄의 세로 가운데에 선다.
    inline constexpr float color_picker_hex_height { 28.0f };
    // 누른 채 이 거리를 넘으면 끌기다 (물리 픽셀, `pointer_drag_target::start_distance`와 같은 단위, 별점과 같다).
    //  - 판·색상 막대·알파 막대 셋 모두다 (docs/color-picker-design.md 결정 5). 터치는 이 픽셀 값 대신 터치 허용치를 쓴다.
    inline constexpr float color_picker_drag_start_distance { 6.0f };

    // 미리 정한 견본 칸 하나다. 색은 앱이 주는 내용의 색이다 (docs/concepts/theming.md의 예외).
    struct color_picker_preset
    {
        ui_color color { 0 };
        // 접근 이름과 툴팁이다. 비면 16진 글(`format_hex_color`)이다.
        std::u8string name {};
    };

    // 16진 칸이다. 칸은 `text_input_element`이고 글 편집은 여느 텍스트 칸처럼 앱이 든다 (결정 2).
    struct color_picker_hex
    {
        // 앱 kind다. 앱 정책의 `text_target_of`가 편집 대상으로 옮긴다. 앱은 `apply_text_edit`에 `hex_color_filter()`를 준다.
        ui_element_id id {};
        // 앱의 초안이다.
        text_input_view value {};
        // 초안이 색으로 서지 않는다. 앱이 `check_hex_color`로 정한다. 테두리를 오류색으로 그리고 보조 기술에 알린다.
        bool invalid { false };
        // 초안을 확정하라는 메시지다. 같은 표면 안에서 칸을 떠날 때, 칸에서 Enter를 칠 때(Android 키보드의 확정 단추도) 낸다.
        //  - popup이 닫힐 때는 그 tree가 사라져 떠남 훅이 서지 않는다. 앱은 닫는 메시지를 처리할 때 초안을 확정한다.
        //  - 앱은 받은 초안을 `check_hex_color`로 읽고 `hsv_from_color(색, 지금 값)`으로 바꿔 회색·검정에서도 색상을 지킨다.
        ui_action commit {};
        // 접근 이름이다. 비면 기본 문구(16진 색)다.
        std::u8string name {};
    };

    struct color_picker_config
    {
        // root와 부분의 owner다. 부분의 owner는 이 값, 단위 구분자(U+001F), `plane`·`hue`·`alpha`·견본 번호(1부터) 순이다.
        std::u8string owner {};
        // 앱 상태다. 그리기는 `clamp_hsv`로 자른 값을 쓴다.
        hsv_color value {};
        // 알파 막대와 8자리 16진이다 (색 공간 결정 2). 끄면 막대도 체커도 없다.
        bool alpha { false };
        // 색을 **이것으로** 하라는 절대 메시지다. 판·막대·키·견본·보조 기술이 모두 이 하나를 탄다 (결정 1).
        //  - 부분은 자기 채널만 바꾸고 나머지는 그 frame의 `value`에서 옮긴다. 판을 끄는 메시지는 build 때의 색상·알파를 담는다.
        //  - 없으면 표시 전용이다. Tab 자리도 끌기도 커서도 없고 보조 기술은 읽기만 한다.
        std::function<input_action(hsv_color value)> change_to {};
        // 16진 칸이다. 없으면 견본 줄에 견본만 선다.
        std::optional<color_picker_hex> hex {};
        // 미리 정한 견본이다 (결정 6). 비면 견본 칸 줄이 없다. 원래 색 되돌리기는 앱이 첫 견본으로 준다.
        std::vector<color_picker_preset> presets {};
        // 판의 한 변이다 (논리 픽셀). 0이면 `color_picker_plane_size`다. 자리 폭보다 크면 그 폭으로 줄인다.
        float plane_size { 0.0f };
        // 막대의 높이다 (논리 픽셀). 0이면 `color_picker_bar_height`다. 터치 앱이 올린다.
        float bar_height { 0.0f };
        // 접근 이름이다. 비면 기본 문구(색·채도와 명도·색상·불투명도)다.
        std::u8string name {};
        std::u8string plane_name {};
        std::u8string hue_name {};
        std::u8string alpha_name {};
    };

    // 채도·명도 판, 색상 막대, 알파 막대, 견본, 16진 칸, 미리 정한 견본으로 색 하나를 고르는 인라인 본체다
    // (docs/color-picker-design.md).
    //
    // root(`ui_element_kind::color_picker`)는 접근 역할이 group이고 값 글이 16진 글이다 (UIA Value 읽기 전용). 지금 색의 견본을
    // 직접 그리고 자식을 그린다. 자식은 세로로 판, 색상 막대, 알파 막대, 견본과 16진 칸의 줄, 견본 칸 줄 순이다.
    //  - 판(`color_picker_plane`)은 slider이고 두 값을 글로만 알린다 (결정 4). ↑↓·Page·Home·End가 명도, ←→가 채도다.
    //  - 막대(`color_picker_bar`)는 slider이고 범위와 걸음(SmallChange·LargeChange)을 알린다. ←→·Page·Home·End가 그 채널이다.
    //  - 견본 칸(`color_picker_preset`)은 단추이고, 줄 하나가 Tab의 한 자리다 (가로 묶음, ←→가 칸 사이).
    //  - 16진 칸은 앱 kind의 `text_input_element`다.
    //  - 판과 막대는 누른 채 `color_picker_drag_start_distance`를 넘으면 끌기다. 그 안에서 떼면 그 자리 값의 클릭이다.
    //  - 비활성은 앱이 root에 거는 `set_enabled(false)`이고 자식이 arrange에서 따라간다.
    //
    // root의 bounds는 본체가 차지한 크기다. 자리가 더 넓으면 왼쪽 위에 붙인다.
    class color_picker_element final : public ui_element
    {
    public:
        explicit color_picker_element(color_picker_config config);

        // 본체의 논리 크기다. 폭은 판의 한 변이고 높이는 부분과 간격의 합이다 (견본 칸은 그 폭에 맞춰 줄을 넘긴다).
        //  - 글 폭을 재지 않는 논리 상수의 합이라 배율과 글자 크기에 매이지 않는다.
        [[nodiscard]] static size_f size_for(const color_picker_config& config) noexcept;
        // 판의 id다. popup을 열 때 앱이 초점 요청의 대상으로 쓴다 (결정 3).
        [[nodiscard]] static ui_element_id plane_id(std::u8string_view owner);

        // arrange 뒤에 유효하다. 물리 좌표를 받고 판·막대 밖은 끝으로 자른다. 끌기·클릭·test가 같은 함수를 쓴다.
        //  - 판의 왼쪽이 채도 0, 오른쪽이 1이고 위가 명도 1, 아래가 0이다. 색상과 알파는 build 때의 값이다.
        [[nodiscard]] hsv_color value_at_plane(float x, float y) const noexcept;
        // 색상 막대의 왼쪽이 0도, 오른쪽이 360도다.
        [[nodiscard]] float hue_at(float x) const noexcept;
        // 알파 막대의 왼쪽이 0, 오른쪽이 1이다.
        [[nodiscard]] float alpha_at(float x) const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 부분이 무엇을 바꾸는가다.
        enum class channel
        {
            plane,
            hue,
            alpha,
        };

        // 절대 메시지 하나다. 비활성이거나 `change_to`가 없거나 지금 값과 같으면 비어 있다.
        [[nodiscard]] std::vector<input_action> emit(const hsv_color& target) const;
        // 포인터 자리의 값이다.
        [[nodiscard]] hsv_color value_at(channel part, float x, float y) const noexcept;
        // 키 걸음의 메시지다. 끝에 닿았으면 빈 목록이라 키가 앱 단축키로 새지 않는다.
        [[nodiscard]] std::vector<input_action> step_by_key(channel part, value_step step, bool horizontal) const;
        // 막대의 값 정하기다 (보조 기술). 표시 전용이면 nullopt다.
        [[nodiscard]] std::optional<std::vector<input_action>> set_bar_value(channel part, float value) const;
        [[nodiscard]] access_info part_accessibility(channel part) const;
        void draw_part(draw_context& context, channel part, const rect_f& box) const;
        void draw_swatch(draw_context& context) const;

        color_picker_config config_ {};
        // 자른 값이다. 그리기·키·접근이 이것을 읽는다.
        hsv_color value_ {};
        float scale_ { 1.0f };
        rect_f plane_box_ {};
        rect_f hue_box_ {};
        rect_f alpha_box_ {};
        rect_f swatch_box_ {};
        ui_element* plane_ { nullptr };
        ui_element* hue_ { nullptr };
        // 알파를 끄면 nullptr다.
        ui_element* alpha_bar_ { nullptr };
        // 16진 칸이 없으면 nullptr다.
        ui_element* hex_ { nullptr };
        // 견본이 없으면 nullptr다. 칸은 이 줄의 자식이다.
        ui_element* presets_ { nullptr };
    };

    struct color_field_config
    {
        std::u8string owner {};
        // 앱 상태다. 견본과 16진 글로 그린다.
        hsv_color value {};
        // 8자리 16진 글과 견본의 체커다.
        bool alpha { false };
        // popup이 떠 있는지는 앱 상태다 (popup이 frame에 실려 있는지와 같이 간다).
        bool open { false };
        // popup을 **이 상태로** 하라는 절대 메시지다. 누름은 build 때의 `open` 반대를 담고, 보조 기술의 펼치기·접기는 목표를
        // 담는다. 없으면 칸이 눌리지 않고 보조 기술이 열지 못한다.
        std::function<input_action(bool open)> set_open {};
        // 접근 이름이다. 비면 기본 문구(색)다.
        std::u8string name {};
    };

    // 색 고르기를 popup으로 여는 닫힌 칸이다 (`ui_element_kind::color_field`). 높이는 `dropdown_height`(28)다.
    // 견본 사각형, 16진 글, 펼침 화살표를 그린다. 닫힌 필드 popup 규약(field_popup.h)을 따른다.
    //  - 클릭은 `set_open(!open)`, 키는 Alt+↓·F4·↓가 연다. 열린 동안 키보드 초점이 칸을 같은 표면 안에서 떠나면 닫는다.
    //  - 열 때 앱은 popup 표면의 판으로 초점을 옮기는 요청을 싣는다 (`color_picker_element::plane_id`). Esc로 닫으면 칸으로
    //    돌아오는 요청을 한 번 더 싣는다 (docs/color-picker-design.md 결정 3).
    class color_field_element final : public ui_element
    {
    public:
        explicit color_field_element(color_field_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 펼치기·접기는 절대 메시지(`set_open`)로 답한다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        color_field_config config_ {};
    };
}
