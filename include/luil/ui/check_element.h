#pragma once

#include "luil/ui/ui_element.h"

#include <span>
#include <string>

namespace luil {
    // 낱개 컨트롤이 그려지는 모양이다.
    // 넷은 **그리는 모양만 다르고 계약이 같다** — 상태를 설정으로 받아 그리고,
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
        // 누른 상태가 남는 글자 버튼이다. 배타 선택은 choice_group으로 묶는다.
        toggle_button,
        // 따로 켜고 끄는 알약 모양 칩이다. 켜지면 체크 표시가 붙고 옅은 강조 바탕에 강조 테를 두른다.
        //  - 서식·거르기처럼 여럿을 함께 켜는 줄에 쓴다. 높이는 글자 버튼과 같다.
        //  - 폭은 켬/끔과 무관하게 같다 — 체크 자리를 늘 비워 두고 꺼지면 글을 가운데로 옮긴다.
        //    줄에 맞춰 놓은 칩이 누를 때마다 들썩이지 않는다.
        toggle_chip,
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
        // 일부만 켜졌다 (앱 상태). 폴더 안의 몇 항목만 고른 것, 고른 항목들의 값이 서로 다른 것이다.
        //  - 참이면 `checked`보다 앞서 가운데 줄로 그리고, 보조 기술은 일부 상태(Indeterminate)로 읽는다.
        //  - 체크박스만 뜻이 있다. 라디오와 스위치에는 가운데 상태가 없어 무시한다.
        //  - 누름은 지금처럼 `toggle` 하나다. 일부 상태에서 무엇이 될지는 앱이 정한다 (보통은 켜짐 —
        //    Windows 탐색기와 Qt의 기본이다). 자식들의 상태를 모아 이 값을 정하는 것은 `aggregate_check`다.
        bool mixed { false };
        // 없으면 컨트롤이 눌리지 않는다.
        // 바꿀 수 없는 자리에 바꾸는 시늉을 두지 않는다.
        ui_action toggle {};
        std::u8string tooltip {};
    };

    // 체크 하나의 세 상태다.
    enum class check_state
    {
        unchecked,
        checked,
        mixed,
    };

    // 자식들의 상태를 모은 부모의 상태다 (폴더의 체크).
    //  - 모두 켜졌으면 켜짐, 모두 꺼졌으면 꺼짐, 그 밖(섞였거나 일부 상태인 자식이 있다)은 일부다.
    //  - 자식이 없으면 꺼짐이다.
    [[nodiscard]] check_state aggregate_check(std::span<const check_state> children) noexcept;

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
        // 표시 부분과 라벨 한 줄을 그리는 그 자리 그대로 더한 폭(양옆 안쪽 여백 포함)과 `height_for` 높이다.
        //  - 라벨 글은 스타일의 본문 글자로 잰다 (`measure_context::style_metrics`). 그리기와 같은 글꼴이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        // 표시 부분이 놓이는 자리다 (물리 픽셀).
        [[nodiscard]] rect_f indicator_bounds(float scale) const noexcept;

        check_config config_ {};
    };
}
