#include "luil/ui/sheet_host_element.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/panel_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace luil {
    namespace {
        // 아래 시트 위의 끌기 손잡이 띠와 그 안의 막대다 (논리 픽셀).
        constexpr float handle_strip_height { 24.0f };
        constexpr float handle_bar_width { 32.0f };
        constexpr float handle_bar_height { 4.0f };
        // 옆 시트·서랍이 자리에서 남기는 폭이다 — 뒤의 화면이 보여야 시트임을 안다.
        constexpr float side_reserved_width { 56.0f };
        constexpr float corner_radius { 16.0f };

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] float finite_or(const float value, const float fallback) noexcept
        {
            return std::isfinite(value) ? value : fallback;
        }

        // 아래 시트의 끌기 손잡이다. 가운데에 짧은 막대를 그리고 끌기는 host가 세운다.
        class sheet_handle_element final : public ui_element
        {
        public:
            explicit sheet_handle_element(const std::u8string& owner)
                : ui_element { { ui_element_kind::sheet_handle, owner } }
            {
                set_tab_stop(false);
                set_cursor(ui_cursor::grab);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                scale_ = positive_scale(context.scale);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                static_cast<void>(interaction);
                const rect_f box { bounds() };
                if (box.width <= 0.0f || box.height <= 0.0f)
                    return;
                const float width { handle_bar_width * scale_ };
                const float height { handle_bar_height * scale_ };
                const SkRect bar { SkRect::MakeXYWH(box.x + (box.width - width) / 2.0f, box.y + (box.height - height) / 2.0f, width, height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(bar, height / 2.0f, height / 2.0f), solid_paint(with_alpha(context.palette.secondary_foreground, 0.6f)));
            }

        private:
            float scale_ { 1.0f };
        };

        // 시트의 바탕이다. 손잡이와 내용을 품고, 누름을 흡수하며, 시트 밖으로 나간 내용을 자른다.
        class sheet_surface_element final : public ui_element
        {
        public:
            explicit sheet_surface_element(const std::u8string& owner)
                : ui_element { { ui_element_kind::none, owner } }
            {
                // 시트의 빈 자리를 누른 것이 뒤(scrim·화면)로 새지 않는다.
                set_hit_opaque(true);
                set_clip_children(true);
            }

            void add(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            // 손잡이(있으면)를 위 띠에, 내용을 그 아래 자리에 놓는다.
            void place(const arrange_context& context, const rect_f& host, ui_element* const handle, ui_element* const content, const sheet_kind kind)
            {
                set_bounds(context.slot);
                host_ = host;
                screen_ = context.knows_surface() ? context.surface : rect_f {};
                paint_ = sheet_background_bounds(context.slot, host, screen_);
                kind_ = kind;
                scale_ = positive_scale(context.scale);
                const rect_f box { context.slot };
                float top { box.y };
                if (handle != nullptr)
                {
                    const bool shown { kind == sheet_kind::bottom };
                    handle->set_visible(shown);
                    const float strip { shown ? std::min(box.height, handle_strip_height * scale_) : 0.0f };
                    handle->arrange(context.for_child({ box.x, box.y, box.width, strip }));
                    top += strip;
                }
                if (content != nullptr)
                    content->arrange(context.for_child({ box.x, top, box.width, std::max(0.0f, box.y + box.height - top) }));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                screen_ = {};
                paint_ = context.slot;
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const rect_f box { paint_ };
                if (box.width > 0.0f && box.height > 0.0f)
                {
                    // 화면 가장자리에 붙은 쪽은 각지고, 화면 안쪽을 향한 모서리만 둥글다.
                    const float radius { corner_radius * scale_ };
                    const SkVector round { radius, radius };
                    const SkVector square { 0.0f, 0.0f };
                    SkVector radii[4] { square, square, square, square };
                    // 왼쪽 위, 오른쪽 위, 오른쪽 아래, 왼쪽 아래 차례다.
                    if (kind_ == sheet_kind::bottom)
                        radii[0] = radii[1] = round;
                    else if (kind_ == sheet_kind::end)
                        radii[0] = radii[3] = round;
                    else
                        radii[1] = radii[2] = round;
                    SkRRect shape {};
                    shape.setRectRadii(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radii);
                    // 그림자는 표면(모르면 host의 자리) 안에만 드리운다. 화면 가장자리에 붙은 쪽 너머로 비치지 않게.
                    const rect_f clip { screen_.width > 0.0f && screen_.height > 0.0f ? screen_ : host_ };
                    context.canvas.save();
                    context.canvas.clipRect(SkRect::MakeXYWH(clip.x, clip.y, clip.width, clip.height));
                    draw_surface_shadow(context, box, radius, 0.25f);
                    context.canvas.restore();
                    context.canvas.drawRRect(shape, solid_paint(context.palette.surface_background));
                }
                draw_children(context, interaction);
            }

        private:
            sheet_kind kind_ { sheet_kind::bottom };
            float scale_ { 1.0f };
            // host가 받은 자리다. 표면을 모르면 그림자를 이 안으로 자른다.
            rect_f host_ {};
            // 표면 전체다 (모르면 비었다).
            rect_f screen_ {};
            // 바탕을 칠할 자리다. 자리 가장자리에 닿은 변은 표면 끝까지 늘었다.
            rect_f paint_ {};
        };

        // 놓은 자리에서 낼 메시지다. 닫는 쪽이 가까우면 닫힘, 아니면 가장 가까운 멈춤점이다.
        [[nodiscard]] input_action release_action(const sheet_host_config& config, const float height, const float velocity)
        {
            const sheet_settle settle { settle_sheet(config, height, velocity) };
            if (settle.dismiss && config.dismiss)
                return config.dismiss(sheet_dismiss_reason::drag);
            if (config.settle)
                return config.settle(settle.dismiss ? sheet_detent::half : settle.detent);
            return {};
        }

        [[nodiscard]] std::vector<input_action> as_list(input_action action)
        {
            if (std::holds_alternative<std::monostate>(action))
                return {};
            return { std::move(action) };
        }
    }

    sheet_kind resolve_sheet_kind(const sheet_host_config& config, const float width) noexcept
    {
        if (config.kind == sheet_kind::bottom && config.side_from_width.has_value() && std::isfinite(width) && width >= *config.side_from_width)
            return sheet_kind::end;
        return config.kind;
    }

    float sheet_detent_height(const sheet_host_config& config, const sheet_detent detent, const float height) noexcept
    {
        const float available { std::max(0.0f, finite_or(height, 0.0f)) };
        const float half { available / 2.0f };
        const float full { std::max(half, available - std::max(0.0f, finite_or(config.full_top_margin, 0.0f))) };
        return detent == sheet_detent::full ? full : half;
    }

    float sheet_visible_height(const sheet_host_config& config, const float height) noexcept
    {
        const float base { sheet_detent_height(config, config.detent, height) };
        const float full { sheet_detent_height(config, sheet_detent::full, height) };
        return std::clamp(base - finite_or(config.drag_offset, 0.0f), 0.0f, full);
    }

    sheet_settle settle_sheet(const sheet_host_config& config, const float height, const float velocity) noexcept
    {
        const float visible { sheet_visible_height(config, height) };
        const float half { sheet_detent_height(config, sheet_detent::half, height) };
        const float full { sheet_detent_height(config, sheet_detent::full, height) };
        // 튕겼으면 지금 높이에서 그 방향의 다음 자리다. 높이가 아니라 방향으로 정하므로, 끈 양이 아직 tree에 실리지
        // 않아 멈춤점 높이 그대로 보여도 한 칸은 옮긴다.
        const float fling { finite_or(config.fling_velocity, 0.0f) };
        if (fling > 0.0f && std::isfinite(velocity) && std::abs(velocity) >= fling)
        {
            if (velocity > 0.0f)
                return visible > half ? sheet_settle { sheet_detent::half, false } : sheet_settle { sheet_detent::half, true };
            return visible < half ? sheet_settle { sheet_detent::half, false } : sheet_settle { sheet_detent::full, false };
        }
        // 닫힘·반·전체 중 가장 가까운 자리다. 같은 거리면 닫지 않는 쪽, 그다음 반을 고른다.
        const float to_closed { visible };
        const float to_half { std::abs(visible - half) };
        const float to_full { std::abs(full - visible) };
        if (to_closed < to_half && to_closed < to_full)
            return { sheet_detent::half, true };
        return { to_full < to_half ? sheet_detent::full : sheet_detent::half, false };
    }

    rect_f sheet_background_bounds(const rect_f& sheet, const rect_f& slot, const rect_f& surface) noexcept
    {
        if (surface.width <= 0.0f || surface.height <= 0.0f || sheet.width <= 0.0f || sheet.height <= 0.0f)
            return sheet;
        // 닿았는지는 반 픽셀 안에서 본다. 배율을 곱한 자리가 소수로 떨어진다.
        constexpr float touch { 0.5f };
        float left { sheet.x };
        float top { sheet.y };
        float right { sheet.x + sheet.width };
        float bottom { sheet.y + sheet.height };
        if (std::abs(left - slot.x) <= touch)
            left = std::min(left, surface.x);
        if (std::abs(top - slot.y) <= touch)
            top = std::min(top, surface.y);
        if (std::abs(right - (slot.x + slot.width)) <= touch)
            right = std::max(right, surface.x + surface.width);
        if (std::abs(bottom - (slot.y + slot.height)) <= touch)
            bottom = std::max(bottom, surface.y + surface.height);
        return { left, top, right - left, bottom - top };
    }

    rect_f sheet_surface_bounds(const sheet_host_config& config, const rect_f& slot, const float scale) noexcept
    {
        const float factor { positive_scale(scale) };
        const float width { slot.width / factor };
        const float height { slot.height / factor };
        const sheet_kind kind { resolve_sheet_kind(config, width) };
        if (kind == sheet_kind::bottom)
        {
            const float sheet_width { std::max(0.0f, std::min(width, finite_or(config.bottom_max_width, width))) };
            const float visible { sheet_visible_height(config, height) };
            return { slot.x + (slot.width - sheet_width * factor) / 2.0f, slot.y + slot.height - visible * factor, sheet_width * factor, visible * factor };
        }
        const float side { std::clamp(finite_or(config.side_width, 0.0f), 0.0f, std::max(0.0f, width - side_reserved_width)) };
        if (kind == sheet_kind::end)
            return { slot.x + slot.width - side * factor, slot.y, side * factor, slot.height };
        return { slot.x, slot.y, side * factor, slot.height };
    }

    sheet_host_element::sheet_host_element(sheet_host_config config)
        : ui_element { ui_element_id { ui_element_kind::sheet_host, config.owner } }
        , config_ { std::move(config) }
    {
        if (config_.modal)
        {
            // modal dialog와 같은 셋이다: 초점 가둠, Esc, 뒤를 막는 scrim.
            set_focus_trap(true);
            if (config_.dismiss)
                set_dismiss_action([dismiss = config_.dismiss](const ui_action_context&) { return as_list(dismiss(sheet_dismiss_reason::escape)); });
            set_focus_entry(config_.focus_entry);
            set_focus_return(config_.focus_return);

            panel_config scrim {};
            scrim.background = [opacity = config_.scrim_opacity](const ui_color_palette& palette) { return with_alpha(palette.content_shadow, opacity); };
            auto panel { std::make_unique<panel_element>(ui_element_id { ui_element_kind::sheet_scrim, config_.owner }, std::move(scrim)) };
            if (config_.dismiss)
                panel->set_action(ui_trigger::left_click, [dismiss = config_.dismiss](const ui_action_context&) { return as_list(dismiss(sheet_dismiss_reason::scrim)); });
            // 액션이 없어도 뒤로 새지 않게 흡수한다. Tab의 자리는 아니다.
            panel->set_hit_opaque(true);
            panel->set_tab_stop(false);
            scrim_ = panel.get();
            add_child(std::move(panel));
        }

        auto surface { std::make_unique<sheet_surface_element>(config_.owner) };
        auto handle { std::make_unique<sheet_handle_element>(config_.owner) };
        handle_ = handle.get();
        surface->add(std::move(handle));
        surface_ = surface.get();
        add_child(std::move(surface));
    }

    void sheet_host_element::set_content(std::unique_ptr<ui_element> content)
    {
        if (content == nullptr)
            return;
        content_ = content.get();
        static_cast<sheet_surface_element*>(surface_)->add(std::move(content));
    }

    void sheet_host_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { positive_scale(context.scale) };
        kind_ = resolve_sheet_kind(config_, context.slot.width / scale);
        screen_ = context.knows_surface() ? context.surface : rect_f {};
        if (scrim_ != nullptr)
            scrim_->arrange(context);
        static_cast<sheet_surface_element*>(surface_)->place(context.for_child(sheet_surface_bounds(config_, context.slot, scale)), context.slot, handle_, content_, kind_);

        // 끌기는 아래 시트만이다. 이번 배치의 자리 높이로 놓을 자리를 정한다.
        if (kind_ != sheet_kind::bottom || config_.drag == nullptr)
        {
            handle_->set_pointer_drag_target(std::nullopt);
            surface_->set_pan_handoff(std::nullopt);
            return;
        }
        const float height { context.slot.height / scale };
        auto release = [config = config_, height](const float final_delta, const float velocity) {
            sheet_host_config current { config };
            current.drag_offset += final_delta;
            return release_action(current, height, velocity);
        };
        pointer_drag_target handle {};
        handle.on_move = [drag = config_.drag, scale](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            const float delta { (current.y - previous.y) / scale };
            if (delta == 0.0f)
                return {};
            return { drag(delta) };
        };
        handle.on_release = [release, scale](const ui_action_context&, const drag_velocity velocity) { return as_list(release(0.0f, velocity.y / scale)); };
        // 손잡이는 손가락을 따라 내려간다. 누른 자리가 곧 scrim에 덮여도 끌기가 이어진다.
        handle.follows_pointer = true;
        handle_->set_pointer_drag_target(std::move(handle));
        surface_->set_pan_handoff(pan_handoff_target { scroll_axis::vertical, scale, config_.drag, release });
    }

    void sheet_host_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // modal이면 자리 밖의 띠도 같은 scrim 색으로 칠한다. 누르기는 자리 안의 scrim만 받는다.
        if (scrim_ != nullptr && screen_.width > 0.0f && screen_.height > 0.0f)
        {
            const SkPaint paint { solid_paint(with_alpha(context.palette.content_shadow, config_.scrim_opacity)) };
            for (const rect_f& band : bands_outside(screen_, bounds()))
                context.canvas.drawRect(SkRect::MakeXYWH(band.x, band.y, band.width, band.height), paint);
        }
        // scrim이 먼저, 시트가 그 위다 (자식 순서 그대로).
        draw_children(context, interaction);
    }

    access_info sheet_host_element::accessibility() const
    {
        return { .role = config_.modal ? access_role::dialog : access_role::pane, .name = config_.name };
    }

    sheet_kind sheet_host_element::kind() const noexcept
    {
        return kind_;
    }

    rect_f sheet_host_element::surface_bounds() const noexcept
    {
        return surface_->bounds();
    }
}
