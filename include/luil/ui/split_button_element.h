#pragma once

#include "luil/ui/dialog_elements.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <optional>
#include <string>

namespace luil {
    // 분할 버튼에서 화살표 쪽이 차지하는 기본 폭이다 (논리 픽셀).
    inline constexpr float split_button_arrow_width { 28.0f };

    struct split_button_config
    {
        // 세 부분(root·본체·화살표)의 owner다.
        std::u8string owner {};
        // 본체의 글·아이콘·모양·기본 버튼·색 선택자다.
        //  - `selected`는 쓰지 않는다. 고른 값을 나르는 라디오가 아니다.
        //  - `link`는 상자가 없어 두 절반을 한 상자로 묶을 수 없으므로 normal로 읽는다.
        //  - `default_button`은 본체가 있을 때만 Enter의 기본 대상이다.
        text_button_config look {};
        // 본체를 누르면 낼 액션이다. 비어 있으면 본체가 없고 버튼 전체가 메뉴를 여는 드롭다운 단추가 된다.
        ui_action invoke {};
        // 메뉴가 떠 있는지는 앱 상태다 (popup이 frame에 실려 있는지와 같이 간다).
        // 참이면 화살표가 눌린 바탕이고 chevron이 위를 가리킨다.
        bool open { false };
        // 메뉴를 **이 상태로** 하라는 절대 메시지다. 누름은 build 때의 `open` 반대를 담아 이 factory를 부르고, 보조 기술의
        // 펼치기·접기는 목표를 담아 같은 factory를 부른다. 없으면 화살표가 눌리지 않고 보조 기술이 열지 못한다.
        std::function<input_action(bool open)> set_open {};
        bool enabled { true };
        // 거짓이면 본체만 막는다. 메뉴는 그대로 열린다.
        bool invoke_enabled { true };
        // 본체의 툴팁이다. 본체 글이 없으면 접근 이름의 대신도 된다. 본체가 없으면 버튼 전체의 툴팁이다.
        std::u8string tooltip {};
        // 화살표의 접근 이름이다. 이름이 비면 본체 글 뒤에 기본 문구(" 옵션")를 붙인다.
        std::u8string arrow_name {};
        // 화살표의 툴팁이다. 본체가 없을 때는 비어 있으면 `tooltip`을 쓴다.
        std::u8string arrow_tooltip {};
        // 화살표 쪽의 폭이다 (논리 픽셀). 슬롯이 좁아도 줄이는 것은 본체 글이고 화살표 폭은 지킨다.
        float arrow_width { split_button_arrow_width };
    };

    // 본체(기본 동작)와 화살표(메뉴 열기)를 한 상자에 담은 버튼이다.
    // 본체를 누르면 `invoke`가 나가고, 화살표를 누르면 `set_open`이 메뉴를 연다. 본체가 없으면 버튼 전체가 드롭다운 단추다.
    //
    // root는 구조일 뿐이고 자식이 둘이다 (`split_button_body`·`split_button_arrow`).
    //  - 두 절반이 hover·눌림·초점·활성을 따로 가진다. 한 element가 포인터 판정을 반으로 가르지 않는다.
    //  - Tab에서는 한 자리이고(가로 focus group) 들어올 때 본체에 선다. ←/→가 절반 사이를 옮긴다.
    //  - 보조 기술에는 단추 둘로 읽힌다. 본체는 Invoke, 화살표는 ExpandCollapse를 낸다.
    //  - 메뉴 popup의 자리는 `place_field_popup`·`menu_popup_config::below`가 정한다 (field_popup.h).
    class split_button_element final : public ui_element
    {
    public:
        explicit split_button_element(split_button_config config);

        // 두 절반의 자리를 나눈다. 화살표는 오른쪽 `arrow_width`이고 본체가 나머지다. 본체가 없으면 화살표가 슬롯 전부다.
        void arrange(const arrange_context& context) override;
        // 두 절반이 한 둥근 상자로 보이게 바탕과 구분선을 그리고, 글과 chevron은 각 절반이 그린다.
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 본체 내용(`text_button_element`와 같은 식)에 화살표 폭을 더한 폭과 `text_button_height`다.
        //  - 글은 스타일의 버튼 글자로 잰다 (`measure_context::style_metrics`). 그리기와 같은 글꼴이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        split_button_config config_ {};
        // 채움 종류다. 상자 하나의 색이므로 두 절반이 같은 종류를 쓴다.
        text_button_fill fill_ { text_button_fill::plain };
        // 본체가 없으면 nullptr다.
        ui_element* body_ { nullptr };
        ui_element* arrow_ { nullptr };
    };
}
