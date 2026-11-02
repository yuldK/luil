#include "luil/ui/ink_canvas_element.h"

#include "luil/ui/zoom_view_element.h"
#include "ui/ink_backing.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] float finite_rotation(const float rotation) noexcept
        {
            return std::isfinite(rotation) ? rotation : 0.0f;
        }

        // 내용 원점의 화면 자리다 (ink-design.md). 확대 보기는 내용 slot을 `rotated_bounds`의 왼쪽 위에 둔다.
        [[nodiscard]] point_f content_origin(const rect_f& slot, const float scale, const float rotation, const std::optional<rect_f>& content_bounds) noexcept
        {
            if (content_bounds.has_value() == false)
                return { slot.x, slot.y };
            const rect_f turned { rotated_bounds(*content_bounds, rotation) };
            return { slot.x - turned.x * scale, slot.y - turned.y * scale };
        }

        // 기본 이름과 기본 요약의 자리다. 라이브러리 문자열 정책(default-text-design.md)이 들어오면 그 함수로 채운다.
        [[nodiscard]] std::u8string default_name()
        {
            return {};
        }

        [[nodiscard]] std::u8string default_summary(const std::size_t strokes)
        {
            static_cast<void>(strokes);
            return {};
        }
    }

    point_f ink_content_point(const rect_f& slot, const float scale, const float rotation, const std::optional<rect_f>& content_bounds, const float x, const float y) noexcept
    {
        const float factor { positive_scale(scale) };
        const float degrees { finite_rotation(rotation) };
        const point_f origin { content_origin(slot, factor, degrees, content_bounds) };
        const zoom_point back { rotate_point({ (x - origin.x) / factor, (y - origin.y) / factor }, -degrees) };
        return { back.x, back.y };
    }

    ink_canvas_element::ink_canvas_element(ink_canvas_config config)
        : ui_element { { ui_element_kind::ink_canvas, config.owner } }
        , config_ { std::move(config) }
    {
    }

    void ink_canvas_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = positive_scale(context.scale);
        set_ink_source(make_source());
    }

    ink_source ink_canvas_element::make_source() const
    {
        // 표본마다 tree가 다시 서므로 역할은 이 배치의 값을 그대로 쥔다 (확대 보기의 `make_source`와 같다).
        const rect_f slot { bounds() };
        const float scale { scale_ };
        const float rotation { finite_rotation(config_.rotation) };
        const std::optional<rect_f> content_bounds { config_.content_bounds };
        const auto to_content = [slot, scale, rotation, content_bounds](const ink_sample& sample) {
            const point_f point { ink_content_point(slot, scale, rotation, content_bounds, sample.x, sample.y) };
            const float pressure { sample.pressure.has_value() && std::isfinite(*sample.pressure) ? std::clamp(*sample.pressure, 0.0f, 1.0f) : 1.0f };
            return ink_point { point.x, point.y, pressure };
        };
        ink_source source {};
        source.mouse = config_.mouse;
        source.touch = config_.touch;
        source.eraser = true;
        // 묶음과 콜백은 공유 실체라 복사가 싸다. 지우개는 이 배치가 그린 묶음으로 닿은 획을 찾는다.
        source.on_ink = [to_content, config { config_ }](const ink_input& input) -> std::vector<input_action> {
            if (input.eraser || config.tool == ink_tool::eraser)
            {
                // 지우개는 직전 표본에서 이번 표본까지를 쓴다. 뗌·취소는 새 자리가 없다.
                if (config.on_erase == nullptr || input.phase == ink_phase::end || input.phase == ink_phase::cancel)
                    return {};
                const ink_point from { to_content(input.phase == ink_phase::begin ? input.sample : input.previous) };
                const ink_point to { to_content(input.sample) };
                std::vector<std::uint64_t> ids { ink_strokes_hit(config.strokes, from, to, config.eraser_radius) };
                if (ids.empty())
                    return {};
                return { config.on_erase(std::move(ids)) };
            }
            if (config.on_stroke == nullptr)
                return {};
            ink_stroke_input stroke {};
            stroke.phase = input.phase;
            stroke.device = input.device;
            stroke.pointer_id = input.pointer_id;
            stroke.point = to_content(input.sample);
            stroke.tilt_x = input.sample.tilt_x;
            stroke.tilt_y = input.sample.tilt_y;
            stroke.interrupted = input.interrupted;
            stroke.pressure_reported = input.sample.pressure.has_value();
            stroke.time = input.sample.time;
            return { config.on_stroke(stroke) };
        };
        return source;
    }

    void ink_canvas_element::draw(draw_context& context, const interaction_snapshot&) const
    {
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const float scale { positive_scale(context.scale) };
        const float rotation { finite_rotation(config_.rotation) };
        const point_f origin { content_origin(box, scale, rotation, config_.content_bounds) };
        SkCanvas& canvas { context.canvas };
        canvas.save();
        canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), true);
        // 화면 자리 = 원점 + scale × R(rotation) × 내용 점이다. 폭·반지름도 내용 논리 픽셀이라 scale만 곱해진다.
        canvas.translate(origin.x, origin.y);
        canvas.scale(scale, scale);
        canvas.rotate(rotation);
        ink_access::draw(canvas, config_.strokes, context.palette.primary_foreground);
        ink_access::draw(canvas, config_.wet, context.palette.primary_foreground);
        canvas.restore();
    }

    access_info ink_canvas_element::accessibility() const
    {
        std::u8string name { config_.access_name.empty() ? access_name() : config_.access_name };
        if (name.empty())
            name = default_name();
        std::u8string summary { config_.access_summary ? config_.access_summary(config_.strokes.size()) : std::u8string {} };
        if (summary.empty())
            summary = default_summary(config_.strokes.size());
        return { .role = access_role::image, .name = std::move(name), .value = std::move(summary) };
    }
}
