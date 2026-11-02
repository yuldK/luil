#pragma once

#include "luil/ui/message_box.h"
#include "luil/ui/text_block.h"
#include "luil/ui/ui_element.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace luil {
    // 확인 대화 상자의 내용 element와 그 규칙이다. 공개하지 않는다. 앱은 `make_message_box`로 짓는다.

    // 마지막 accept 단추다. Enter의 대상(기본 버튼)이다. destructive는 되지 않는다.
    [[nodiscard]] std::optional<std::size_t> message_box_default_index(std::span<const message_box_button> buttons) noexcept;
    // 뜨는 순간 초점이 설 단추다. 마지막 accept, 없으면 마지막 reject, 그것도 없으면 첫 단추다.
    [[nodiscard]] std::optional<std::size_t> message_box_entry_index(std::span<const message_box_button> buttons) noexcept;
    // Esc와 바깥 누름이 누르는 단추다. 마지막 reject이고, reject가 없으면서 단추가 하나이면 그 단추다.
    [[nodiscard]] std::optional<std::size_t> message_box_dismiss_index(std::span<const message_box_button> buttons) noexcept;

    // 단추 폭들(논리 픽셀)을 사이 간격과 함께 한 행에 놓을 수 없어 쌓아야 하는가다. 폭의 합이 안쪽 폭과 같으면 한 행이다.
    [[nodiscard]] bool message_box_stacks_buttons(std::span<const float> widths, float inner_width) noexcept;

    // 제목, 본문, 글리프를 직접 그리고 확인 칸과 단추를 자식으로 둔다.
    // 단추를 한 행에 둘지 쌓을지는 폭을 알아야 정하는데 stack은 만들 때 방향을 정하므로 한 element로 짓는다 (정보 막대와 같은 까닭).
    //  - 높이는 폭에서 나온다 (`preferred_size`). 줄바꿈과 쌓임이 폭에 달려 있어서다. 자리가 모자라면 본문의 줄 수를 줄인다.
    //  - 자식 순서가 Tab 순서다. 확인 칸, 단추들(준 순서)이다.
    class message_box_element final : public ui_element
    {
    public:
        explicit message_box_element(message_box_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 본문이 있으면 `static_text`(이름은 전체 본문), 없으면 구조(none)다.
        [[nodiscard]] access_info accessibility() const override;
        // 폭이 정해지지 않았으면 답하지 않는다. 폭은 받은 폭이고 높이는 그 폭에서 글과 단추가 필요로 하는 값이다.
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;

    private:
        // arrange가 정한 자리다 (논리 픽셀, 상자 왼쪽 위 기준). 그리기가 줄을 다시 바꾸지 않는다.
        struct placement
        {
            float text_left { 0.0f };
            float text_width { 0.0f };
            float title_top { 0.0f };
            float title_height { 0.0f };
            float body_top { 0.0f };
            std::size_t body_max_lines { 0 };
            text_block_layout title {};
            text_block_layout body {};
        };

        // 단추 폭과 쌓임 같은 규칙은 `.cpp`의 계획 함수 하나가 정한다. 단추의 글 폭은 단추에게 묻는다.
        [[nodiscard]] std::vector<float> button_widths(const measure_context& context) const;

        message_box_config config_ {};
        ui_element* check_ { nullptr };
        std::vector<ui_element*> buttons_ {};
        placement placement_ {};
    };
}
