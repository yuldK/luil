#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/common.h"
#include "luil/theme/appearance.h"
#include "luil/win32/win32_window.h"

#include <optional>

namespace demo {
    // --- 이 페이지의 메시지 ---
    struct theme_scroll_intent
    {
        float delta { 0.0f };
    };

    struct theme_intent
    {
        luil::theme_preference theme { luil::theme_preference::system };
    };

    struct accent_intent
    {
        std::u8string id {};
    };

    // 주 창의 배경 효과를 고른다. 셸이 frame에 싣는다 (`ui_frame::backdrop`).
    struct backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    // 라이브러리가 알린 배경 효과의 상태다 (`window_delegate::make_backdrop_message`).
    struct backdrop_report_intent
    {
        luil::win32::backdrop_report report {};
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

        // 셸이 frame에 싣는 배경 효과다.
        [[nodiscard]] luil::window_backdrop backdrop() const noexcept
        {
            return backdrop_;
        }

        // 배경 효과를 고르고 시작한다 (`--backdrop=`).
        void set_backdrop(const luil::window_backdrop backdrop) noexcept
        {
            backdrop_ = backdrop;
        }

    private:
        luil::appearance_settings appearance_ {};
        luil::window_backdrop backdrop_ { luil::window_backdrop::none };
        // 마지막으로 알린 상태다. 알림이 오기 전에는 비어 있다.
        std::optional<luil::win32::backdrop_report> report_ {};
        float scroll_ { 0.0f };
    };
} // namespace demo
