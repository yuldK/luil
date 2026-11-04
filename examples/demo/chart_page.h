#pragma once

// 차트 페이지다 (docs/chart-design.md의 "확인"). 차트 하나(owner `chart/sales`)와 종류 고르기(`chart/kind`), 큰 자료 켜기(`chart/large`)를 두고,
// 고른 점과 접근 요약을 label(`chart/state`, `chart/summary`)에 그려 화면과 UIA Name으로 되읽는다.
//  - 차트는 선택 라이브러리다. 이 헤더는 차트 헤더를 들이지 않고 차트 include는 .cpp와 견본에만 둔다.

#include "demo/chart_sample.h"
#include "demo/common.h"

#include <memory>
#include <string_view>

namespace demo {
    inline constexpr std::u8string_view page_chart { u8"chart" };

    // 페이지 본문의 휠 스크롤이다.
    struct chart_scroll_intent
    {
        float delta { 0.0f };
    };

    class chart_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build();

    private:
        chart_sample sample_ {};
        float scroll_ { 0.0f };
    };
}
