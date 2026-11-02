#pragma once

#include "demo/inspector_state.h"
#include "mobile_demo/common.h"

#include "luil/ui/ui_inspect.h"

namespace mobile_demo {
    // 실행 중 검사기 overlay다 (inspector-design.md). 손가락만 있는 화면이라 집기는 탭한 칸(focused)과 누르는 동안(pressed),
    // 단추로 옮기는 선택(selected)이다. 상태 줄 다섯(`inspector-state`·`inspector-pick`·`inspector-levels`·`inspector-selected`·
    // `inspector-count`)을 화면과 test가 되읽는다. 위에서 아래로 흘려 보는 페이지다.
    class inspector_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);
        // frame에 실을 설정이다 (`ui_frame::inspector`).
        [[nodiscard]] luil::inspector_config config() const;
        [[nodiscard]] const demo::inspector_state& state() const noexcept;

    private:
        // 요소 수를 정해 두지 않고 한 번 짓는다. 글 길이가 달라도 요소 수는 같다.
        [[nodiscard]] page_content assemble() const;
        [[nodiscard]] luil::ui_tree page_tree() const;

        demo::inspector_state state_ { luil::inspector_pick::focused };
        // 마지막으로 지은 폭과 배율이다. 선택 이동이 같은 크기의 tree 위에서 움직인다.
        float width_ { 400.0f };
        float scale_ { 1.0f };
    };
}
