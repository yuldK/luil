#include "luil/ui/draw_primitives.h"

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
#include <chrono>
#include <atomic>
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
            for (std::size_t index = 0; index < text.size(); ++index)
                if ((static_cast<std::uint8_t>(text[index]) & 0xC0U) != 0x80U)
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
    } // namespace

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

    void set_font_fallback(const font_fallback* const fallback) noexcept
    {
        installed_fallback.store(fallback, std::memory_order_relaxed);
    }

    void set_text_render_quality(const text_render_quality quality) noexcept
    {
        installed_quality.store(quality, std::memory_order_relaxed);
    }

    void draw_text(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
    {
        if (text.empty())
            return;
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

    ui_color with_alpha(const ui_color color, const float alpha) noexcept
    {
        const float clamped { alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha) };
        const auto value { static_cast<std::uint32_t>(clamped * 255.0f + 0.5f) };
        return (color & 0x00ffffffU) | (value << 24U);
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
            if (measure_text(text.substr(0, boundaries[middle]), font) + ellipsis_width <= max_width)
                low = middle;
            else
                high = middle - 1;
        }

        std::u8string result { text.substr(0, boundaries[low]) };
        result.append(ellipsis);
        return result;
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

} // namespace luil
