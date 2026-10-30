#include "luil/ui/draw_primitives.h"
#include "luil/text/grapheme.h"
#include "ui/single_line_text.h"
#include "ui/text_layout_skia.h"

#include "luil/text/utf8_text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace luil {
    namespace {
        // 자를 수 있는 위치, 즉 UTF-8 이어지는 바이트(10xxxxxx)가 아닌 지점들이다.
        // 끝 위치(size)를 포함해 오름차순으로 담는다.
        std::vector<std::size_t> character_boundaries(const std::u8string_view text)
        {
            std::vector<std::size_t> boundaries {};
            boundaries.reserve(text.size() + 1);
            for (std::size_t index { 0 }; index < text.size(); index = text::grapheme_next(text, index))
                boundaries.push_back(index);
            boundaries.push_back(text.size());
            return boundaries;
        }

        // platform이 등록한 대체 해석기다.
        // 시작할 때 한 번 정해지고 그 뒤로 바뀌지 않지만,
        // 그리기 thread와 입력 thread가 함께 읽으므로 atomic이다.
        std::atomic<const font_fallback*> installed_fallback { nullptr };

        // platform이 OS 설정에서 읽어 둔 글자 그리기 방식이다.
        // 그리기 thread와 입력 thread(측정)가 함께 읽으므로 atomic이다.
        std::atomic<text_render_quality> installed_quality { text_render_quality::grayscale };

        // 측정과 그리기가 같은 품질 설정을 쓰도록 하는 관문이다.
        // 서브픽셀 배치를 켜지 않으면 글리프가 정수 픽셀에 스냅돼
        // 분수 배율에서 caret 자리와 글자가 미세하게 어긋난다.
        [[nodiscard]] SkFont quality_font(const SkFont& font)
        {
            SkFont adjusted { font };
            adjusted.setSubpixel(true);
            adjusted.setEdging(installed_quality.load(std::memory_order_relaxed) == text_render_quality::subpixel_lcd ? SkFont::Edging::kSubpixelAntiAlias : SkFont::Edging::kAntiAlias);
            return adjusted;
        }

        // host가 frame의 스타일에서 옮겨 둔 한 줄 글의 줄바꿈 처리다.
        // 그리기 thread와 입력 thread(측정)가 함께 읽으므로 atomic이다.
        std::atomic<single_line_break> installed_line_break { single_line_break::space };

        // 줄바꿈이 없는 한 줄을 그린다.
        // 배치에는 원문을 그대로 넘긴다. 잘못된 byte는 배치가 offset을 지키며 U+FFFD glyph로 그리고 Skia에 UTF-8로 넘기지 않는다.
        //  - 고친 사본을 넘기면 같은 글이 입력 칸의 배치와 다른 backend로 재여 폭이 갈린다.
        void draw_line(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
        {
            if (text.empty())
                return;
            if (ui_text_shaping_enabled())
            {
                const auto layout { make_font_text_layout({ .text = std::u8string { text }, .font_size = font.getSize() }, font) };
                draw_text_layout(canvas, layout, { x, y - layout.lines().front().baseline }, paint);
                return;
            }
            std::u8string storage {};
            const std::u8string_view drawable { drawable_text(text, storage) };
            const SkFont adjusted { quality_font(font) };
            float pen { x };
            for (const text_run& run : split_text_runs(drawable, adjusted))
            {
                if (run.text.empty())
                    continue;
                if (run.typeface == nullptr)
                {
                    canvas.drawSimpleText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8, pen, y, adjusted, paint);
                    pen += adjusted.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
                    continue;
                }

                // 크기·품질 같은 나머지 설정은 그대로 두고 typeface만 바꾼다.
                SkFont run_font { adjusted };
                run_font.setTypeface(run.typeface);
                canvas.drawSimpleText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8, pen, y, run_font, paint);
                pen += run_font.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
            }
        }

        // 줄바꿈이 없는 한 줄의 폭이다.
        // 그리기와 같이 원문으로 잰다. 배치의 잘못된 UTF-8 처리와 폭이 같아야 caret과 글자가 맞는다.
        [[nodiscard]] float measure_line(const std::u8string_view text, const SkFont& font)
        {
            if (text.empty())
                return 0.0f;
            if (ui_text_shaping_enabled())
                return make_font_text_layout({ .text = std::u8string { text }, .font_size = font.getSize() }, font).size().width;
            std::u8string storage {};
            const std::u8string_view drawable { drawable_text(text, storage) };
            // 그리기와 같은 품질 설정으로 재야 caret 자리와 글자가 일치한다.
            const SkFont adjusted { quality_font(font) };
            float width { 0.0f };
            for (const text_run& run : split_text_runs(drawable, adjusted))
            {
                if (run.text.empty())
                    continue;
                if (run.typeface == nullptr)
                {
                    width += adjusted.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
                    continue;
                }
                SkFont run_font { adjusted };
                run_font.setTypeface(run.typeface);
                width += run_font.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
            }
            return width;
        }
    }

    std::u8string_view single_line_text(const std::u8string_view text, const single_line_break mode, std::u8string& storage)
    {
        if (mode == single_line_break::paragraphs || text.find_first_of(u8"\r\n") == std::u8string_view::npos)
            return text;
        // U+21B5 "↵"다. 한 줄 칸 안에서 줄바꿈이 있었다는 것만 보인다.
        const std::u8string_view mark { mode == single_line_break::symbol ? std::u8string_view { u8"↵" } : std::u8string_view { u8" " } };
        storage.clear();
        storage.reserve(text.size() + 2u);
        for (std::size_t index { 0 }; index < text.size(); ++index)
        {
            const char8_t unit { text[index] };
            if (unit != u8'\r' && unit != u8'\n')
            {
                storage.push_back(unit);
                continue;
            }
            storage.append(mark);
            // CRLF 짝은 줄바꿈 하나다. 둘로 세면 Windows에서 온 글만 공백이 두 칸이 된다.
            if (unit == u8'\r' && index + 1 < text.size() && text[index + 1] == u8'\n')
                ++index;
        }
        return storage;
    }

    std::vector<std::u8string_view> single_line_paragraphs(const std::u8string_view text)
    {
        std::vector<std::u8string_view> lines {};
        std::size_t begin { 0 };
        while (true)
        {
            const std::size_t line_end { text.find_first_of(u8"\r\n", begin) };
            if (line_end == std::u8string_view::npos)
            {
                lines.push_back(text.substr(begin));
                return lines;
            }
            lines.push_back(text.substr(begin, line_end - begin));
            begin = line_end + 1;
            if (text[line_end] == u8'\r' && begin < text.size() && text[begin] == u8'\n')
                ++begin;
        }
    }

    SkPaint solid_paint(const ui_color color)
    {
        SkPaint paint {};
        paint.setAntiAlias(true);
        paint.setColor(color);
        return paint;
    }

    std::u8string_view drawable_text(const std::u8string_view text, std::u8string& storage)
    {
        if (text::utf8_is_valid(text))
            return text;
        storage = text::utf8_replace_invalid(text);
        return storage;
    }

    std::vector<text_run> split_text_runs(const std::u8string_view text, const SkFont& font)
    {
        std::vector<text_run> runs {};
        if (text.empty())
            return runs;

        const font_fallback* const fallback { installed_fallback.load(std::memory_order_relaxed) };
        if (fallback == nullptr)
        {
            runs.push_back({ text, nullptr });
            return runs;
        }

        std::vector<SkUnichar> codepoints {};
        std::vector<std::size_t> offsets {};
        codepoints.reserve(text.size());
        offsets.reserve(text.size() + 1u);
        for (std::size_t offset = 0; offset < text.size();)
        {
            const text::utf8_decoded decoded { text::utf8_decode(text, offset) };
            codepoints.push_back(static_cast<SkUnichar>(decoded.codepoint));
            offsets.push_back(offset);
            offset += decoded.length;
        }
        offsets.push_back(text.size());

        // 글자마다 묻지 않고 한 번에 묻는다.
        // DirectWrite 조회는 호출당 비용이 있다.
        std::vector<SkGlyphID> glyphs(codepoints.size(), 0);
        font.unicharsToGlyphs(codepoints, glyphs);

        const bool complete { std::ranges::none_of(glyphs, [](const SkGlyphID glyph) { return glyph == 0; }) };
        if (complete)
        {
            // 대부분의 글이 이 경로다.
            // 나누지 않고 그대로 그린다.
            runs.push_back({ text, nullptr });
            return runs;
        }

        std::size_t run_begin { 0 };
        sk_sp<SkTypeface> run_typeface {};
        for (std::size_t index = 0; index < codepoints.size(); ++index)
        {
            sk_sp<SkTypeface> wanted { glyphs[index] != 0 ? sk_sp<SkTypeface> {} : fallback->for_codepoint(static_cast<char32_t>(codepoints[index])) };
            if (index == 0)
            {
                run_typeface = std::move(wanted);
                continue;
            }
            // 같은 typeface면 run이 이어진다. 비교는 가리키는 대상으로 한다.
            if (wanted.get() == run_typeface.get())
                continue;

            runs.push_back({ text.substr(run_begin, offsets[index] - run_begin), std::move(run_typeface) });
            run_begin = offsets[index];
            run_typeface = std::move(wanted);
        }
        runs.push_back({ text.substr(run_begin), std::move(run_typeface) });
        return runs;
    }

    SkFont text_layout_font(const SkFont& font) { return quality_font(font); }

    sk_sp<SkTypeface> text_layout_fallback(const char32_t codepoint)
    {
        const auto* const fallback { installed_fallback.load(std::memory_order_relaxed) };
        return fallback != nullptr ? fallback->for_codepoint(codepoint) : nullptr;
    }

    void set_font_fallback(const font_fallback* const fallback) noexcept
    {
        if (installed_fallback.exchange(fallback, std::memory_order_relaxed) != fallback)
            invalidate_text_layouts();
    }

    void set_text_render_quality(const text_render_quality quality) noexcept
    {
        if (installed_quality.exchange(quality, std::memory_order_relaxed) != quality)
            invalidate_text_layouts();
    }

    void set_single_line_break(const single_line_break mode) noexcept
    {
        // 배치 cache를 비우지 않는다. 줄바꿈은 배치에 닿기 전에 바뀌므로 cache는 바뀐 글을 key로 갖는다 —
        // 줄바꿈 없는 글은 어느 값에서나 같은 항목이고, 있는 글은 값마다 다른 글이라 항목이 섞이지 않는다.
        installed_line_break.store(mode, std::memory_order_relaxed);
    }

    single_line_break single_line_break_mode() noexcept { return installed_line_break.load(std::memory_order_relaxed); }

    void draw_text(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
    {
        if (text.empty())
            return;
        // 측정(`measure_text`)과 같은 값을 한 번만 읽어 한 호출 안에서 규칙이 갈리지 않게 한다.
        const single_line_break mode { single_line_break_mode() };
        if (mode == single_line_break::paragraphs)
        {
            // 두 구성이 같은 줄 간격으로 내려가도록 배치의 여러 줄 대신 줄을 직접 나눠 한 줄씩 그린다.
            const float spacing { font.getSpacing() };
            float baseline { y };
            for (const std::u8string_view line : single_line_paragraphs(text))
            {
                draw_line(canvas, line, x, baseline, font, paint);
                baseline += spacing;
            }
            return;
        }
        std::u8string storage {};
        draw_line(canvas, single_line_text(text, mode, storage), x, y, font, paint);
    }

    void draw_hover_fill(draw_context& context, const rect_f& box, const ui_element_id& id, const interaction_snapshot& interaction, const bool enabled, const float radius)
    {
        if (enabled == false)
            return;
        ui_color color { 0 };
        if (interaction.pressed == id)
            color = context.palette.button_pressed_background;
        else if (interaction.hovered == id)
            color = context.palette.button_hover_background;
        else
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        if (radius > 0.0f)
            context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius * scale, radius * scale), solid_paint(color));
        else
            context.canvas.drawRect(shape, solid_paint(color));
    }

    float measure_text(const std::u8string_view text, const SkFont& font)
    {
        if (text.empty())
            return 0.0f;
        // 그리기와 같은 변환을 지나야 폭과 그림이 맞는다.
        const single_line_break mode { single_line_break_mode() };
        if (mode == single_line_break::paragraphs)
        {
            float widest { 0.0f };
            for (const std::u8string_view line : single_line_paragraphs(text))
                widest = std::max(widest, measure_line(line, font));
            return widest;
        }
        std::u8string storage {};
        return measure_line(single_line_text(text, mode, storage), font);
    }

    std::u8string elide_text(const std::u8string_view text, const float max_width, const SkFont& font)
    {
        if (text.empty() || max_width <= 0.0f)
            return {};
        if (measure_text(text, font) <= max_width)
            return std::u8string { text };

        constexpr std::u8string_view ellipsis { u8"…" };
        const float ellipsis_width { measure_text(ellipsis, font) };
        if (ellipsis_width > max_width)
            return {};

        // 경계 목록에서 들어가는 가장 긴 앞부분을 이분 탐색한다.
        // 빈 앞부분은 항상 들어가고(위에서 `…` 폭을 확인했다) 전체는 들어가지 않는 것이 확정이다.
        const std::vector<std::size_t> boundaries { character_boundaries(text) };
        std::size_t low { 0 };
        std::size_t high { boundaries.size() - 1 };
        while (low < high)
        {
            const std::size_t middle { low + (high - low + 1) / 2 };
            std::u8string candidate {};
            if (ui_text_shaping_enabled())
            {
                candidate = text.substr(0, boundaries[middle]);
                candidate.append(ellipsis);
            }
            const float width { ui_text_shaping_enabled() ? measure_text(candidate, font) : measure_text(text.substr(0, boundaries[middle]), font) + ellipsis_width };
            if (width <= max_width)
                low = middle;
            else
                high = middle - 1;
        }

        std::u8string result { text.substr(0, boundaries[low]) };
        result.append(ellipsis);
        return result;
    }

    std::u8string elide_text_middle(const std::u8string_view text, const float max_width, const SkFont& font)
    {
        if (text.empty() || max_width <= 0.0f)
            return {};
        if (measure_text(text, font) <= max_width)
            return std::u8string { text };
        constexpr std::u8string_view ellipsis { u8"…" };
        if (measure_text(ellipsis, font) > max_width)
            return {};
        const std::vector<std::size_t> boundaries { character_boundaries(text) };
        const std::size_t count { boundaries.size() - 1 };
        const auto candidate = [&](const std::size_t keep) {
            const std::size_t front { (keep + 1) / 2 };
            const std::size_t back { keep / 2 };
            std::u8string result { text.substr(0, boundaries[front]) };
            result.append(ellipsis);
            result.append(text.substr(boundaries[count - back]));
            return result;
        };
        std::size_t low { 0 };
        std::size_t high { count };
        while (low < high)
        {
            const std::size_t middle { low + (high - low + 1) / 2 };
            if (measure_text(candidate(middle), font) <= max_width)
                low = middle;
            else
                high = middle - 1;
        }
        return candidate(low);
    }

    std::u8string elide_text_start(const std::u8string_view text, const float max_width, const SkFont& font)
    {
        if (text.empty() || max_width <= 0.0f)
            return {};
        if (measure_text(text, font) <= max_width)
            return std::u8string { text };
        constexpr std::u8string_view ellipsis { u8"…" };
        if (measure_text(ellipsis, font) > max_width)
            return {};
        // 남길 뒷부분의 첫 경계를 이분 탐색한다. 맨 끝(빈 뒷부분)은 언제나 들어간다.
        const std::vector<std::size_t> boundaries { character_boundaries(text) };
        const auto candidate = [&](const std::size_t from) {
            std::u8string result { ellipsis };
            result.append(text.substr(boundaries[from]));
            return result;
        };
        std::size_t low { 0 };
        std::size_t high { boundaries.size() - 1 };
        // `low`는 들어가지 않는 쪽, `high`는 들어가는 쪽이다.
        while (high - low > 1)
        {
            const std::size_t middle { low + (high - low) / 2 };
            if (measure_text(candidate(middle), font) <= max_width)
                high = middle;
            else
                low = middle;
        }
        return candidate(high);
    }

    float draw_text_within(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const float max_width, const SkFont& font, const SkPaint& paint)
    {
        const std::u8string drawn { elide_text(text, max_width, font) };
        if (drawn.empty())
            return 0.0f;
        draw_text(canvas, drawn, x, y, font, paint);
        return measure_text(drawn, font);
    }

    void draw_downward_shadow(SkCanvas& canvas, const rect_f& area, const ui_color color, const float strength)
    {
        if (area.width <= 0.0f || area.height <= 0.0f || strength <= 0.0f)
            return;

        // 띠 개수는 고정이다.
        // DPI가 커지면 띠가 두꺼워질 뿐 계단이 더 보이지 않는다.
        constexpr int band_count { 8 };
        const float band_height { area.height / static_cast<float>(band_count) };
        for (int band = 0; band < band_count; ++band)
        {
            // 위쪽이 가장 진하고 아래로 갈수록 제곱으로 옅어진다.
            const float distance { static_cast<float>(band) / static_cast<float>(band_count) };
            const float remaining { 1.0f - distance };
            SkPaint paint { solid_paint(with_alpha(color, strength * remaining * remaining)) };
            paint.setAntiAlias(false);
            const float top { area.y + band_height * static_cast<float>(band) };
            canvas.drawRect(SkRect::MakeXYWH(area.x, top, area.width, band_height), paint);
        }
    }

    void draw_upward_shadow(SkCanvas& canvas, const rect_f& area, const ui_color color, const float strength)
    {
        if (area.width <= 0.0f || area.height <= 0.0f || strength <= 0.0f)
            return;

        // `draw_downward_shadow`의 거울이다.
        // 아래쪽이 가장 진하고 위로 갈수록 제곱으로 옅어진다.
        constexpr int band_count { 8 };
        const float band_height { area.height / static_cast<float>(band_count) };
        for (int band = 0; band < band_count; ++band)
        {
            const float distance { static_cast<float>(band) / static_cast<float>(band_count) };
            const float remaining { 1.0f - distance };
            SkPaint paint { solid_paint(with_alpha(color, strength * remaining * remaining)) };
            paint.setAntiAlias(false);
            const float bottom { area.y + area.height - band_height * static_cast<float>(band) };
            canvas.drawRect(SkRect::MakeXYWH(area.x, bottom - band_height, area.width, band_height), paint);
        }
    }

    void draw_surface_shadow(draw_context& context, const rect_f& box, const float radius, const float strength)
    {
        if (box.width <= 0.0f || box.height <= 0.0f || strength <= 0.0f)
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        // 바깥 띠부터 안쪽으로 겹쳐 쌓는다. 안쪽 띠는 바깥 띠 위에 다시 얹히므로 상자
        // 가장자리에 닿을수록 겹친 알파가 쌓여 진해진다 — 띠 하나의 알파는 가장자리에서의
        // 합이 대략 `strength`가 되도록 나눈다.
        // 띠 개수는 고정이다 (`draw_downward_shadow`와 같은 이유).
        constexpr int band_count { 8 };
        // 띠 하나의 폭이다 (물리 픽셀). 닿는 거리는 전부 합쳐 12 논리 픽셀이다.
        const float band_spread { 1.5f * scale };
        // 가장자리 가까움의 제곱합이다 — 알파를 이 비율로 나눠 무게의 합이 1이 되게 한다.
        float closeness_total { 0.0f };
        for (int band = 1; band <= band_count; ++band)
        {
            const float closeness { 1.0f - static_cast<float>(band) / static_cast<float>(band_count + 1) };
            closeness_total += closeness * closeness;
        }
        for (int band = band_count; band >= 1; --band)
        {
            const float spread { static_cast<float>(band) * band_spread };
            const float closeness { 1.0f - static_cast<float>(band) / static_cast<float>(band_count + 1) };
            // 바깥 띠에도 옅은 바닥값을 둔다 — 제곱만으로는 먼 띠가 사라져 경계가 딱딱하다.
            const float weight { 0.1f / static_cast<float>(band_count) + 0.9f * closeness * closeness / closeness_total };
            const SkPaint paint { solid_paint(with_alpha(context.palette.content_shadow, strength * weight)) };
            // 아래로 반만큼 밀어 빛이 위에서 오는 것처럼 보인다.
            const SkRect outset { shape.makeOutset(spread, spread).makeOffset(0.0f, spread * 0.5f) };
            context.canvas.drawRRect(SkRRect::MakeRectXY(outset, radius + spread, radius + spread), paint);
        }
    }

    float scroll_edge_strength(const float distance) noexcept
    {
        // 반 픽셀 안쪽은 끝에 붙은 것으로 본다.
        if (distance <= 0.5f)
            return 0.0f;
        const float linear { std::min(distance / scroll_edge_fade_distance, 1.0f) };
        // smoothstep이라 오르기 시작할 때도, 다 짙어질 때도 꺾이지 않는다.
        return linear * linear * (3.0f - 2.0f * linear);
    }

    void draw_scroll_edges(draw_context& context, const rect_f& box, const float scroll_offset, const float maximum_scroll, const scroll_edges& edges)
    {
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        if (edges.shadows)
        {
            // 그림자의 깊이다 (물리 픽셀). 창이 얕으면 절반까지다 — 위·아래가 겹치지 않는다.
            const float reach { std::min(10.0f * scale, box.height / 2.0f) };
            // 짙기는 끝에서 떨어진 거리로 정한다(`scroll_edge_strength`). 흘리기 시작한 순간 툭 켜지지 않고, 내용이 경계 밑으로
            // 들어가는 만큼 서서히 선다. 맨 위·맨 아래에 선 창은 거리가 0이라 깨끗하다.
            const float top { scroll_edge_strength(scroll_offset) };
            const float bottom { scroll_edge_strength(maximum_scroll - scroll_offset) };
            if (top > 0.0f)
                draw_downward_shadow(context.canvas, { box.x, box.y, box.width, reach }, context.palette.content_shadow, 0.18f * top);
            if (bottom > 0.0f)
                draw_upward_shadow(context.canvas, { box.x, box.y + box.height - reach, box.width, reach }, context.palette.content_shadow, 0.14f * bottom);
        }
        if (edges.top_rule == false && edges.bottom_rule == false)
            return;
        // 구분선은 흘린 양과 무관하게 늘 선다. 그림자 위에 긋는다.
        SkPaint rule { solid_paint(context.palette.divider) };
        rule.setAntiAlias(false);
        if (edges.top_rule)
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, scale), rule);
        if (edges.bottom_rule)
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), rule);
    }

    void draw_row_selection(draw_context& context, const rect_f& box)
    {
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        // `row_selection_background`는 `accent_soft`에 낮은 알파를 얹은 파생 역할이다
        // (양은 `accent_tones::row_selection`). 온전한 색으로 깔면 밝은 띠가 되어 그 위의
        // 글이 읽히지 않는다. 좌우 2px·위아래 1px 들여 이웃 행과 hover 채움에서 갈린다.
        const float radius { context.metrics.row_corner_radius * scale };
        const SkRect fill { SkRect::MakeXYWH(box.x + 2.0f * scale, box.y + scale, std::max(0.0f, box.width - 4.0f * scale), std::max(0.0f, box.height - 2.0f * scale)) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(fill, radius, radius), solid_paint(context.palette.row_selection_background));
        // 왼쪽 표식은 고대비 팔레트가 `accent_soft`를 접어도 어느 행인지 남긴다
        // (배지가 tone에 글리프를 딸려 보낸 것과 같은 규칙이다).
        // 길이는 행에서 위아래 6px씩 뺀 값이고, 행이 그보다 얕으면 행의 절반이다.
        const float mark_height { std::max(box.height - 12.0f * scale, box.height / 2.0f) };
        const SkRect mark { SkRect::MakeXYWH(box.x + 2.0f * scale, box.y + (box.height - mark_height) / 2.0f, 3.0f * scale, mark_height) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(mark, 1.5f * scale, 1.5f * scale), solid_paint(context.palette.accent));
    }

    void draw_centered_glyph(SkCanvas& canvas, const char32_t codepoint, const rect_f& target, const SkFont& base_font, const SkPaint& paint)
    {
        const SkFont font { quality_font(base_font) };
        const SkGlyphID glyph { font.unicharToGlyph(static_cast<SkUnichar>(codepoint)) };
        if (glyph == 0)
            return;

        const SkRect glyph_bounds { font.getBounds(glyph, &paint) };
        if (glyph_bounds.isEmpty())
            return;

        const SkRect bounds { SkRect::MakeXYWH(target.x, target.y, target.width, target.height) };
        const std::array glyphs { glyph };
        const std::array positions {
            SkPoint {
                bounds.centerX() - glyph_bounds.centerX(),
                bounds.centerY() - glyph_bounds.centerY(),
            },
        };
        canvas.drawGlyphs(glyphs, positions, SkPoint {}, font, paint);
    }

    float spinner_angle(const std::chrono::steady_clock::time_point now) noexcept
    {
        // 위상은 steady clock의 절대 시각에서만 나온다.
        // 시작 시각을 들고 다니지 않아도 되고, 같은 frame의 여러 표시가 함께 돈다.
        const auto elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % spinner_period };
        return static_cast<float>(elapsed.count()) / static_cast<float>(spinner_period.count()) * 360.0f;
    }

    void draw_spinning_glyph(SkCanvas& canvas, const char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint, const std::chrono::steady_clock::time_point now)
    {
        const SkAutoCanvasRestore restore { &canvas, true };
        canvas.rotate(spinner_angle(now), target.x + target.width / 2.0f, target.y + target.height / 2.0f);
        draw_centered_glyph(canvas, codepoint, target, font, paint);
    }

    float centered_text_baseline(const SkFont& font, const float target_height)
    {
        SkFontMetrics font_metrics {};
        font.getMetrics(&font_metrics);
        return target_height * 0.5f - (font_metrics.fAscent + font_metrics.fDescent) * 0.5f;
    }

}
