#include "luil/ui/ink.h"

#include "luil/ui/draw_primitives.h"
#include "ui/image_backing.h"
#include "ui/ink_backing.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace luil {
    namespace {
        // 보간한 두 점 사이의 거리 상한이다 (그 자리 반지름에 대한 비율). 원과 사다리꼴이 이 간격으로 이어져
        // 굽은 곳의 바깥선이 각지지 않는다.
        constexpr float ink_spacing_ratio { 0.5f };
        // 선분 하나를 나누는 수의 상한이다. 아주 가는 획의 긴 선분이 path를 부풀리지 않게 한다.
        constexpr int ink_maximum_steps { 32 };

        struct ink_disc
        {
            float x { 0.0f };
            float y { 0.0f };
            float radius { 0.0f };
        };

        [[nodiscard]] float fold_pressure(const float pressure) noexcept
        {
            return std::isfinite(pressure) ? std::clamp(pressure, 0.0f, 1.0f) : 1.0f;
        }

        [[nodiscard]] float catmull_rom(const float p0, const float p1, const float p2, const float p3, const float t) noexcept
        {
            const float t2 { t * t };
            const float t3 { t2 * t };
            return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
        }

        // 저장한 점 사이를 Catmull-Rom으로 보간한 원들이다. 저장한 점 자체는 그대로 지나간다.
        [[nodiscard]] std::vector<ink_disc> ink_discs(const std::vector<ink_point>& points, const ink_style& style)
        {
            std::vector<ink_disc> discs {};
            if (points.empty())
                return discs;
            const auto radius_of = [&style](const float pressure) { return ink_width_at(style, pressure) / 2.0f; };
            discs.push_back({ points.front().x, points.front().y, radius_of(points.front().pressure) });
            for (std::size_t index { 0 }; index + 1 < points.size(); ++index)
            {
                const ink_point& p0 { points[index > 0 ? index - 1 : index] };
                const ink_point& p1 { points[index] };
                const ink_point& p2 { points[index + 1] };
                const ink_point& p3 { points[index + 2 < points.size() ? index + 2 : index + 1] };
                const float r1 { radius_of(p1.pressure) };
                const float r2 { radius_of(p2.pressure) };
                const float length { std::hypot(p2.x - p1.x, p2.y - p1.y) };
                const float spacing { std::max(0.5f, std::min(r1, r2) * ink_spacing_ratio * 2.0f) };
                const int steps { std::clamp(static_cast<int>(std::ceil(length / spacing)), 1, ink_maximum_steps) };
                for (int step { 1 }; step <= steps; ++step)
                {
                    const float t { static_cast<float>(step) / static_cast<float>(steps) };
                    discs.push_back({ catmull_rom(p0.x, p1.x, p2.x, p3.x, t), catmull_rom(p0.y, p1.y, p2.y, p3.y, t), r1 + (r2 - r1) * t });
                }
            }
            return discs;
        }

        // 두 원을 잇는 바깥 접선의 사다리꼴이다. 한 원이 다른 원을 품으면 없다.
        //  - 원과 같은 방향(화면 좌표에서 시계 방향, 신발끈 넓이가 양수)으로 감는다. nonzero가 겹친 곳을 한 번만 칠하려면
        //    모든 윤곽의 감는 방향이 같아야 한다. 반대로 감긴 윤곽이 겹치면 그 자리가 구멍이 된다.
        void add_bridge(SkPathBuilder& path, const ink_disc& from, const ink_disc& to)
        {
            const float dx { to.x - from.x };
            const float dy { to.y - from.y };
            const float distance { std::hypot(dx, dy) };
            if (distance <= std::abs(from.radius - to.radius) || distance <= 0.0f)
                return;
            const float ux { dx / distance };
            const float uy { dy / distance };
            const float sine { (from.radius - to.radius) / distance };
            const float cosine { std::sqrt(std::max(0.0f, 1.0f - sine * sine)) };
            // 접점 방향은 법선(−uy, ux)을 선분 쪽으로 기울인 것이다. 두 원이 같은 방향을 쓴다.
            const float left_x { -uy * cosine + ux * sine };
            const float left_y { ux * cosine + uy * sine };
            const float right_x { uy * cosine + ux * sine };
            const float right_y { -ux * cosine + uy * sine };
            std::array<SkPoint, 4> corners {
                SkPoint { from.x + from.radius * left_x, from.y + from.radius * left_y },
                SkPoint { to.x + to.radius * left_x, to.y + to.radius * left_y },
                SkPoint { to.x + to.radius * right_x, to.y + to.radius * right_y },
                SkPoint { from.x + from.radius * right_x, from.y + from.radius * right_y },
            };
            float area { 0.0f };
            for (std::size_t index { 0 }; index < corners.size(); ++index)
            {
                const SkPoint& a { corners[index] };
                const SkPoint& b { corners[(index + 1) % corners.size()] };
                area += a.fX * b.fY - b.fX * a.fY;
            }
            if (area < 0.0f)
                std::reverse(corners.begin(), corners.end());
            path.addPolygon(corners, true);
        }

        [[nodiscard]] SkPath ink_path(const std::vector<ink_point>& points, const ink_style& style)
        {
            SkPathBuilder path { SkPathFillType::kWinding };
            const std::vector<ink_disc> discs { ink_discs(points, style) };
            for (std::size_t index { 0 }; index < discs.size(); ++index)
            {
                if (discs[index].radius > 0.0f)
                    path.addCircle(discs[index].x, discs[index].y, discs[index].radius, SkPathDirection::kCW);
                if (index + 1 < discs.size())
                    add_bridge(path, discs[index], discs[index + 1]);
            }
            return path.detach();
        }

        [[nodiscard]] float distance_to_segment(const float x, const float y, const ink_point& a, const ink_point& b, float* const along = nullptr) noexcept
        {
            const float dx { b.x - a.x };
            const float dy { b.y - a.y };
            const float length_squared { dx * dx + dy * dy };
            float t { 0.0f };
            if (length_squared > 0.0f)
                t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / length_squared, 0.0f, 1.0f);
            if (along != nullptr)
                *along = t;
            return std::hypot(x - (a.x + dx * t), y - (a.y + dy * t));
        }

        [[nodiscard]] float cross(const ink_point& o, const ink_point& a, const ink_point& b) noexcept
        {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        // 두 선분 사이의 거리다. 엇갈려 만나면 0이다.
        [[nodiscard]] float segment_distance(const ink_point& a, const ink_point& b, const ink_point& c, const ink_point& d) noexcept
        {
            const float d1 { cross(c, d, a) };
            const float d2 { cross(c, d, b) };
            const float d3 { cross(a, b, c) };
            const float d4 { cross(a, b, d) };
            if (((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) && ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f)))
                return 0.0f;
            return std::min({ distance_to_segment(a.x, a.y, c, d), distance_to_segment(b.x, b.y, c, d), distance_to_segment(c.x, c.y, a, b), distance_to_segment(d.x, d.y, a, b) });
        }

        [[nodiscard]] bool finite_point(const ink_point& point) noexcept
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        [[nodiscard]] sk_sp<SkPicture> record_layer(const std::vector<ink_stroke>& strokes, const ui_color default_color)
        {
            SkRect cull { SkRect::MakeEmpty() };
            for (const ink_stroke& stroke : strokes)
            {
                const rect_f box { stroke.bounds() };
                cull.join(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
            }
            SkPictureRecorder recorder {};
            SkCanvas* const canvas { recorder.beginRecording(cull) };
            for (const ink_stroke& stroke : strokes)
                ink_access::draw(*canvas, stroke, default_color);
            return recorder.finishRecordingAsPicture();
        }
    }

    float ink_width_at(const ink_style& style, const float pressure) noexcept
    {
        if (std::isfinite(style.width) == false || style.width < 0.0f || std::isfinite(style.minimum_ratio) == false)
            return 0.0f;
        const float ratio { std::clamp(style.minimum_ratio, 0.0f, 1.0f) };
        return style.width * (ratio + (1.0f - ratio) * fold_pressure(pressure));
    }

    std::uint64_t ink_stroke::id() const noexcept
    {
        return backing_ != nullptr ? backing_->id : 0;
    }

    std::span<const ink_point> ink_stroke::points() const noexcept
    {
        if (backing_ == nullptr)
            return {};
        return backing_->points;
    }

    const ink_style& ink_stroke::style() const noexcept
    {
        static const ink_style empty_style {};
        return backing_ != nullptr ? backing_->style : empty_style;
    }

    rect_f ink_stroke::bounds() const noexcept
    {
        return backing_ != nullptr ? backing_->bounds : rect_f {};
    }

    bool ink_stroke::empty() const noexcept
    {
        return backing_ == nullptr || backing_->points.empty();
    }

    ink_stroke make_ink_stroke(const std::uint64_t id, std::vector<ink_point> points, ink_style style)
    {
        std::erase_if(points, [](const ink_point& point) { return finite_point(point) == false; });
        for (ink_point& point : points)
            point.pressure = fold_pressure(point.pressure);
        auto backing { std::make_shared<ink_stroke_backing>() };
        backing->id = id;
        backing->path = ink_path(points, style);
        backing->points = std::move(points);
        backing->style = std::move(style);
        if (backing->path.isEmpty() == false)
        {
            const SkRect box { backing->path.getBounds() };
            backing->bounds = { box.x(), box.y(), box.width(), box.height() };
        }
        ink_stroke stroke {};
        stroke.backing_ = std::move(backing);
        return stroke;
    }

    std::vector<ink_point> simplify_ink_points(const std::span<const ink_point> points, const float tolerance, const float pressure_tolerance)
    {
        std::vector<ink_point> source { points.begin(), points.end() };
        if (std::isfinite(tolerance) == false || tolerance <= 0.0f || source.size() < 3)
            return source;
        const bool watch_pressure { std::isfinite(pressure_tolerance) && pressure_tolerance > 0.0f };
        std::vector<bool> keep(source.size(), false);
        keep.front() = true;
        keep.back() = true;
        // 재귀 대신 구간 stack으로 돈다. 긴 획에서 호출 깊이가 점 수만큼 깊어지지 않게 한다.
        std::vector<std::pair<std::size_t, std::size_t>> spans { { 0, source.size() - 1 } };
        while (spans.empty() == false)
        {
            const auto [first, last] { spans.back() };
            spans.pop_back();
            if (last <= first + 1)
                continue;
            std::size_t farthest { first };
            float worst { 0.0f };
            for (std::size_t index { first + 1 }; index < last; ++index)
            {
                float along { 0.0f };
                const float distance { distance_to_segment(source[index].x, source[index].y, source[first], source[last], &along) };
                const float expected { source[first].pressure + (source[last].pressure - source[first].pressure) * along };
                // 두 오차를 각자의 허용치로 나눠 견준다. 하나라도 1을 넘으면 그 점은 남는다.
                float error { distance / tolerance };
                if (watch_pressure)
                    error = std::max(error, std::abs(source[index].pressure - expected) / pressure_tolerance);
                if (error > worst)
                {
                    worst = error;
                    farthest = index;
                }
            }
            if (worst <= 1.0f)
                continue;
            keep[farthest] = true;
            spans.emplace_back(first, farthest);
            spans.emplace_back(farthest, last);
        }
        std::vector<ink_point> result {};
        for (std::size_t index { 0 }; index < source.size(); ++index)
            if (keep[index])
                result.push_back(source[index]);
        return result;
    }

    std::span<const ink_stroke> ink_layer::strokes() const noexcept
    {
        if (backing_ == nullptr)
            return {};
        return backing_->strokes;
    }

    std::size_t ink_layer::size() const noexcept
    {
        return backing_ != nullptr ? backing_->strokes.size() : 0;
    }

    bool ink_layer::empty() const noexcept
    {
        return size() == 0;
    }

    ink_layer ink_layer::with(ink_stroke stroke) const
    {
        if (stroke.empty())
            return *this;
        auto backing { std::make_shared<ink_layer_backing>() };
        if (backing_ != nullptr)
            backing->strokes = backing_->strokes;
        backing->strokes.push_back(std::move(stroke));
        ink_layer layer {};
        layer.backing_ = std::move(backing);
        return layer;
    }

    ink_layer ink_layer::without(const std::span<const std::uint64_t> ids) const
    {
        if (backing_ == nullptr || ids.empty())
            return *this;
        const auto removed = [ids](const ink_stroke& stroke) { return std::find(ids.begin(), ids.end(), stroke.id()) != ids.end(); };
        // 지울 것이 없으면 같은 묶음이다. 그림 memo가 그대로 남는다.
        if (std::none_of(backing_->strokes.begin(), backing_->strokes.end(), removed))
            return *this;
        auto backing { std::make_shared<ink_layer_backing>() };
        for (const ink_stroke& stroke : backing_->strokes)
            if (removed(stroke) == false)
                backing->strokes.push_back(stroke);
        ink_layer layer {};
        layer.backing_ = std::move(backing);
        return layer;
    }

    std::vector<std::uint64_t> ink_strokes_hit(const ink_layer& layer, const ink_point from, const ink_point to, const float radius)
    {
        std::vector<std::uint64_t> ids {};
        if (finite_point(from) == false || finite_point(to) == false)
            return ids;
        const float reach { std::isfinite(radius) && radius > 0.0f ? radius : 0.0f };
        const rect_f sweep {
            std::min(from.x, to.x) - reach,
            std::min(from.y, to.y) - reach,
            std::abs(to.x - from.x) + reach * 2.0f,
            std::abs(to.y - from.y) + reach * 2.0f,
        };
        for (const ink_stroke& stroke : layer.strokes())
        {
            const rect_f box { stroke.bounds() };
            if (stroke.empty() || box.x > sweep.x + sweep.width || box.x + box.width < sweep.x || box.y > sweep.y + sweep.height || box.y + box.height < sweep.y)
                continue;
            const std::span<const ink_point> points { stroke.points() };
            // 점 하나의 획은 그 점과 같은 점을 잇는 선분 하나로 본다.
            const std::size_t segments { points.size() > 1 ? points.size() - 1 : 1 };
            bool hit { false };
            for (std::size_t index { 0 }; index < segments && hit == false; ++index)
            {
                const ink_point& a { points[index] };
                const ink_point& b { points[std::min(index + 1, points.size() - 1)] };
                const float half { std::max(ink_width_at(stroke.style(), a.pressure), ink_width_at(stroke.style(), b.pressure)) / 2.0f };
                hit = segment_distance(from, to, a, b) <= reach + half;
            }
            if (hit)
                ids.push_back(stroke.id());
        }
        return ids;
    }

    ui_image render_ink_image(const ink_layer& layer, const size_f size, const float scale, const ui_color_palette& palette)
    {
        if (std::isfinite(size.width) == false || std::isfinite(size.height) == false || std::isfinite(scale) == false || size.width <= 0.0f || size.height <= 0.0f || scale <= 0.0f)
            return {};
        const int width { static_cast<int>(std::ceil(size.width * scale)) };
        const int height { static_cast<int>(std::ceil(size.height * scale)) };
        sk_sp<SkSurface> surface { SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height)) };
        if (surface == nullptr)
            return {};
        SkCanvas& canvas { *surface->getCanvas() };
        canvas.clear(SK_ColorTRANSPARENT);
        canvas.scale(scale, scale);
        for (const ink_stroke& stroke : layer.strokes())
            ink_access::draw(canvas, stroke, palette.primary_foreground);
        return image_access::raster_image(surface->makeImageSnapshot());
    }

    void ink_access::draw(SkCanvas& canvas, const ink_stroke& stroke, const ui_color default_color)
    {
        if (stroke.backing_ == nullptr || stroke.backing_->path.isEmpty())
            return;
        canvas.drawPath(stroke.backing_->path, solid_paint(stroke.backing_->style.color.value_or(default_color)));
    }

    void ink_access::draw(SkCanvas& canvas, const ink_layer& layer, const ui_color default_color)
    {
        if (layer.backing_ == nullptr || layer.backing_->strokes.empty())
            return;
        sk_sp<SkPicture> picture {};
        {
            const std::lock_guard lock { layer.backing_->mutex };
            if (layer.backing_->picture == nullptr || layer.backing_->picture_color != default_color)
            {
                layer.backing_->picture = record_layer(layer.backing_->strokes, default_color);
                layer.backing_->picture_color = default_color;
            }
            picture = layer.backing_->picture;
        }
        if (picture != nullptr)
            canvas.drawPicture(picture);
    }

    const SkPicture* ink_access::memo(const ink_layer& layer)
    {
        if (layer.backing_ == nullptr)
            return nullptr;
        const std::lock_guard lock { layer.backing_->mutex };
        return layer.backing_->picture.get();
    }
}
