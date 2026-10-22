#pragma once

#include "luil/ui/ui_element.h"

#include <limits>

class SkCanvas;
class SkPaint;

namespace luil {
    enum class text_layout_face
    {
        ui,
        code
    };
    enum class text_layout_backend
    {
        unavailable,
        measured,
        primitive,
        harfbuzz
    };

    struct text_layout_style
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        bool bold { false };
        bool italic { false };
        bool code { false };
        [[nodiscard]] bool operator==(const text_layout_style&) const noexcept = default;
    };

    struct text_layout_request
    {
        std::u8string text {};
        // 물리 픽셀이다. 배율은 크기·폭·줄 높이에 반영해서 넘긴다.
        float font_size { 12.0f };
        text_layout_face face { text_layout_face::ui };
        float width { 0.0f };
        bool wrap { false };
        float line_height { 0.0f };
        // begin 순서의 비중첩 글자소 범위다. 색은 배치와 분리한다.
        std::vector<text_layout_style> styles {};
        [[nodiscard]] bool operator==(const text_layout_request&) const noexcept = default;
    };

    struct text_layout_size
    {
        float width { 0.0f };
        float height { 0.0f };
    };
    struct text_layout_cluster
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        std::size_t line { 0 };
        float left { 0.0f };
        float right { 0.0f };
        bool rtl { false };
    };
    struct text_layout_run
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        std::size_t line { 0 };
        std::size_t glyph_count { 0 };
        bool rtl { false };
    };
    struct text_layout_stop
    {
        std::size_t offset { 0 };
        float x { 0.0f };
    };
    struct text_layout_line
    {
        std::size_t begin { 0 };
        std::size_t end { 0 };
        std::size_t logical_line { 0 };
        bool wrapped { false };
        float width { 0.0f };
        float top { 0.0f };
        float height { 0.0f };
        float baseline { 0.0f };
        std::size_t stop_begin { 0 };
        std::size_t stop_count { 0 };
        bool increasing { true };
        rect_f ink_bounds {};
        float ascent { 0.0f };
        float descent { 0.0f };
    };

    struct text_layout_data;
    struct text_layout_access;

    // Glyph·글꼴·원문을 소유하는 불변 값이다. 복사는 같은 발행본을 공유한다.
    class text_layout
    {
    public:
        text_layout() = default;
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] std::u8string_view text() const noexcept;
        [[nodiscard]] text_layout_backend backend() const noexcept;
        [[nodiscard]] text_layout_size size() const noexcept;
        [[nodiscard]] std::span<const text_layout_line> lines() const noexcept;
        [[nodiscard]] std::span<const text_layout_cluster> clusters() const noexcept;
        [[nodiscard]] std::span<const text_layout_run> runs() const noexcept;
        [[nodiscard]] std::span<const text_layout_stop> caret_stops() const noexcept;
        // 합자 안의 글자소 caret을 균등 분할했는가다. SkShaper는 OpenType caret 표를 내주지 않는다.
        [[nodiscard]] bool approximate_ligature_carets() const noexcept;
        [[nodiscard]] std::size_t storage_bytes() const noexcept;
        [[nodiscard]] std::size_t line_at(std::size_t offset, bool upstream = false) const noexcept;
        [[nodiscard]] rect_f caret_rect(std::size_t offset, bool upstream = false) const noexcept;
        [[nodiscard]] text_caret_position hit_test(zoom_point point) const noexcept;
        [[nodiscard]] std::vector<rect_f> selection_rects(std::size_t begin, std::size_t end) const;

    private:
        friend struct text_layout_access;
        std::shared_ptr<const text_layout_data> data_ {};
    };

    [[nodiscard]] text_layout make_text_layout(text_layout_request request);
    // 주입한 측정기의 기하만 만든다. native glyph payload·전역 cache를 섞지 않는다.
    [[nodiscard]] text_layout make_text_layout(text_layout_request request, const text_measurer& measurer);
    [[nodiscard]] bool ui_text_shaping_enabled() noexcept;
    // 글꼴·언어·fallback·품질 변경을 다음 배치에 반영한다. 발행된 값은 바꾸지 않는다.
    void invalidate_text_layouts() noexcept;
    [[nodiscard]] std::uint64_t text_layout_revision() noexcept;

    struct text_layout_cache_statistics
    {
        std::size_t entries { 0 };
        std::size_t bytes { 0 };
        std::uint64_t hits { 0 };
        std::uint64_t misses { 0 };
    };
    [[nodiscard]] text_layout_cache_statistics text_layout_cache_usage();
    void clear_text_layout_cache();

    // origin은 첫 줄의 왼쪽 위다. 범위는 원문 byte offset이며 전체 glyph를 다시 shape하지 않는다.
    void draw_text_layout(SkCanvas& canvas, const text_layout& layout, zoom_point origin, const SkPaint& paint, std::size_t begin = 0, std::size_t end = std::numeric_limits<std::size_t>::max());
} // namespace luil
