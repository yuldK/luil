#pragma once

#include "luil/ui/text_block.h"
#include "luil/ui/ui_element.h"

namespace luil {
    enum class label_color_role
    {
        primary,
        dim,
        error,
    };

    // 바탕 역할이다.
    // 구체 색은 theme이 그리기 시점에 정한다.
    enum class label_background_role
    {
        none,
        // notice 배너다.
        // 일반 표면과 같은 색으로 보이지 않도록 바탕을 깐다.
        notice,
        // 뜻 없는 중립 바탕을 모서리를 둥글려 깐다 — 번호·짧은 꼬리표처럼 옆 글과 한 덩어리로 읽히지 않아야 할 때다.
        //  - 모서리는 `ui_metrics::control_corner_radius`다.
        raised,
    };

    struct label_config
    {
        std::u8string text {};
        float font_size { 12.0f };
        label_color_role color { label_color_role::dim };
        label_background_role background { label_background_role::none };
        // 바탕이 있을 때 글자를 좌우로 들여쓰는 여백이다 (논리 픽셀).
        float padding { 0.0f };
        text_align align { text_align::start };
        text_elide elide { text_elide::end };
        // 폭에서 줄을 바꾼다 (설명·오류 글 — label-wrap-design.md). 거짓이면 지금처럼 한 줄이다.
        //  - 줄 바꿈은 여러 줄 칸과 같은 text_layout 규칙이다: 공백에서 먼저 바꾸고, 공백이 없는 긴 낱말은 글자소
        //    경계에서 바꾼다. 조합형 한글·이모지 결합 글자가 줄 경계에서 갈리지 않는다. `\n`은 줄 바꿈이다.
        //  - 바꾼 줄도 줄마다 `align`을 따른다. `elide`는 한 줄 label의 것이다 — 넘친 마지막 줄은 늘 끝을 줄인다.
        bool wrap { false };
        // 바꾼 줄의 최대 수다. 0이면 무제한이다. 넘치면 마지막 줄의 끝을 `…`로 줄인다.
        std::size_t max_lines { 0 };
    };

    // 줄을 바꾼 결과다 (`label_element::measure_wrap`). 직접 그리는 글과 같은 값이다 (`measure_text_block`).
    //  - 폭은 여백을 뺀 논리 픽셀이고, 높이는 보이는 줄 높이의 합이다.
    using label_wrap = text_block_layout;

    // 텍스트다. 기본은 한 줄이고 `wrap`이면 여러 줄이다.
    // 한 줄은 bounds 안에서 설정한 가로 정렬과 세로 중앙 baseline으로 그린다.
    //  - 그리기는 `draw_text_block`이다. element 없이 같은 글을 그리려는 element는 그 함수를 부른다.
    class label_element final : public ui_element
    {
    public:
        label_element(ui_element_id id, label_config config);

        // `wrap`인 설정을 폭 `available_width`(논리 픽셀, 여백 포함)에서 바꾼 결과다. 그리기·배치와 같은 규칙이다.
        //  - 한 줄의 높이는 `height_for`다 — 한 줄 label과 같은 행 높이가 줄마다 쌓인다.
        [[nodiscard]] static label_wrap measure_wrap(const label_config& config, const measure_context& context, float available_width);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        // 글 한 줄의 폭(양옆 `padding` 포함)과 `height_for` 높이다. 폭은 올림한다 — 잰 폭에 꼭 맞춘 자리에서
        // 부동소수 잔차로 말줄임이 서지 않게 한다.
        //  - `wrap`이면 그 폭에서 바꾼 줄 수만큼의 높이다 (`measure_wrap`). 폭은 바꾼 줄이 있으면 받은 폭이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

        // 한 줄이 차지하는 자연 높이다 (논리 픽셀).
        // 글자 크기에 위아래 숨 쉴 여백을 더한 값이라 stack의 고정 길이로 바로 넣는다.
        // 호출부마다 흩어지던 높이 매직 넘버를 대신한다.
        [[nodiscard]] static float height_for(const label_config& config) noexcept;

    private:
        label_config config_ {};
        // `wrap`일 때 배치가 바꾼 줄이다 (물리 픽셀 배치의 결과를 그리기가 쓴다).
        label_wrap wrapped_ {};
    };
}
