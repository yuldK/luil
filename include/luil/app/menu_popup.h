#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/field_popup.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/menu_element.h"

namespace luil {
    struct menu_popup_config
    {
        std::u8string id {};
        std::u8string anchor {};
        // 앵커 표면의 논리 픽셀이다. 포인터 좌표라면 해당 표면 배율로 나눈다.
        float x { 0.0f };
        float y { 0.0f };
        // 0이면 메뉴 글·아이콘·단축키 열을 측정해 정한다.
        float width { 0.0f };
        // 표면의 배율이다. tree를 이 배율로 배치하고, `width`가 0일 때 글 폭도 이 배율의 물리 글자 크기로 잰다
        // (그리는 크기와 같아야 가장 긴 항목이 말줄임되지 않는다). 0 이하거나 유한하지 않으면 1로 본다.
        float scale { 1.0f };
        std::function<input_action(popup_dismiss_reason)> dismiss {};
        // popup 창의 모서리다 (`ui_popup::corner`). 비어 있으면 내장 스타일의 값이다 — 실행 시점에 스타일을 세운 앱은
        // 그 스타일의 `popup.corner`를 넘긴다.
        //  - 둥근 창이 되면 메뉴의 사각 선을 끈다 (`menu_config::border`). 경계는 OS 테두리가 맡는다.
        std::optional<popup_corner> corner {};
        // popup의 경계다 (`ui_popup::border`). 끄면 메뉴 자신의 선도 긋지 않는다.
        bool border { true };
        // popup 뒤의 배경 효과다 (`ui_popup::backdrop`). 효과가 서면 메뉴 몸통이 효과를 비춘다. popup에는 `acrylic`만 선다.
        window_backdrop backdrop { window_backdrop::none };
        // 폭(`width`가 0일 때)을 잴 글 크기다 (논리 픽셀). 비어 있으면 내장 스타일의 본문 글자다 — 항목은 그리는
        // 시점 스타일의 `metrics.body_font_size`로 그려지므로, 실행 시점에 스타일을 세운 앱은 그 값을 넘긴다.
        // 넘기지 않으면 큰 글자에서 글이 잘린다.
        std::optional<float> font_size {};
        // 폭을 잴 글 굵기다. 비어 있으면 내장 스타일의 본문 굵기(`metrics.font_weights.body_text`)다 — `font_size`와 같은
        // 까닭으로 실행 시점에 스타일을 세운 앱은 그 값을 넘긴다.
        std::optional<font_weight> weight {};
        // 폭을 잴 함수다. 비어 있으면 `weight`를 건 UI 글꼴이다 (`styled_text_measurer`). 넣으면 `weight`는 쓰지 않는다.
        text_measurer measurer {};
        // 닫힌 필드 아래에 놓을 때의 앵커다 (`place_field_popup`). 값이 있으면 `x`·`y`는 쓰지 않는다.
        //  - 폭을 스스로 재는 곳이 `make_menu_popup`이라, 크기를 잰 뒤에 자리를 정한다. 앱이 같은 식을 되풀이하지 않는다.
        //  - `scale`은 앵커의 물리 픽셀을 논리 좌표로 옮기는 배율이기도 하다.
        std::optional<field_popup_anchor> below {};
    };

    // 메뉴 높이·배율·분리 tree를 한곳에서 조립한다. 열림 상태와 명령 실행은 앱의 몫이다.
    // 반환한 popup을 ui_frame::popups에 싣고, 닫힘 메시지에서 목록에서 뺀다.
    [[nodiscard]] ui_popup make_menu_popup(menu_config menu, menu_popup_config popup);

    // 막대 항목 아래에 그 메뉴를 띄운다 (docs/menu-bar-design.md). 막대 상태의 `current`가 그 항목이다.
    //  - `popup.below`는 항목 자리와 `limit`(앵커 표면의 client 사각형, 물리 픽셀)으로 채운다. 간격은 0이라 메뉴가
    //    항목 바로 아래에 붙는다. 항목이 없으면 `popup.x`·`popup.y`를 그대로 쓴다.
    //  - dismiss는 Esc면 { current, false, keyboard }, 그 밖의 계기면 {}다. `menu.after_select`가 비면 {}로 채운다.
    //    막대에 `set_state`가 없으면 둘 다 건드리지 않는다.
    //  - `menu.owner`는 항목 키다. 나머지는 `make_menu_popup`이다.
    [[nodiscard]] ui_popup make_menu_bar_popup(const menu_bar_element& bar, menu_config menu, menu_popup_config popup, rect_f limit);
}
