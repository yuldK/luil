#pragma once

// 서식 있는 글 페이지다 (docs/rich-edit-design.md). 서식 칸 하나와 서식 토글 다섯, 상태 글 한 줄이다.
//  - 칸·토글·상태 글은 모바일 데모와 함께 쓰는 `rich_text_sample`이 짓는다.
//  - 상태 글(`rich-state`)이 켜짐·구간 수·링크 수·되돌리기 수를 그려 UIA Name으로 되읽는다.
//  - 서식 복사·붙여넣기 단추(`rich-copy`·`rich-paste`)와 교환 상태 글(`rich-clipboard`)이 있다 (docs/rich-edit-exchange-design.md).
//    클립보드 요청은 셸이 `take_clipboard_request`로 가져가 host에 넣는다.

#include "demo/common.h"
#include "demo/rich_text_sample.h"

#include <memory>
#include <optional>

namespace demo {
    struct rich_text_page_scroll_intent
    {
        float delta { 0.0f };
    };

    class rich_text_page
    {
    public:
        // 이 페이지의 메시지를 처리했으면 참이다. 편집·조합 메시지는 서식 칸의 대상일 때만 갖는다.
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        [[nodiscard]] const rich_text_sample& sample() const noexcept
        {
            return sample_;
        }

        [[nodiscard]] std::optional<luil::clipboard_request> take_clipboard_request()
        {
            return sample_.take_clipboard_request();
        }

    private:
        rich_text_sample sample_ {};
        std::optional<luil::text_composition_event> composition_ {};
        float scroll_ { 0.0f };
    };
}
