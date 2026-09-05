#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/common.h"
#include "luil/theme/appearance.h"

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct theme_intent
    {
        luil::theme_preference theme { luil::theme_preference::system };
    };

    struct accent_intent
    {
        std::u8string id {};
    };

    class theme_page
    {
    public:
        bool handle(const luil::app_message& message);
        [[nodiscard]] std::unique_ptr<luil::ui_element> build(float width, float height, float scale);

        // 셸이 frame에 싣는 유효 선호다.
        [[nodiscard]] const luil::appearance_settings& appearance() const noexcept
        {
            return appearance_;
        }

    private:
        luil::appearance_settings appearance_ {};
    };
} // namespace demo
