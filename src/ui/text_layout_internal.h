#pragma once

#include "luil/ui/text_layout.h"

#include <functional>

namespace luil {
    // Skia 객체는 native 구현에만 둔다. 기하 시험은 이 불변 payload를 요구하지 않는다.
    struct text_layout_draw_data
    {
        virtual ~text_layout_draw_data() = default;
        virtual void draw(SkCanvas&, zoom_point origin, const SkPaint&, std::size_t begin, std::size_t end) const = 0;
        [[nodiscard]] virtual std::size_t storage_bytes() const noexcept = 0;
        [[nodiscard]] virtual text_layout relayout(text_layout_request request) const = 0;
    };
    struct text_layout_segment
    {
        float width { 0.0f };
        float height { 0.0f };
        float baseline { 0.0f };
        rect_f ink_bounds {};
        float ascent { 0.0f };
        float descent { 0.0f };
        std::vector<text_layout_cluster> clusters {};
        std::vector<text_layout_run> runs {};
        std::shared_ptr<const text_layout_draw_data> drawing {};
    };
    struct text_layout_data
    {
        std::u8string text {};
        text_layout_backend backend { text_layout_backend::unavailable };
        text_layout_size size {};
        std::vector<text_layout_line> lines {};
        std::vector<text_layout_cluster> clusters {};
        std::vector<text_layout_run> runs {};
        std::vector<text_layout_stop> stops {};
        std::vector<std::shared_ptr<const text_layout_draw_data>> drawings {};
        bool approximate { false };
    };
    struct text_layout_access
    {
        [[nodiscard]] static text_layout make(std::shared_ptr<const text_layout_data> data)
        {
            text_layout value {};
            value.data_ = std::move(data);
            return value;
        }
        [[nodiscard]] static const text_layout_data* get(const text_layout& value) noexcept { return value.data_.get(); }
    };
    [[nodiscard]] text_layout remake_text_layout(const text_layout& reference, text_layout_request request);
    using text_layout_shaper = std::function<text_layout_segment(std::u8string_view text, std::size_t document_offset)>;
    [[nodiscard]] text_layout build_text_layout(text_layout_request request, text_layout_backend backend, const text_layout_shaper& shape);
} // namespace luil
