#pragma once

#include "luil/ui/ui_element.h"

#include <string>

namespace luil {
    // 배지가 말하는 뜻이다. 구체 색은 theme이 그리기 시점에 정한다.
    enum class badge_tone
    {
        // 뜻이 색에 있지 않은 이름표다 (개수·분류·상태 이름).
        neutral,
        // 눈에 띄어야 하는 이름표다 ("새것"·"베타").
        accent,
        // 심각도다. **색과 함께 글리프가 붙는다.**
        //  - 고대비는 경고색과 오류색을 같은 전경색으로 접으므로 색만으로는 둘을
        //    가를 수 없다. 글리프를 설정으로 두면 "글리프 없는 경고 배지"라는 조합이
        //    생기는데 그것은 고대비에서 뜻을 잃은 배지다 — 그래서 tone이 함께 정한다.
        warning,
        error,
    };

    struct badge_config
    {
        std::u8string text {};
        badge_tone tone { badge_tone::neutral };
        float font_size { 11.0f };
    };

    // 알약 모양의 작은 상태 표시다. 액션도 커서도 갖지 않는다.
    //
    // **내용에 맞춰 줄어들지 않는다 — 폭은 담는 쪽이 준다.**
    // 배치 시점에는 글꼴이 없어(`arrange_context`에 typeface가 없다) 글자 폭을 잴 수
    // 없다. `wrap_element`가 항목을 전부 같은 크기로 못박은 것과 **같은 이유이자 같은
    // 답**이다. 준 폭이 좁으면 글자가 잘린다(`draw_text_within`) — 배지가 옆 UI를
    // 침범하지 않는다.
    //
    // 어느 tone이든 **1px 테를 두른다.** 고대비는 바탕을 전부 창 배경으로 접으므로
    // 채우기만으로는 배지가 표면에서 사라진다. 테가 tone의 색을 나르고, 심각도는
    // 거기에 글리프까지 더해 색이 접혀도 뜻이 남는다.
    class badge_element final : public ui_element
    {
    public:
        badge_element(ui_element_id id, badge_config config);

        // 이 설정으로 배지가 차지할 높이다 (논리 픽셀).
        // 라벨 한 줄과 같은 식이라 같은 줄에 나란히 서면 높이가 맞는다.
        [[nodiscard]] static constexpr float height_for(const badge_config& config) noexcept
        {
            return config.font_size + 7.0f;
        }

        // 이 tone이 글리프를 함께 그리는지다.
        // 심각도만 참이다 — test가 "색만으로 말하지 않는다"를 이 함수로 잠근다.
        [[nodiscard]] static constexpr bool shows_glyph(const badge_tone tone) noexcept
        {
            return tone == badge_tone::warning || tone == badge_tone::error;
        }

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        badge_config config_ {};
    };
} // namespace luil
