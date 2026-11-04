#pragma once

#include "luil/ui/ui_events.h"

#include <cstdint>

namespace luil {
    // 플랫폼 키 값을 글자를 만드는 이름 키(Backspace·구두점·키패드)로 옮긴다 (keyboard-shortcut-design.md).
    // 이름이 없는 키는 none이다. 영문자·숫자·화살표 같은 다른 이름 키는 각 플랫폼의 표가 그대로 맡는다.
    //  - 플랫폼 header 없이 값으로 적어 두어 두 표를 한 test가 Windows에서 함께 잠근다.
    [[nodiscard]] key_code named_key_from_win32_virtual(std::uint32_t virtual_key) noexcept;
    [[nodiscard]] key_code named_key_from_android(std::int32_t key_code) noexcept;

    // `key_code`를 Win32 가상 키로 되돌린다 (global-hotkey-design.md). 앞의 표와 Win32 표면의 `named_key_from_virtual`,
    // `platform_key_of`를 거꾸로 읽은 것이다. 걸 수 없는 키는 0이다.
    //  - 0인 키: `none`, 글자에서 짓는 `zoom_in`·`zoom_out`, 휴대폰의 `volume_up`·`volume_down`, 이름 표에 없는 값,
    //    수정자 가상 키(0x10~0x12, 0x5B·0x5C, 0xA0~0xA5)와 1~0xFE 밖의 플랫폼 대역 값.
    //  - 값이 VK라 자판 배열이 바뀌어도 앱 안 키 이벤트와 같은 키를 가리킨다.
    [[nodiscard]] std::uint32_t win32_virtual_from_key(key_code key) noexcept;
}
