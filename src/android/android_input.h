#pragma once

#include "host/pointer_sequence.h"
#include "luil/ui/ui_events.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

namespace luil::android {
    // 한 표본의 포인터 하나다. 좌표는 창의 물리 픽셀이다.
    struct motion_pointer
    {
        std::int32_t id { 0 };
        // `AMOTION_EVENT_TOOL_TYPE_*`다.
        std::int32_t tool_type { 0 };
        float x { 0.0f };
        float y { 0.0f };
    };

    // 한 시각의 모든 포인터다.
    struct motion_frame
    {
        // `CLOCK_MONOTONIC` 기준 나노초다.
        std::int64_t time_ns { 0 };
        std::vector<motion_pointer> pointers {};
    };

    // Android 움직임 이벤트 하나에서 복사해 둔 값이다.
    // GameActivity의 구조체를 모르게 둔다 — 그래야 변환을 기기의 core test로 돌릴 수 있다.
    struct motion_input
    {
        // `AINPUT_SOURCE_*`다.
        std::int32_t source { 0 };
        // `AMOTION_EVENT_ACTION_*`와 포인터 번호를 그대로 담는다.
        std::int32_t action { 0 };
        std::int32_t flags { 0 };
        // `ACTION_BUTTON_PRESS`·`RELEASE`에서 바뀐 버튼이다 (`AMOTION_EVENT_BUTTON_*`).
        std::int32_t action_button { 0 };
        std::int32_t button_state { 0 };
        // `AMETA_*`다.
        std::int32_t meta_state { 0 };
        // `ACTION_SCROLL`의 휠 값이다. 한 눈금이 1이다.
        float vertical_scroll { 0.0f };
        float horizontal_scroll { 0.0f };
        // 한 이벤트로 묶여 온 과거 표본이다. 오래된 것이 먼저다.
        std::vector<motion_frame> history {};
        motion_frame current {};
    };

    // Android 키 이벤트 하나에서 복사해 둔 값이다.
    struct key_input
    {
        // `AKEY_EVENT_ACTION_*`다.
        std::int32_t action { 0 };
        // `AKEYCODE_*`다.
        std::int32_t key_code { 0 };
        std::int32_t meta_state { 0 };
        std::int32_t repeat_count { 0 };
        // 키가 만드는 글자다 (`KeyEvent.getUnicodeChar`). 없으면 0이다.
        std::int32_t unicode_char { 0 };
        std::int64_t time_ns { 0 };
    };

    // 창 좌표를 tree 좌표로 옮기는 값이다.
    // tree는 안전 영역(시스템 막대·컷아웃)을 뺀 자리에서 그려지므로 그만큼 뺀다.
    struct surface_mapping
    {
        float origin_x { 0.0f };
        float origin_y { 0.0f };
        // 물리 픽셀 / 논리 픽셀 (density / 160)이다.
        float scale { 1.0f };
    };

    // 앱이 지금 시스템에게서 가져오기로 한 키다. 키 필터가 Java thread에서 읽는 값이다.
    struct key_claims
    {
        // popup이 떠 있다 — 뒤로 가기가 Esc처럼 popup을 닫는다.
        bool popups_visible { false };
        // 앱이 뒤로 가기를 받겠다고 했다 (`ui_frame::back`).
        bool back_claimed { false };
        // 앱이 볼륨 올림·내림을 받겠다고 했고 보이는 중이다 (`device_request::volume_keys`).
        bool volume_keys_claimed { false };
    };

    // 키 하나를 앱이 받을지(참), Activity의 기본 처리에 남길지(거짓)다 (GameActivity의 키 필터).
    //  - 뒤로 가기: popup이 떠 있거나 앱이 받겠다고 했을 때만 앱의 것이다. 아니면 Activity가 앱을 끝낸다.
    //  - 볼륨 올림·내림: 앱이 받겠다고 했을 때만 앱의 것이다. 아니면 시스템이 소리 크기를 바꾼다.
    //  - 홈·음소거·카메라·전원·앱 전환·미디어 키는 언제나 시스템의 것이다. 앱이 먹으면 소리도 못 끈다.
    //  - 그 밖의 키는 앱의 것이다.
    [[nodiscard]] bool app_takes_key(std::int32_t key_code, const key_claims& claims) noexcept;

    // Android 이벤트 시각을 공통 단조 시계로 옮긴다.
    // libc++의 `steady_clock`은 `CLOCK_MONOTONIC`이라 값을 그대로 옮긴다
    // (기기 test가 이 사실을 고정한다).
    [[nodiscard]] std::chrono::steady_clock::time_point event_time(std::int64_t nanoseconds) noexcept;

    // Android 입력을 luil 입력 이벤트로 옮긴다 (touch-pen-input-design.md).
    //  - 손가락·펜은 Win32와 같은 접촉 추적기(`pointer_sequence_tracker`)를 거친다. 배럴
    //    버튼은 우클릭, 지우개 끝은 삼키기, 펜 호버는 접촉 없는 이동이다.
    //  - 마우스는 버튼 동작(`ACTION_BUTTON_PRESS`·`RELEASE`)으로 누름을 만들고 휠은 120 단위로
    //    옮긴다.
    //  - 창도 시계도 모른다. UI thread에서만 쓴다.
    class input_translator
    {
    public:
        [[nodiscard]] std::vector<raw_input_event> translate(const motion_input& input, const surface_mapping& mapping);
        [[nodiscard]] std::vector<raw_input_event> translate(const key_input& input);
        using dead_key_combiner = std::function<char32_t(char32_t, char32_t)>;
        void set_dead_key_combiner(dead_key_combiner combine);
        // 초점을 가진 element가 다음 키를 모두 받는 중이다 (`interaction_snapshot::key_capture`).
        // 그동안은 글자 키도 수정자 없이 키로 보내고 글자는 보내지 않는다.
        void set_key_capture(bool capture) noexcept
        {
            key_capture_ = capture;
        }
        // 창이 사라진다. 진행 중인 접촉을 정상적인 뗌 없이 끝내고 마우스 hover를 거둔다.
        [[nodiscard]] std::vector<raw_input_event> cancel_all(std::chrono::steady_clock::time_point time);

    private:
        void accept(const pointer_sample& sample, std::vector<raw_input_event>& events);
        void translate_mouse(const motion_input& input, const surface_mapping& mapping, std::vector<raw_input_event>& events);

        pointer_sequence_tracker tracker_ {};
        // 접촉 중인 손가락·펜의 id다. 창이 사라질 때 모두 거두려고 든다.
        std::vector<std::uint32_t> contacts_ {};
        // 눌려 있는 마우스 버튼이다.
        bool mouse_left_ { false };
        bool mouse_right_ { false };
        bool mouse_inside_ { false };
        char32_t pending_accent_ {};
        dead_key_combiner combine_dead_key_ {};
        bool key_capture_ { false };
    };
}
