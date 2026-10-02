#pragma once

#include "luil/ui/ui_events.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <optional>
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
    };

    // 포인터·휠 메시지를 입력 이벤트로 옮긴다.
    // 아는 메시지가 아니면 nullopt다 (창이 자기 처리를 이어 본다).
    // 표면 세 종류가 같은 번역을 쓰므로 표면이 늘어도 갈래가 늘지 않는다.
    [[nodiscard]] std::optional<raw_input_event> translate_pointer_message(const pointer_message& message);

    // 터치·펜 `WM_POINTER*` 메시지 하나에서 UI thread가 복사해 둔 값이다.
    // `POINTER_INFO`는 받은 thread와 지금 메시지에만 매인 값이라, 받는 즉시 여기로
    // 옮기고 나중에 다시 묻지 않는다 (touch-pen-input-design.md).
    //  - 좌표는 그 표면의 client 물리 픽셀, 시각은 공통 단조 시계로 옮긴 값이다.
    struct pointer_sample
    {
        // WM_POINTERDOWN·UPDATE·UP·LEAVE 중 하나다.
        UINT message { 0 };
        pointer_device device { pointer_device::touch };
        std::uint32_t pointer_id { 0 };
        // POINTER_FLAG_INCONTACT다.
        bool in_contact { false };
        // POINTER_FLAG_CANCELED다. 정상적인 뗌이 아니다.
        bool canceled { false };
        // 펜 배럴 버튼이다. 접촉 중이면 우클릭이다.
        bool barrel { false };
        // 펜의 지우개 끝이다. 일반 컨트롤을 실행하지 않는다.
        bool eraser { false };
        float x { 0.0f };
        float y { 0.0f };
        // 표면의 물리 / 논리 배율이다.
        float scale { 1.0f };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
    };

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

    // 표면 하나가 소비하기로 한 터치·펜 시퀀스들을 입력 이벤트로 옮긴다.
    //
    // 접촉마다 지금 버튼을 기억한다 — 접촉 중에 배럴 버튼이 바뀌면 이전 버튼의
    // 누름을 **취소**하고 새 버튼으로 다시 누른다. 좌클릭과 우클릭이 함께 나가지 않는다.
    //  - 창도 시계도 모르므로 test가 결정적이다.
    class pointer_sequence_tracker
    {
    public:
        [[nodiscard]] std::vector<raw_input_event> accept(const pointer_sample& sample);
        // 그 포인터의 접촉을 정상적인 뗌 없이 끝낸다 (캡처 상실·조회 실패).
        // 진행 중인 접촉이 없으면 빈 목록이다.
        [[nodiscard]] std::vector<raw_input_event> cancel(std::uint32_t pointer_id, std::chrono::steady_clock::time_point time, const std::u8string& surface);
        // 이 포인터의 접촉을 소비 중인가.
        [[nodiscard]] bool in_contact(std::uint32_t pointer_id) const noexcept;

    private:
        struct contact
        {
            std::uint32_t pointer_id { 0 };
            pointer_device device { pointer_device::touch };
            pointer_button button { pointer_button::left };
            bool eraser { false };
        };

        [[nodiscard]] contact* find(std::uint32_t pointer_id) noexcept;
        void forget(std::uint32_t pointer_id) noexcept;

        std::vector<contact> contacts_ {};
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
