#pragma once

#include "host/frame_state.h"
#include "luil/ui/ui_element.h"

#include <windows.h>

#include <cstdint>

namespace luil::win32 {
    // 끄는 그림이 포인터에서 비켜 서는 거리다 (논리 픽셀). 창 안의 ghost와 같은 비킴이다.
    inline constexpr float drag_ghost_offset { 10.0f };

    // 포인터 자리에 대한 그림 창의 왼쪽 위다 (화면 물리 픽셀). 포인터의 오른쪽 아래로 비킨다.
    [[nodiscard]] POINT drag_ghost_position(POINT cursor, std::uint32_t dpi) noexcept;

    // 창 밖으로 끌고 나간 항목의 그림이다 (multi-window-design.md).
    // 화면 위를 포인터를 따라오는 투명한 최상위 창 하나다 — 탭을 창 밖에 놓으면 새 창이 된다는 것을 끄는 동안
    // 보인다. 끄는 동안 창이 실제로 따라오지는 않는다.
    //  - 입력을 받지 않고(`WS_EX_TRANSPARENT`) 활성이 되지 않는다(`WS_EX_NOACTIVATE`). 그래서 포인터 밑의 표면을
    //    찾는 OS hit test(`WindowFromPoint`)가 이 창을 지나친다 — 그림이 포인터 밑에 깔려도 놓을 자리가 가려지지
    //    않는다.
    //  - 그림은 그 점이 있는 모니터의 배율로 그린다. 배율·내용·외양이 바뀔 때만 다시 그리고, 나머지는 옮기기만 한다.
    //  - UI thread 전용이다.
    class drag_ghost_window
    {
    public:
        drag_ghost_window() = default;
        drag_ghost_window(const drag_ghost_window&) = delete;
        drag_ghost_window(drag_ghost_window&&) = delete;
        drag_ghost_window& operator=(const drag_ghost_window&) = delete;
        drag_ghost_window& operator=(drag_ghost_window&&) = delete;
        ~drag_ghost_window();

        // 그 포인터 자리에 그림을 세운다. `appearance`는 외양만 본다 (테마·키 컬러·고대비·스타일).
        // 창을 만들거나 그리지 못하면 조용히 없이 간다 — 끌기 자체는 그림 없이도 선다.
        void show(HINSTANCE instance, const drag_payload& payload, const frame_state& appearance, POINT cursor) noexcept;
        void hide() noexcept;
        void destroy() noexcept;

    private:
        [[nodiscard]] bool ensure_window(HINSTANCE instance) noexcept;
        // 그림을 다시 그려 창에 싣는다. 실패하면 거짓이다.
        [[nodiscard]] bool paint(const drag_payload& payload, const frame_state& appearance, std::uint32_t dpi, POINT position) noexcept;

        HWND window_ { nullptr };
        bool visible_ { false };
        // 마지막으로 그린 것이다. 같으면 옮기기만 한다.
        drag_payload painted_payload_ {};
        ui_color_palette painted_palette_ {};
        std::uint32_t painted_dpi_ { 0 };
        bool painted_ { false };
    };
} // namespace luil::win32
