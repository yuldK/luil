#include "luil/ui/split_button_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/field_popup.h"
#include "ui/default_text.h"
#include "ui/text_button_paint.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 화살표의 chevron 글리프 크기다 (논리 픽셀). 드롭다운의 펼침 화살표와 같다.
        constexpr float chevron_size { 12.0f };

        // 분할 버튼의 한 절반이다.
        // 자리를 나누는 일과 상자 바탕·구분선은 root가 그리고, 이 절반은 자기 글과 chevron만 그린다.
        // 절반마다 hover·눌림·초점·활성이 따로 서므로 별 element여야 한다.
        class split_button_part final : public ui_element
        {
        public:
            struct appearance
            {
                // 모양을 읽는 설정이다. root가 정규화한 것(`link`는 normal, `selected`는 없음)을 받는다.
                text_button_config look {};
                text_button_fill fill { text_button_fill::plain };
                // 글을 그리는 절반인가 (본체, 또는 본체가 없을 때의 화살표).
                bool label { false };
                // 오른쪽에 chevron을 그리는 절반인가 (화살표).
                bool chevron { false };
                // 왼쪽 가장자리에 구분선이 서는 절반인가. chevron이 그만큼 비켜 가운데에 앉는다.
                bool divider { false };
                bool open { false };
                float arrow_width { split_button_arrow_width };
                // 접근성이 펼침 상태를 알리는 절반인가.
                bool expandable { false };
                std::function<input_action(bool open)> set_open {};
            };

            split_button_part(const ui_element_id id, appearance config)
                : ui_element { id }
                , appearance_ { std::move(config) }
            {
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const text_button_state state { .enabled = enabled(), .hovered = interaction.hovered == id(), .pressed = interaction.pressed == id() };
                rect_f label_box { box };
                if (appearance_.chevron)
                {
                    const float chevron_width { std::min(appearance_.arrow_width * scale, box.width) };
                    rect_f chevron_box { box.x + box.width - chevron_width, box.y, chevron_width, box.height };
                    if (appearance_.label)
                        label_box.width = box.width - chevron_width;
                    if (appearance_.divider)
                    {
                        // 구분선 한 줄은 화살표 자리 안에 있다. chevron은 그 줄을 뺀 나머지의 가운데에 앉는다.
                        chevron_box.x += scale;
                        chevron_box.width = std::max(0.0f, chevron_box.width - scale);
                    }
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), chevron_size * scale };
                    const SkPaint glyph { solid_paint(text_button_label_color(appearance_.look, appearance_.fill, state, context.palette)) };
                    draw_centered_glyph(context.canvas, appearance_.open ? codicons::icon_chevron_up : codicons::icon_chevron_down, chevron_box, glyph_font, glyph);
                }
                if (appearance_.label)
                    draw_text_button_label(context, appearance_.look, appearance_.fill, state, label_box);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 절반마다 단추 하나다. 펼침을 알리는 절반은 `expanded`가 있어 Invoke 대신 ExpandCollapse로 선다.
                access_info info { .role = access_role::button, .name = access_name() };
                if (appearance_.expandable)
                    info.expanded = appearance_.open;
                return info;
            }

            // 펼치기·접기는 목표 상태를 나르는 절대 메시지로 답한다. 클릭(뒤집기)으로 흘리면 오래된 발행본을 본 같은 명령 둘이
            // 메뉴를 열었다 도로 닫는다 (accessibility-action-design.md).
            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                if (request.command != access_command::expand && request.command != access_command::collapse)
                    return ui_element::access_actions(request);
                if (appearance_.expandable == false || appearance_.set_open == nullptr)
                    return std::nullopt;
                return std::vector<input_action> { appearance_.set_open(request.command == access_command::expand) };
            }

        private:
            appearance appearance_ {};
        };

        [[nodiscard]] float arrow_width_of(const split_button_config& config) noexcept
        {
            return std::isfinite(config.arrow_width) ? std::max(0.0f, config.arrow_width) : split_button_arrow_width;
        }
    }

    split_button_element::split_button_element(split_button_config config)
        : ui_element { ui_element_id { ui_element_kind::split_button, config.owner } }
        , config_ { std::move(config) }
    {
        // 두 절반이 한 상자라 상자가 없는 link는 normal로 읽고, 고른 값을 나르는 `selected`는 쓰지 않는다.
        if (config_.look.visual == text_button_visual::link)
            config_.look.visual = text_button_visual::normal;
        config_.look.selected.reset();
        const bool has_body { config_.invoke != nullptr };
        // 본체가 없으면 Enter가 갈 기본 동작도 없다. 채움이 헛된 약속을 하지 않게 한다.
        if (has_body == false)
            config_.look.default_button = false;
        // 채움은 상자 하나의 색이다. Enter의 대상인 본체가 꺼졌으면 화살표도 강조색으로 채우지 않는다.
        fill_ = text_button_fill_for(config_.look, config_.enabled && (has_body == false || config_.invoke_enabled));

        const float arrow_width { arrow_width_of(config_) };
        const std::u8string& text { config_.look.text };
        const auto make_arrow = [&](const bool alone) {
            split_button_part::appearance appearance {};
            appearance.look = config_.look;
            appearance.fill = fill_;
            appearance.label = alone;
            appearance.chevron = true;
            appearance.divider = alone == false;
            appearance.open = config_.open;
            appearance.arrow_width = arrow_width;
            appearance.expandable = true;
            appearance.set_open = config_.set_open;
            auto arrow { std::make_unique<split_button_part>(ui_element_id { ui_element_kind::split_button_arrow, config_.owner }, std::move(appearance)) };
            arrow->set_enabled(config_.enabled);
            if (config_.set_open != nullptr)
            {
                // 누름은 build 때의 상태 반대를 담아 절대 메시지 factory를 부른다. 접근성 expand·collapse도 같은 factory다.
                const ui_action press { [set_open = config_.set_open, target = config_.open == false](const ui_action_context&) -> std::vector<input_action> { return { set_open(target) }; } };
                arrow->set_action(ui_trigger::left_click, press);
                arrow->set_cursor(ui_cursor::hand);
            }
            // 화살표 쪽에서는 ↓가 달리 쓰이지 않는다. 메뉴 단추의 관례대로 ↓도 연다.
            arrow->set_key_handler(field_popup_key_handler(config_.open, config_.set_open, true));
            if (alone)
            {
                // 본체가 없을 때 이 단추가 버튼 전체다. 이름은 본체 글이고, 글이 없으면 화살표 이름과 툴팁이 대신한다.
                arrow->set_access_name(text.empty() ? (config_.arrow_name.empty() ? config_.tooltip : config_.arrow_name) : text);
                arrow->set_tooltip(config_.arrow_tooltip.empty() ? config_.tooltip : config_.arrow_tooltip);
            }
            else
            {
                const std::u8string& base { text.empty() ? config_.tooltip : text };
                arrow->set_access_name(config_.arrow_name.empty() ? default_text::split_button_arrow_name(base) : config_.arrow_name);
                arrow->set_tooltip(config_.arrow_tooltip);
            }
            arrow_ = arrow.get();
            add_child(std::move(arrow));
        };

        if (has_body)
        {
            split_button_part::appearance appearance {};
            appearance.look = config_.look;
            appearance.fill = fill_;
            appearance.label = true;
            appearance.open = config_.open;
            auto body { std::make_unique<split_button_part>(ui_element_id { ui_element_kind::split_button_body, config_.owner }, std::move(appearance)) };
            // 본체만 막는 설정이다. 메뉴는 그대로 열린다.
            body->set_enabled(config_.enabled && config_.invoke_enabled);
            body->set_action(ui_trigger::left_click, config_.invoke);
            body->set_cursor(ui_cursor::hand);
            body->set_tooltip(config_.tooltip);
            if (text.empty() == false)
                body->set_access_name(text);
            if (config_.look.default_button)
                body->set_default_button(true);
            // 본체의 ↓는 앱으로 흐른다. 목록 안 버튼의 ↓까지 가로채지 않는다.
            body->set_key_handler(field_popup_key_handler(config_.open, config_.set_open, false));
            body_ = body.get();
            add_child(std::move(body));
            make_arrow(false);

            // Tab에서 한 자리이고 ←/→가 절반 사이를 옮긴다. 들어올 때 본체에 서고, 본체가 꺼졌으면 첫 자리인 화살표에 선다.
            set_focus_group(focus_axis::horizontal);
            set_focus_entry(body_->id());
        }
        else
            make_arrow(true);

        // 메뉴가 열린 동안 키보드 초점이 버튼을 떠나면 닫는다 (콤보와 같다). 절반 사이의 이동은 떠남이 아니다.
        // 목표 상태를 나르는 메시지라 popup의 바깥 누름 닫기와 겹쳐 두 번 와도 같다.
        if (config_.open && config_.set_open != nullptr)
            set_focus_leave_action([close = config_.set_open](const ui_action_context&) { return std::vector<input_action> { close(false) }; });
    }

    void split_button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        if (arrow_ == nullptr)
            return;
        if (body_ == nullptr)
        {
            arrow_->arrange(context.for_child(context.slot));
            return;
        }
        // 화살표 폭은 지킨다. 좁으면 줄어드는 것은 본체 글이다.
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f slot { context.slot };
        const float arrow_width { std::min(arrow_width_of(config_) * scale, std::max(0.0f, slot.width)) };
        const float body_width { std::max(0.0f, slot.width - arrow_width) };
        body_->arrange(context.for_child({ slot.x, slot.y, body_width, slot.height }));
        arrow_->arrange(context.for_child({ slot.x + body_width, slot.y, arrow_width, slot.height }));
    }

    void split_button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float radius { context.metrics.control_corner_radius * scale };
        // 두 절반은 이 둥근 상자를 자기 사각형으로 잘라 칠한다. 바깥 모서리만 둥글고 안쪽 가장자리는 곧다.
        //  - 사각형 자르기는 안티에일리어싱 없이 픽셀 경계에서 자르므로 이웃한 절반 사이에 틈도 겹침도 생기지 않는다.
        const SkRRect shape { SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius) };
        for (const ui_element* const half : { body_, arrow_ })
        {
            if (half == nullptr)
                continue;
            // 메뉴가 열린 동안 화살표는 눌림 바탕이다 (`dropdown_element`와 같다). 포인터가 본체 위여도 두 절반은 따로 선다.
            const bool pressed { interaction.pressed == half->id() || (half == arrow_ && config_.open) };
            const text_button_state state { .enabled = half->enabled(), .hovered = interaction.hovered == half->id(), .pressed = pressed };
            ui_color background { text_button_background(fill_, state, context.palette) };
            // 선택자는 마지막에 얹고, 그 색은 hover·눌림에도 그대로 남는다. 두 절반에 같이 걸린다.
            if (config_.look.fill)
                background = config_.look.fill(context.palette);
            const rect_f area { half->bounds() };
            const SkAutoCanvasRestore restore { &context.canvas, true };
            context.canvas.clipRect(SkRect::MakeXYWH(area.x, area.y, area.width, area.height));
            context.canvas.drawRRect(shape, solid_paint(background));
        }

        // 구분선은 화살표 왼쪽 가장자리의 1 논리 픽셀 세로선이다. 화살표 자리 안이라 눌리지 않는 틈이 없다.
        if (body_ != nullptr && arrow_ != nullptr)
        {
            const rect_f area { arrow_->bounds() };
            const ui_color line { fill_ == text_button_fill::solid_accent ? context.palette.accent_foreground : context.palette.divider };
            context.canvas.drawRect(SkRect::MakeXYWH(area.x, area.y, scale, area.height), solid_paint(line));
        }
        draw_children(context, interaction);
    }

    std::optional<size_f> split_button_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(available_width);
        const ui_metrics& metrics { context.style_metrics() };
        const float content { text_button_element::icon_width_for(config_.look) + std::ceil(context.text_width(config_.look.text, metrics.body_font_size, { metrics.font_weights.button_text })) };
        return size_f { std::max(text_button_minimum_width, content + text_button_padding * 2.0f) + arrow_width_of(config_), text_button_height };
    }
}
