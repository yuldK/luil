#pragma once

// 기기 페이지다: 앱이 기기에 바라는 것(`ui_frame::device`)을 고른다.
// 고른 값은 앱 상태라 다른 페이지로 가도, 홈에 갔다 와도, 화면을 돌려도 그대로 frame에 실린다.
// 플랫폼이 하지 못하는 항목은 그 사실을 적고 고를 자리를 두지 않는다 (`ui_platform::capabilities`).

#include "luil/app/app_host.h"
#include "mobile_demo/common.h"

namespace mobile_demo {
    // --- 이 페이지의 메시지 ---
    struct orientation_intent
    {
        luil::screen_orientation orientation { luil::screen_orientation::system };
    };

    class device_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] page_content build(float width, float scale);

        // 셸이 frame에 싣는 요청이다.
        [[nodiscard]] const luil::device_request& request() const noexcept
        {
            return request_;
        }

    private:
        luil::device_request request_ {};
    };
} // namespace mobile_demo
