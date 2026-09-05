#pragma once

#include "luil/ui/split_handle_element.h"
#include "luil/ui/ui_element.h"

#include <memory>
#include <optional>
#include <string>

namespace luil {
    enum class sidebar_side
    {
        left,
        right,
    };

    struct sidebar_config
    {
        // 같은 화면에 사이드바가 여럿일 때 구분하는 키다.
        std::u8string owner {};
        sidebar_side side { sidebar_side::left };
        // 펼친 폭과 접힌 폭이다 (논리 픽셀).
        // 접힌 폭이 0이면 접는 순간 완전히 사라진다 (다시 펴는 수단은 앱이 밖에 둔다).
        float expanded_width { 240.0f };
        float collapsed_width { 48.0f };
        // 접힘 여부는 앱 상태다.
        // element는 이 값을 받아 그리고 토글 메시지만 낸다.
        //  - 전환 중에는 이 값이 **가려는 곳**이다 (토글 버튼의 방향도 이 값이 정한다).
        bool collapsed { false };
        // 지금 이 순간의 폭이다 (논리 픽셀). 비어 있으면 `collapsed`가 정한다.
        //  - **펴고 접는 사이의 중간 폭은 전환하는 앱만 안다.** 전환은 앱 상태이고
        //    (`transition`) element는 "전환 중"이라는 것을 몰라도 되게 하려면,
        //    지금 값을 그냥 받는 것이 맞다.
        //  - `collapsed`와 어긋나 보이지만 둘은 다른 것을 말한다 — 저쪽은 **목표**,
        //    이쪽은 **지금**이다. 전환이 끝나면 둘이 같아진다.
        std::optional<float> width {};
        // 접힘 토글이다. 액션이 없으면 버튼을 만들지 않는다.
        ui_action toggle {};
        std::u8string toggle_tooltip {};
        // 폭 조절 손잡이다. 없으면 손잡이를 만들지 않는다.
        // delta는 논리 픽셀이고 +가 넓어지는 쪽이다 — 어느 쪽에 붙었는지와 무관하게
        // `split_handle_element`가 부호를 맞춘다.
        // 폭의 범위 다듬기는 폭을 소유한 앱의 몫이다.
        split_resize_message_factory resize {};
    };

    // 토글 버튼이 있을 때 내용 위에 남겨 두는 머리 높이와 손잡이의 hit 폭이다 (논리 픽셀).
    inline constexpr float sidebar_header_height { 40.0f };
    inline constexpr float sidebar_handle_width { 8.0f };

    // 접었다 펼 수 있는 옆 판이다.
    // slot의 왼쪽 또는 오른쪽 가장자리에 붙어 지금 상태의 폭만큼만 차지한다.
    class sidebar_element final : public ui_element
    {
    public:
        // 내용은 만들 때 받는다 (nullptr이면 없는 것이다).
        // 내용이 자식 중 가장 아래에 깔려야 토글·손잡이가 위에서 hit를 가져간다.
        explicit sidebar_element(sidebar_config config, std::unique_ptr<ui_element> content = nullptr);

        // 지금 상태의 폭이다 (논리 픽셀).
        // 담는 쪽이 배치(stack의 길이)에 같은 값을 쓴다.
        [[nodiscard]] static float width_for(const sidebar_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        sidebar_config config_ {};
        ui_element* content_ { nullptr };
        ui_element* toggle_ { nullptr };
        ui_element* handle_ { nullptr };
    };
} // namespace luil
