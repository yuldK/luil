#pragma once

#include "luil/ui/caption_metrics.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <string>
#include <vector>

namespace luil {
    [[nodiscard]] ui_element_id caption_button_element_id(caption_button_hover hover) noexcept;

    // caption 안에 앱이 두는 단추다 (설정 같은 창 전체의 명령).
    //
    // **끌기와 갈린다.** 창 버튼이 아닌 caption의 빈 자리는 창을 끄는 자리인데, 이 단추의 자리는
    // 비클라이언트 판정이 client로 답해 평소의 단추처럼 눌린다 (caption-button-design.md).
    //  - 판정은 게시된 tree의 이 단추를 본다. 그래서 단추를 더하거나 빼거나 비활성으로 두는 것이
    //    다음 frame부터 그대로 판정에 든다 — 창을 만들 때의 설정에 묶이지 않는다.
    //  - 창 버튼과 달리 Tab의 자리다. 창 버튼은 Alt+Space의 시스템 메뉴가 키보드의 길이지만 이 단추에는
    //    다른 길이 없다.
    struct caption_app_button
    {
        // 같은 caption 안에서 단추를 가르는 앱 정의 키다 (`ui_element_id::owner`).
        std::u8string key {};
        // codicon 코드포인트다.
        char32_t glyph { 0 };
        // tooltip이자 **보조 기술이 읽는 이름**이다. 글리프뿐인 단추라 이름의 원천이 이것뿐이다.
        std::u8string tooltip {};
        // 누르면 낼 액션이다. 없으면 단추를 만들지 않는다 — 없는 것은 두지 않는다.
        ui_action action {};
        bool enabled { true };
    };

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
        // 창 버튼 바로 왼쪽에 서는 앱 단추다. 목록 차례대로 왼쪽에서 오른쪽으로 서고,
        // 폭은 창 버튼과 같다 (`caption_ui_metrics::button_width`). 제목은 그 앞에서 잘린다.
        std::vector<caption_app_button> app_buttons {};
    };

    // custom caption 막대다.
    // 제목·앱 아이콘을 직접 그리고 창 버튼 3개를 일반 button element로 담는다.
    // 버튼 액션은 `ui_command`를 반환한다.
    // 창 끌기 영역 판정(WM_NCHITTEST)은 platform의 `caption_layout`이
    // 같은 metrics로 동기 계산하며, 두 계산의 일치는 test로 고정한다.
    //
    // **자리는 앱의 배치가 정한다.** 창 아래에 두든, 아래에서 위로 밀어 올리든, 창 일부만 덮든
    // arrange에 준 slot이 곧 caption이다. 비클라이언트 판정은 게시된 tree에서 이 element가 선 자리를
    // 읽어 끌기 띠·창 버튼·시스템 메뉴 칸을 그 자리에 둔다 (caption-button-design.md).
    // 숨기면(`set_visible(false)`) 끌기 띠도 사라진다.
    class caption_element final : public ui_element
    {
    public:
        explicit caption_element(caption_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 이 설정으로 캡션이 차지할 높이다 (논리 픽셀).
        // 창 caption이 없는 플랫폼(`current_ui_platform().window_caption`이 거짓)에서는 0이다.
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
        // 만든 앱 단추다 (설정 차례).
        std::vector<ui_element*> app_buttons_ {};
    };

    // smoke 화면처럼 view snapshot 없이 caption만 그릴 때 쓰는 단독 tree다.
    [[nodiscard]] ui_tree make_caption_tree(float window_width, float scale, caption_config config);
} // namespace luil
