#pragma once

#include "luil/ui/ui_events.h"

#include <windows.h>

#include <chrono>
#include <optional>
#include <string>

namespace luil::win32 {
    // 표면 하나가 받은 포인터·휠 메시지다.
    // 좌표는 그 표면의 client 픽셀이고 시각은 창이 기록해 넣는다 —
    // 이 계층은 창도 시계도 모르므로 test가 결정적이다.
    //  - 휠 메시지의 lparam은 화면 좌표라 창이 먼저 client로 옮긴다.
    struct pointer_message
    {
        UINT message { 0 };
        WPARAM word_parameter { 0 };
        float x { 0.0f };
        float y { 0.0f };
        std::chrono::steady_clock::time_point time {};
        // 이벤트가 난 표면이다.
        // 주 창은 비어 있고 값은 popup·보조 창 id다.
        std::u8string surface {};
    };

    // 포인터·휠 메시지를 입력 이벤트로 옮긴다.
    // 아는 메시지가 아니면 nullopt다 (창이 자기 처리를 이어 본다).
    // 표면 세 종류가 같은 번역을 쓰므로 표면이 늘어도 갈래가 늘지 않는다.
    [[nodiscard]] std::optional<raw_input_event> translate_pointer_message(const pointer_message& message);

    // 이 메시지가 누른·뗀 버튼이다.
    // 버튼 메시지가 아니면 none이다.
    //  - 두 번째 누름(`WM_xBUTTONDBLCLK`)도 같은 누름으로 본다.
    //    더블 클릭 판정은 interaction controller의 몫이다.
    [[nodiscard]] pointer_button pointer_button_of(UINT message) noexcept;

    // 이름 붙은 키다.
    // 없으면 none이고 수정자 조합은 `modified_key_from_virtual`이 본다.
    [[nodiscard]] key_code named_key_from_virtual(WPARAM virtual_key) noexcept;

    // 수정자 조합의 키다.
    // 문자를 만드는 키(`character_key`)는 Ctrl·Alt가 눌린 동안에만 이벤트가 된다 —
    // 그냥 치는 글자는 문자 입력(`WM_CHAR`)으로 흘러야 그 글자를 칠 수 있다.
    // 영문자·숫자는 공통 이름 키로, 나머지는 platform 대역으로 나른다.
    //  - 어떤 키가 문자를 만드는지는 layout이 아는 값이라 창이 조회해 넘긴다.
    [[nodiscard]] key_code modified_key_from_virtual(WPARAM virtual_key, bool control, bool alt, bool character_key) noexcept;
} // namespace luil::win32
