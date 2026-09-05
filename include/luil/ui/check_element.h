#pragma once

#include "luil/ui/ui_element.h"

#include <string>

namespace luil {
    // 낱개 컨트롤이 그려지는 모양이다.
    // 셋은 **그리는 모양만 다르고 계약이 같다** — 상태를 설정으로 받아 그리고,
    // 바꾸자는 메시지만 낸다. 무엇이 바뀌는지(뒤집기·고르기)는 앱이 액션으로 정한다.
    enum class check_style
    {
        // 네모와 체크 표시다.
        checkbox,
        // 동그라미와 안쪽 점이다. 낱개 배타 선택에 쓴다.
        //  - 서로 배타적인 묶음은 `choice_group_element`가 낫다.
        radio,
        // 미끄러지는 손잡이가 있는 스위치다. 켬/끔이 즉시 반영되는 설정에 쓴다.
        toggle_switch,
    };

    struct check_config
    {
        // 같은 화면에 여럿일 때 구분하는 키다.
        std::u8string owner {};
        check_style style { check_style::checkbox };
        // 비어 있으면 컨트롤만 그린다 (표 안의 칸처럼 라벨이 밖에 있을 때).
        std::u8string label {};
        // 켜졌는지다 (앱 상태).
        bool checked { false };
        // 없으면 컨트롤이 눌리지 않는다.
        // 바꿀 수 없는 자리에 바꾸는 시늉을 두지 않는다.
        ui_action toggle {};
        std::u8string tooltip {};
    };

    // 컨트롤 한 줄의 높이와 표시 부분의 크기다 (논리 픽셀).
    inline constexpr float check_row_height { 24.0f };
    inline constexpr float check_box_size { 14.0f };
    inline constexpr float check_switch_width { 30.0f };
    inline constexpr float check_switch_height { 16.0f };

    // 체크박스·라디오·스위치 한 개다.
    // 상태는 앱이 갖고 element는 그것을 그린 뒤 바꾸자는 메시지만 낸다.
    // 라벨까지 포함한 줄 전체가 눌린다 — 작은 네모만 겨냥하게 하지 않는다.
    class check_element final : public ui_element
    {
    public:
        explicit check_element(check_config config);

        // 이 설정으로 차지할 높이다 (논리 픽셀).
        // 담는 쪽이 배치(stack의 길이)에 같은 값을 쓴다.
        [[nodiscard]] static float height_for(const check_config& config) noexcept;
        // 표시 부분이 차지하는 폭이다 (논리 픽셀).
        // 라벨 없이 여럿을 줄 맞춰 놓을 때 담는 쪽이 쓴다.
        [[nodiscard]] static float indicator_width_for(const check_config& config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 표시 부분이 놓이는 자리다 (물리 픽셀).
        [[nodiscard]] rect_f indicator_bounds(float scale) const noexcept;

        check_config config_ {};
    };
} // namespace luil
