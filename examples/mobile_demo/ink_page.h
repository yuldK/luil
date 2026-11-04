#pragma once

#include "demo/ink_sketch.h"
#include "mobile_demo/common.h"

#include <memory>

namespace mobile_demo {
    // 펜 잉크 캔버스다 (ink-design.md). 스타일러스로 그리고, 지우개 끝이나 지우개 도구로 획을 통째로 지운다.
    // 손가락은 켜기 전까지 그리지 않는다. 상태 줄 셋(`ink-count`·`ink-tool`·`ink-last`)을 화면과 test가 되읽는다.
    class ink_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);
        [[nodiscard]] const demo::ink_sketch& sketch() const noexcept;

    private:
        demo::ink_sketch sketch_ {};
    };
}
