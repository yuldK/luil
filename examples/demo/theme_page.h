#pragma once

// 테마 페이지다: 테마 선호 셋과 키 컬러 카탈로그.
// 고른 값은 frame에 실려 가고 실제 팔레트 선택은 UI thread가 한다.

#include "demo/common.h"
#include "luil/theme/appearance.h"
#include "luil/win32/win32_window.h"

#include <memory>
#include <optional>
#include <vector>

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
    // 주 창과 견본 창이 같은 메시지로 온다. 어느 표면인지는 `report.surface`다.
    struct backdrop_report_intent
    {
        luil::win32::backdrop_report report {};
    };

    // 견본 보조 창·견본 메뉴의 배경 효과를 고른다 (`ui_window::backdrop`·`ui_popup::backdrop`).
    struct sample_window_backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    struct sample_popup_backdrop_intent
    {
        luil::window_backdrop backdrop { luil::window_backdrop::none };
    };

    struct sample_window_toggle_intent
    {
        bool open { false };
    };

    struct sample_window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct sample_menu_toggle_intent
    {
        bool open { false };
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

        // 효과를 건 견본 보조 창이다. 셸이 frame의 windows 목록에 싣는다.
        [[nodiscard]] std::vector<luil::win32::ui_window> make_windows() const;
        // 효과를 건 견본 메뉴다. 이 페이지의 tree를 만든 뒤에 단다 — 메뉴 단추의 자리를 쓴다.
        [[nodiscard]] std::vector<luil::win32::ui_popup> make_popups(float scale) const;
        // 페이지를 떠날 때 견본 메뉴를 닫는다.
        void close_popups() noexcept
        {
            sample_menu_open_ = false;
        }

    private:
        [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_sample_tree() const;

        luil::appearance_settings appearance_ {};
        luil::window_backdrop backdrop_ { luil::window_backdrop::none };
        // 마지막으로 알린 상태다. 알림이 오기 전에는 비어 있다.
        std::optional<luil::win32::backdrop_report> report_ {};
        // 견본 창·견본 메뉴다. 견본 창의 상태 보고도 따로 든다 (popup은 알리지 않는다).
        luil::window_backdrop sample_window_backdrop_ { luil::window_backdrop::acrylic };
        luil::window_backdrop sample_popup_backdrop_ { luil::window_backdrop::acrylic };
        bool sample_window_open_ { false };
        bool sample_menu_open_ { false };
        sample_window_metrics_intent sample_metrics_ {};
        std::optional<luil::win32::backdrop_report> sample_report_ {};
        // 견본 메뉴가 붙는 단추다. 이번 frame의 tree가 소유하며 `build`가 다시 잡는다.
        const luil::ui_element* sample_menu_button_ { nullptr };
        float scroll_ { 0.0f };
    };
}
