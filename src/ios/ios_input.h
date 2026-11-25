#pragma once

#include <cstdint>
#include <optional>

// iPhone·iPad 하드웨어 키보드의 키 자리다 (`UIKey::keyCode`, USB HID 키보드 쪽의 사용 번호).
//  - UIKit을 모르는 순수 층이라 core test에서 돈다.
//  - 이름과 단축키의 규칙은 macOS와 같다 (iPad 키보드도 Command가 주 단축키다). 그래서 HID 자리를 Carbon 가상 키로 옮겨
//    macOS 변환(macos/macos_input.h의 `key_event_for`)을 그대로 쓴다.
namespace luil::ios {
    // HID 사용 번호를 Carbon 가상 키(`kVK_*`)로 옮긴다. macOS 자판에 자리가 없는 키(Caps Lock·수정자·미디어 키 등)는 없다.
    //  - Insert는 옛 확장 키보드의 Help 자리다 (macOS 변환이 그것을 Insert로 읽는다).
    //  - 키패드 Enter는 Carbon의 키패드 Enter다. 둘 다 `key_code::enter`가 된다.
    [[nodiscard]] std::optional<std::uint16_t> carbon_key_of_hid(std::uint32_t usage) noexcept;

    // 글을 입력하는 중에 iOS 글 입력 체계(`insertText:`·`deleteBackward`·조합)가 맡을 키인가.
    //  - 글자를 만드는 키(영문자·숫자·구두점·키패드·Space), Backspace, Return이다. 입력기(한글 등)의 조합이 이 키들로 선다.
    //  - 나머지(화살표·Tab·Esc·Home·End·기능 키, Delete)는 앱의 키다. Command·Control이 함께면 이 값과 상관없이 앱의 키다.
    [[nodiscard]] bool text_system_key(std::uint32_t usage) noexcept;
}
