#pragma once

#include "luil/ui/text_document.h"
#include "luil/ui/ui_element.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace luil {
    // 링크를 여는 앱 메시지를 짓는다 (`text_area_config::activate_link`·`text_input_config::activate_link`).
    using text_link_activation = std::function<input_action(std::u8string_view destination)>;

    // 텍스트 칸 안의 링크 하나다 (`text_area_element`·`text_input_element`의 자식).
    //
    // 글자와 밑줄은 칸이 그린다 — 줄·조합·선택과 함께 칠해야 해서다. 이것은 그 위의 보이지 않는 자리다.
    //  - hover·머무름 tooltip·포인터 모양·보조 기술의 Hyperlink(Invoke)가 이것의 몫이다.
    //  - 편집 칸 안이면 수식 키 없는 누름은 칸이 받는다 (`ui_element::inline_link`). 링크 위에서도 caret을 놓고 끌어
    //    고른다. Ctrl+누름만 이것의 클릭이다.
    //  - 칸이 배치에서 화면 조각을 놓는다 (`place`). 창 밖으로 흘린 줄의 링크는 조각이 없어 hit하지 않는다.
    class text_link_element final : public ui_element
    {
    public:
        // `name`은 보이는 글이다 (보조 기술이 읽는 이름). `hint`는 편집 칸의 tooltip에서 주소 뒤에 붙는 안내다.
        text_link_element(ui_element_id id, const text_link& link, std::u8string name, bool editable, const text_link_activation& activate, std::u8string_view hint);

        [[nodiscard]] const std::u8string& destination() const noexcept
        {
            return destination_;
        }
        // 화면 조각과 그것을 자를 창이다 (물리 픽셀).
        void place(std::vector<rect_f> fragments, const rect_f& clip);
        // 밑줄을 굵게 그릴 때다. 포인터가 올라와 있고 지금 누르면 열린다 (편집 칸이면 Ctrl을 누른 채다).
        [[nodiscard]] bool emphasized(const interaction_snapshot& interaction) const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] const ui_element* hit_test(float x, float y) const override;
        [[nodiscard]] ui_cursor cursor_at(float x, float y, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

    private:
        std::u8string name_ {};
        std::u8string destination_ {};
        std::vector<rect_f> fragments_ {};
        bool editable_ { false };
        bool active_ { false };
    };

    // 칸의 링크 자식 id다. kind 수와 owner를 함께 담아 kind가 다른 두 칸의 같은 owner가 겹치지 않는다.
    [[nodiscard]] ui_element_id text_link_id(const ui_element_id& owner, std::size_t index);

    // 칸 글 `text`의 링크마다 자식을 지어 `add`에 그 링크의 순번과 함께 넘긴다. 글을 벗어나거나 빈 구간은 짓지 않는다.
    void make_text_link_elements(const ui_element_id& owner, std::u8string_view text, std::span<const text_link> links, bool editable, const text_link_activation& activate, std::u8string_view hint,
        const std::function<void(std::size_t index, std::unique_ptr<text_link_element> element)>& add);
} // namespace luil
