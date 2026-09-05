#pragma once

#include "luil/ui/caption_metrics.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <string>

namespace luil {
    [[nodiscard]] ui_element_id caption_button_element_id(caption_button_hover hover) noexcept;

    // 앱이 정하는 caption의 겉모습이다.
    // 아이콘 글리프는 codicon 코드포인트이고 0이면 아이콘 없이 제목이 왼쪽 여백에서 시작한다.
    // 툴팁은 앱의 언어로 넣는다.
    struct caption_config
    {
        std::u8string title {};
        char32_t application_icon { 0 };
        std::u8string minimize_tooltip {};
        std::u8string maximize_tooltip {};
        std::u8string close_tooltip {};
        caption_ui_metrics metrics { default_caption_ui_metrics };
        // 둘 창 버튼이다. 없는 버튼은 만들지 않고 자리도 차지하지 않으며,
        // 그 창 스타일(`WS_MINIMIZEBOX`·`WS_MAXIMIZEBOX`)도 함께 빠진다.
        caption_buttons buttons { default_caption_buttons };
    };

    // custom caption 막대다.
    // 제목·앱 아이콘을 직접 그리고 창 버튼 3개를 일반 button element로 담는다.
    // 버튼 액션은 `ui_command`를 반환한다.
    // 창 끌기 영역 판정(WM_NCHITTEST)은 platform의 `caption_layout`이
    // 같은 metrics로 동기 계산하며, 두 계산의 일치는 test로 고정한다.
    class caption_element final : public ui_element
    {
    public:
        explicit caption_element(caption_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 이 설정으로 캡션이 차지할 높이다 (논리 픽셀).
        // 캡션 아래 배치를 계산하는 호출자가 쓴다 — 높이를 알아내려고
        // 캡션을 미리 arrange해 보는 우회가 필요 없다.
        [[nodiscard]] static float height_for(const caption_config& config) noexcept;

    private:
        // 만들어진 버튼 중 가장 왼쪽 것이다 (제목이 침범하지 않을 한계).
        // 하나도 없으면 nullptr다.
        [[nodiscard]] const ui_element* leftmost_button() const noexcept;

        caption_config config_ {};
        // 설정에서 빠진 버튼은 nullptr다.
        ui_element* minimize_ { nullptr };
        ui_element* maximize_ { nullptr };
        ui_element* close_ { nullptr };
    };

    // smoke 화면처럼 view snapshot 없이 caption만 그릴 때 쓰는 단독 tree다.
    [[nodiscard]] ui_tree make_caption_tree(float window_width, float scale, caption_config config);
} // namespace luil
