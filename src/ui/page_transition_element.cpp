#include "luil/ui/page_transition_element.h"

#include "luil/ui/ui_tree.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    namespace {
        // 확대의 작은 쪽·큰 쪽 배율이다. forward는 이전 화면이 커지며 사라지고 새 화면이 작은 데서 커지며 나타난다.
        //  - 차이가 몇 %뿐인 까닭은 화면 전체가 움직여서다. 더 크면 글이 번지는 시간이 눈에 띈다.
        constexpr float zoom_small_scale { 0.94f };
        constexpr float zoom_large_scale { 1.04f };
        // 이전 root와 요소의 크기가 같다고 보는 차다 (물리 픽셀). 배치의 반올림만 넘긴다.
        constexpr float size_tolerance { 0.5f };

        // 칠하지 않는 덮개다. 전환이 도는 동안 포인터를 막는다 (scrim이 진하기 0이어도 막는 선례).
        //  - `hit_test` 재정의가 아니라 자식으로 두는 까닭은 끌어 놓기(`drop_hit_test`)가 virtual이 아니고 자식부터 걷기 때문이다.
        class page_transition_cover final : public ui_element
        {
        public:
            explicit page_transition_cover(std::u8string owner)
                : ui_element { ui_element_id { ui_element_kind::page_transition_cover, std::move(owner) } }
            {
                set_hit_opaque(true);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }
        };

        // 한 화면을 그리는 자세다. 옮김은 물리 픽셀, 배율은 요소 bounds의 가운데가 기준이다.
        struct page_pose
        {
            float dx { 0.0f };
            float dy { 0.0f };
            float scale { 1.0f };
            float alpha { 1.0f };
        };

        struct page_poses
        {
            page_pose prior {};
            page_pose next {};
        };

        [[nodiscard]] float lerp(const float from, const float to, const float t) noexcept
        {
            return from + (to - from) * t;
        }

        [[nodiscard]] page_poses poses_at(const page_transition_style& style, const float progress, const rect_f& box) noexcept
        {
            page_poses poses {};
            const bool forward { style.direction == page_transition_direction::forward };
            if (style.effect == page_transition_effect::slide)
            {
                const bool horizontal { style.axis == page_transition_axis::horizontal };
                const float extent { horizontal ? box.width : box.height };
                // 나간 거리만 기기 픽셀로 반올림하고 들어오는 쪽은 그 나머지다. 둘을 따로 반올림하면 사이에 한 픽셀 틈이 생긴다.
                const float shift { std::round(progress * extent) };
                const float sign { forward ? 1.0f : -1.0f };
                const float prior_offset { -sign * shift };
                const float next_offset { sign * (extent - shift) };
                if (horizontal)
                {
                    poses.prior.dx = prior_offset;
                    poses.next.dx = next_offset;
                }
                else
                {
                    poses.prior.dy = prior_offset;
                    poses.next.dy = next_offset;
                }
                return poses;
            }
            // 앞 절반에 이전 화면이 사라지고 뒤 절반에 새 화면이 나타난다. 반투명한 두 화면이 겹쳐 비치지 않는다.
            poses.prior.alpha = std::clamp(1.0f - 2.0f * progress, 0.0f, 1.0f);
            poses.next.alpha = std::clamp(2.0f * progress - 1.0f, 0.0f, 1.0f);
            if (style.effect == page_transition_effect::zoom)
            {
                poses.prior.scale = lerp(1.0f, forward ? zoom_large_scale : zoom_small_scale, progress);
                poses.next.scale = lerp(forward ? zoom_small_scale : zoom_large_scale, 1.0f, progress);
            }
            return poses;
        }

        // 자세를 canvas에 건다. 알파가 1보다 작으면 화면 하나를 한 장으로 옅게 그리도록 층을 연다 (겹친 자식이 따로 비치지 않는다).
        void apply_pose(SkCanvas& canvas, const page_pose& pose, const rect_f& box)
        {
            const SkRect area { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            if (pose.alpha < 1.0f)
                canvas.saveLayerAlphaf(&area, pose.alpha);
            canvas.translate(pose.dx, pose.dy);
            if (pose.scale != 1.0f)
            {
                const float center_x { box.x + box.width / 2.0f };
                const float center_y { box.y + box.height / 2.0f };
                canvas.translate(center_x, center_y);
                canvas.scale(pose.scale, pose.scale);
                canvas.translate(-center_x, -center_y);
            }
        }
    }

    page_transition_element::page_transition_element(page_transition_config config, std::unique_ptr<ui_element> content)
        : ui_element { ui_element_id { ui_element_kind::page_transition, config.owner } }
        , transition_ { std::move(config.transition) }
    {
        set_clip_children(true);
        if (content != nullptr)
        {
            content_ = content.get();
            add_child(std::move(content));
        }
        // 덮개는 마지막 자식이라 hit test가 새 화면보다 먼저 만난다.
        if (transition_.prior != nullptr)
        {
            auto cover { std::make_unique<page_transition_cover>(std::move(config.owner)) };
            cover_ = cover.get();
            add_child(std::move(cover));
        }
    }

    void page_transition_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (content_ != nullptr)
            content_->arrange(context);
        if (cover_ != nullptr)
            cover_->arrange(context.for_child(context.slot));
    }

    bool page_transition_element::prior_fits() const noexcept
    {
        if (transition_.prior == nullptr || transition_.prior->root() == nullptr)
            return false;
        const rect_f& prior { transition_.prior->root()->bounds() };
        const rect_f& box { bounds() };
        return std::abs(prior.width - box.width) <= size_tolerance && std::abs(prior.height - box.height) <= size_tolerance;
    }

    void page_transition_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 움직임 줄이기면 진행을 묻기 전에 끝으로 본다. 크기가 다른 이전 root는 옛 크기 그림을 남기지 않으려고 그리지 않는다 (결정 5).
        if (context.reduce_motion || prior_fits() == false)
        {
            draw_children(context, interaction);
            return;
        }
        const float progress { page_transition_progress(transition_, context.now) };
        if (progress >= 1.0f)
        {
            draw_children(context, interaction);
            return;
        }

        const rect_f& box { bounds() };
        const page_poses poses { poses_at(transition_.style, progress, box) };
        const SkAutoCanvasRestore restore { &context.canvas, true };
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
        // 이전 화면을 먼저, 새 화면을 위에 그린다.
        if (poses.prior.alpha > 0.0f)
        {
            const SkAutoCanvasRestore restore_prior { &context.canvas, true };
            apply_pose(context.canvas, poses.prior, box);
            // 이전 root의 왼쪽 위를 요소의 왼쪽 위에 맞춘다. 같은 배율로 배치한 tree라 키우지 않는다.
            const ui_element& prior { *transition_.prior->root() };
            context.canvas.translate(box.x - prior.bounds().x, box.y - prior.bounds().y);
            // 빈 snapshot이다. 떠나는 화면에는 hover·누름·caret·초점 테·tooltip이 없다.
            prior.draw(context, interaction_snapshot {});
        }
        if (poses.next.alpha > 0.0f && content_ != nullptr && content_->visible())
        {
            const SkAutoCanvasRestore restore_next { &context.canvas, true };
            apply_pose(context.canvas, poses.next, box);
            content_->draw(context, interaction);
        }
    }

    std::optional<size_f> page_transition_element::preferred_size(const measure_context& context, const float available_width) const
    {
        return content_ != nullptr ? content_->preferred_size(context, available_width) : std::nullopt;
    }

    std::optional<std::chrono::steady_clock::time_point> page_transition_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (context.reduce_motion || prior_fits() == false)
            return std::nullopt;
        return page_transition_next_update(transition_, context.now);
    }
}
