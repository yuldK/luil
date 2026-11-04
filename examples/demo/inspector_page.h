#pragma once

#include "demo/common.h"
#include "demo/inspector_state.h"

#include "luil/ui/commands.h"
#include "luil/ui/ui_inspect.h"

#include <memory>

namespace demo {
    // 검사기를 켜는 단축키다 (palette_shortcut의 Ctrl+Shift+P와 겹치지 않는다).
    inline constexpr luil::key_chord inspector_shortcut { luil::key_code::key_i, true, true };

    // 실행 중 검사기 overlay다 (inspector-design.md). 켜면 마우스를 올린 요소의 윤곽과 정보 상자가 창 위에 얹히고, 단추로
    // 조상 단계와 선택을 옮긴다. 상태 줄 다섯(`inspector-state`·`inspector-pick`·`inspector-levels`·`inspector-selected`·
    // `inspector-count`)을 UIA와 화면으로 되읽는다.
    class inspector_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);
        // frame에 실을 설정이다 (`ui_frame::inspector`).
        [[nodiscard]] luil::inspector_config config() const;
        [[nodiscard]] const inspector_state& state() const noexcept;

    private:
        // 요소 수를 넣지 않고 한 번 짓는다. 글 길이가 달라도 요소 수는 같다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> assemble() const;
        [[nodiscard]] luil::ui_tree page_tree() const;

        inspector_state state_ { luil::inspector_pick::hovered };
        // 마지막으로 지은 크기다. 선택 이동이 같은 크기의 tree 위에서 움직인다.
        float width_ { 1000.0f };
        float height_ { 700.0f };
        float scale_ { 1.0f };
    };
}
