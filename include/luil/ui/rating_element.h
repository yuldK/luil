#pragma once

#include "luil/ui/ui_element.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace luil {
    inline constexpr std::size_t rating_default_count { 5 };
    inline constexpr std::size_t rating_max_count { 20 };
    // 별 한 칸의 한 변이다 (논리 픽셀).
    inline constexpr float rating_default_cell_size { 28.0f };
    // 칸 안쪽 여백이다 (논리 픽셀). 별은 칸보다 한 변이 이 값의 두 배만큼 작다.
    //  - 칸이 이 여백의 네 배보다 좁으면 여백이 칸의 1/4로 줄어 별이 칸의 절반 아래로 작아지지 않는다.
    inline constexpr float rating_star_inset { 4.0f };
    // 누른 채 이 거리를 넘으면 끌기다 (물리 픽셀, `pointer_drag_target::start_distance`와 같은 단위).
    inline constexpr float rating_drag_start_distance { 6.0f };

    // 입력의 걸음이다.
    enum class rating_precision
    {
        // 한 별이 한 걸음이다.
        whole,
        // 반 별이 한 걸음이다.
        half,
    };

    struct rating_config
    {
        // 상자와 칸의 owner다.
        std::u8string owner {};
        // 별 수다. 1 이상 `rating_max_count` 이하로 자른다.
        std::size_t count { rating_default_count };
        // 앱 상태다. 0은 아직 매기지 않음이다. [0, count]로 자르고 소수는 별을 일부만 채워 그린다.
        //  - 자르기가 여기 있어도 되는 이유는 진행률 막대와 같다. 받은 값을 그릴 뿐이라 앱 값과 어긋날 여지가 없다.
        float value { 0.0f };
        rating_precision precision { rating_precision::whole };
        // 값을 **이것으로** 하라는 절대 메시지다. 눌림, 끌기, 키, 보조 기술이 모두 이 하나를 탄다.
        //  - 값이 이산이라 변화량(`slider_config::change`)을 두지 않는다. 다음 frame 전에 같은 키가 겹쳐도 같은 목표라
        //    한 걸음만 가는 것은 받아들인다 (슬라이더의 `change_to`와 같은 이유다).
        //  - 없으면 표시 전용이다. 칸도 커서도 Tab 자리도 서지 않고 보조 기술은 읽기만 한다 (슬라이더에서는 `change`가 가르던 자리다).
        std::function<input_action(float value)> change_to {};
        // 참이면 칸의 값이 현재 값과 같은 칸을 다시 눌러 0으로 지울 수 있다.
        // 거짓이면 한 번 매긴 뒤에는 0으로 돌아갈 수 없다 (최소가 한 걸음이다).
        bool clearable { true };
        // 별 칸의 한 변이다 (논리 픽셀). 0이면 `rating_default_cell_size`다. 터치 앱이 올린다.
        float control_size { 0.0f };
        // 접근 이름이다. 비면 기본 문구(별점)다.
        std::u8string name {};
        // 접근 값 글이다. 비면 기본 문구("3 / 5", 소수 첫째 자리가 있으면 "3.5 / 5")다.
        std::function<std::u8string(float value, std::size_t count)> value_name {};
    };

    // 별 몇 개로 점수를 매기게 하고, 같은 모양으로 평균 점수를 보여 주는 요소다.
    //
    // root(`ui_element_kind::rating`)는 접근 역할이 slider이고 별, 채움, 미리 보기를 직접 그린다.
    // `change_to`가 있으면 걸음 하나마다 자식 칸(`rating_cell`)을 더한다. 정수 별은 `count`개, 반 별은 `2 * count`개다.
    //  - 칸은 hit와 hover와 끌기만 맡는다. 그리지 않고, 접근 역할은 없으며, Tab 자리가 아니다. 눌러서 얻는 초점은
    //    root로 보낸다 (칸을 누른 뒤 곧바로 ← →가 먹히게 한다).
    //  - 칸이 빈틈없이 이어져 별 사이를 지나도 hover가 끊기지 않는다. 그래서 미리 보기가 깜빡이지 않는다.
    //  - 표시 전용(`change_to` 없음)에는 칸도 커서도 키 역할도 없다.
    //
    // root의 bounds는 별 줄이 차지한 폭이다. 자리가 더 넓으면 왼쪽에 붙이고 남는 자리는 비운다.
    // 초점 테와 보조 기술의 사각형이 별 줄에 붙고, 빈 자리를 눌러도 아무 일이 없다.
    //
    // 값을 조작하는 규약은 `slider_element`를, 표시 전용의 선례는 `progress_element`를 따른다 (docs/rating-design.md).
    class rating_element final : public ui_element
    {
    public:
        explicit rating_element(rating_config config);

        // 칸의 한 변이다 (논리 픽셀, 자리와 무관한 설정 값).
        [[nodiscard]] static constexpr float height_for(const rating_config& config) noexcept
        {
            return config.control_size > 0.0f ? config.control_size : rating_default_cell_size;
        }

        // 별 수 곱하기 칸의 한 변이다 (논리 픽셀). 별 수는 [1, `rating_max_count`]로 자른 값이다.
        [[nodiscard]] static constexpr float width_for(const rating_config& config) noexcept
        {
            return static_cast<float>(count_of(config)) * height_for(config);
        }

        // 자른 별 수다.
        [[nodiscard]] static constexpr std::size_t count_of(const rating_config& config) noexcept
        {
            if (config.count < 1u)
                return 1u;
            return config.count > rating_max_count ? rating_max_count : config.count;
        }

        // 한 걸음의 값이다. 정수 별은 1, 반 별은 0.5다.
        [[nodiscard]] static constexpr float step_of(const rating_config& config) noexcept
        {
            return config.precision == rating_precision::half ? 0.5f : 1.0f;
        }

        // 범위 안으로 자른 값이다. NaN은 0이다. 그리기와 test가 같은 함수를 쓴다.
        [[nodiscard]] static float clamp_value(const rating_config& config) noexcept;
        // 만들 수 있는 가장 작은 값이다. 지울 수 있거나 값이 0이면 0, 아니면 한 걸음이다.
        [[nodiscard]] static float minimum_for(const rating_config& config) noexcept;

        // arrange 뒤에 유효하다. 가로 자리(물리 픽셀)가 정하는 값이다. 왼쪽 밖은 최소, 오른쪽 밖은 `count`다.
        //  - 칸 번호 곱하기 걸음이다. 줄의 폭을 넘는 오른쪽은 마지막 칸의 값이라 `count`다.
        //  - 왼쪽 밖의 최소는 `minimum_for`와 달리 지금 값을 읽지 않는다. 끄는 동안 값을 읽으면 같은 끌기가 frame에 따라
        //    다른 값을 낸다. 지울 수 있으면 0, 아니면 한 걸음이다.
        [[nodiscard]] float value_at(float x) const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 값 정하기만 가로챈다. 요청을 가장 가까운 격자점으로 반올림해(같은 거리면 큰 쪽) 절대 메시지로 내보낸다.
        // `change_to`가 없으면 nullopt라 읽기 전용이다.
        [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override;

    private:
        // 값을 격자로 옮기는 걸음의 위쪽·아래쪽 이웃이다. 부동소수 오차는 걸음의 1/1000 안에서 같다고 본다.
        [[nodiscard]] float grid_above(float value) const noexcept;
        [[nodiscard]] float grid_below(float value) const noexcept;
        // 절대 메시지 하나다. 비활성이거나 `change_to`가 없으면 비어 있다.
        [[nodiscard]] std::vector<input_action> emit(float value) const;
        // 칸을 눌렀다 뗐을 때의 메시지다. 칸의 값이 현재 값과 같고 지울 수 있으면 0이다.
        [[nodiscard]] std::vector<input_action> click(float cell_value) const;
        // 키 걸음의 메시지다. 이미 끝에 닿았으면 빈 목록이라 키가 앱 단축키로 새지 않는다.
        //  - 이름이 `key_step`이 아닌 이유는 상속받은 접근자(`ui_element::key_step`)를 가리기 때문이다.
        [[nodiscard]] std::vector<input_action> step_by_key(value_step step) const;
        // 포인터가 올라간 칸의 값이다. 미리 보기가 서지 않으면 nullopt다.
        [[nodiscard]] std::optional<float> preview_value(const interaction_snapshot& interaction) const;

        rating_config config_ {};
        float scale_ { 1.0f };
        // 별 칸의 한 변이다 (물리 픽셀). 자리가 좁으면 설정 값보다 작다.
        float cell_ { 0.0f };
    };
}
