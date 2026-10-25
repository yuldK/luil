#pragma once

#include "luil/ui/ui_events.h"

#include <cstdint>

namespace luil {
    // 플랫폼 키 값을 글자를 만드는 이름 키(Backspace·구두점·키패드)로 옮긴다 (keyboard-shortcut-design.md).
    // 이름이 없는 키는 none이다. 영문자·숫자·화살표 같은 다른 이름 키는 각 플랫폼의 표가 그대로 맡는다.
    //  - 플랫폼 header 없이 값으로 적어 두어 두 표를 한 test가 Windows에서 함께 잠근다.
    [[nodiscard]] key_code named_key_from_win32_virtual(std::uint32_t virtual_key) noexcept;
    [[nodiscard]] key_code named_key_from_android(std::int32_t key_code) noexcept;
} // namespace luil
