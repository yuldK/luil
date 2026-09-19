#pragma once

#include "luil/ui/ui_element.h"

#include <functional>

namespace luil {
    // 색은 theme이 그리기 시점에 정해지므로 구체 색이 아니라 역할만 담는다.
    enum class button_visual_role
    {
        toolbar,
        caption,
        caption_close,
        // 되돌릴 수 없는 동작이다 (지우기·초기화).
        // 쉬는 동안에도 글리프가 오류색이라는 점에서 `caption_close`와 갈린다 —
        // 창의 닫기(×)는 자리와 모양이 이미 뜻을 말해 hover에서만 붉어지면 되지만,
        // 도구 막대의 아이콘 하나는 눌러 보기 전에 무엇이 사라지는지 말하지 않는다.
        danger,
    };

    // 켜진 토글이 깔고 앉는 옅은 강조 바탕은 팔레트의 `active_toggle_background`다
    // (양은 `accent_tones::active_toggle`). 전에는 이 element의 상수였다.

    struct button_config
    {
        char32_t glyph { 0 };
        // 0이 아니면 최대화된 창에서 이 글리프를 대신 그린다 (복원 아이콘).
        char32_t maximized_glyph { 0 };
        float icon_size { 14.0f };
        float corner_radius { 2.0f };
        button_visual_role role { button_visual_role::toolbar };
        // 토글 버튼이 켜진 상태다.
        // hover가 아니어도 배경과 강조색으로 표시한다.
        bool active { false };
        // 팔레트에서 이 자리의 색을 직접 고른다.
        // 비어 있으면 `role`이 정한 색 그대로다 — 그것이 다섯 줄 모두의 기본값이고,
        // 지금까지의 아이콘 버튼이 전부 그 특수 경우다.
        //
        // **왜 색이 아니라 선택자인가.** 앱이 `ui_color`를 건네는 순간 그 색은 테마를
        // 따라오지 못한다. dark에서 고른 회색이 light에서 그대로 남고, 고대비에서는
        // 사용자가 OS에서 고른 색을 이 버튼 하나만 무시한다 — 접근성 설정을 element가
        // 뒤엎는 자리가 생기는 것이다. 선택자는 "무엇을 고를지"만 담고 고르는 시점은
        // 여전히 그리기라 그 일이 일어나지 않는다. 라이브러리에서 같은 자리를 열어 둔
        // 곳은 `panel_config::background` 하나뿐이고 이 다섯도 그 규칙을 그대로 따른다
        // (concepts/theming.md의 "선택자" 절).
        //
        // **역할이 먼저다.** 버튼이 무엇을 뜻하는지는 여전히 `role`이 말한다. 선택자는
        // 어떤 역할로도 이름 붙일 수 없는 모양이 필요할 때의 escape hatch이지 역할의
        // 대체가 아니다 — 같은 선택자가 앱마다 되풀이되면 그것은 팔레트에 역할이 하나
        // 빠져 있다는 신호이고, 답은 선택자를 늘리는 것이 아니라 역할을 세우는 것이다.
        std::function<ui_color(const ui_color_palette&)> foreground {};
        std::function<ui_color(const ui_color_palette&)> hover_background {};
        std::function<ui_color(const ui_color_palette&)> hover_foreground {};
        std::function<ui_color(const ui_color_palette&)> pressed_background {};
        // 쉬는 동안의 바탕이다.
        // 어느 역할도 이 자리를 채우지 않는다 — 아이콘 버튼은 hover에서만 바탕이 서고,
        // 비어 있으면 지금처럼 아무것도 깔지 않는다. 다만 `active`가 켜져 있으면 그 자리를
        // 켜진 토글의 옅은 강조 바탕이 먼저 채우고, 선택자는 그 위에 얹힌다.
        std::function<ui_color(const ui_color_palette&)> rest_background {};
    };

    // 한 버튼이 상태별로 쓰는 색이다.
    // `rest_background`와 `foreground`는 hover도 눌림도 아닌 동안의 색이다.
    struct button_colors
    {
        // 0이면 쉬는 동안 아무것도 깔지 않는다 — 없는 것은 그리지 않는다.
        ui_color rest_background { 0 };
        ui_color foreground { 0 };
        ui_color hover_background { 0 };
        ui_color hover_foreground { 0 };
        ui_color pressed_background { 0 };

        [[nodiscard]] bool operator==(const button_colors&) const noexcept = default;
    };

    // 설정과 팔레트만으로 버튼의 색을 정하는 순수 함수다.
    // 창도 픽셀도 없이 test가 이 판정을 잠근다 — `text_button_fill_for`와 같은 자리다.
    // 그리기 안에 숨어 있던 동안에는 역할 하나를 더할 때마다 확인할 길이 raster뿐이었다.
    //  - `active`는 **쉬는 동안의 두 색**으로만 나타난다 (바탕과 글리프). hover·눌림은
    //    켜진 토글에서도 자기 색으로 답한다 — 누르는 동안 상태 표시가 반응을 덮으면
    //    버튼이 눌린 것인지 알 수 없다.
    //  - 선택자는 마지막에 얹는다. `active`가 정한 색도 선택자가 있으면 그것이 이긴다 —
    //    앱이 이름 지어 고른 색보다 라이브러리의 기본값이 세면 escape hatch가 아니다.
    //  - 비활성 흐림은 여기에 없다. 그것은 색이 아니라 팔레트 역할 하나(`disabled_foreground`)로
    //    그리는 쪽에서 갈린다 — element마다 알파를 발명하지 않는다.
    [[nodiscard]] button_colors button_colors_for(const button_config& config, const ui_color_palette& palette);

    // 아이콘 버튼이다.
    // hover·눌림 강조와 비활성 흐림을 일관되게 그린다.
    // 클릭 액션과 tooltip은 기반 클래스 API로 등록한다.
    class button_element final : public ui_element
    {
    public:
        // 설정이 선택자(`std::function`)를 담으므로 옮겨 받는다.
        //  - 그래서 `noexcept`가 아니다. 빈 약속을 걸어 두면 복사 한 번이 곧 종료다.
        button_element(ui_element_id id, button_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        button_config config_ {};
    };
} // namespace luil
