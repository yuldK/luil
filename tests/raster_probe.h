#pragma once

#include "luil/theme/ui_theme.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkBitmap.h"

#include <chrono>

namespace luil::testing {
    // tree를 SkBitmap에 그려 픽셀을 조회한다. 창이나 GPU는 필요하지 않다.
    // plan_drag_overlay·text_button_fill_for 같은 순수 판정 함수의 결과가
    // 실제 그리기 경로에 반영되는지 검사한다.
    class raster_frame
    {
    public:
        // 배율 2가 기본이다.
        // 그리기는 전부 안티에일리어싱이 켜져 있어(`solid_paint`), 배율 1에서는
        // 1 논리 픽셀 획이 물리 픽셀 하나에 반씩 섞여 **어떤 팔레트 색과도 정확히
        // 같지 않다.** 배율 2면 그 획이 물리 2픽셀을 온전히 덮어 색이 그대로 남는다.
        //  - 그래서 이 도우미가 다루는 자리(slot·질의 영역)는 전부 **물리 픽셀**이다.
        raster_frame(int width, int height, ui_color_palette palette, float scale = 2.0f);

        // 배경을 `window_background`로 지운 뒤 tree를 그린다.
        // `draw_frame`이 하는 두 줄과 같아, 아무도 그리지 않은 자리는 언제나
        // 배경색이다.
        //  - 글꼴은 싣지 않는다. 글이 빈 element는 글꼴 없이 그대로 그려지고
        //    (`draw_text`가 빈 글에서 곧바로 돌아온다), 이 축이 보는 것은 도형과
        //    색이라 OS 글꼴에 매이지 않는 편이 낫다.
        //  - 시각의 기본값은 시계의 원점(epoch)이다. tooltip 지연·caret 깜빡임·
        //    spinner 각도가 전부 `now`의 함수라, 실제 시계를 쓰면 같은 tree가
        //    실행마다 다른 픽셀을 낸다 — 상수라야 같은 tree가 같은 그림이다.
        //  - **`now`를 받는 이유는 그림 자체가 시각의 함수인 축이 생겨서다.**
        //    움직이는 그림은 어느 장이 서는지가 `config.playback`과 이 시각에서만
        //    나오므로, 시각을 고를 수 없으면 언제나 첫 장만 보게 되어 재생이
        //    픽셀로 잠기지 않는다 (`image_animation_tests.cpp`). 기본값이 지금까지
        //    쓰던 그 값이라 시각을 묻지 않는 test는 하나도 달라지지 않는다.
        void draw(const ui_tree& tree, const interaction_snapshot& interaction, std::chrono::steady_clock::time_point now = {});

        [[nodiscard]] const ui_color_palette& palette() const noexcept;

        // 물리 픽셀 하나의 색이다 (알파를 푼 ARGB — `ui_color`와 같은 배치다).
        // 화면 밖이면 0이다.
        [[nodiscard]] ui_color pixel_at(int x, int y) const;
        // area 안에서 지정한 색의 픽셀 수다.
        // 강조색 테와 강조색 채움을 구분하려면 색의 유무뿐 아니라 면적도 검사한다.
        [[nodiscard]] int count_color(const rect_f& area, ui_color color) const;
        [[nodiscard]] bool contains_color(const rect_f& area, ui_color color) const;

    private:
        ui_color_palette palette_ {};
        float scale_ { 2.0f };
        SkBitmap pixels_ {};
    };
} // namespace luil::testing
