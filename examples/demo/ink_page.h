#pragma once

#include "demo/common.h"
#include "demo/ink_sketch.h"

#include <memory>

namespace demo {
    // 펜 잉크 캔버스다 (ink-design.md). 펜으로 그리고, 지우개 끝이나 지우개 도구로 획을 통째로 지운다.
    // 상태 줄 셋(`ink-count`·`ink-tool`·`ink-last`)이 UIA와 화면으로 되읽힌다.
    class ink_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);
        [[nodiscard]] const ink_sketch& sketch() const noexcept;

    private:
        ink_sketch sketch_ {};
    };
}
