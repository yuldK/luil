#pragma once

#include "luil/ui/ui_element.h"

#include <functional>

namespace luil {
    // 손잡이를 끄는 축이다. 커서도 여기서 나온다.
    enum class split_axis
    {
        // 세로로 선 손잡이를 좌우로 끈다.
        horizontal,
        // 가로로 누운 손잡이를 위아래로 끈다.
        vertical,
    };

    // 손잡이가 나르는 길이를 가진 판이 어느 쪽인지다.
    //  - 부호를 element가 맞추므로 앱은 언제나 "+ = 그 판이 넓어진다"만 안다.
    //    이 규약이 없으면 왼쪽 판과 오른쪽 판이 같은 메시지에 반대로 반응하고,
    //    부호를 맞추는 자리가 부르는 쪽마다 하나씩 늘어난다.
    enum class split_grows
    {
        // 앞(왼쪽·위) 판의 길이다. 손잡이를 끝쪽으로 끌면 넓어진다.
        toward_end,
        // 뒤(오른쪽·아래) 판의 길이다. 손잡이를 앞쪽으로 끌면 넓어진다.
        toward_start,
    };

    // 손잡이를 끈 만큼의 길이 변화를 앱 메시지로 바꾼다.
    // delta는 논리 픽셀이고 +가 넓어지는 쪽이다.
    using split_resize_message_factory = std::function<input_action(float delta)>;
    // 길이 바꾸기 한 번이 끝났다는 메시지다. 앱이 끝난 길이를 저장하는 자리다.
    using split_finish_message_factory = std::function<input_action()>;

    // 키 한 번이 옮기는 거리다 (논리 픽셀).
    // 손잡이는 자기 범위를 모르므로 Home/End에 답할 값이 없다 — 그 둘은 키를
    // 그대로 흘려보낸다 (value-step-design.md).
    inline constexpr float split_handle_key_step { 8.0f };
    inline constexpr float split_handle_key_page_step { 64.0f };

    struct split_handle_config
    {
        split_axis axis { split_axis::horizontal };
        split_grows grows { split_grows::toward_end };
        // 없으면 끌기 대상도 커서 주인도 되지 않는다 — 없는 것은 두지 않는다.
        // 길이의 범위 다듬기는 그 길이를 가진 쪽의 몫이다.
        split_resize_message_factory resize {};
        // 길이 바꾸기가 끝났을 때 한 번 낸다. 비어 있으면 알리지 않는다.
        //  - 끌기는 포인터를 뗄 때다. 시스템이 접촉을 취소했거나 입력을 잃어 거둔 끌기도 끝이다 —
        //    그때까지 보낸 변화량이 그대로 남는다. 움직이지 않고 뗀 누름에도 낸다.
        //  - 끄는 도중 손잡이가 tree에서 사라졌거나 가려진 뒤의 뗌에는 내지 않는다 (`pointer_drag_target::on_release`).
        //  - 키는 한 번이 곧 한 번의 바꾸기라 그 키의 `resize` 바로 뒤에 낸다. 눌러 두어 되풀이되는 키도
        //    되풀이마다 낸다 — 길이를 저장하는 쪽이 잦은 호출을 견뎌야 한다.
        //  - `resize`가 없으면 끌 수도 키로 옮길 수도 없으므로 이것도 듣지 않는다.
        split_finish_message_factory finish {};
        // 쉬는 동안에도 가운데에 1 논리 px `divider` 선을 긋는다. 기본(거짓)은 지금처럼 hover·누름 동안만 그린다.
        //  - 두 판 사이를 다른 무엇(테두리·바탕 차이)도 가르지 않는 화면에서 쓴다. 이미 판의 테두리가 가르는
        //    자리에서 켜면 선이 두 겹이 된다.
        //  - hover·누름 동안의 강조(`accent` 2 px)는 그대로다.
        bool resting_divider { false };
    };

    // 끌어서 옆 판의 길이를 바꾸는 손잡이다.
    //
    // **자기 자리를 정하지 않는다** — 담는 쪽이 준 slot을 그대로 쓴다. 그래서 같은
    // element가 판 **사이**의 한 칸으로도 서고 가장자리에 절반씩 걸친 띠로도 선다.
    // "자리가 바깥인가 사이인가"는 담는 쪽의 결정이지 이 element의 성질이 아니다.
    //  - 걸치기는 부모가 자식을 자르지 않을 때만 성립한다. 자르는 컨테이너
    //    (`strip_element`) 안에서는 바깥 절반이 hit test에서 조용히 빠지므로, 그런
    //    자리에서는 판 사이의 한 칸으로 담는다.
    //
    // **길이를 담지 않고 변화량만 나른다.**
    //  - 좌표 변화량은 지금 tree의 상태를 읽지 않으므로 tree가 frame마다 다시
    //    지어져도 끌기가 이어진다.
    //  - 길이의 하한·상한은 그 길이를 가진 쪽에 남는다. "길이는 담을 때 정한다"와
    //    "배치가 길이를 다시 흥정하지 않는다"가 그대로 성립한다.
    //
    // 평소에는 아무것도 그리지 않고(`resting_divider`면 얇은 구분선만) 포인터가 올라오거나 눌린 동안
    // 가운데에 강조 선을 긋는다. **잡는 두께와 보이는 두께가 다른 것**이 이 element의 요점이다 —
    // 잡기 좋으려면 두꺼워야 하고 보기 좋으려면 얇아야 한다.
    class split_handle_element final : public ui_element
    {
    public:
        // id는 담는 쪽이 준다.
        //  - 손잡이가 kind를 정해 버리면 같은 화면의 손잡이 둘을 owner로만 갈라야
        //    하고, 이미 자기 kind를 가진 사이드바가 그것을 지킬 수 없다.
        split_handle_element(ui_element_id id, split_handle_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        split_handle_config config_ {};
        float scale_ { 1.0f };
    };
} // namespace luil
