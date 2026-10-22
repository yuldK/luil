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
    };

    // 메뉴 높이·배율·분리 tree를 한곳에서 조립한다. 열림 상태와 명령 실행은 앱의 몫이다.
    // 반환한 popup을 ui_frame::popups에 싣고, 닫힘 메시지에서 목록에서 뺀다.
    [[nodiscard]] ui_popup make_menu_popup(menu_config menu, menu_popup_config popup);
} // namespace luil
