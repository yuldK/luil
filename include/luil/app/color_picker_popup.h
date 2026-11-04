#pragma once

#include "luil/app/app_host.h"
#include "luil/ui/color_picker_element.h"
#include "luil/ui/field_popup.h"

#include <functional>
#include <optional>
#include <string>

namespace luil {
    // popup 안에서 본체를 두르는 여백이다 (논리 픽셀).
    inline constexpr float color_picker_popup_padding { 8.0f };

    struct color_picker_popup_config
    {
        std::u8string id {};
        // 앵커 표면이다. 비어 있으면 주 창이다.
        std::u8string anchor {};
        // 앵커 표면의 배율이다. tree를 이 배율로 배치하고 앵커의 물리 픽셀을 논리 좌표로 옮긴다.
        // 0 이하거나 유한하지 않으면 1로 본다.
        float scale { 1.0f };
        // 닫힌 칸 아래의 자리다 (`place_field_popup`). 아래가 모자라면 위로 뒤집힌다.
        field_popup_anchor below {};
        // 닫힘 계기의 메시지다. 16진 칸을 실은 popup은 글을 치는 popup이라 앱이 `surface_moved`·`activation_changed`에
        // 빈 액션을 줘 남긴다. 닫는 메시지를 처리할 때 앱은 읽히는 16진 초안을 확정한다 (popup이 사라지면 떠남 훅이 없다).
        std::function<input_action(popup_dismiss_reason)> dismiss {};
        // popup 창의 모서리다. 비어 있으면 내장 스타일의 값이다.
        std::optional<popup_corner> corner {};
    };

    // 색 고르기 popup을 조립한다 (`make_menu_popup`과 같은 꼴, docs/color-picker-design.md).
    //  - 크기는 `color_picker_element::size_for`에 여백을 더한 논리 상수의 합이다. 글 폭을 재지 않아 배율이 달라도 잘리지 않는다.
    //  - tree를 `scale`로 배치하고 `place_field_popup`으로 자리를 놓는다. 바탕 판이 popup 전체를 칠하고 여백의 누름을 흡수한다.
    //  - 열림 상태와 초점 요청은 앱의 몫이다. 열 때 popup 표면의 판(`color_picker_element::plane_id`)으로 초점을 옮긴다.
    [[nodiscard]] ui_popup make_color_picker_popup(color_picker_config picker, color_picker_popup_config popup);
}
