#pragma once

#include "luil/ui/ui_element.h"

namespace luil {
    struct progress_config
    {
        // 진행률이다 (0..1). 범위 밖 값은 element가 자른다.
        //  - 자르기가 여기 있어도 되는 이유는 **표시뿐이라서**다. 값을 나르는
        //    컨트롤이라면 다듬기가 값의 임자에게 남아야 하지만(끌기가 쌓이는 자리),
        //    이 element는 받은 값을 그릴 뿐이라 앱 값과 어긋날 여지가 없다.
        float value { 0.0f };
        // 막대 두께다 (논리 픽셀). 양 끝은 이 값의 절반으로 둥글다.
        float thickness { 4.0f };
    };

    // 진행률 막대다.
    //
    // 표시뿐이라 액션도 커서도 갖지 않는다 — 눌러서 자리를 옮기는 것은 slider의
    // 일이고, 둘을 한 element에 담으면 "누를 수 있는 진행률"이라는 불가능한 조합이
    // 생긴다.
    //
    // 트랙은 `input_background`, 채움은 `accent`다.
    //  - 트랙에 `accent_soft`를 쓰지 않는다. 고대비에서 그 둘이 같은 색으로 접혀
    //    트랙과 채움의 경계가 사라진다.
    //
    // **값을 모르는 진행은 담지 않는다.** 그 자리는 도는 글리프
    // (`draw_spinning_glyph`)가 이미 갖고 있고, 막대가 그것을 흉내 내면 같은 것을
    // 말하는 법이 둘이 된다. 미끄러지는 토막으로 그리고 싶어지면 그때 설정 하나가
    // 붙는다 — 지금 소비자가 없다.
    class progress_element final : public ui_element
    {
    public:
        progress_element(ui_element_id id, progress_config config);

        // 이 설정으로 막대가 차지할 높이다 (논리 픽셀 — 다른 정적 사이저와 같은 단위).
        //  - `wrap_element`의 사이저들처럼 `constexpr`이라 test가 컴파일 타임에 잠근다.
        [[nodiscard]] static constexpr float height_for(const progress_config& config) noexcept
        {
            return config.thickness > 0.0f ? config.thickness : 0.0f;
        }
        // [0, 1]로 자른 값이다.
        // 그리기와 test가 같은 함수를 쓴다 — 자르는 식이 둘이면 언젠가 어긋난다.
        [[nodiscard]] static constexpr float clamp_value(const float value) noexcept
        {
            if (value < 0.0f)
                return 0.0f;
            return value > 1.0f ? 1.0f : value;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        progress_config config_ {};
        float scale_ { 1.0f };
    };
} // namespace luil
