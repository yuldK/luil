#pragma once

#include "luil/ui/ui_element.h"

#include <optional>
#include <string>
#include <vector>

namespace luil {
    // 앱 바의 치수다 (논리 픽셀, 모바일에서는 1이 1dp다).
    // 기본값은 Material의 작은 상단 앱 바를 따른다.
    struct app_bar_metrics
    {
        int height { 64 };
        // 아이콘 버튼 하나의 터치 영역이다. 손가락이 닿는 최소 크기라 아이콘보다 넓다.
        int button_size { 48 };
        int icon_size { 24 };
        // 바 양 끝에서 첫 버튼까지다.
        int edge_padding { 4 };
        // 선행 버튼이 없을 때 제목이 시작하는 자리다.
        int title_left_padding { 16 };
        // 선행 버튼과 제목, 제목과 동작 버튼 사이다.
        int title_gap { 4 };
        int title_font_size { 22 };
    };

    inline constexpr app_bar_metrics default_app_bar_metrics {};

    // 앱 바의 아이콘 버튼 하나다.
    struct app_bar_button
    {
        // codicon 코드포인트다.
        char32_t glyph { 0 };
        // 보조 기술이 읽는 이름이자 tooltip이다. 아이콘만으로는 뜻이 말해지지 않는다.
        std::u8string label {};
        ui_action action {};
    };

    // 앱이 정하는 앱 바의 내용이다.
    struct app_bar_config
    {
        std::u8string title {};
        // 제목 왼쪽의 버튼이다 (뒤로 가기, 메뉴 열기). 없으면 제목이 왼쪽 여백에서 시작한다.
        std::optional<app_bar_button> navigation {};
        // 오른쪽 끝부터 쌓이는 동작 버튼이다. 앞의 것이 왼쪽에 온다.
        std::vector<app_bar_button> actions {};
        app_bar_metrics metrics { default_app_bar_metrics };
    };

    // 모바일 화면 맨 위의 앱 바다 (제목, 선행 버튼, 동작 버튼).
    //
    // 데스크톱 caption(caption_element)의 모바일 짝이다. 창 버튼이 없고, 시스템이 그리는
    // 상태 표시줄 아래에 앱이 둔다. 무엇을 넣을지는 앱이 정하고, 어느 쪽을 쓸지는
    // `current_ui_platform()`을 보고 고른다 (docs/android-port-plan.md).
    //  - 버튼은 일반 button element다. 액션은 앱 메시지를 낸다 — 뒤로 가기도 앱의 화면 상태다.
    class app_bar_element final : public ui_element
    {
    public:
        explicit app_bar_element(app_bar_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 이 설정으로 앱 바가 차지할 높이다 (논리 픽셀).
        [[nodiscard]] static float height_for(const app_bar_config& config) noexcept;

    private:
        app_bar_config config_ {};
        // 설정에 없으면 nullptr다.
        ui_element* navigation_ { nullptr };
        // 설정의 `actions`와 같은 순서다.
        std::vector<ui_element*> actions_ {};
    };
} // namespace luil
