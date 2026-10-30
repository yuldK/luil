#pragma once

#include "luil/ui/text_layout.h"
#include "luil/ui/ui_tree.h"

namespace luil {
    struct markdown_span
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        bool bold { false };
        bool italic { false };
        bool code { false };
        bool strike { false };
        std::u8string link {};
        std::size_t link_id { 0 };
    };

    struct markdown_block
    {
        std::u8string text {};
        std::vector<markdown_span> spans {};
        unsigned heading { 0 };
        unsigned indent { 0 };
        bool quote { false };
        bool code { false };
        bool rule { false };
    };

    // CommonMark의 문단·제목·강조·코드·목록·인용·링크와 취소선을 파싱한다.
    // HTML은 글로 표시하며 이미지의 대체 글만 읽는다. 외부 자원을 가져오지 않는다.
    [[nodiscard]] std::vector<markdown_block> parse_markdown(std::u8string_view text);

    struct markdown_config
    {
        std::u8string text {};
        float font_size { 12.0f };
        float line_height { 18.0f };
        float block_spacing { 8.0f };
        // 링크를 눌렀을 때 앱 액션을 만든다. 없으면 링크는 표시만 한다.
        std::function<input_action(std::u8string_view destination)> activate_link {};
    };

    // 선택·편집 상태가 없는 마크다운 본문이다. 링크는 자식 접근 요소이자 클릭 대상이다.
    class markdown_element final : public ui_element
    {
    public:
        markdown_element(ui_element_id id, markdown_config config);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;
        [[nodiscard]] std::optional<size_f> preferred_size(const measure_context& context, float available_width) const override;
        [[nodiscard]] const std::vector<markdown_block>& blocks() const noexcept
        {
            return blocks_;
        }

    private:
        struct block_layout
        {
            text_layout text {};
            rect_f box {};
            zoom_point origin {};
        };
        [[nodiscard]] std::vector<block_layout> layout(const measure_context& context, float width) const;
        markdown_config config_ {};
        std::vector<markdown_block> blocks_ {};
        std::vector<block_layout> layouts_ {};
        std::vector<std::size_t> link_ids_ {};
    };

    // 마크다운을 논리 좌표로 측정·배치해 툴팁에 붙일 내용으로 만든다.
    [[nodiscard]] tooltip_config make_markdown_tooltip(markdown_config config, float width = 320.0f, tooltip_config appearance = {});
}
