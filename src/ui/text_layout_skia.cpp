#include "ui/text_layout_skia.h"

#include "host/font_registry.h"
#include "luil/text/grapheme.h"
#include "luil/text/utf8_text.h"
#include "luil/ui/draw_primitives.h"
#include "ui/text_layout_internal.h"
#include "ui/text_shaping.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#if defined(LUIL_UI_TEXT_LAYOUT)
#include "modules/skshaper/include/SkShaper_factory.h"
#endif

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>

namespace luil {
    namespace {
        struct glyph_group
        {
            std::size_t begin { 0 };
            std::size_t end { 0 };
            std::size_t first { 0 };
            std::size_t count { 0 };
        };
        struct glyph_run
        {
            SkFont font {};
            std::vector<SkGlyphID> glyphs {};
            std::vector<SkPoint> positions {};
            std::vector<glyph_group> groups {};
        };
        struct glyph_drawing final : text_layout_draw_data
        {
            SkFont base_font {};
            std::vector<glyph_run> runs {};
            text_layout relayout(text_layout_request request) const override
            {
                return make_font_text_layout(std::move(request), base_font);
            }
            void draw(SkCanvas& canvas, const zoom_point origin, const SkPaint& paint, const std::size_t begin, const std::size_t end) const override
            {
                for (const auto& run : runs)
                {
                    const auto first { std::lower_bound(run.groups.begin(), run.groups.end(), begin, [](const auto& group, const auto at) { return group.end <= at; }) };
                    std::size_t glyph_begin { run.glyphs.size() };
                    std::size_t glyph_end { 0 };
                    for (auto group { first }; group != run.groups.end() && group->begin < end; ++group)
                    {
                        glyph_begin = std::min(glyph_begin, group->first);
                        glyph_end = std::max(glyph_end, group->first + group->count);
                    }
                    if (glyph_begin < glyph_end)
                        canvas.drawGlyphs(
                            { run.glyphs.data() + glyph_begin, glyph_end - glyph_begin }, { run.positions.data() + glyph_begin, glyph_end - glyph_begin }, { origin.x, origin.y }, run.font, paint);
                }
            }
            std::size_t storage_bytes() const noexcept override
            {
                std::size_t size { sizeof(glyph_drawing) + runs.capacity() * sizeof(glyph_run) };
                for (const auto& run : runs)
                    size += run.glyphs.capacity() * sizeof(SkGlyphID) + run.positions.capacity() * sizeof(SkPoint) + run.groups.capacity() * sizeof(glyph_group);
                return size;
            }
        };

        rect_f ink_of(const glyph_drawing& drawing)
        {
            SkRect ink { SkRect::MakeEmpty() };
            for (const auto& run : drawing.runs)
            {
                std::vector<SkRect> bounds(run.glyphs.size());
                run.font.getBounds(run.glyphs, bounds, nullptr);
                for (std::size_t index { 0 }; index < bounds.size(); ++index)
                    ink.join(bounds[index].makeOffset(run.positions[index].x(), run.positions[index].y()));
            }
            return { ink.x(), ink.y(), ink.width(), ink.height() };
        }

        struct font_segment
        {
            std::size_t begin;
            std::size_t end;
            SkFont font;
        };

        bool invisible_format(const char32_t codepoint) noexcept
        {
            return codepoint == 0x200D || (codepoint >= 0xFE00 && codepoint <= 0xFE0F) || (codepoint >= 0xE0100 && codepoint <= 0xE01EF) || (codepoint >= 0xE0020 && codepoint <= 0xE007F);
        }

        bool covers(const SkFont& font, const std::u8string_view cluster)
        {
            for (std::size_t at { 0 }; at < cluster.size();)
            {
                const auto point { text::utf8_decode(cluster, at) };
                if (invisible_format(point.codepoint) == false && font.unicharToGlyph(static_cast<SkUnichar>(point.codepoint)) == 0)
                    return false;
                at += point.length;
            }
            return true;
        }

        std::vector<font_segment> fonts_for(const std::u8string_view text, const std::size_t offset, const SkFont& font, const std::span<const text_layout_style> styles, const bool primitive)
        {
            std::vector<font_segment> fonts {};
            auto style { std::lower_bound(styles.begin(), styles.end(), offset, [](const auto& item, const auto at) { return item.end <= at; }) };
            if (primitive)
            {
                // 끈 구성의 glyph·advance는 기존 코드포인트별 fallback과 같다.
                for (std::size_t at { 0 }; at < text.size();)
                {
                    const auto point { text::utf8_decode(text, at) };
                    const auto next { at + point.length };
                    while (style != styles.end() && style->end <= offset + at)
                        ++style;
                    SkFont chosen { font };
                    if (style != styles.end() && style->begin <= offset + at)
                    {
                        chosen.setEmbolden(style->bold);
                        chosen.setSkewX(style->italic ? -0.2f : 0.0f);
                        if (style->code)
                            chosen.setTypeface(configured_code_typeface());
                    }
                    if (chosen.unicharToGlyph(static_cast<SkUnichar>(point.codepoint)) == 0)
                        if (auto fallback { text_layout_fallback(point.codepoint) }; fallback != nullptr)
                            chosen.setTypeface(std::move(fallback));
                    if (fonts.empty() == false && fonts.back().font == chosen)
                        fonts.back().end = next;
                    else
                        fonts.push_back({ at, next, std::move(chosen) });
                    at = next;
                }
                return fonts;
            }
            const auto boundaries { text::grapheme_boundaries(text) };
            for (std::size_t index { 0 }; index + 1 < boundaries.size(); ++index)
            {
                const auto at { boundaries[index] };
                const auto next { boundaries[index + 1] };
                while (style != styles.end() && style->end <= offset + at)
                    ++style;
                SkFont chosen { font };
                if (style != styles.end() && style->begin <= offset + at)
                {
                    chosen.setEmbolden(style->bold);
                    chosen.setSkewX(style->italic ? -0.2f : 0.0f);
                    if (style->code)
                        chosen.setTypeface(configured_code_typeface());
                }
                const auto cluster { text.substr(at, next - at) };
                if (covers(chosen, cluster) == false)
                {
                    // 하나의 글자소를 지원하는 face를 먼저 고른다. ZWJ·결합 부호를 font run으로 쪼개지 않는다.
                    //  - 다 담는 face가 없으면 처음 받은 대체 face로 그린다. 끈 구성과 같이 그 face의 빈 네모가 보인다.
                    //    주 글꼴이 아이콘 글꼴이면 빈 글자의 폭이 0이라 글이 사라진다 — 빈 네모가 보이는 편이 침묵보다 낫다.
                    std::optional<SkFont> uncovered {};
                    bool found { false };
                    for (std::size_t cursor { at }; cursor < next;)
                    {
                        const auto point { text::utf8_decode(text, cursor) };
                        if (auto fallback { text_layout_fallback(point.codepoint) }; fallback != nullptr)
                        {
                            SkFont candidate { chosen };
                            candidate.setTypeface(std::move(fallback));
                            if (covers(candidate, cluster))
                            {
                                chosen = std::move(candidate);
                                found = true;
                                break;
                            }
                            if (uncovered.has_value() == false)
                                uncovered = std::move(candidate);
                        }
                        cursor += point.length;
                    }
                    if (found == false && uncovered.has_value())
                        chosen = std::move(*uncovered);
                }
                if (fonts.empty() == false && fonts.back().font == chosen)
                    fonts.back().end = next;
                else
                    fonts.push_back({ at, next, std::move(chosen) });
            }
            return fonts;
        }

        void set_metrics(text_layout_segment& result, const SkFont& font, const float requested, const glyph_drawing* drawing = nullptr)
        {
            SkFontMetrics metrics {};
            font.getMetrics(&metrics);
            if (drawing != nullptr)
                for (const auto& run : drawing->runs)
                {
                    SkFontMetrics current {};
                    run.font.getMetrics(&current);
                    metrics.fAscent = std::min(metrics.fAscent, current.fAscent);
                    metrics.fDescent = std::max(metrics.fDescent, current.fDescent);
                    metrics.fLeading = std::max(metrics.fLeading, current.fLeading);
                }
            result.ascent = -metrics.fAscent;
            result.descent = metrics.fDescent;
            result.height = requested > 0.0f ? requested : std::max(1.0f, metrics.fDescent - metrics.fAscent + metrics.fLeading);
            result.baseline = result.height * 0.5f - (metrics.fAscent + metrics.fDescent) * 0.5f;
        }

        text_layout_segment primitive_segment(const std::u8string_view text, const std::vector<font_segment>& fonts, const SkFont& font, const float height)
        {
            text_layout_segment result {};
            set_metrics(result, font, height);
            auto drawing { std::make_shared<glyph_drawing>() };
            drawing->base_font = font;
            float pen { 0.0f };
            std::vector<text_layout_stop> ends { { 0, 0.0f } };
            for (const auto& part : fonts)
            {
                glyph_run run {};
                run.font = part.font;
                for (std::size_t at { part.begin }; at < part.end;)
                {
                    const auto decoded { text::utf8_decode(text, at) };
                    const auto next { at + decoded.length };
                    const SkGlyphID glyph { part.font.unicharToGlyph(static_cast<SkUnichar>(decoded.codepoint)) };
                    float width {};
                    part.font.getWidths({ &glyph, 1 }, { &width, 1 });
                    run.groups.push_back({ at, next, run.glyphs.size(), 1 });
                    run.glyphs.push_back(glyph);
                    run.positions.push_back({ pen, 0.0f });
                    pen += width;
                    ends.push_back({ next, pen });
                    at = next;
                }
                result.runs.push_back({ part.begin, part.end, 0, run.glyphs.size(), false });
                drawing->runs.push_back(std::move(run));
            }
            const auto boundaries { text::grapheme_boundaries(text) };
            const auto advance = [&ends](const std::size_t at) {
                const auto found { std::lower_bound(ends.begin(), ends.end(), at, [](const auto& stop, const auto offset) { return stop.offset < offset; }) };
                return found != ends.end() ? found->x : ends.back().x;
            };
            for (std::size_t index { 0 }; index + 1 < boundaries.size(); ++index)
                result.clusters.push_back({ boundaries[index], boundaries[index + 1], 0, advance(boundaries[index]), advance(boundaries[index + 1]), false });
            result.width = pen;
            result.ink_bounds = ink_of(*drawing);
            set_metrics(result, font, height, drawing.get());
            result.drawing = std::move(drawing);
            return result;
        }

#if defined(LUIL_UI_TEXT_LAYOUT)
        class layout_font_iterator final : public SkShaper::FontRunIterator
        {
        public:
            explicit layout_font_iterator(const std::vector<font_segment>& fonts)
                : fonts_ { fonts }
            {}
            void consume() override
            {
                current_ = next_++;
                end_ = fonts_[current_].end;
            }
            std::size_t endOfCurrentRun() const override
            {
                return end_;
            }
            bool atEnd() const override
            {
                return next_ == fonts_.size();
            }
            const SkFont& currentFont() const override
            {
                return fonts_[current_].font;
            }

        private:
            const std::vector<font_segment>& fonts_;
            std::size_t current_ { 0 };
            std::size_t next_ { 0 };
            std::size_t end_ { 0 };
        };

        class layout_run_handler final : public SkShaper::RunHandler
        {
        public:
            text_layout_segment result {};
            std::shared_ptr<glyph_drawing> drawing { std::make_shared<glyph_drawing>() };
            void beginLine() override
            {
                pen_ = 0.0f;
            }
            void runInfo(const RunInfo&) override
            {}
            void commitRunInfo() override
            {}
            Buffer runBuffer(const RunInfo& info) override
            {
                glyph_run run {};
                run.font = info.fFont;
                run.glyphs.resize(info.glyphCount);
                run.positions.resize(info.glyphCount);
                drawing->runs.push_back(std::move(run));
                advances_.assign(info.glyphCount + 1, {});
                offsets_.assign(info.glyphCount, {});
                clusters_.assign(info.glyphCount, 0);
                return { drawing->runs.back().glyphs.data(), advances_.data(), offsets_.data(), clusters_.data(), { pen_, 0.0f } };
            }
            void commitRunBuffer(const RunInfo& info) override
            {
                auto& run { drawing->runs.back() };
                const bool rtl { (info.fBidiLevel & 1u) != 0 };
                const auto end { info.utf8Range.end() };
                advances_.back() = { pen_ + info.fAdvance.x(), info.fAdvance.y() };
                for (std::size_t index { 0 }; index < run.glyphs.size();)
                {
                    const auto first { index };
                    const auto begin { static_cast<std::size_t>(clusters_[index]) };
                    while (index < run.glyphs.size() && clusters_[index] == begin)
                        ++index;
                    run.groups.push_back({ begin, end, first, index - first });
                    const float left { std::min(advances_[first].x(), advances_[index].x()) };
                    const float right { std::max(advances_[first].x(), advances_[index].x()) };
                    result.clusters.push_back({ begin, end, 0, left, right, rtl });
                }
                for (std::size_t index { 0 }; index < run.positions.size(); ++index)
                    run.positions[index] = { advances_[index].x() + offsets_[index].x(), advances_[index].y() + offsets_[index].y() };
                std::sort(run.groups.begin(), run.groups.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
                for (std::size_t index { 0 }; index + 1 < run.groups.size(); ++index)
                    run.groups[index].end = run.groups[index + 1].begin;
                result.runs.push_back({ info.utf8Range.begin(), end, 0, info.glyphCount, rtl });
                pen_ += info.fAdvance.x();
            }
            void commitLine() override
            {
                result.width = pen_;
            }
            text_layout_segment finish()
            {
                std::sort(result.clusters.begin(), result.clusters.end(), [](const auto& a, const auto& b) { return a.begin < b.begin; });
                for (std::size_t index { 0 }; index + 1 < result.clusters.size(); ++index)
                    result.clusters[index].end = result.clusters[index + 1].begin;
                result.ink_bounds = ink_of(*drawing);
                result.drawing = std::move(drawing);
                return std::move(result);
            }

        private:
            float pen_ { 0.0f };
            std::vector<SkPoint> advances_ {};
            std::vector<SkPoint> offsets_ {};
            std::vector<std::uint32_t> clusters_ {};
        };
#endif

        struct layout_cache_entry
        {
            text_layout_request request {};
            SkFont font {};
            std::uint64_t revision { 0 };
            text_layout layout {};
            std::size_t bytes { 0 };
        };
        struct layout_cache
        {
            std::mutex mutex {};
            std::list<layout_cache_entry> entries {};
            text_layout_cache_statistics usage {};
        };
        layout_cache& cache()
        {
            static layout_cache state {};
            return state;
        }
        constexpr std::size_t cache_byte_limit { 16 * 1024 * 1024 };
        constexpr std::size_t cache_entry_limit { 512 };
    } // namespace

    bool ui_text_shaping_enabled() noexcept
    {
#if defined(LUIL_UI_TEXT_LAYOUT)
        return true;
#else
        return false;
#endif
    }

    text_layout_cache_statistics text_layout_cache_usage()
    {
        auto& state { cache() };
        const std::scoped_lock lock { state.mutex };
        return state.usage;
    }
    void clear_text_layout_cache()
    {
        auto& state { cache() };
        const std::scoped_lock lock { state.mutex };
        state.entries.clear();
        state.usage = {};
    }

    text_layout make_text_layout(text_layout_request request)
    {
        const auto face { request.face == text_layout_face::code ? configured_code_typeface() : configured_ui_typeface() };
        const SkFont font { face, std::isfinite(request.font_size) ? std::max(0.0f, request.font_size) : 12.0f };
        return make_font_text_layout(std::move(request), font);
    }

    text_layout make_font_text_layout(text_layout_request request, const SkFont& source_font)
    {
        const SkFont font { text_layout_font(source_font) };
        request.font_size = font.getSize();
        const auto revision { text_layout_revision() };
        auto& state { cache() };
        {
            const std::scoped_lock lock { state.mutex };
            const auto matches = [&](const auto& entry) { return entry.revision == revision && entry.font == font && entry.request == request; };
            const auto found { std::find_if(state.entries.begin(), state.entries.end(), matches) };
            if (found != state.entries.end())
            {
                ++state.usage.hits;
                state.entries.splice(state.entries.begin(), state.entries, found);
                return state.entries.front().layout;
            }
            ++state.usage.misses;
        }
        text_layout_backend backend { font.getTypeface() != nullptr ? text_layout_backend::primitive : text_layout_backend::unavailable };
#if defined(LUIL_UI_TEXT_LAYOUT)
        const auto factory { text_shaping_factory() };
        if (backend != text_layout_backend::unavailable && factory != nullptr && factory->getUnicode() != nullptr && text::utf8_is_valid(request.text))
            backend = text_layout_backend::harfbuzz;
#endif
        const auto shape = [&](const std::u8string_view text, const std::size_t offset) {
            const auto fonts { fonts_for(text, offset, font, request.styles, backend != text_layout_backend::harfbuzz) };
#if defined(LUIL_UI_TEXT_LAYOUT)
            if (backend == text_layout_backend::harfbuzz && text.empty() == false)
            {
                auto shaper { factory->makeShaper(nullptr) };
                auto bidi { factory->makeBidiRunIterator(reinterpret_cast<const char*>(text.data()), text.size(), 0) };
                auto script { factory->makeScriptRunIterator(reinterpret_cast<const char*>(text.data()), text.size(), 0) };
                if (shaper != nullptr && bidi != nullptr && script != nullptr)
                {
                    layout_font_iterator iterator { fonts };
                    const auto language { user_ui_language() };
                    SkShaper::TrivialLanguageRunIterator languages { language.empty() ? "und" : language.c_str(), text.size() };
                    layout_run_handler handler {};
                    handler.drawing->base_font = font;
                    shaper->shape(reinterpret_cast<const char*>(text.data()), text.size(), iterator, *bidi, *script, languages, nullptr, 0, std::numeric_limits<float>::max(), &handler);
                    auto segment { handler.finish() };
                    set_metrics(segment, font, request.line_height, static_cast<const glyph_drawing*>(segment.drawing.get()));
                    return segment;
                }
                // backend를 실행 도중 조용히 섞지 않는다.
                throw std::runtime_error { "Cannot create the configured UI text shaper" };
            }
#endif
            return primitive_segment(text, fonts, font, request.line_height);
        };
        const auto layout { build_text_layout(request, backend, shape) };
        const std::size_t bytes { layout.storage_bytes() + sizeof(layout_cache_entry) + request.text.capacity() + request.styles.capacity() * sizeof(text_layout_style) };
        if (bytes <= cache_byte_limit)
        {
            const std::scoped_lock lock { state.mutex };
            while (state.entries.empty() == false && (state.usage.bytes + bytes > cache_byte_limit || state.entries.size() >= cache_entry_limit))
            {
                state.usage.bytes -= state.entries.back().bytes;
                state.entries.pop_back();
            }
            state.entries.push_front({ std::move(request), font, revision, layout, bytes });
            state.usage.bytes += bytes;
            state.usage.entries = state.entries.size();
        }
        return layout;
    }

    void draw_text_layout(SkCanvas& canvas, const text_layout& layout, const zoom_point origin, const SkPaint& paint, const std::size_t begin, const std::size_t end)
    {
        const auto* const data { text_layout_access::get(layout) };
        if (data == nullptr || begin >= end)
            return;
        for (std::size_t index { layout.line_at(begin) }; index < data->lines.size(); ++index)
        {
            const auto& line { data->lines[index] };
            if (line.begin >= end)
                break;
            const auto& drawing { data->drawings[index] };
            if (drawing == nullptr)
                continue;
            const auto from { std::max(begin, line.begin) };
            const auto to { std::min(end, line.end) };
            // 합자의 일부 색만 바꿔도 glyph와 위치는 같은 결과다.
            const bool clip { from > line.begin || to < line.end };
            if (clip)
            {
                const auto left { layout.caret_rect(from).x };
                const auto right { layout.caret_rect(to, true).x };
                canvas.save();
                const float top { std::min(line.top, line.ink_bounds.y) };
                const float bottom { std::max(line.top + line.height, line.ink_bounds.y + line.ink_bounds.height) };
                canvas.clipRect(SkRect::MakeXYWH(origin.x + std::min(left, right), origin.y + top, std::abs(right - left), bottom - top));
            }
            drawing->draw(canvas, { origin.x, origin.y + line.top + line.baseline }, paint, from - line.begin, to - line.begin);
            if (clip)
                canvas.restore();
        }
    }
} // namespace luil
