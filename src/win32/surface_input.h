#pragma once

#include "host/pointer_sequence.h"
#include "luil/ui/ui_events.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

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
        // 그 메시지를 꺼낸 순간 Alt가 눌려 있었는가다 (`GetKeyState(VK_MENU)`).
        // Ctrl·Shift는 wparam(`MK_CONTROL`·`MK_SHIFT`)이 싣지만 Alt는 싣지 않아 창이 조회해 넣는다.
        bool alt { false };
        float scale { 1.0f };
    };

    // 포인터·휠 메시지를 입력 이벤트로 옮긴다.
    // 아는 메시지가 아니면 nullopt다 (창이 자기 처리를 이어 본다).
    // 표면 세 종류가 같은 번역을 쓰므로 표면이 늘어도 갈래가 늘지 않는다.
    [[nodiscard]] std::optional<raw_input_event> translate_pointer_message(const pointer_message& message);

    // 터치·펜 `WM_POINTER*` 메시지의 접촉 단계다.
    // 추적기(host/pointer_sequence.h)가 쓰지 않는 메시지(`WM_POINTERENTER` 등)는 nullopt다.
    [[nodiscard]] std::optional<pointer_phase> pointer_phase_of(UINT message) noexcept;

    // 웹뷰로 넘길 터치·펜 원본이다.
    // 일반 컨트롤 이벤트로 줄이기 **전에** 복사한다 — 펜 종류·버튼·지우개·필압을
    // 웹 콘텐츠가 자기 동작으로 쓰게 한다. 받은 메시지 안에서만 쓰이고 thread를 넘지 않는다.
    struct webview_pointer_input
    {
        UINT message { 0 };
        POINTER_INFO info {};
        // `info.pointerType`이 펜이면 pen, 터치면 touch가 유효하다.
        POINTER_PEN_INFO pen {};
        POINTER_TOUCH_INFO touch {};
        RECT device_rect {};
        RECT display_rect {};
        // 포인터의 그 표면 client 자리다 (물리 픽셀).
        POINT client {};
        // 화면 좌표에 더하면 client 좌표가 되는 이동량이다.
        // 접촉 사각형처럼 화면 좌표로 온 다른 값도 같은 식으로 옮긴다.
        POINT screen_to_client {};
    };

    // OS가 기록한 입력 시각을 공통 단조 시계(`steady_clock`)로 옮긴다.
    // 메시지 큐에서 기다린 시간을 접촉 시간으로 오인하지 않게 한다.
    //  - 성능 카운터 값이 있으면 그것을 쓴다. 0(장치가 주지 않음)이거나 지금보다
    //    뒤면 `now`다.
    [[nodiscard]] std::chrono::steady_clock::time_point pointer_counter_time(
        std::uint64_t message_count, std::uint64_t now_count, std::uint64_t frequency, std::chrono::steady_clock::time_point now) noexcept;
    // 32비트 밀리초 tick(`POINTER_INFO::dwTime`)은 49.7일마다 돈다. 부호 없는 뺄셈이
    // 그 경계를 넘어 나이를 잰다. 지금보다 뒤인 값은 `now`다.
    [[nodiscard]] std::chrono::steady_clock::time_point pointer_tick_time(std::uint32_t message_tick, std::uint32_t now_tick, std::chrono::steady_clock::time_point now) noexcept;

    // `POINTER_PEN_INFO`에서 옮긴 펜 값이다 (`pointer_sample`의 같은 이름 칸에 들어간다).
    struct pen_values
    {
        std::optional<float> pressure {};
        float tilt_x { 0.0f };
        float tilt_y { 0.0f };

        [[nodiscard]] bool operator==(const pen_values&) const noexcept = default;
    };

    // 마스크를 보고 압력과 기울기를 옮긴다 (ink-input-design.md).
    //  - 압력은 0~1024를 0~1로 옮긴다. `PEN_MASK_PRESSURE`가 없으면 비어 있다 (합성 입력·원격 연결).
    //  - 기울기는 `tiltX`·`tiltY`(도)를 그대로 옮긴다. 마스크가 없으면 0이다. 펜 축 회전(`rotation`)은 옮기지 않는다.
    [[nodiscard]] pen_values pen_values_of(const POINTER_PEN_INFO& pen) noexcept;

    // `GetPointerPenInfoHistory`의 기록 중 현재 표본 앞에 보낼 것의 자리다. 오래된 것부터다.
    //  - 기록은 최근 것이 앞이고 0번이 현재 메시지의 표본이다. 0번은 현재 표본으로 따로 나가므로 넣지 않는다.
    //  - `last_frame`은 그 포인터로 마지막에 보낸 `frameId`다. 그보다 이르거나 같은 표본은 이미 보냈다. 비어 있으면 거르지 않는다.
    //  - frameId는 32비트에서 돈다. 차이를 부호 있는 값으로 보아 경계를 넘어도 순서가 맞다.
    [[nodiscard]] std::vector<std::size_t> pen_history_order(std::span<const POINTER_PEN_INFO> entries, std::optional<std::uint32_t> last_frame);

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

    // Alt를 홀로 눌렀다 뗐는지 지켜본다 (docs/menu-bar-design.md의 Win32 절). 창도 시계도 모른다.
    //  - 무장: Alt(`VK_MENU`)의 첫 누름이고, Ctrl이 함께가 아니고(AltGr), 막대가 키를 받을 수 있을 때다 (`eligible`).
    //    누르고 있는 동안의 자동 반복(bit 30)은 무장도 풀기도 하지 않는다.
    //  - 풀기: 다른 키의 누름, 마우스 누름, 창의 비활성화, 전역 단축키 눌림(`cancel`). 전역 단축키로 건 Alt 조합은
    //    OS가 글자 키를 가져가 창에 Alt의 누름과 뗌만 온다. 풀지 않으면 Alt 단독으로 읽힌다.
    //  - 뗌: 무장한 채 Alt를 떼면 참이다 (`key_code::alt_tap`을 보낸다). 어느 쪽이든 무장은 풀린다.
    class alt_tap_tracker
    {
    public:
        // `WM_SYSKEYDOWN`·`WM_KEYDOWN` 하나다. `repeat`는 lparam의 bit 30이다.
        void key_down(WPARAM virtual_key, bool repeat, bool control, bool eligible) noexcept;
        // `WM_SYSKEYUP`·`WM_KEYUP` 하나다. Alt 단독이 끝났으면 참이다.
        [[nodiscard]] bool key_up(WPARAM virtual_key) noexcept;
        void cancel() noexcept;

        [[nodiscard]] bool armed() const noexcept
        {
            return armed_;
        }

    private:
        bool armed_ { false };
    };

    // 시스템 키 메시지(`WM_SYSKEYUP`·`WM_SYSCHAR`)를 `DefWindowProc`에 넘기지 않고 삼킬지다.
    //  - 키 잡기 중(`capturing`)이면 둘 다 삼킨다. Alt를 떼면 메뉴 모드(SC_KEYMENU)로 들어가 다음 키를 OS가 가져간다.
    //  - 막대가 있으면(`menu_bar`) Alt·F10의 뗌을 삼켜 메뉴 모드를 막고, Alt+글자의 `WM_SYSCHAR`도 삼켜 메뉴 없는 창의
    //    경고음을 막는다. Alt+Space(시스템 메뉴)의 `WM_SYSCHAR`는 넘긴다.
    //  - 그 밖에는 지금처럼 OS의 것이다.
    [[nodiscard]] bool swallow_system_key(UINT message, WPARAM word_parameter, bool menu_bar, bool capturing) noexcept;
}
