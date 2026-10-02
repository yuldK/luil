#pragma once

// 그룹 페이지다: 제목 있는 접이식 섹션과 선택 그룹(라디오·토글 묶음).

#include "luil/ui/ui_events.h"
#include "mobile_demo/common.h"

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    // 접이식 섹션의 목표 접힘 상태다.
    // 절대 상태를 실으면 factory 하나로 제목 줄 클릭과 보조 기술의
    // Expand·Collapse가 함께 선다 (뒤집기 메시지는 겹치면 도로 뒤집힌다).
    struct group_collapse_intent
    {
        bool collapsed { false };
    };

    struct coffee_intent
    {
        std::u8string value {};
    };

    struct view_intent
    {
        std::u8string value {};
    };

    class groups_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

    private:
        bool advanced_collapsed_ { false };
        std::u8string coffee_ { u8"mocha" };
        std::u8string view_ { u8"grid" };
    };
} // namespace mobile_demo
