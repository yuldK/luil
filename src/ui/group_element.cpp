#include "luil/ui/group_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <optional>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 접이식 섹션의 제목 줄이다.
        // 펼침 표시 글리프와 제목을 그리고, 토글이 있으면 줄 전체가 눌린다.
        class group_header_element final : public ui_element
        {
        public:
            group_header_element(std::u8string owner, std::u8string title, const bool collapsed, const ui_action& toggle)
                : ui_element { ui_element_id { ui_element_kind::group_header, std::move(owner) } }
                , title_ { std::move(title) }
                , collapsed_ { collapsed }
            {
                if (toggle == nullptr)
                    return;
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, toggle);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                draw_hover_fill(context, box, id(), interaction, enabled());

                const float inset { 8.0f * scale };
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
                const SkPaint dim { solid_paint(context.palette.secondary_foreground) };
                draw_centered_glyph(context.canvas, collapsed_ ? codicons::icon_chevron_right : codicons::icon_chevron_down, { box.x + inset, box.y, 14.0f * scale, box.height }, glyph_font, dim);

                const float text_left { box.x + inset + 20.0f * scale };
                const float text_width { box.x + box.width - inset - text_left };
                if (text_width > 0.0f && title_.empty() == false)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
                    const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                    static_cast<void>(draw_text_within(context.canvas, title_, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 누를 수 있으면 접기 단추이고, 아니면 제목 글일 뿐이다.
                return { .role = action(ui_trigger::left_click) != nullptr ? access_role::button : access_role::static_text, .name = title_ };
            }

        private:
            std::u8string title_ {};
            bool collapsed_ { false };
        };
    } // namespace

    group_element::group_element(group_config config, std::unique_ptr<ui_element> content)
        : ui_element { ui_element_id { ui_element_kind::group, config.owner } }
        , config_ { std::move(config) }
    {
        // 제목 줄의 클릭이다. 토글이 없으면 절대 메시지에서 지금 상태의 반대를
        // 담아 만든다 — factory 하나로 클릭과 접근 실행이 함께 선다.
        ui_action toggle { config_.toggle };
        if (toggle == nullptr && config_.set_collapsed != nullptr)
        {
            const bool target { config_.collapsed == false };
            toggle = [this, target](const ui_action_context&) -> std::vector<input_action> { return { config_.set_collapsed(target) }; };
        }
        auto header { std::make_unique<group_header_element>(config_.owner, config_.title, config_.collapsed, toggle) };
        header_ = header.get();
        add_child(std::move(header));

        // 접힌 그룹의 내용은 tree에 넣지 않는다.
        // 보이지 않는 내용이 눌리는 유령을 없앤다.
        if (config_.collapsed || content == nullptr)
            return;
        content_ = content.get();
        add_child(std::move(content));
    }

    float group_element::height_for(const group_config& config) noexcept
    {
        return group_header_height + (config.collapsed ? 0.0f : (config.content_height > 0.0f ? config.content_height : 0.0f));
    }

    void group_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, height_for(config_) * scale });

        const float header_height { group_header_height * scale };
        header_->arrange(context.for_child({ context.slot.x, context.slot.y, context.slot.width, header_height }));
        if (content_ != nullptr)
            content_->arrange(context.for_child({ context.slot.x, context.slot.y + header_height, context.slot.width, config_.content_height * scale }));
    }

    void group_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const float radius { context.metrics.row_corner_radius * scale };
        SkPaint border { solid_paint(context.palette.group_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), border);
        draw_children(context, interaction);
    }
    access_info group_element::accessibility() const
    {
        // 접힌 그룹의 내용은 tree에 아예 없다 — "접혀 있지만 존재하는 자식"이
        // 아니라 접힘이 곧 없음이다 (accessibility-design.md).
        return { .role = access_role::group, .name = config_.title, .expanded = config_.collapsed == false };
    }

    std::optional<std::vector<input_action>> group_element::access_actions(const access_request& request) const
    {
        // 펼치기·접기는 절대 메시지만 탄다 — 머리행의 클릭(토글)으로 흘리면
        // 오래된 발행본을 본 같은 명령 둘이 두 번 뒤집는다
        // (accessibility-action-design.md).
        if (request.command == access_command::expand || request.command == access_command::collapse)
        {
            if (config_.set_collapsed == nullptr)
                return std::nullopt;
            return std::vector<input_action> { config_.set_collapsed(request.command == access_command::collapse) };
        }
        // 나머지 실행이 사는 곳은 머리행이다 (accessibility-action-design.md).
        if (header_ == nullptr)
            return std::nullopt;
        return header_->access_actions(request);
    }
} // namespace luil
