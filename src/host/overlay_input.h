#pragma once

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"

#include <cstdint>
#include <string>
#include <vector>

namespace luil {
    // 주 표면 위에 겹친 popup 하나의 입력 자리다.
    struct overlay_area
    {
        // popup id다. 이 layer의 이벤트에 표면 id로 실린다.
        std::u8string id {};
        // 주 tree 좌표의 자리다 (물리 픽셀).
        rect_f bounds {};
    };

    // 이벤트 하나를 layer로 보낸 결과다.
    struct routed_input
    {
        std::vector<raw_input_event> events {};
        // 어느 popup도 아닌 곳을 눌렀다 (`popup_dismiss_reason::pointer_press_outside`).
        bool pressed_outside { false };
        // 어느 popup도 아닌 곳에서 휠을 굴렸다 (`popup_dismiss_reason::wheel_scrolled`).
        bool wheel_outside { false };
    };

    // 창 하나에 popup을 겹쳐 그리는 플랫폼(모바일)이 포인터 입력을 layer로 보낸다.
    //
    // 데스크톱은 popup마다 창이 있어 OS가 이 일을 한다. 여기서는 같은 결과를 낸다: 이벤트에
    // popup의 표면 id와 popup 좌표를 싣고, controller가 `surface_tree_list`에서 그 tree를 찾는다
    // (popup-overlay-design.md).
    //  - 누름은 그 자리의 맨 위 layer로 간다. 그 접촉의 이동·뗌·취소는 밖으로 나가도 같은
    //    layer로 간다 (Win32의 암묵적 캡처와 같다).
    //  - 마우스·펜의 호버가 다른 layer로 옮겨 가면 앞 layer에 이탈을 낸다.
    //  - 키는 지나간다. 키는 논리 초점이 라우팅한다.
    //  - 창도 시계도 모른다. UI thread에서만 쓴다.
    class overlay_input_router
    {
    public:
        // 지금 frame의 popup 자리다. 뒤의 것이 위다.
        void set_areas(std::vector<overlay_area> areas);
        [[nodiscard]] routed_input route(raw_input_event event);
        [[nodiscard]] bool has_areas() const noexcept;

    private:
        struct capture
        {
            pointer_device device { pointer_device::mouse };
            std::uint32_t pointer_id { 0 };
            std::u8string surface {};
            float origin_x { 0.0f };
            float origin_y { 0.0f };
            // 같은 포인터로 눌린 버튼 수다 (마우스는 둘을 겹쳐 누를 수 있다).
            int buttons { 0 };
        };

        [[nodiscard]] const overlay_area* hit(float x, float y) const noexcept;
        [[nodiscard]] capture* find(pointer_device device, std::uint32_t pointer_id) noexcept;
        // 호버를 이 표면으로 옮긴다. 다른 표면에서 오면 앞 표면에 이탈을 낸다.
        void hover(pointer_device device, const std::u8string& surface, std::vector<raw_input_event>& events);

        std::vector<overlay_area> areas_ {};
        std::vector<capture> captures_ {};
        bool hovering_ { false };
        pointer_device hover_device_ { pointer_device::mouse };
        std::u8string hover_surface_ {};
    };
} // namespace luil
