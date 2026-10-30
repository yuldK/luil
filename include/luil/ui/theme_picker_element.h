#pragma once

#include "luil/theme/appearance.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/ui_element.h"

#include <vector>

#include <array>
#include <functional>
#include <string>

namespace luil {
    struct theme_picker_config
    {
        std::u8string owner {};
        theme_preference selected { theme_preference::system };
        std::u8string name { u8"테마" };
        // 밝게·시스템·어둡게 순서다. 표시 언어는 앱이 정한다.
        std::array<std::u8string, 3> labels { u8"밝게", u8"시스템", u8"어둡게" };
        std::function<input_action(theme_preference)> select {};
        std::u8string accent_id { default_accent_id };
        // 카드 한 장의 폭과 미리 보기 높이, 카드 사이 간격이다 (논리 픽셀).
        //  - 카드는 이 크기로 고정이다. 받은 폭이 넓어도 늘어나지 않고 왼쪽부터 놓인다 — 미리 보기의 비율이
        //    창 폭에 따라 일그러지지 않는다.
        //  - 세 장이 한 줄에 들지 않으면 다음 줄로 넘어간다. 한 장도 들지 않으면 받은 폭으로 줄인다.
        float card_width { 112.0f };
        float preview_height { 70.0f };
        float gap { 12.0f };
    };

    // 밝은 화면·시스템(대각선 분할)·어두운 화면을 카드로 고르는 배타 선택이다.
    // Tab에서는 한 자리이고 방향키로 카드를 오간다. 값은 앱이 소유한다.
    // 카드는 고정 크기이고 낮은 폭에서는 다음 줄로 넘어간다.
    class theme_picker_element final : public ui_element
    {
    public:
        explicit theme_picker_element(theme_picker_config config);
        [[nodiscard]] static float height_for(const theme_picker_config& config, float width) noexcept;
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        theme_picker_config config_ {};
    };
    struct accent_picker_config
    {
        std::u8string owner {};
        std::u8string selected { default_accent_id };
        std::u8string name { u8"강조색" };
        bool include_system { true };
        std::function<input_action(const std::u8string&)> select {};
    };

    // 카탈로그와 시스템 강조색을 고르는 색상 견본이다. 충분한 클릭 영역과 선택 표시를 제공한다.
    // 받은 폭에 드는 만큼 한 줄에 놓되 줄마다 개수를 고르게 나눈다 — 마지막 줄에 한두 개만 남지 않는다.
    class accent_picker_element final : public ui_element
    {
    public:
        explicit accent_picker_element(accent_picker_config config);
        [[nodiscard]] static float height_for(const accent_picker_config& config, float width) noexcept;
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        accent_picker_config config_ {};
    };
}
