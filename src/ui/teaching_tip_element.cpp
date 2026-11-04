#include "luil/ui/teaching_tip_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/text_block.h"
#include "luil/ui/transition.h"
#include "ui/default_text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace luil {
    namespace {
        // 치수는 논리 픽셀이고 그릴 때 배율을 곱한다.
        // 안쪽 여백은 사방 같다. 부분 사이(그림과 머리 줄, 본문과 단추 줄)는 `part_gap`, 제목과 본문 사이는 `title_body_gap`이다.
        constexpr float padding { 12.0f };
        constexpr float part_gap { 8.0f };
        constexpr float title_body_gap { 4.0f };
        constexpr float button_gap { 8.0f };
        constexpr float corner_radius { 8.0f };
        constexpr float image_max_height { 160.0f };
        constexpr float close_default_size { 24.0f };
        // 꼬리 끝과 대상 사이의 틈이다. 놓는 간격은 꼬리 길이에 이것을 더한 값이다.
        constexpr float tail_clearance { 2.0f };
        // 나타날 때 꼬리 반대쪽에서 다가오는 거리다.
        constexpr float appear_distance { 4.0f };
        constexpr float shadow_strength { 0.25f };
        // 배율을 곱했다 나눈 높이가 한 부동소수점 단위 어긋나도 줄 수를 줄이지 않게 하는 여유다 (message_box와 같다).
        constexpr float fit_tolerance { 0.01f };

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] bool has_button(const std::optional<teaching_tip_button>& button) noexcept
        {
            return button.has_value() && button->text.empty() == false && button->action != nullptr;
        }

        // 0이거나 잘못된 값은 "정하지 않음"이다.
        [[nodiscard]] float control_size_of(const teaching_tip_config& config) noexcept
        {
            return std::isfinite(config.control_size) && config.control_size > 0.0f ? config.control_size : 0.0f;
        }

        [[nodiscard]] std::vector<input_action> as_list(input_action action)
        {
            if (std::holds_alternative<std::monostate>(action))
                return {};
            return { std::move(action) };
        }

        [[nodiscard]] ui_action dismiss_with(const std::function<input_action(teaching_tip_dismiss_reason)>& dismiss, const teaching_tip_dismiss_reason reason)
        {
            return [dismiss, reason](const ui_action_context&) { return as_list(dismiss(reason)); };
        }

        [[nodiscard]] ui_element_id part_id(const ui_element_kind kind, const std::u8string_view owner, const std::u8string_view part)
        {
            std::u8string key { owner };
            key += u8'\u001f';
            key += part;
            return { kind, std::move(key) };
        }

        // 그림의 높이 비율(높이 / 폭)이다. 그리는 그림(Lottie, 움직이는 그림, 정지 그림 순)의 크기로 잰다. 크기가 없으면 nullopt다.
        [[nodiscard]] std::optional<float> image_ratio(const image_config& image) noexcept
        {
            int width { 0 };
            int height { 0 };
            if (image.lottie.valid())
            {
                width = image.lottie.width();
                height = image.lottie.height();
            }
            else if (image.animation.valid())
            {
                width = image.animation.width();
                height = image.animation.height();
            }
            else if (image.image.valid())
            {
                width = image.image.width();
                height = image.image.height();
            }
            if (width <= 0 || height <= 0)
                return std::nullopt;
            return static_cast<float>(height) / static_cast<float>(width);
        }

        // 제목은 본문과 굵기로 갈린다 (정보 막대와 같은 까닭). 둘 다 위에서부터 쌓고 줄을 바꾼다.
        [[nodiscard]] text_block_style title_style(const ui_metrics& metrics) noexcept
        {
            return text_block_style {
                .font = { .size = metrics.body_font_size, .weight = font_weight::semibold },
                .vertical = text_vertical_align::top,
                .wrap = true,
            };
        }

        [[nodiscard]] text_block_style body_style(const ui_metrics& metrics, const std::size_t max_lines) noexcept
        {
            return text_block_style {
                .font = { .size = metrics.body_font_size, .weight = metrics.font_weights.body_text },
                .vertical = text_vertical_align::top,
                .wrap = true,
                .max_lines = max_lines,
            };
        }

        // 단추가 원하는 폭이다. 단추의 식을 옮겨 적지 않고 단추에게 묻는다.
        [[nodiscard]] float button_width_of(const teaching_tip_button& button, const text_button_visual visual, const measure_context& context)
        {
            const text_button_element probe { ui_element_id { ui_element_kind::teaching_tip_action }, text_button_config { .text = button.text, .visual = visual } };
            const std::optional<size_f> size { probe.preferred_size(context, std::numeric_limits<float>::infinity()) };
            return size.has_value() ? size->width : text_button_minimum_width;
        }

        // 한 폭에서 풍선의 높이와 부분의 자리를 정한 결과다 (논리 픽셀, 풍선 왼쪽 위 기준). `place`와 arrange가 이 식 하나를 쓴다.
        struct tip_plan
        {
            float width { 0.0f };
            float inner_width { 0.0f };
            float height { 0.0f };
            bool has_image { false };
            float image_top { 0.0f };
            float image_height { 0.0f };
            bool has_close { false };
            float close_size { 0.0f };
            // 머리 줄은 제목과 닫기 단추가 나눠 쓴다. 닫기는 줄 오른쪽 위, 제목의 첫 줄은 닫기의 세로 가운데에 선다.
            float header_top { 0.0f };
            float title_top { 0.0f };
            float title_width { 0.0f };
            text_block_layout title {};
            float body_top { 0.0f };
            std::size_t body_max_lines { 0 };
            text_block_layout body {};
            float button_height { 0.0f };
            bool stacked { false };
            float buttons_top { 0.0f };
            // 놓는 차례(primary, secondary)의 단추 폭이다. 쌓이면 모두 안쪽 폭이다.
            std::vector<float> button_widths {};
        };

        [[nodiscard]] tip_plan plan_tip(const teaching_tip_config& config, const measure_context& context, const float width, const float max_height)
        {
            const ui_metrics& metrics { context.style_metrics() };
            const float control { control_size_of(config) };

            tip_plan plan {};
            plan.width = std::isfinite(width) ? std::max(0.0f, width) : 0.0f;
            plan.inner_width = std::max(0.0f, plan.width - 2.0f * padding);
            plan.has_close = config.dismiss != nullptr;
            plan.close_size = plan.has_close ? (control > 0.0f ? control : close_default_size) : 0.0f;
            plan.title_width = std::max(0.0f, plan.inner_width - (plan.has_close ? plan.close_size + part_gap : 0.0f));

            std::optional<float> ratio {};
            if (config.image.has_value())
                ratio = image_ratio(*config.image);
            plan.has_image = ratio.has_value() && plan.inner_width > 0.0f;
            if (plan.has_image)
                plan.image_height = std::min(image_max_height, plan.inner_width * *ratio);

            // 글이 설 폭이 없으면 재지 않는다. 한 글자 폭으로 접어 풍선이 끝없이 길어지는 것을 막는다.
            if (config.title.empty() == false && plan.title_width > 0.0f)
                plan.title = measure_text_block(context, config.title, title_style(metrics), plan.title_width);
            const auto measure_body = [&](const std::size_t max_lines) {
                if (config.body.empty() || plan.inner_width <= 0.0f)
                    return text_block_layout {};
                return measure_text_block(context, config.body, body_style(metrics, max_lines), plan.inner_width);
            };
            plan.body = measure_body(0);

            plan.button_height = control > 0.0f ? control : text_button_height;
            if (has_button(config.primary))
                plan.button_widths.push_back(button_width_of(*config.primary, text_button_visual::accent, context));
            if (has_button(config.secondary))
                plan.button_widths.push_back(button_width_of(*config.secondary, text_button_visual::normal, context));
            const std::size_t button_count { plan.button_widths.size() };
            float row_width { button_count > 0u ? static_cast<float>(button_count - 1u) * button_gap : 0.0f };
            for (const float button : plan.button_widths)
                row_width += button;
            plan.stacked = button_count > 1u && row_width > plan.inner_width + fit_tolerance;
            if (plan.stacked)
                plan.button_widths.assign(button_count, plan.inner_width);
            float button_area { 0.0f };
            if (button_count > 0u)
                button_area = plan.stacked ? static_cast<float>(button_count) * plan.button_height + static_cast<float>(button_count - 1u) * button_gap : plan.button_height;

            // 위에서 아래로 그림, 머리 줄(제목·닫기), 본문, 단추 줄이다. 빈 부분은 자리도 간격도 갖지 않는다.
            const auto lay_out = [&] {
                float top { padding };
                if (plan.has_image)
                {
                    plan.image_top = top;
                    top += plan.image_height + part_gap;
                }
                plan.header_top = top;
                const float title_line { text_block_line_height(title_style(metrics)) };
                const float title_offset { plan.has_close && plan.title.height > 0.0f ? std::max(0.0f, (plan.close_size - title_line) / 2.0f) : 0.0f };
                plan.title_top = top + title_offset;
                const float header { std::max(plan.title.height > 0.0f ? title_offset + plan.title.height : 0.0f, plan.close_size) };
                top += header;
                if (plan.body.height > 0.0f)
                {
                    if (header > 0.0f)
                        top += title_body_gap;
                    plan.body_top = top;
                    top += plan.body.height;
                }
                if (button_count > 0u)
                {
                    if (top > padding)
                        top += padding;
                    plan.buttons_top = top;
                    top += button_area;
                }
                plan.height = top + padding;
            };
            lay_out();

            // 자리가 모자라면 본문의 줄 수를 줄이고 마지막 줄을 `…`로 줄인다 (message_box와 같은 규칙). 제목·그림·단추는 줄이지 않는다.
            if (std::isfinite(max_height) && plan.height > max_height + fit_tolerance && plan.body.lines.empty() == false)
            {
                const float line_height { text_block_line_height(body_style(metrics, 0)) };
                const std::size_t lines { plan.body.lines.size() };
                if (line_height > 0.0f)
                {
                    const std::size_t excess { static_cast<std::size_t>(std::ceil((plan.height - max_height) / line_height)) };
                    const std::size_t keep { excess >= lines ? 1u : lines - excess };
                    if (keep < lines)
                    {
                        plan.body_max_lines = keep;
                        plan.body = measure_body(keep);
                        lay_out();
                    }
                }
            }
            return plan;
        }

        [[nodiscard]] std::optional<rect_f> overlap(const rect_f& first, const rect_f& second) noexcept
        {
            const float left { std::max(first.x, second.x) };
            const float top { std::max(first.y, second.y) };
            const float right { std::min(first.x + first.width, second.x + second.width) };
            const float bottom { std::min(first.y + first.height, second.y + second.height) };
            if (right <= left || bottom <= top)
                return std::nullopt;
            return rect_f { left, top, right - left, bottom - top };
        }

        struct tip_layout
        {
            teaching_tip_placement placement {};
            tip_plan plan {};
        };

        [[nodiscard]] tip_layout lay_out_tip(const teaching_tip_config& config, const measure_context& context, const rect_f& slot)
        {
            const float scale { positive_scale(context.scale) };
            const float margin { teaching_tip_screen_margin * scale };
            const rect_f limit { slot.x + margin, slot.y + margin, std::max(0.0f, slot.width - 2.0f * margin), std::max(0.0f, slot.height - 2.0f * margin) };
            const float wanted { std::isfinite(config.width) && config.width > 0.0f ? config.width : teaching_tip_default_width };
            const float width { std::min(wanted, limit.width / scale) };

            tip_layout layout {};
            layout.plan = plan_tip(config, context, width, limit.height / scale);
            const float bubble_width { layout.plan.width * scale };
            const float bubble_height { layout.plan.height * scale };

            // 대상은 root 자리와 겹친 부분만 본다. 겹치지 않으면 대상이 없는 것과 같다 (결정 2).
            std::optional<rect_f> target {};
            if (config.target.has_value())
                target = overlap(*config.target, slot);
            if (target.has_value() == false)
            {
                // limit 가운데에 꼬리 없이 둔다. limit보다 크면 왼쪽·위 가장자리에 붙는다.
                const float x { std::max(limit.x, limit.x + (limit.width - bubble_width) / 2.0f) };
                const float y { std::max(limit.y, limit.y + (limit.height - bubble_height) / 2.0f) };
                layout.placement.bubble = { x, y, bubble_width, bubble_height };
                return layout;
            }

            const field_popup_anchor anchor { .field = *target, .limit = limit, .gap = teaching_tip_tail_length + tail_clearance };
            const field_popup_side_position position { place_field_popup_at_side(anchor, { layout.plan.width, layout.plan.height }, scale, config.side) };
            layout.placement.bubble = { position.x * scale, position.y * scale, bubble_width, bubble_height };
            // 네 방향 모두 모자라 밀었으면 꼬리가 대상을 바로 가리킬 수 없어 그리지 않는다.
            if (position.fits == false)
                return layout;

            // 꼬리 밑변은 모서리 반지름 안쪽으로 물린다. 대상 가운데가 그 밖이면 끝에 멈춘다.
            const rect_f& bubble { layout.placement.bubble };
            const bool vertical { position.side == field_popup_side::below || position.side == field_popup_side::above };
            const float inset { (corner_radius + teaching_tip_tail_width / 2.0f) * scale };
            const float start { vertical ? bubble.x : bubble.y };
            const float extent { vertical ? bubble.width : bubble.height };
            const float aim { vertical ? target->x + target->width / 2.0f : target->y + target->height / 2.0f };
            const float low { start + inset };
            const float high { start + extent - inset };
            layout.placement.side = position.side;
            layout.placement.tail_center = low <= high ? std::clamp(aim, low, high) : start + extent / 2.0f;
            return layout;
        }

        // 몸통과 꼬리를 한 윤곽으로 잇는다. 테가 꼬리 밑변을 가로지르지 않고 꼬리 둘레를 돈다.
        //  - 시계 방향으로 위 변, 오른쪽 변, 아래 변, 왼쪽 변을 차례로 지나며, 꼬리가 선 변에서 밑변 두 점과 끝을 거친다.
        [[nodiscard]] SkPath bubble_path(const rect_f& box, const float radius, const teaching_tip_placement& placement, const float tail_width, const float tail_length)
        {
            const float left { box.x };
            const float top { box.y };
            const float right { box.x + box.width };
            const float bottom { box.y + box.height };
            const float corner { std::max(0.0f, std::min(radius, std::min(box.width, box.height) / 2.0f)) };
            // 사분원은 무게 √½의 원뿔 곡선이다.
            constexpr float quarter { 0.70710678f };
            const float half { tail_width / 2.0f };
            const float center { placement.tail_center };
            const auto tail_on = [&placement](const field_popup_side side) { return placement.side.has_value() && *placement.side == side; };

            SkPathBuilder path {};
            path.moveTo(left + corner, top);
            // 풍선이 대상 아래에 서면 꼬리는 위 변에서 위를 가리킨다.
            if (tail_on(field_popup_side::below))
            {
                path.lineTo(center - half, top);
                path.lineTo(center, top - tail_length);
                path.lineTo(center + half, top);
            }
            path.lineTo(right - corner, top);
            path.conicTo(right, top, right, top + corner, quarter);
            if (tail_on(field_popup_side::left))
            {
                path.lineTo(right, center - half);
                path.lineTo(right + tail_length, center);
                path.lineTo(right, center + half);
            }
            path.lineTo(right, bottom - corner);
            path.conicTo(right, bottom, right - corner, bottom, quarter);
            if (tail_on(field_popup_side::above))
            {
                path.lineTo(center + half, bottom);
                path.lineTo(center, bottom + tail_length);
                path.lineTo(center - half, bottom);
            }
            path.lineTo(left + corner, bottom);
            path.conicTo(left, bottom, left, bottom - corner, quarter);
            if (tail_on(field_popup_side::right))
            {
                path.lineTo(left, center + half);
                path.lineTo(left - tail_length, center);
                path.lineTo(left, center - half);
            }
            path.lineTo(left, top + corner);
            path.conicTo(left, top, left + corner, top, quarter);
            path.close();
            return path.detach();
        }

        // 나타남의 진행이다 [0, 1]. 연 시각이 없으면 1이고, 움직임 줄이기면 길이가 0이라 처음부터 1이다.
        [[nodiscard]] transition appear_motion(const teaching_tip_config& config, const bool reduce_motion) noexcept
        {
            if (config.opened_at == std::chrono::steady_clock::time_point {})
                return transition { 1.0f, 1.0f };
            return transition { 0.0f, 1.0f, config.opened_at, motion_duration(transition_duration, reduce_motion) };
        }
    }

    // 풍선과 꼬리, 제목, 본문을 직접 그리고 그림, 닫기, 행동 단추를 자식으로 둔다 (msg_box 내용과 같은 꼴).
    // 자리는 root가 정해 넘긴다. 몸은 그 자리를 그대로 받아 계획대로 자식을 놓는다.
    class teaching_tip_element::body final : public ui_element
    {
    public:
        explicit body(const teaching_tip_config& config)
            : ui_element { ui_element_id { ui_element_kind::teaching_tip_body, config.owner } }
            , config_ { config }
        {
            // 풍선의 빈 자리 누름이 덮개나 뒤 화면으로 새지 않는다.
            set_hit_opaque(true);

            if (config_.image.has_value())
            {
                auto image { std::make_unique<image_element>(ui_element_id { ui_element_kind::teaching_tip_image, config_.owner }, *config_.image) };
                image_ = image.get();
                add_child(std::move(image));
            }
            if (config_.dismiss != nullptr)
            {
                auto close { std::make_unique<button_element>(close_id(config_.owner), button_config { .glyph = codicons::icon_close, .role = button_visual_role::toolbar }) };
                const std::u8string name { default_text::text_or_default(config_.close_tooltip, default_text::teaching_tip_close()) };
                close->set_tooltip(name);
                close->set_access_name(name);
                close->set_cursor(ui_cursor::hand);
                close->set_action(ui_trigger::left_click, dismiss_with(config_.dismiss, teaching_tip_dismiss_reason::close_button));
                close_ = close.get();
                add_child(std::move(close));
            }
            // 기본 버튼은 세우지 않는다. modal의 진입이 primary라 Enter가 초점의 단추로 간다.
            const auto add_button = [this](const teaching_tip_button& button, ui_element_id id, const text_button_visual visual) {
                auto element { std::make_unique<text_button_element>(std::move(id), text_button_config { .text = button.text, .visual = visual }) };
                element->set_cursor(ui_cursor::hand);
                element->set_action(ui_trigger::left_click, button.action);
                buttons_.push_back(element.get());
                add_child(std::move(element));
            };
            if (has_button(config_.primary))
                add_button(*config_.primary, primary_id(config_.owner), text_button_visual::accent);
            if (has_button(config_.secondary))
                add_button(*config_.secondary, secondary_id(config_.owner), text_button_visual::normal);
        }

        void set_layout(const teaching_tip_placement& placement, tip_plan plan)
        {
            placement_ = placement;
            plan_ = std::move(plan);
        }

        void arrange(const arrange_context& context) override
        {
            set_bounds(context.slot);
            const float scale { positive_scale(context.scale) };
            const rect_f box { context.slot };
            const float left { box.x + padding * scale };
            if (image_ != nullptr)
            {
                // 크기를 모르는 그림은 자리를 갖지 않는다. 배치는 빠뜨리지 않는다.
                const float height { plan_.has_image ? plan_.image_height * scale : 0.0f };
                image_->arrange(context.for_child({ left, box.y + plan_.image_top * scale, plan_.inner_width * scale, height }));
            }
            if (close_ != nullptr)
            {
                const float size { plan_.close_size * scale };
                close_->arrange(context.for_child({ box.x + box.width - padding * scale - size, box.y + plan_.header_top * scale, size, size }));
            }
            if (buttons_.empty())
                return;

            const float height { plan_.button_height * scale };
            if (plan_.stacked)
            {
                for (std::size_t index { 0 }; index < buttons_.size(); ++index)
                {
                    const float top { plan_.buttons_top + static_cast<float>(index) * (plan_.button_height + button_gap) };
                    buttons_[index]->arrange(context.for_child({ left, box.y + top * scale, plan_.button_widths[index] * scale, height }));
                }
                return;
            }
            // 한 줄은 오른쪽 끝에 붙인다. 차례는 primary, secondary다.
            float total { static_cast<float>(buttons_.size() - 1u) * button_gap };
            for (const float width : plan_.button_widths)
                total += width;
            float x { plan_.width - padding - total };
            for (std::size_t index { 0 }; index < buttons_.size(); ++index)
            {
                buttons_[index]->arrange(context.for_child({ box.x + x * scale, box.y + plan_.buttons_top * scale, plan_.button_widths[index] * scale, height }));
                x += plan_.button_widths[index] + button_gap;
            }
        }

        void draw(draw_context& context, const interaction_snapshot& interaction) const override
        {
            const rect_f box { bounds() };
            if (box.width <= 0.0f || box.height <= 0.0f)
                return;
            const float scale { positive_scale(context.scale) };
            const float progress { transition_value(appear_motion(config_, context.reduce_motion), context.now) };
            if (progress <= 0.0f)
                return;

            // 나타나는 동안 몸 전체(자식 포함)를 함께 흐리고 꼬리 반대쪽에서 다가오게 옮긴다. 꼬리가 없으면 아래에서 온다.
            const bool moving { progress < 1.0f };
            if (moving)
            {
                context.canvas.saveLayerAlphaf(nullptr, progress);
                const float distance { (1.0f - progress) * appear_distance * scale };
                float dx { 0.0f };
                float dy { distance };
                if (placement_.side.has_value())
                {
                    switch (*placement_.side)
                    {
                    case field_popup_side::above:
                        dy = -distance;
                        break;
                    case field_popup_side::left:
                        dx = -distance;
                        dy = 0.0f;
                        break;
                    case field_popup_side::right:
                        dx = distance;
                        dy = 0.0f;
                        break;
                    case field_popup_side::below:
                        break;
                    }
                }
                context.canvas.translate(dx, dy);
            }

            const float radius { corner_radius * scale };
            draw_surface_shadow(context, box, radius, shadow_strength);
            const SkPath shape { bubble_path(box, radius, placement_, teaching_tip_tail_width * scale, teaching_tip_tail_length * scale) };
            context.canvas.drawPath(shape, solid_paint(context.palette.raised_background));
            // 테는 꼬리까지 돈다. 고대비가 바탕을 창 배경으로 접어도 풍선과 꼬리가 남는다.
            SkPaint border { solid_paint(context.palette.control_border) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(scale);
            context.canvas.drawPath(shape, border);

            const float left { box.x + padding * scale };
            if (plan_.title.lines.empty() == false)
            {
                const rect_f title_box { left, box.y + plan_.title_top * scale, plan_.title_width * scale, plan_.title.height * scale };
                static_cast<void>(draw_text_block(context, config_.title, title_box, title_style(context.metrics), solid_paint(context.palette.primary_foreground), plan_.title));
            }
            if (plan_.body.lines.empty() == false)
            {
                const rect_f body_box { left, box.y + plan_.body_top * scale, plan_.inner_width * scale, plan_.body.height * scale };
                static_cast<void>(
                    draw_text_block(context, config_.body, body_box, body_style(context.metrics, plan_.body_max_lines), solid_paint(context.palette.secondary_foreground), plan_.body)
                );
            }

            draw_children(context, interaction);
            if (moving)
                context.canvas.restore();
        }

        [[nodiscard]] access_info accessibility() const override
        {
            if (config_.body.empty())
                return { .role = access_role::none };
            return { .role = access_role::static_text, .name = config_.body };
        }

    private:
        teaching_tip_config config_ {};
        ui_element* image_ { nullptr };
        ui_element* close_ { nullptr };
        std::vector<ui_element*> buttons_ {};
        teaching_tip_placement placement_ {};
        tip_plan plan_ {};
    };

    teaching_tip_element::teaching_tip_element(teaching_tip_config config)
        : ui_element { ui_element_id { ui_element_kind::teaching_tip, config.owner } }
        , config_ { std::move(config) }
    {
        if (config_.modal)
        {
            // modal host·시트와 같은 셋이다: 초점 가둠, Esc, 뒤를 막는 덮개. 없는 dismiss는 세우지 않는다 (Esc가 앱으로 흐른다).
            set_focus_trap(true);
            if (config_.dismiss != nullptr)
                set_dismiss_action(dismiss_with(config_.dismiss, teaching_tip_dismiss_reason::escape));
            ui_element_id entry {};
            if (has_button(config_.primary))
                entry = primary_id(config_.owner);
            else if (has_button(config_.secondary))
                entry = secondary_id(config_.owner);
            else if (config_.dismiss != nullptr)
                entry = close_id(config_.owner);
            set_focus_entry(std::move(entry));
            set_focus_return(config_.focus_return);

            // 덮개는 투명하다. 누름과 휠을 흡수해 대상을 눌러도 대상은 눌리지 않는다.
            //  - panel의 빈 선택자는 `surface_background`라 알파 0인 색을 고른다.
            panel_config look {};
            look.background = [](const ui_color_palette& palette) { return with_alpha(palette.content_shadow, 0.0f); };
            auto scrim { std::make_unique<panel_element>(ui_element_id { ui_element_kind::teaching_tip_scrim, config_.owner }, std::move(look)) };
            if (config_.dismiss != nullptr)
                scrim->set_action(ui_trigger::left_click, dismiss_with(config_.dismiss, teaching_tip_dismiss_reason::outside));
            scrim->set_hit_opaque(true);
            scrim->set_tab_stop(false);
            scrim_ = scrim.get();
            add_child(std::move(scrim));
        }

        auto part { std::make_unique<body>(config_) };
        body_ = part.get();
        add_child(std::move(part));
    }

    teaching_tip_placement teaching_tip_element::place(const teaching_tip_config& config, const measure_context& context, const rect_f& slot)
    {
        return lay_out_tip(config, context, slot).placement;
    }

    ui_element_id teaching_tip_element::close_id(const std::u8string_view owner)
    {
        return part_id(ui_element_kind::teaching_tip_close, owner, u8"close");
    }

    ui_element_id teaching_tip_element::primary_id(const std::u8string_view owner)
    {
        return part_id(ui_element_kind::teaching_tip_action, owner, u8"primary");
    }

    ui_element_id teaching_tip_element::secondary_id(const std::u8string_view owner)
    {
        return part_id(ui_element_kind::teaching_tip_action, owner, u8"secondary");
    }

    void teaching_tip_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (scrim_ != nullptr)
            scrim_->arrange(context);
        // 매 frame 다시 놓는다. 창 크기·배율·스크롤이 바뀌면 앱이 그 frame의 대상 사각형을 준다.
        tip_layout layout { lay_out_tip(config_, context.measure(), context.slot) };
        const rect_f bubble { layout.placement.bubble };
        body_->set_layout(layout.placement, std::move(layout.plan));
        body_->arrange(context.for_child(bubble));
    }

    void teaching_tip_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 덮개는 투명하다. 몸이 그 위다 (자식 순서 그대로).
        draw_children(context, interaction);
    }

    access_info teaching_tip_element::accessibility() const
    {
        std::u8string name { config_.name.empty() ? config_.title : config_.name };
        if (name.empty())
            name = default_text::teaching_tip_name();
        return { .role = config_.modal ? access_role::dialog : access_role::pane, .name = std::move(name) };
    }

    std::optional<std::chrono::steady_clock::time_point> teaching_tip_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.opened_at == std::chrono::steady_clock::time_point {})
            return std::nullopt;
        return transition_next_update(appear_motion(config_, context.reduce_motion), context.now);
    }

    namespace {
        // 찾았는가와 그 보이는 자리다. 찾았는데 다 잘렸으면 자리가 비어 있다.
        struct target_search
        {
            bool found { false };
            std::optional<rect_f> shown {};
        };

        [[nodiscard]] target_search find_target(const ui_element& node, const ui_element_id& id, const std::optional<rect_f>& clip)
        {
            // 숨은 가지는 그려지지도 눌리지도 않는다. 가리킬 것이 없다.
            if (node.visible() == false)
                return {};
            if (node.id() == id)
                return { true, clip.has_value() ? overlap(node.bounds(), *clip) : std::optional<rect_f> { node.bounds() } };

            std::optional<rect_f> next_clip { clip };
            if (node.clip_children())
            {
                next_clip = clip.has_value() ? overlap(*clip, node.bounds()) : std::optional<rect_f> { node.bounds() };
                // 자른 결과가 비면 이 아래는 아무것도 보이지 않는다 (`ui_tree::visible_bounds`와 같다).
                if (next_clip.has_value() == false)
                    return {};
            }
            for (const std::unique_ptr<ui_element>& child : node.children())
                if (target_search search { find_target(*child, id, next_clip) }; search.found)
                    return search;
            return {};
        }
    }

    std::optional<rect_f> teaching_tip_target(const ui_element& root, const ui_element_id& id)
    {
        return find_target(root, id, std::nullopt).shown;
    }
}
