#pragma once

#include "luil/app/app_host.h"
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
        // 폭을 잴 함수다. 비어 있으면 `default_text_measurer`다.
        text_measurer measurer {};
    };

    // 메뉴 높이·배율·분리 tree를 한곳에서 조립한다. 열림 상태와 명령 실행은 앱의 몫이다.
    // 반환한 popup을 ui_frame::popups에 싣고, 닫힘 메시지에서 목록에서 뺀다.
    [[nodiscard]] ui_popup make_menu_popup(menu_config menu, menu_popup_config popup);
}
