#pragma once

// 토스트 페이지다: 심각도별 토스트와 액션 버튼이 있는 계속 남는 토스트.
// 토스트 목록과 만료 판정은 여기(앱)가 소유하고, 다른 페이지도
// toast_request_intent로 토스트를 띄울 수 있다.

#include "mobile_demo/common.h"

#include <chrono>
#include <optional>
#include <vector>

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    // 계속 남는 토스트의 실행 취소·닫기다.
    struct toast_undo_intent
    {
        std::u8string id {};
    };

    class toasts_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 화면 오른쪽 아래에 쌓이는 토스트 오버레이다.
        // 어느 페이지가 보이든 창 전체 위에 얹는다. 토스트가 없으면 nullptr다.
        [[nodiscard]] std::unique_ptr<luil::ui_element> make_overlay();

        // 다음 토스트가 만료되는 시각이다 (만료될 것이 없으면 nullopt).
        // driver가 next_tick으로 이 값을 예고하면 메시지가 없어도
        // 그 시각에 tick → prune → frame 재게시가 돈다.
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_expiry() const;

        // 끝난 토스트를 목록에서 지운다 (만료 판정은 앱 몫).
        // driver의 tick이 부른다.
        void prune();

    private:
        struct toast_entry
        {
            std::u8string id {};
            std::u8string text {};
            luil::toast_severity severity { luil::toast_severity::info };
            std::chrono::steady_clock::time_point shown_at {};
            std::chrono::milliseconds duration { 0 };
        };

        void push(std::u8string text, luil::toast_severity severity, std::chrono::milliseconds duration);

        std::vector<toast_entry> toasts_ {};
        int next_id_ { 0 };
    };
} // namespace mobile_demo
