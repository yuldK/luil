#pragma once

#include "luil/ui/panel_element.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace luil {
    // 화면을 덮는 modal dialog host의 설정이다.
    // 내용(dialog 본체)은 앱이 만들어 `set_content`로 넣는다.
    //
    // 앱 정책은 밖으로.
    //  - 무엇을 닫을지는 앱이 액션으로 정하고 host는 메시지만 나른다.
    //  - 열림 상태와 밀어낸 양도 앱 상태다. host는 그 값을 받아 자리를 잡는다.
    struct modal_host_config
    {
        // 같은 화면에 modal이 여럿일 때 구분하는 키다.
        // 하나뿐이면 비워 둔다.
        std::u8string owner {};
        // 뒤를 덮는 scrim의 진하기다 (0~1).
        // 0이면 보이지 않지만 **여전히 막는다** — 포인터를 막는 것이 modal의 몫이라
        // scrim 자체는 언제나 있다.
        float scrim_opacity { 0.45f };
        // 팔레트에서 scrim의 색을 직접 고른다.
        // 비어 있으면 `content_shadow`에 위의 진하기를 얹은 색이다 — 지금까지의 scrim이
        // 그 특수 경우다.
        //  - **선택자를 주면 `scrim_opacity`는 쓰이지 않는다.** 알파까지 고른 색이 돌아오는데
        //    그 위에 다시 진하기를 곱하면 앱이 적은 값과 화면의 값이 갈린다. 진하기가 필요하면
        //    선택자 안에서 `with_alpha`로 말한다 — 그 자리에 팔레트가 통째로 와 있다.
        //  - 구체 색이 아니라 선택자인 이유는 다른 곳과 같다. 색을 받으면 테마가 바뀌어도
        //    따라오지 못한다 (concepts/theming.md의 "선택자" 절).
        std::function<ui_color(const ui_color_palette&)> scrim_background {};
        // 바깥(scrim)을 눌렀을 때의 액션이다.
        // 비어 있으면 누름을 흡수만 하고 아무 일도 하지 않는다.
        ui_action outside {};
        // 가둠이 떠 있는 동안의 Esc다.
        // 비어 있으면 Esc가 그대로 앱으로 흐른다 — "이 dialog는 Esc로 닫지 않는다"
        // 가 따로 플래그 없이 그렇게 표현된다 (modal-dialog-design.md).
        ui_action dismiss {};
        // 이 host가 뜨는 순간 초점이 설 자리다.
        // **비어 있으면 자동 초점이 없다** — Tab을 한 번 쳐야 초점이 선다.
        //  - 그 자리가 지금 초점을 받을 수 없으면 dialog 안 첫 자리로 물러선다.
        //  - 사용자가 먼저 다른 자리를 잡았으면 그 자리가 이긴다. 술어의 전제가
        //    "초점이 없다"라서 규칙을 따로 쓰지 않는다 (focus-entry-design.md).
        ui_element_id focus_entry {};
        // 이 host가 사라진 뒤 초점이 돌아갈 **바깥** 자리다 (통상 dialog를 연 그 자리).
        // 비어 있으면 되돌리지 않는다 — 초점은 그대로 사라진다.
        //  - `focus_entry`와 나란히 선다. 자동 초점은 되돌리기와 함께 와야 뜻이
        //    생기고(modal-dialog-design.md), 두 줄이 붙어 있는 것이 그 요구를
        //    어휘로 말한다.
        //  - **바깥에 또 다른 가둠이 서 있으면 이 값은 쓰이지 않는다.** 되돌리기는
        //    "가둠이 하나도 없다"를 전제로 발화한다 — 중첩에서 이 host가 닫히면
        //    그때 초점을 받는 것은 **바깥 host의 `focus_entry`**이고, 여기 적은
        //    이름은 그냥 버려진다 (focus-entry-design.md).
        ui_element_id focus_return {};
        // 내용 아래에 깔 표면이다 (바탕색과 둥근 모서리).
        // **비어 있으면 표면을 아예 만들지 않는다** — 내용이 host의 가운데 자리를 그대로
        // 받고, 지금까지의 host가 그 특수 경우다.
        //
        // 편의다. 없어도 앱이 `panel_element`로 감싸면 같은 화면이 나온다. 그런데 dialog를
        // 세우는 앱이 예외 없이 같은 다섯 줄(surface_background 선택자 + corner_radius 8을
        // 담은 panel로 내용을 감싸기)을 적고 있었고(examples/demo/basics_page.cpp의 dialog),
        // 되풀이되는 다섯 줄은 언젠가 한 앱에서만 모서리가 달라진다. 그 다섯 줄을 host의
        // 설정 한 줄로 옮긴다.
        //  - 크기를 대신 재 주지는 않는다. 표면은 여전히 `content_width`·`content_height`가
        //    정한 자리를 받는다 — 이 framework에는 측정 단계가 없어 내용이 자기 크기를
        //    말할 길이 없고, host가 추측하면 그 추측이 앱의 배치와 갈린다.
        std::optional<panel_config> surface {};
        // 내용의 크기다 (논리 픽셀).
        float content_width { 360.0f };
        float content_height { 200.0f };
        // 가운데에서 밀어낼 양이다 (논리 픽셀).
        // 캡션을 잡아 끄는 앱 상태를 그대로 받는다 — host가 기억하면 tree가 다시
        // 지어질 때 사라진다.
        //  - 화면 밖으로 나가지 않게 다듬는 것도 앱의 몫이다. host가 몰래 다듬으면
        //    앱 상태는 화면 밖에 남고 다음 끌기가 거기서 시작한다.
        float offset_x { 0.0f };
        float offset_y { 0.0f };
    };

    // scrim으로 뒤를 덮고 내용을 가운데에 놓는 modal dialog host다.
    // 세 가지를 한곳으로 모은다.
    //  1. **포인터를 막는다** — scrim이 뒤로 새는 누름을 흡수한다 (`hit_opaque`).
    //  2. **초점을 가둔다** — Tab이 이 안을 벗어나지 않고, 이름 지은 자리가 있으면
    //     뜨는 순간 거기 서며 사라질 때 바깥 자리로 돌아간다
    //     (`focus_trap`·`focus_entry`·`focus_return`).
    //  3. **Esc를 받는다** — 설정의 dismiss 액션을 낸다.
    //
    // 앱이 잊을 수 없는 자리로 이 셋을 옮기는 것이 이 element의 값어치다.
    //
    // **무엇을 담는지는 묻지 않는다.** 앱이 만든 어떤 element든 `set_content`로 들어가고
    // host는 그것에 자리 하나와 위의 셋을 줄 뿐이다 — 앱마다 modal을 새로 짓지 말고 이
    // host를 다시 쓴다 (modal-dialog-design.md).
    class modal_host_element final : public ui_element
    {
    public:
        explicit modal_host_element(modal_host_config config);

        // dialog 본체다.
        // 가운데 자리를 통째로 받는다 — 안쪽 여백이 필요하면 padding 있는 stack을 넣는다.
        //  - 내용은 자기 자리의 누름을 흡수하도록 세워진다. 그러지 않으면 dialog의
        //    빈 자리를 누른 것이 뒤의 scrim으로 새어 dialog가 닫힌다.
        //  - 설정에 `surface`가 있으면 그 표면 **안에** 놓인다. 자리도 흡수도 그대로다.
        void set_content(std::unique_ptr<ui_element> content);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        modal_host_config config_ {};
        ui_element* scrim_ { nullptr };
        // 가운데 자리를 받는 자식이다.
        // 표면이 있으면 그 표면이고, 없으면 내용 자체다 — 어느 쪽이든 자리를 받는 것은
        // 하나뿐이라 arrange가 갈리지 않는다.
        //  - 앱이 넣지 않았으면 nullptr다 (scrim만 있는 host).
        ui_element* content_ { nullptr };
    };
} // namespace luil
