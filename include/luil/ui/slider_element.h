#pragma once

#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace luil {
    // 끈 만큼의 값 변화를 앱 메시지로 바꾼다.
    //
    // delta는 **값 단위**다 (픽셀이 아니다). 픽셀을 값으로 바꾸려면 배율과 손잡이가
    // 움직일 수 있는 거리를 알아야 하고, 그 둘을 아는 것은 element뿐이다 — 앱에
    // 픽셀을 넘기면 그 환산이 앱마다 다시 쓰인다.
    using slider_change_message_factory = std::function<input_action(float delta)>;

    struct slider_config
    {
        // 값의 범위와 지금 값이다. 셋 다 앱 상태다.
        float minimum { 0.0f };
        float maximum { 1.0f };
        float value { 0.0f };
        // 없으면 끌기 대상도 커서 주인도 되지 않는다 — 없는 것은 두지 않는다.
        // 범위 다듬기와 눈금 반올림은 **값을 가진 앱**의 몫이다. element가
        // 반올림하면 상대 변화량이 쌓이는 자리에서 앱 값과 조용히 어긋난다.
        slider_change_message_factory change {};
        // 값을 **이것으로** 하라는 절대 메시지다. 없으면 보조 기술이 값을 정할 수
        // 없다 (UIA `SetValue`가 읽기 전용으로 선다).
        //  - 델타(`change`)로 환산해 보내면 오래된 발행본 기준의 델타가 겹쳐
        //    쌓인다 — 다음 frame 전에 같은 목표가 두 번 오면 두 배로 움직인다.
        //    절대 값은 같은 메시지가 몇 번 겹쳐도 마지막 값 그대로다.
        //  - 끌기와 키는 여전히 델타다. 그쪽은 사람이 누적을 눈으로 보며 미는
        //    길이라 상대가 옳고, 이쪽은 목표를 말하는 길이라 절대가 옳다.
        std::function<input_action(float value)> change_to {};
        // 끌기가 붙는 값이다 (값 단위). 비어 있으면 자석이 없다.
        //  - 끌기만 붙는다. 키와 보조 기술의 값 정하기는 정확한 값을 고르는 길이라
        //    붙지 않는다.
        //  - 범위 밖의 값은 버린다. 순서는 상관없다.
        //  - 붙는 순간은 `change_to`가 있으면 그것으로 지점 값을 그대로 보낸다 — 델타를
        //    이어 더한 값은 부동소수 오차로 지점에서 조금 비켜 선다. 앱이 값을 눈금으로
        //    반올림한다면 지점도 그 눈금 위에 둔다.
        std::vector<float> magnets {};
    };

    // 지점에서 이 거리 안이면 손잡이가 지점에 머물고, 풀리는 거리까지 포인터를
    // 따라잡는다 (논리 픽셀). 그 밖에서는 포인터 그대로다.
    // 거리를 앱이 정하지 않는 이유는 키 걸음과 같다 — 손에 닿는 느낌이 앱마다 갈리지 않는다.
    //  - 붙는 거리는 손잡이 반지름이다. 포인터가 이 안이면 손잡이가 표식을 덮는다.
    //  - 풀리는 거리는 손잡이 지름이다. 붙는 거리에서 바로 포인터로 건너뛰면 손잡이가
    //    튀므로, 그 사이를 따라잡는 구간으로 이어 붙인다.
    //  - 둘 다 이웃 지점까지 간격의 절반을 넘지 않는다. 가까운 두 지점이 서로 당기지 않는다.
    inline constexpr float slider_magnet_stick_distance { 6.0f };
    inline constexpr float slider_magnet_release_distance { 12.0f };

    // 지점 표식과 붙은 동안의 고리다 (논리 픽셀).
    inline constexpr float slider_magnet_mark_width { 2.0f };
    inline constexpr float slider_magnet_mark_height { 10.0f };
    inline constexpr float slider_magnet_ring_size { 22.0f };

    // 키 한 번이 옮기는 걸음이다 (범위에 대한 비율).
    // 걸음의 크기를 앱이 정하지 않는 이유는 델타를 값 단위로 내주는 이유와 같다 —
    // 범위를 아는 것이 element뿐이다.
    inline constexpr float slider_key_step_ratio { 0.01f };
    inline constexpr float slider_key_page_ratio { 0.1f };

    // 한 줄 높이와 손잡이·트랙의 크기다 (논리 픽셀).
    inline constexpr float slider_row_height { 20.0f };
    inline constexpr float slider_knob_size { 12.0f };
    inline constexpr float slider_track_thickness { 4.0f };

    // 끌어서 값을 바꾸는 막대다.
    //
    // **값을 담지 않고 변화량만 나른다** — `split_handle_element`와 같은 규약이다.
    // 좌표 변화량은 지금 tree를 읽지 않으므로 frame마다 다시 지어도 끌기가 이어지고,
    // 하한·상한은 값을 가진 쪽에 남는다.
    //  - 끌어 범위 밖으로 나간 만큼이 쌓이지 않도록, 앱은 tree를 짓기 전에 값을
    //    다듬어 상태에 되돌려 써야 한다 (사이드바 폭·나눈 자리와 같은 규칙).
    //
    // **양끝 여백이 스크롤 막대와 갈리는 자리다.** 손잡이의 *중심*이 양 끝에 닿으므로
    // 값이 도는 거리는 트랙 폭에서 손잡이 하나를 뺀 것이다. 스크롤 막대의 thumb는
    // 트랙 **안에** 통째로 들어가고 길이도 내용 길이를 따라 변한다 — 식이 다르고
    // 설정도 겹치지 않아 한 element로 합치면 양쪽에 죽은 필드가 생긴다.
    //
    // 진행률 막대(`progress_element`)와도 갈린다: 저쪽은 표시뿐이라 액션도 커서도
    // 갖지 않는다. 하나로 합치면 "누를 수 있는 진행률"이라는 조합이 생긴다.
    //
    // **끌기는 손잡이를 포인터 자리의 함수로 둔다** (docs/slider-magnet-design.md).
    // 이동마다 그 함수의 앞·뒤 차이를 델타로 내므로, 델타를 이어 더한 값은 언제나
    // "지금 포인터 자리의 손잡이"다. 끄는 동안의 상태를 element도 앱도 들지 않고,
    // 끄는 중에는 지금 값을 읽지 않아 다음 frame 전에 이동이 여럿 와도 겹쳐 쌓이지 않는다.
    //  - 포인터 자리는 트랙 안으로 자른다. 범위 밖으로 끌었다 돌아오면 포인터가 트랙에
    //    들어올 때까지 손잡이가 끝에 머문다.
    //  - Ctrl이 눌린 이동은 자석 없이 포인터 그대로다. 각 문맥의 수식 키로 함수를 고르므로
    //    끄는 도중 Ctrl을 누르고 떼도 합이 맞는다.
    class slider_element final : public ui_element
    {
    public:
        slider_element(ui_element_id id, slider_config config);

        // 이 설정으로 막대가 차지할 높이다 (논리 픽셀).
        [[nodiscard]] static constexpr float height_for(const slider_config&) noexcept
        {
            return slider_row_height;
        }

        // 범위 안으로 자른 값이다. 그리기와 test가 같은 함수를 쓴다.
        //  - 범위가 뒤집혀 있으면(최대 <= 최소) 값이 곧 최소다.
        [[nodiscard]] static constexpr float clamp_value(const slider_config& config) noexcept
        {
            if (config.maximum <= config.minimum || config.value < config.minimum)
                return config.minimum;
            return config.value > config.maximum ? config.maximum : config.value;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 값 정하기만 가로챈다 — 목표 값을 절대 메시지(`change_to`)로 내보내고,
        // 나머지 명령에는 누를 자리가 없다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

        // arrange 뒤에 유효한 손잡이 중심 x다 (물리 픽셀).
        // 배치 계산을 test가 여기서 고정한다 (스크롤 막대의 thumb 자리와 같은 자리).
        [[nodiscard]] float knob_center() const noexcept;
        // 손잡이 중심이 움직일 수 있는 거리다 (물리 픽셀).
        // 0이면 끌어도 값이 변하지 않는다 (칸이 손잡이보다 좁다).
        [[nodiscard]] float travel() const noexcept;

        // 포인터 x에서 손잡이 중심이 설 자리다 (트랙 왼쪽 끝에서 잰 물리 픽셀, 0..travel).
        // `free`면 자석 없이 포인터 그대로다. 붙은 지점이 있으면 그 번호를 `magnet`에 쓴다.
        [[nodiscard]] float knob_offset_for(float x, bool free, std::optional<std::size_t>* magnet = nullptr) const noexcept;

    private:
        // 손잡이를 물리 `pixels`만큼 옮기는 값 변화량이다.
        [[nodiscard]] float value_delta_for(float pixels) const noexcept;
        // 손잡이를 지금 자리에서 `target` 자리로 옮기는 메시지다.
        // 지점에 붙으면 절대 메시지로 지점 값을 그대로 보낸다.
        [[nodiscard]] std::vector<input_action> move_knob(float from, float target, std::optional<std::size_t> magnet) const;
        // 지점의 손잡이 자리다 (`knob_offset_for`와 같은 물리 픽셀).
        [[nodiscard]] float magnet_offset(std::size_t index) const noexcept;

        slider_config config_ {};
        float scale_ { 1.0f };
    };
} // namespace luil
