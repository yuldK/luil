#pragma once

#include "luil/ui/ui_events.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 터치·펜 접촉 하나가 지금 어느 단계인가다.
    // 플랫폼이 자기 이벤트에서 옮겨 넣는다 (Win32는 `WM_POINTERDOWN`·`UPDATE`·`UP`·`LEAVE`).
    enum class pointer_phase
    {
        // 접촉이 시작됐다.
        down,
        // 접촉 중 이동이거나, 펜이 닿지 않은 채 움직였다 (hover).
        update,
        // 접촉이 끝났다. `canceled`면 정상적인 뗌이 아니다.
        up,
        // 펜이 감지 범위를 벗어났거나 표면을 떠났다.
        leave,
    };

    // 터치·펜 이벤트 하나에서 UI thread가 복사해 둔 값이다.
    // 플랫폼의 이벤트 구조체는 받은 thread와 지금 이벤트에만 매인 값이라, 받는 즉시 여기로
    // 옮기고 나중에 다시 묻지 않는다 (touch-pen-input-design.md).
    //  - 좌표는 그 표면의 client 물리 픽셀, 시각은 공통 단조 시계로 옮긴 값이다.
    struct pointer_sample
    {
        pointer_phase phase { pointer_phase::update };
        pointer_device device { pointer_device::touch };
        std::uint32_t pointer_id { 0 };
        // 지금 닿아 있는가다.
        bool in_contact { false };
        // 시스템이 접촉을 거두었다. 정상적인 뗌이 아니다.
        bool canceled { false };
        // 펜 배럴 버튼이다. 접촉 중이면 우클릭이다.
        bool barrel { false };
        // 펜의 지우개 끝이다. 표식을 단 이벤트로 나가고, 일반 컨트롤은 controller가 삼킨다 (ink-input-design.md).
        bool eraser { false };
        // 누름이 발생한 순간 Shift가 눌려 있었는가다 (Win32는 POINTER_MOD_SHIFT).
        // 펜도 Shift+선택을 지원한다.
        bool shift { false };
        // 같은 순간의 Ctrl·Alt다 (Win32는 POINTER_MOD_CTRL과 `GetKeyState(VK_MENU)`, Android는 meta state).
        // 하드웨어 키보드가 없으면 거짓이다. 누름·이동·뗌이 모두 싣는다 (`ui_action_context`).
        bool control { false };
        bool alt { false };
        float x { 0.0f };
        float y { 0.0f };
        // 표면의 물리 / 논리 배율이다.
        float scale { 1.0f };
        std::chrono::steady_clock::time_point time {};
        std::u8string surface {};
        // 펜의 압력이다 (플랫폼 값을 0~1로 옮긴 것). 장치가 알리지 않으면 비어 있다. 추적기가 0~1로 접는다.
        std::optional<float> pressure {};
        // 펜의 기울기다 (도, W3C의 tiltX·tiltY). 모르면 0이다.
        float tilt_x { 0.0f };
        float tilt_y { 0.0f };
    };

    // 표면 하나가 소비하기로 한 터치·펜 시퀀스들을 입력 이벤트로 옮긴다.
    //
    // 접촉마다 지금 버튼을 기억한다 — 접촉 중에 배럴 버튼이 바뀌면 이전 버튼의
    // 누름을 **취소**하고(`button_switch`) 새 버튼으로 다시 누른다. 좌클릭과 우클릭이 함께 나가지 않는다.
    //  - 펜 표본이면 이동·누름·뗌에 `pen`을 채운다. 지우개인지는 누를 때 정하고 접촉 동안 바꾸지 않는다.
    //    지우개 접촉에는 배럴 전환이 없다.
    //  - 창도 시계도 모르므로 test가 결정적이다. 플랫폼마다 다시 쓰지 않는다.
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
}
