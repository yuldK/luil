#pragma once

// 차트 페이지다 (docs/chart-design.md의 "확인"). 차트 하나(owner `sales`)와 종류 고르기(`kind`), 큰 자료 켜기(`large`)를 두고,
// 고른 점과 접근 요약을 label(`state`, `summary`)에 그려 화면과 UIA Name으로 되읽는다.
//  - 세로로 흘리는 페이지 안의 차트다. 탭이 점을 고르고, 축에 맞는 빠른 쓸기는 페이지를 흘리고, 오래 눌렀다 옮기면 값을 훑는다.
//  - **이 헤더는 차트 헤더를 들이지 않는다.** 페이지 키를 얻으려고 이 헤더를 들이는 `page_list.cpp`가 차트를 링크하지 않는 core test에도
//    컴파일된다. 차트 include는 견본의 .cpp에만 둔다.

#include "demo/chart_sample.h"
#include "mobile_demo/common.h"

#include <string_view>

namespace mobile_demo {
    // 이 페이지의 키다. 공용 등록부(common.h)를 고치지 않으려고 페이지가 든다.
    inline constexpr std::u8string_view page_chart { u8"chart" };

    class chart_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

    private:
        demo::chart_sample sample_ {};
    };
}
