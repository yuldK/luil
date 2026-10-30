#pragma once

#include "luil/ui/layout_metrics.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace luil {
    // 표 배치의 열 하나 또는 행 하나의 길이 규칙이다.
    enum class grid_track_kind
    {
        // `length`만큼이다.
        fixed,
        // 그 줄에 놓인 자식이 원하는 크기 중 가장 큰 것이다 (`ui_element::preferred_size`). 폼의 라벨 열이다.
        fit,
        // 고정·맞춤 줄을 뺀 나머지를 `weight` 비율로 나눠 갖는다.
        flexible,
    };

    // 열·행 하나다 (길이는 전부 논리 픽셀).
    //  - **없음은 언제나 0으로 말한다** (`stack_item`과 같은 규칙).
    struct grid_track
    {
        grid_track_kind kind { grid_track_kind::fit };
        // `fixed`의 길이다.
        float length { 0.0f };
        // `flexible`의 비율이다. 0 이하면 1로 본다.
        float weight { 0.0f };
        // 맞춤·유연 줄이 반드시 받는 길이다. 유연 줄의 부족분은 stack처럼 다른 유연 줄의 몫에서 나오고, 유연 줄
        // 하한들의 합이 남은 자리를 넘을 때만 넘친다. 맞춤 줄의 하한은 고정 길이처럼 먼저 빠진다.
        float minimum { 0.0f };

        [[nodiscard]] static constexpr grid_track fixed(const float length) noexcept
        {
            return { .kind = grid_track_kind::fixed, .length = length };
        }

        [[nodiscard]] static constexpr grid_track fit(const float minimum = 0.0f) noexcept
        {
            return { .kind = grid_track_kind::fit, .minimum = minimum };
        }

        [[nodiscard]] static constexpr grid_track flexible(const float weight = 1.0f, const float minimum = 0.0f) noexcept
        {
            return { .kind = grid_track_kind::flexible, .weight = weight, .minimum = minimum };
        }

        [[nodiscard]] bool operator==(const grid_track&) const noexcept = default;
    };

    struct grid_config
    {
        // 열과 행의 규칙이다. 자식이 그 너머의 칸에 놓이면 모자란 줄은 맞춤 줄로 본다.
        std::vector<grid_track> columns {};
        std::vector<grid_track> rows {};
        // 열 사이·행 사이 간격이다 (논리 픽셀).
        float column_spacing { 0.0f };
        float row_spacing { 0.0f };
        edge_insets padding {};
    };

    // 자식 하나가 놓이는 칸이다.
    struct grid_cell
    {
        std::size_t row { 0 };
        std::size_t column { 0 };
        // 걸치는 줄 수다. 0이면 1로 본다.
        std::size_t row_span { 1 };
        std::size_t column_span { 1 };
        // 칸 안의 자리다. `stretch`가 아니면 자식이 원하는 크기에 선다 (답이 없으면 칸을 채운다).
        layout_alignment horizontal { layout_alignment::stretch };
        layout_alignment vertical { layout_alignment::stretch };
    };

    // 자식을 행과 열의 칸에 놓는 배치 컨테이너다 (Qt `QGridLayout`, WPF `Grid`).
    //
    // 폼은 이 배치의 한 사용법이다. 라벨 열을 맞춤(`fit`)으로, 입력 열을 유연(`flexible`)으로 두면 라벨 열이 가장 긴
    // 라벨 폭이 되고 언어가 바뀌어도 손으로 고칠 폭이 없다.
    //  - **열 폭을 먼저, 행 높이를 나중에 정한다.** 맞춤 행의 높이는 자식이 자기 칸 폭에서 원하는 높이라, 여러 줄
    //    글이 열 폭에서 접힌 높이가 된다.
    //  - 맞춤 줄은 그 줄 **하나에만** 놓인 자식만 잰다. 여러 줄에 걸친 자식은 걸친 줄을 넓히지 않는다 — 넓힐 몫을
    //    어느 줄에 줄지가 규칙마다 달라 읽는 사람이 예측할 수 없다. 걸친 자식은 줄들이 정해진 자리를 받는다.
    //  - 칸 밖으로 넘치는 자식을 자르지 않는다 (stack과 같다).
    //  - 그리기와 hit test 순서는 담은 순서다. 칸이 겹치면 뒤에 담은 것이 위다.
    class grid_element final : public ui_element
    {
    public:
        grid_element(ui_element_id id, grid_config config);

        void add(std::unique_ptr<ui_element> child, grid_cell cell);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 유연 줄도 맞춤처럼 잰 크기다 (하한과 자식의 원하는 크기 중 큰 것). 자식이 하나도 없으면 답하지 않는다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

        // `arrange`가 정한 열 폭과 행 높이다 (논리 픽셀). test와 진단이 읽는다.
        [[nodiscard]] const std::vector<float>& column_widths() const noexcept;
        [[nodiscard]] const std::vector<float>& row_heights() const noexcept;

    private:
        struct entry
        {
            ui_element* child { nullptr };
            grid_cell cell {};
        };

        // 열 폭이다 (논리 픽셀). `inner_width`가 무한대면 유연 열도 맞춤으로 잰다.
        [[nodiscard]] std::vector<float> measure_columns(const measure_context& context, float inner_width) const;
        // 그 열 폭에서의 행 높이다 (논리 픽셀). `inner_height`가 무한대면 유연 행도 맞춤으로 잰다.
        [[nodiscard]] std::vector<float> measure_rows(const measure_context& context, const std::vector<float>& columns, float inner_height) const;

        grid_config config_ {};
        std::vector<entry> entries_ {};
        std::vector<float> column_widths_ {};
        std::vector<float> row_heights_ {};
    };
}
