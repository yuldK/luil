#include "luil/ui/label_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/text_layout.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

namespace luil {
    namespace {
        SkPaint label_paint(const ui_color_palette& palette, const label_color_role role)
        {
            switch (role)
            {
            case label_color_role::error:
                return solid_paint(palette.error_accent);
            case label_color_role::dim:
                // 흐린 보조 글자의 역할색이다 (element마다 알파를 발명하지 않는다).
                return solid_paint(palette.secondary_foreground);
            case label_color_role::primary:
                break;
            }
            return solid_paint(palette.primary_foreground);
        }
    } // namespace

    label_element::label_element(ui_element_id id, label_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    label_wrap label_element::measure_wrap(const label_config& config, const measure_context& context, const float available_width)
    {
        label_wrap result {};
        if (config.text.empty())
            return result;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float line_height { height_for(config) };
        const float width { std::max(1.0f, available_width - config.padding * 2.0f) };
        // 여러 줄 칸과 같은 배치다 — 공백 우선, 긴 낱말은 글자소 경계에서 바꾼다.
        text_layout_request request {
            .text = config.text,
            .font_size = scaled_font_size(config.font_size, scale),
            .width = width * scale,
            .wrap = true,
            .line_height = line_height * scale,
        };
        const text_layout layout { make_text_layout(std::move(request), context.measure_text()) };
        const std::span<const text_layout_line> lines { layout.lines() };
        const std::size_t shown { config.max_lines > 0 ? std::min(config.max_lines, lines.size()) : lines.size() };
        for (std::size_t index { 0 }; index < shown; ++index)
        {
            result.lines.push_back({ lines[index].begin, lines[index].end });
            result.width = std::max(result.width, lines[index].width / scale);
        }
        if (shown < lines.size() && shown > 0)
        {
            // 마지막 보이는 줄이 남은 글 전체를 맡고 그리기가 끝을 줄인다.
            result.lines.back().end = config.text.size();
            result.truncated = true;
            result.width = width;
        }
        result.height = static_cast<float>(result.lines.size()) * line_height;
        return result;
    }

    void label_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (config_.wrap)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            wrapped_ = measure_wrap(config_, context.measure(), context.slot.width / scale);
        }
    }

    void label_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.text.empty())
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(config_.font_size, scale) };
        const SkPaint paint { label_paint(context.palette, config_.color) };
        const rect_f box { bounds() };

        if (config_.background == label_background_role::notice)
        {
            const SkPaint fill { solid_paint(context.palette.notice_background) };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        }

        // 창이 좁아 slot이 줄면 글자를 잘라 옆 UI를 침범하지 않게 한다.
        const float padding { config_.padding * scale };
        const float text_width { box.width - padding * 2.0f };
        if (config_.wrap)
        {
            // 배치가 바꾼 줄마다 한 줄 label과 같은 높이·baseline으로 긋는다. 말줄임한 마지막 줄은 남은 글을 한 줄로
            // 펴서(줄 바꿈은 공백) 끝을 줄인다.
            const float line_height { height_for(config_) * scale };
            for (std::size_t index { 0 }; index < wrapped_.lines.size(); ++index)
            {
                const label_wrap::line& line { wrapped_.lines[index] };
                std::u8string piece { std::u8string_view { config_.text }.substr(line.begin, line.end - line.begin) };
                const bool last_truncated { wrapped_.truncated && index + 1 == wrapped_.lines.size() };
                if (last_truncated)
                    std::replace(piece.begin(), piece.end(), u8'\n', u8' ');
                while (piece.empty() == false && (piece.back() == u8'\n' || piece.back() == u8'\r'))
                    piece.pop_back();
                if (last_truncated)
                    piece = elide_text(piece, text_width, font);
                const float top { box.y + static_cast<float>(index) * line_height };
                draw_text(context.canvas, piece, box.x + padding, top + centered_text_baseline(font, line_height), font, paint);
            }
            return;
        }
        // 기본 경로는 그대로 두어 기존 글리프 자리와 raster를 유지한다.
        if (config_.align == text_align::start && config_.elide == text_elide::end)
        {
            static_cast<void>(draw_text_within(context.canvas, config_.text, box.x + padding, box.y + centered_text_baseline(font, box.height), text_width, font, paint));
            return;
        }
        std::u8string drawn {};
        if (config_.elide == text_elide::middle)
            drawn = elide_text_middle(config_.text, text_width, font);
        else if (config_.elide == text_elide::start)
            drawn = elide_text_start(config_.text, text_width, font);
        else
            drawn = elide_text(config_.text, text_width, font);
        const float width { measure_text(drawn, font) };
        const float offset { config_.align == text_align::end ? text_width - width
            : (config_.align == text_align::center ? (text_width - width) * 0.5f : 0.0f)
        };
        draw_text(context.canvas, drawn, box.x + padding + offset, box.y + centered_text_baseline(font, box.height), font, paint);
    }

    float label_element::height_for(const label_config& config) noexcept
    {
        // 글자 크기 + 위아래 여백이다.
        // 11px 글에서 18px 줄이 되는, 데모가 손으로 쓰던 그 간격이다.
        return text_line_height(config.font_size, 7.0f);
    }
    access_info label_element::accessibility() const
    {
        return { .role = access_role::static_text, .name = config_.text };
    }

    std::optional<size_f> label_element::preferred_size(const measure_context& context, const float available_width) const
    {
        if (config_.wrap)
        {
            const label_wrap measured { measure_wrap(config_, context, available_width) };
            const bool broke { measured.lines.size() > 1 || measured.truncated };
            const float width { broke ? available_width : std::ceil(measured.width) + config_.padding * 2.0f };
            return size_f { width, std::max(measured.height, height_for(config_)) };
        }
        static_cast<void>(available_width);
        return size_f { std::ceil(context.text_width(config_.text, config_.font_size)) + config_.padding * 2.0f, height_for(config_) };
    }
} // namespace luil
