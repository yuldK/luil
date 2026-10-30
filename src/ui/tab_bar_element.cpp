#include "luil/ui/tab_bar_element.h"

#include "ui/badge_paint.h"
#include "ui/reorder_drop.h"

#include "luil/generated/codicons.h"
#include "luil/ui/button_element.h"
#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace luil {
    namespace {
        // 탭 배지의 글자 크기, 상태 점의 반지름, 이름과의 사이다 (논리 픽셀).
        constexpr float tab_badge_font_size { 10.0f };
        constexpr float tab_badge_dot_radius { 3.5f };
        constexpr float tab_badge_gap { 6.0f };
        // 놓을 틈을 보이는 세로 막대의 두께다 (논리 픽셀).
        constexpr float tab_gap_marker_width { 2.0f };
        // 탭 안쪽 여백, 아이콘 칸과 그 뒤의 자리, 아이콘 글자 크기다 (논리 픽셀).
        constexpr float tab_padding { 10.0f };
        constexpr float tab_icon_slot { 16.0f };
        constexpr float tab_icon_advance { 22.0f };
        constexpr float tab_icon_size { 13.0f };
        // 닫기 단추의 크기와 오른쪽 여백, 그 단추가 이름에서 떼어 가는 자리다 (논리 픽셀).
        constexpr float tab_close_size { 18.0f };
        constexpr float tab_close_inset { 6.0f };
        constexpr float tab_close_reserved { 28.0f };
        // 이어진 탭의 위 틈, 위 모서리, 아래 끝의 바깥 휨이다 (논리 픽셀).
        //  - 휨은 탭 자리 **안**에서 끝난다. 탭은 맞붙어 차례로 그려지므로 자리 밖으로 휘면 다음 탭이 덮는다.
        //  - 휨이 작아 글·단추의 자리는 다른 모양과 같다. 모양이 배치를 흔들지 않는다.
        constexpr float connected_top_gap { 4.0f };
        constexpr float connected_corner { 6.0f };
        constexpr float connected_flare { 4.0f };
        // 이어진 탭의 hover 바탕이 탭 안쪽으로 들어오는 만큼이다 (논리 픽셀).
        constexpr float connected_hover_inset { 2.0f };
        // 선택 강조선의 두께다 (논리 픽셀).
        constexpr float tab_accent_line { 2.0f };
        // 상태 표시의 크기다 (논리 픽셀). 닫기 자리의 것은 단추만 하고, 모서리의 것은 아이콘에 얹혀 작다.
        constexpr float slot_indicator_radius { 4.0f };
        constexpr float slot_busy_radius { 5.5f };
        constexpr float slot_glyph_size { 12.0f };
        constexpr float corner_indicator_radius { 3.0f };
        constexpr float corner_indicator_halo { 1.5f };
        constexpr float corner_glyph_size { 9.0f };
        // 아이콘을 감싸는 진행 호의 반지름이다 (논리 픽셀).
        constexpr float corner_busy_radius { 10.0f };
        constexpr float indicator_stroke { 1.5f };
        // 진행 호가 덮는 각도다. 움직임 줄이기면 돌지 않는 긴 호다 — 짧은 호가 멈춰 있으면 "멈췄다"로 읽힌다.
        constexpr float busy_sweep { 100.0f };
        constexpr float busy_still_sweep { 270.0f };

        // 막대의 탭 키들이다 (그리기 순서). 탭과 막대의 drop 판정이 같은 값을 본다.
        using tab_order = std::shared_ptr<const std::vector<std::u8string>>;

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return scale > 0.0f ? scale : 1.0f;
        }

        // 탭 상자의 어느 반쪽인가다.
        [[nodiscard]] drop_side side_at(const rect_f& box, const float x) noexcept
        {
            return x < box.x + box.width / 2.0f ? drop_side::before : drop_side::after;
        }

        // 이 막대의 탭인가 (자기 자신 포함). 막대 id만 보면 창마다 같은 owner로 지은 막대들이 서로를 자기로 본다 —
        // 키가 이 막대에 있어야 이 막대의 탭이다.
        [[nodiscard]] bool own_tab(const drag_payload& payload, const ui_element_id& container, const std::vector<std::u8string>& order)
        {
            return payload.source.kind == ui_element_kind::tab && payload.container == container && payload.dragged_owner.empty() == false
                && std::find(order.begin(), order.end(), payload.dragged_owner) != order.end();
        }

        // 다른 막대(다른 창)에서 끌어 온 탭인가.
        [[nodiscard]] bool foreign_tab(const drag_payload& payload, const ui_element_id& container, const std::vector<std::u8string>& order)
        {
            return payload.source.kind == ui_element_kind::tab && payload.dragged_owner.empty() == false && payload.files.empty() && own_tab(payload, container, order) == false;
        }

        // 같은 막대 안에서 그 자리에 놓으면 순서가 바뀌는가다. `slot`은 끼울 자리(0 = 맨 앞, 탭 수 = 맨 끝)다.
        //  - 자기 바로 앞 틈과 바로 뒤 틈은 지금 자리 그대로다.
        [[nodiscard]] bool changes_order(const std::vector<std::u8string>& order, const std::u8string_view moved, const std::size_t slot)
        {
            const auto found { std::find(order.begin(), order.end(), moved) };
            if (found == order.end())
                return false;
            const auto from { static_cast<std::size_t>(found - order.begin()) };
            return slot != from && slot != from + 1u;
        }

        // 받는 틈의 세로 막대다. `x`는 막대의 왼쪽 끝이다.
        void draw_gap_marker(draw_context& context, const float x, const rect_f& box)
        {
            const float scale { positive_scale(context.scale) };
            context.canvas.drawRect(SkRect::MakeXYWH(x, box.y, tab_gap_marker_width * scale, box.height), solid_paint(context.palette.accent));
        }

        // 막대 바탕이다. `underline`은 바탕을 칠하지 않으므로(처음부터의 모양) 단추들이 깔던 창 바탕이다.
        [[nodiscard]] ui_color bar_background(const tab_bar_style& style, const ui_color_palette& palette) noexcept
        {
            return style.variant == tab_bar_variant::underline ? palette.window_background : palette_color(palette, style.bar_background);
        }

        // 선택된 탭의 바탕이다.
        [[nodiscard]] ui_color selected_background(const tab_bar_style& style, const ui_color_palette& palette) noexcept
        {
            return style.variant == tab_bar_variant::underline ? palette.surface_background : palette_color(palette, style.selected_background);
        }

        // 선택이 바탕색으로 갈리지 않는가다. 이어지는 모양에서 두 역할이 같은 색이면(고대비는 창 바탕과 표면이 같다)
        // 선택된 탭에 테를 둘러 선택을 남긴다 — 색이 아니라 선으로 읽힌다. 판정은 이름이 아니라 **값**으로 한다.
        [[nodiscard]] bool outlines_selection(const tab_bar_style& style, const ui_color_palette& palette) noexcept
        {
            return style.variant != tab_bar_variant::underline && bar_background(style, palette) == selected_background(style, palette);
        }

        // 이어진 탭의 몸이다. 위 두 모서리는 둥글고, 아래 양 끝은 바깥으로 휘어 막대 바닥선에 녹아든다.
        [[nodiscard]] SkPath connected_shape(const rect_f& box, const float scale, const bool closed)
        {
            const float left { box.x };
            const float right { box.x + box.width };
            const float top { box.y + connected_top_gap * scale };
            const float bottom { box.y + box.height };
            const float flare { connected_flare * scale };
            // 아주 좁은 탭에서 모서리가 음수가 되지 않게 한다.
            const float corner { std::max(0.0f, std::min(connected_corner * scale, (box.width - 2.0f * flare) / 2.0f)) };
            SkPathBuilder path {};
            path.moveTo(left, bottom);
            path.quadTo(left + flare, bottom, left + flare, bottom - flare);
            path.lineTo(left + flare, top + corner);
            path.quadTo(left + flare, top, left + flare + corner, top);
            path.lineTo(right - flare - corner, top);
            path.quadTo(right - flare, top, right - flare, top + corner);
            path.lineTo(right - flare, bottom - flare);
            path.quadTo(right - flare, bottom, right, bottom);
            if (closed)
                path.close();
            return path.detach();
        }

        // 이어진 탭의 hover 바탕이다 — 탭 안쪽으로 들여 그린 둥근 판이다.
        [[nodiscard]] SkRRect connected_hover_shape(const rect_f& box, const float scale)
        {
            const float inset { (connected_flare + connected_hover_inset) * scale };
            const float top { box.y + (connected_top_gap + connected_hover_inset) * scale };
            const float bottom { box.y + box.height - (connected_hover_inset + 1.0f) * scale };
            const SkRect area { SkRect::MakeLTRB(box.x + inset, top, std::max(box.x + inset, box.x + box.width - inset), std::max(top, bottom)) };
            return SkRRect::MakeRectXY(area, connected_corner * scale, connected_corner * scale);
        }

        // 막대 바닥의 구분선이다. 내용 영역과의 구분은 앱이 아니라 막대가 갖는다.
        void draw_bottom_divider(draw_context& context, const rect_f& box)
        {
            const float scale { positive_scale(context.scale) };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), solid_paint(context.palette.divider));
        }

        // 닫기 글리프다. 살아 있는 막대는 단추 element가 같은 것을 그린다 (`button_element`의 도구 막대 역할) —
        // 견본 막대에는 단추가 없어 같은 그림을 여기서 그린다.
        void draw_close_glyph(draw_context& context, const rect_f& box, const bool hovered, const bool pressed)
        {
            const float scale { positive_scale(context.scale) };
            if (hovered || pressed)
            {
                const float radius { 3.0f * scale };
                const ui_color fill { pressed ? context.palette.button_pressed_background : context.palette.button_hover_background };
                context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), solid_paint(fill));
            }
            if (context.codicon_typeface == nullptr)
                return;
            const SkFont font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
            const SkPaint paint { solid_paint(hovered || pressed ? context.palette.button_hover_foreground : context.palette.primary_foreground) };
            draw_centered_glyph(context.canvas, codicons::icon_close, box, font, paint);
        }

        // 도는 호다. 각도는 `now`만의 함수라 어느 frame에 그려도 위상이 같다 (`spinner_angle`).
        void draw_busy_arc(draw_context& context, const float center_x, const float center_y, const float radius, const ui_color color)
        {
            const float scale { positive_scale(context.scale) };
            SkPaint stroke { solid_paint(color) };
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(indicator_stroke * scale);
            stroke.setStrokeCap(SkPaint::kRound_Cap);
            const SkRect oval { SkRect::MakeLTRB(center_x - radius, center_y - radius, center_x + radius, center_y + radius) };
            if (context.reduce_motion)
                context.canvas.drawArc(oval, -90.0f, busy_still_sweep, false, stroke);
            else
                context.canvas.drawArc(oval, spinner_angle(context.now) - 90.0f, busy_sweep, false, stroke);
        }

        // 상태 표시 하나를 그 중심에 그린다. `radius`는 점·고리·호의 반지름, `glyph_size`는 글리프의 크기다 (물리 픽셀).
        void draw_indicator_mark(draw_context& context, const tab_indicator& indicator, const float center_x, const float center_y, const float radius, const float glyph_size)
        {
            const float scale { positive_scale(context.scale) };
            const ui_color color { badge_tone_color(context.palette, indicator.tone) };
            switch (indicator.kind)
            {
            case tab_indicator_kind::dot:
                draw_badge_dot(context, center_x, center_y, radius, indicator.tone);
                return;
            case tab_indicator_kind::ring: {
                SkPaint stroke { solid_paint(color) };
                stroke.setStyle(SkPaint::kStroke_Style);
                stroke.setStrokeWidth(indicator_stroke * scale);
                context.canvas.drawCircle(center_x, center_y, std::max(radius - indicator_stroke * scale / 2.0f, 0.0f), stroke);
                return;
            }
            case tab_indicator_kind::busy:
                draw_busy_arc(context, center_x, center_y, radius, color);
                return;
            case tab_indicator_kind::glyph:
                if (context.codicon_typeface == nullptr || indicator.glyph == 0)
                    return;
                draw_centered_glyph(context.canvas, indicator.glyph, { center_x - glyph_size / 2.0f, center_y - glyph_size / 2.0f, glyph_size, glyph_size },
                    SkFont { sk_ref_sp(context.codicon_typeface), glyph_size }, solid_paint(color));
                return;
            }
        }

        // 견본 막대가 고정해 그리는 상태다 (`tab_bar_preview`의 키를 탭 하나로 푼 것).
        struct pinned_state
        {
            bool hovered { false };
            bool pressed { false };
            bool focused { false };
            bool close_hovered { false };
        };

        // 탭 하나다 (짓는 데 필요한 막대 쪽 사실들).
        struct tab_place
        {
            std::size_t index { 0 };
            bool selected { false };
            // 바로 뒤 탭이 선택됐는가와 마지막 탭인가다 — 칸막이는 탭의 오른쪽 끝에 서므로 어디에 그을지가 뒤 이웃에 달렸다.
            bool next_selected { false };
            bool last { false };
        };

        // 탭 한 개다.
        // 누르면 선택 메시지를 내고, 끌어 놓으면 순서 변경 메시지를 낸다.
        class tab_element final : public ui_element
        {
        public:
            tab_element(tab_item item, const tab_place place, const tab_bar_config& bar, const ui_element_id& container, tab_order order)
                : ui_element { bar.preview.has_value() ? ui_element_id { ui_element_kind::tab_sample, bar.owner + u8"/" + item.key } : ui_element_id { ui_element_kind::tab, item.key } }
                , item_ { std::move(item) }
                , place_ { place }
                , style_override_ { bar.style }
                , gaps_ { bar.preview.has_value() == false && (bar.move != nullptr || bar.receive != nullptr) }
            {
                // 견본은 그림만 남는다. 동작·끌기·놓기·닫기 단추를 하나도 세우지 않고 접근 tree에서도 접힌다.
                if (bar.preview.has_value())
                {
                    const tab_bar_preview& preview { *bar.preview };
                    pinned_ = pinned_state {
                        .hovered = preview.hovered == item_.key,
                        .pressed = preview.pressed == item_.key,
                        .focused = preview.focused == item_.key,
                        .close_hovered = preview.close_hovered == item_.key,
                    };
                    preview_closable_ = item_.closable;
                    return;
                }

                const tab_message_factory& select { bar.select };
                const tab_message_factory& close { bar.close };
                const tab_reorder_message_factory& reorder { bar.reorder };
                const tab_context_message_factory& context_menu { bar.context_menu };
                const std::size_t index { place.index };
                // 묶음 안에서 글자로 찾을 때 보는 글이다 (탭에 적힌 이름과 같은 것).
                set_search_label(item_.label);
                if (select != nullptr)
                {
                    const std::u8string key { item_.key };
                    set_action(ui_trigger::left_click, [key, &select](const ui_action_context&) -> std::vector<input_action> { return { select(key) }; });
                }

                // 닫기 버튼도 같은 동작을 받으므로 그 버튼을 만들기 전에 한 번 짓는다.
                ui_action open_menu {};
                if (context_menu != nullptr)
                {
                    const std::u8string key { item_.key };
                    open_menu = [key, &context_menu](const ui_action_context& context) -> std::vector<input_action> { return { context_menu(key, context) }; };
                    set_action(ui_trigger::right_click, open_menu);
                }

                // 가운데 버튼은 닫기다. 선택은 바뀌지 않는다 — 선택 메시지를 함께 내지 않는다.
                ui_action close_now {};
                if (close != nullptr && item_.closable && bar.middle_click_close)
                {
                    const std::u8string key { item_.key };
                    close_now = [key, &close](const ui_action_context&) -> std::vector<input_action> { return { close(key) }; };
                    set_action(ui_trigger::middle_click, close_now);
                }

                if (reorder != nullptr || bar.move != nullptr || bar.unclaimed_drop != nullptr)
                {
                    // 잡은 지점과 탭 원점의 차이는 배치가 정하므로 잡는 순간의 bounds에서 읽는다.
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    std::u8string label { item_.label };
                    // 이 탭이 속한 막대다. 놓는 쪽이 같은 막대인지를 이것으로 가른다.
                    drag_source source {};
                    source.make_payload = [self, key, label, container, outside_cursor = bar.outside_cursor](const ui_action_context& context) {
                        drag_payload payload {};
                        payload.source = context.element;
                        payload.container = container;
                        payload.dragged_owner = key;
                        payload.label = label;
                        payload.grab_offset_x = context.x - self->bounds().x;
                        payload.grab_offset_y = context.y - self->bounds().y;
                        payload.outside_cursor = outside_cursor;
                        return payload;
                    };
                    // 받는 대상 없이 놓였다 — 창 밖에 놓아 새 창으로 떼어 낼 자리다. 무엇을 할지는 앱이 정한다.
                    //  - **factory를 값으로 쥔다.** controller는 이 콜백을 끌기가 시작될 때 쥐어 두고 끝날 때 부른다. 그 사이에
                    //    새 frame이 오면 이 막대(와 `bar`)가 든 옛 tree는 풀리므로, 참조로 쥐면 놓는 순간 풀린 자리를 부른다.
                    if (bar.unclaimed_drop != nullptr)
                        source.on_unclaimed_drop
                            = [unclaimed = bar.unclaimed_drop](const drag_payload& payload, const drag_release& release) -> std::vector<input_action> { return { unclaimed(payload, release) }; };
                    set_drag_source(std::move(source));
                }

                if (gaps_)
                {
                    // 앞·뒤 반쪽으로 끼울 틈을 가른다. 같은 막대의 탭은 순서가 바뀌는 틈만, 다른 막대의 탭은 어느 틈이든
                    // 받는다 (bar.move·bar.receive 중 선 것만).
                    //  - `move` 없이 `reorder`와 `receive`만 단 막대는 같은 막대 안을 지금처럼 `reorder`로 옮긴다 (앞·뒤를
                    //    싣지 않는 예전 메시지라 자기 자신만 거절한다).
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    const bool moving { bar.move != nullptr };
                    const bool reordering { reorder != nullptr };
                    const bool receiving { bar.receive != nullptr };
                    drop_target drop {};
                    drop.accepts = [container, order, moving, reordering, receiving](const drag_payload& payload) {
                        return ((moving || reordering) && own_tab(payload, container, *order)) || (receiving && foreign_tab(payload, container, *order));
                    };
                    drop.accepts_at = [self, key, container, order, index, moving](const drag_payload& payload, const float x, const float) {
                        if (own_tab(payload, container, *order) == false)
                            return true;
                        if (moving == false)
                            return payload.dragged_owner != key;
                        const std::size_t slot { index + (side_at(self->bounds(), x) == drop_side::after ? 1u : 0u) };
                        return changes_order(*order, payload.dragged_owner, slot);
                    };
                    drop.on_drop = [self, key, container, order, &move = bar.move, &reorder, &receive = bar.receive](
                                       const drag_payload& payload, const ui_action_context& context) -> std::vector<input_action> {
                        const drop_side side { side_at(self->bounds(), context.x) };
                        if (own_tab(payload, container, *order) == false)
                            return { receive(payload, key, side) };
                        if (move != nullptr)
                            return { move(payload.dragged_owner, key, side) };
                        return { reorder(payload.dragged_owner, key) };
                    };
                    // 테두리 대신 틈의 세로 막대를 스스로 그린다.
                    drop.custom_highlight = true;
                    set_drop_target(std::move(drop));
                }
                else if (reorder != nullptr)
                {
                    const std::u8string key { item_.key };
                    drop_target drop {};
                    // 같은 막대의 탭만 받는다. 자기 자리에 놓는 것은 이동이 아니므로 받지
                    // 않는다 (강조도 뜨지 않는다) — 규칙은 목록과 한 벌이다 (reorder_drop.h).
                    drop.accepts = [key, container](const drag_payload& payload) { return accepts_reorder_drop(payload, ui_element_kind::tab, container, key); };
                    drop.on_drop = [key, &reorder](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> { return { reorder(payload.dragged_owner, key) }; };
                    set_drop_target(std::move(drop));
                }

                if (item_.closable == false || close == nullptr)
                    return;

                const std::u8string key { item_.key };
                const button_config close_button {
                    .glyph = codicons::icon_close,
                    .icon_size = 11.0f,
                    .corner_radius = 3.0f,
                };
                auto button { std::make_unique<button_element>(ui_element_id { ui_element_kind::tab_close, key }, close_button) };
                // 항목에 딸린 보조 버튼이다. 묶음 안에 두면 ←/→가 탭과 닫기를
                // 번갈아 지난다 — 키보드로는 넘침 메뉴가 그 자리다
                // (focus-group-design.md).
                button->set_tab_stop(false);
                button->set_cursor(ui_cursor::hand);
                button->set_action(ui_trigger::left_click, [key, &close](const ui_action_context&) -> std::vector<input_action> { return { close(key) }; });
                // 우클릭은 hit된 element의 것만 나가고 부모로 올라가지 않는다.
                // 버튼 위의 우클릭도 그 탭의 메뉴가 되도록 같은 동작을 단다. 가운데 버튼도 같다.
                if (open_menu != nullptr)
                    button->set_action(ui_trigger::right_click, std::move(open_menu));
                if (close_now != nullptr)
                    button->set_action(ui_trigger::middle_click, std::move(close_now));
                close_ = button.get();
                add_child(std::move(button));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                style_ = style_override_.has_value() ? *style_override_ : context.app_style().tab_bar;
                if (close_ == nullptr)
                    return;
                // 숨을 수 있는 단추는 포인터가 올라가야 보인다. 터치에는 올림이 없어 손가락이 보이지 않는 단추를 누르게
                // 되므로, 그 자리의 탭은 탭 자신의 것이다 (`ui_element::hover_only`).
                close_->set_hover_only(close_hides());
                close_->arrange(context.for_child(close_box(context.slot, positive_scale(context.scale))));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { positive_scale(context.scale) };
                const rect_f box { bounds() };
                const bool previewing { pinned_.has_value() };
                const bool hovered { previewing ? pinned_->hovered : interaction.hovered == id() };
                const bool pressed { previewing ? pinned_->pressed : interaction.pressed == id() };
                const bool close_hovered { previewing ? pinned_->close_hovered : close_ != nullptr && interaction.hovered == close_->id() };
                const bool close_pressed { previewing == false && close_ != nullptr && interaction.pressed == close_->id() };
                const bool close_focused { previewing == false && close_ != nullptr && interaction.focused == close_->id() };
                const bool dragged { previewing == false && interaction.drag.has_value() && interaction.drag->payload.dragged_owner == item_.key };
                const bool highlighting { previewing == false && interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false };
                const bool drop_here { highlighting && interaction.drag->hovered_drop_target == id() };

                // 이어지는 모양은 바탕이 없는 탭도 있어 바탕만 흐리면 끌리는 탭이 보이지 않는다 — 탭 전체를 흐린다.
                //  - `underline`은 처음부터 바탕만 흐렸다. 그 그림을 그대로 둔다.
                SkAutoCanvasRestore restore { &context.canvas, true };
                if (dragged && style_.variant != tab_bar_variant::underline)
                    context.canvas.saveLayerAlphaf(nullptr, 0.35f);

                draw_background(context, box, hovered, pressed, dragged, drop_here);
                draw_content(context, box, hovered || close_hovered, close_hovered, close_pressed, close_focused, interaction);

                if (previewing && pinned_->focused)
                    draw_pinned_focus(context, box);
                if (drop_here && gaps_)
                {
                    const float marker { tab_gap_marker_width * scale };
                    draw_gap_marker(context, side_at(box, interaction.drag->x) == drop_side::before ? box.x : box.x + box.width - marker, box);
                }
            }

            [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot&) const override
            {
                // 도는 그림이 있을 때만 다음 frame을 청한다. 움직임 줄이기면 멈춘 그림이라 청하지 않는다.
                const bool busy { item_.indicator.has_value() && item_.indicator->kind == tab_indicator_kind::busy };
                const bool spinning_badge { item_.badge.has_value() && item_.badge->spinning && item_.badge->glyph != 0 && badge_element::shows_glyph(item_.badge->tone) == false };
                if ((busy || spinning_badge) == false || context.reduce_motion || visible() == false || bounds().width <= 0.0f || bounds().height <= 0.0f)
                    return std::nullopt;
                return context.now + std::chrono::milliseconds { 16 };
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 견본의 탭은 구조로 접힌다 — 막대가 그림 하나로 선다.
                if (pinned_.has_value())
                    return {};
                return { .role = access_role::tab, .name = access_label(), .selected = place_.selected };
            }

        private:
            // 닫기 단추가 그려지지 않을 수 있는가다 (배치에서 정한 스타일로).
            [[nodiscard]] bool close_hides() const noexcept
            {
                if (indicator_in_close_slot())
                    return true;
                return style_.close_visibility == tab_close_visibility::selected_or_hovered && place_.selected == false;
            }

            [[nodiscard]] bool indicator_in_close_slot() const noexcept
            {
                return item_.indicator.has_value() && style_.indicator_placement == tab_indicator_placement::close_slot;
            }

            // 닫기 단추 자리가 있는가다 — 단추가 숨어도, 단추 대신 상태 표시가 서도 자리는 같다.
            [[nodiscard]] bool has_close_slot() const noexcept
            {
                return close_ != nullptr || preview_closable_ || indicator_in_close_slot();
            }

            [[nodiscard]] static rect_f close_box(const rect_f& slot, const float scale) noexcept
            {
                const float size { tab_close_size * scale };
                const float inset { tab_close_inset * scale };
                return { slot.x + slot.width - inset - size, slot.y + (slot.height - size) / 2.0f, size, size };
            }

            void draw_background(draw_context& context, const rect_f& box, const bool hovered, const bool pressed, const bool dragged, const bool drop_here) const
            {
                const float scale { positive_scale(context.scale) };
                const ui_color_palette& palette { context.palette };
                const bool selected { place_.selected };
                switch (style_.variant)
                {
                case tab_bar_variant::underline: {
                    ui_color background { 0 };
                    bool fill { false };
                    // 앞·뒤로 끼우는 막대는 바탕 대신 틈의 막대로 받는 자리를 보인다 (그림은 맨 위에 얹는다).
                    if (drop_here && gaps_ == false)
                    {
                        background = palette.drop_target_background;
                        fill = true;
                    }
                    else if (selected)
                    {
                        background = palette.surface_background;
                        fill = true;
                    }
                    else if (pressed)
                    {
                        background = palette.button_pressed_background;
                        fill = true;
                    }
                    else if (hovered)
                    {
                        background = palette.button_hover_background;
                        fill = true;
                    }
                    if (fill)
                    {
                        SkPaint paint { solid_paint(background) };
                        if (dragged)
                            paint.setAlphaf(0.35f);
                        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), paint);
                    }
                    // 선택된 탭은 아래 가장자리의 강조선으로 표시한다.
                    if (selected)
                    {
                        const float line { tab_accent_line * scale };
                        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - line, box.width, line), solid_paint(palette.accent));
                    }
                    return;
                }
                case tab_bar_variant::connected: {
                    if (drop_here && gaps_ == false)
                        context.canvas.drawRRect(connected_hover_shape(box, scale), solid_paint(palette.drop_target_background));
                    else if (selected)
                    {
                        // 막대 바닥선 위까지 덮어 그 줄이 선택된 탭 밑에서 끊긴다.
                        context.canvas.drawPath(connected_shape(box, scale, true), solid_paint(selected_background(style_, palette)));
                        if (outlines_selection(style_, palette))
                        {
                            SkPaint outline { solid_paint(palette.divider) };
                            outline.setStyle(SkPaint::kStroke_Style);
                            outline.setStrokeWidth(scale);
                            outline.setAntiAlias(true);
                            // 획이 몸 안쪽에 온전히 들도록 양옆과 위를 반 픽셀 들인다. 아래는 막대 바닥선과 이어진다.
                            const float half { scale / 2.0f };
                            const rect_f inner { box.x + half, box.y + half, box.width - scale, box.height - half };
                            context.canvas.drawPath(connected_shape(inner, scale, false), outline);
                        }
                    }
                    else if (pressed || hovered)
                        context.canvas.drawRRect(connected_hover_shape(box, scale), solid_paint(pressed ? palette.button_pressed_background : palette.button_hover_background));

                    // 고르지 않은 탭 사이의 짧은 칸막이다. 선택된 탭에 맞닿은 칸막이는 숨긴다 — 선택된 탭이 한 판으로 이어진다.
                    if (selected == false && place_.next_selected == false && place_.last == false)
                    {
                        const float height { box.height * 0.5f };
                        context.canvas.drawRect(
                            SkRect::MakeXYWH(box.x + box.width - scale, box.y + (box.height - height) / 2.0f + connected_top_gap * scale / 2.0f, scale, height), solid_paint(palette.divider));
                    }
                    return;
                }
                case tab_bar_variant::flat: {
                    const SkRect cell { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                    if (drop_here && gaps_ == false)
                        context.canvas.drawRect(cell, solid_paint(palette.drop_target_background));
                    else if (selected)
                    {
                        // 내용 바탕으로 칠해 막대 바닥선 위까지 덮는다 — 칸이 아래로 열린다.
                        context.canvas.drawRect(cell, solid_paint(selected_background(style_, palette)));
                        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, tab_accent_line * scale), solid_paint(palette.accent));
                        if (outlines_selection(style_, palette))
                        {
                            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, scale, box.height), solid_paint(palette.divider));
                            context.canvas.drawRect(SkRect::MakeXYWH(box.x + box.width - scale, box.y, scale, box.height), solid_paint(palette.divider));
                        }
                    }
                    else if (pressed || hovered)
                        context.canvas.drawRect(cell, solid_paint(pressed ? palette.button_pressed_background : palette.button_hover_background));

                    // 칸을 맞붙이는 세로 칸막이다. 마지막 칸 뒤는 막대의 빈 자리라 긋지 않는다.
                    if (place_.last == false)
                        context.canvas.drawRect(SkRect::MakeXYWH(box.x + box.width - scale, box.y, scale, box.height - scale), solid_paint(palette.divider));
                    return;
                }
                }
            }

            // 탭 자리의 바탕색이다 — 아이콘 모서리 표시의 테가 이 색이라 아이콘과 떨어져 보인다.
            //  - hover 바탕은 반투명이라 그 위에 한 번 더 얹어 같은 색을 만든다 (`draw_corner_halo`).
            [[nodiscard]] ui_color base_background(const ui_color_palette& palette) const noexcept
            {
                return place_.selected ? selected_background(style_, palette) : bar_background(style_, palette);
            }

            void draw_content(draw_context& context, const rect_f& box, const bool pointer_over, const bool close_hovered, const bool close_pressed, const bool close_focused,
                const interaction_snapshot& interaction) const
            {
                const float scale { positive_scale(context.scale) };
                const bool selected { place_.selected };
                float text_left { box.x + tab_padding * scale };

                // 아이콘 모서리 자리다. 아이콘이 없으면 표시가 아이콘 칸에 선다.
                const bool corner_indicator { item_.indicator.has_value() && style_.indicator_placement == tab_indicator_placement::icon_corner };
                if (item_.icon != 0 || corner_indicator)
                {
                    const rect_f icon_box { text_left, box.y, tab_icon_slot * scale, box.height };
                    if (item_.icon != 0 && context.codicon_typeface != nullptr)
                    {
                        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), tab_icon_size * scale };
                        const SkPaint dim { solid_paint(context.palette.secondary_foreground) };
                        draw_centered_glyph(context.canvas, item_.icon, icon_box, glyph_font, dim);
                    }
                    if (corner_indicator)
                        draw_corner_indicator(context, icon_box, pointer_over);
                    text_left += tab_icon_advance * scale;
                }

                // 닫기 단추 자리와 겹치지 않게 오른쪽을 비운다. 단추가 숨어도 자리는 지킨다 — 이름이 들썩이지 않는다.
                const float reserved { (has_close_slot() ? tab_close_reserved : tab_padding) * scale };
                float text_right { box.x + box.width - reserved };
                // 배지가 이름보다 먼저 자리를 갖는다 — 잘린 개수는 틀린 개수로 읽힌다.
                if (item_.badge.has_value() && text_right > text_left)
                    text_right = draw_badge(context, text_left, text_right, box, scale) - tab_badge_gap * scale;
                const float text_width { text_right - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                    SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                    if (selected == false)
                        foreground.setAlphaf(0.75f);
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }

                // 닫기 단추 자리다. 상태 표시가 그 자리에 있으면 평소에는 표시가 서고, 단추 위에 포인터가 오거나 단추에 초점이
                // 가면 단추로 바뀐다. 표시가 없으면 단추는 보이기 정책을 따른다.
                const bool has_button { close_ != nullptr || preview_closable_ };
                bool show_button { false };
                if (has_button)
                {
                    if (indicator_in_close_slot())
                        show_button = close_hovered || close_pressed || close_focused;
                    else
                        show_button = style_.close_visibility == tab_close_visibility::always || selected || pointer_over || close_focused || close_pressed;
                }
                if (show_button)
                {
                    if (close_ != nullptr)
                        draw_children(context, interaction);
                    else
                        draw_close_glyph(context, close_box(box, scale), close_hovered, false);
                }
                else if (indicator_in_close_slot())
                {
                    const rect_f slot { close_box(box, scale) };
                    const float radius { (item_.indicator->kind == tab_indicator_kind::busy ? slot_busy_radius : slot_indicator_radius) * scale };
                    draw_indicator_mark(context, *item_.indicator, slot.x + slot.width / 2.0f, slot.y + slot.height / 2.0f, radius, slot_glyph_size * scale);
                }
            }

            // 아이콘 모서리의 표시다. 오른쪽 아래에 작은 점을 찍고 탭 바탕색 테를 둘러 아이콘과 떨어져 보이게 한다.
            // 진행 중이면 아이콘을 감싸는 호다. 아이콘이 없으면 표시가 아이콘 칸 가운데에 그대로 선다.
            void draw_corner_indicator(draw_context& context, const rect_f& icon_box, const bool pointer_over) const
            {
                const float scale { positive_scale(context.scale) };
                const tab_indicator& indicator { *item_.indicator };
                const float center_x { icon_box.x + icon_box.width / 2.0f };
                const float center_y { icon_box.y + icon_box.height / 2.0f };
                if (item_.icon == 0)
                {
                    const float radius { (indicator.kind == tab_indicator_kind::busy ? slot_busy_radius : slot_indicator_radius) * scale };
                    draw_indicator_mark(context, indicator, center_x, center_y, radius, slot_glyph_size * scale);
                    return;
                }
                if (indicator.kind == tab_indicator_kind::busy)
                {
                    draw_busy_arc(context, center_x, center_y, corner_busy_radius * scale, badge_tone_color(context.palette, indicator.tone));
                    return;
                }
                const float radius { (indicator.kind == tab_indicator_kind::glyph ? corner_glyph_size / 2.0f : corner_indicator_radius) * scale };
                const float mark_x { icon_box.x + icon_box.width - radius };
                const float mark_y { center_y + tab_icon_size * scale / 2.0f - radius };
                // 테는 탭 바탕과 같은 색이다. hover 바탕은 반투명이라 같은 겹으로 한 번 더 얹는다.
                const float halo { radius + corner_indicator_halo * scale };
                context.canvas.drawCircle(mark_x, mark_y, halo, solid_paint(base_background(context.palette)));
                if (pointer_over && place_.selected == false)
                    context.canvas.drawCircle(mark_x, mark_y, halo, solid_paint(context.palette.button_hover_background));
                draw_indicator_mark(context, indicator, mark_x, mark_y, radius, corner_glyph_size * scale);
            }

            // 견본이 고정한 초점 테다. 살아 있는 탭은 tree가 같은 테를 얹는다 (`ui_tree::draw_focus_ring`과 같은 치수).
            static void draw_pinned_focus(draw_context& context, const rect_f& box)
            {
                const float scale { positive_scale(context.scale) };
                const float stroke { 2.0f * scale };
                const float outset { 1.0f * scale + stroke / 2.0f };
                SkPaint ring { solid_paint(context.palette.accent) };
                ring.setStyle(SkPaint::kStroke_Style);
                ring.setStrokeWidth(stroke);
                // 견본은 막대가 잘라 내므로 테를 탭 안쪽으로 접는다 — 밖에 두르면 위아래가 잘린다.
                const SkRect body { SkRect::MakeXYWH(box.x + outset, box.y + outset, box.width - outset * 2.0f, box.height - outset * 2.0f) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, 5.0f * scale, 5.0f * scale), ring);
            }

            // 보조 기술이 읽는 이름이다. 배지와 상태 표시의 말이 이름 뒤에 붙는다 — 개수나 상태가 바뀌면 이름이 바뀌어
            // 보조 기술이 그 변화를 듣는다.
            [[nodiscard]] std::u8string access_label() const
            {
                std::u8string label { item_.label };
                if (item_.badge.has_value())
                {
                    const std::u8string& spoken { item_.badge->description.empty() ? item_.badge->text : item_.badge->description };
                    if (spoken.empty() == false)
                        label += u8", " + spoken;
                }
                if (item_.indicator.has_value() && item_.indicator->description.empty() == false)
                    label += u8", " + item_.indicator->description;
                return label;
            }

            // 배지를 `right`에 붙여 그리고, 그 왼쪽 끝을 답한다 (물리 픽셀).
            //  - `left`보다 왼쪽으로는 나가지 않는다. 그만큼도 없으면 배지가 잘린다.
            [[nodiscard]] float draw_badge(draw_context& context, const float left, const float right, const rect_f& box, const float scale) const
            {
                const tab_badge& badge { *item_.badge };
                const char32_t glyph { badge_element::shows_glyph(badge.tone) ? char32_t { 0 } : badge.glyph };
                if (badge.text.empty() && glyph == 0 && badge_element::shows_glyph(badge.tone) == false)
                {
                    const float radius { tab_badge_dot_radius * scale };
                    draw_badge_dot(context, right - radius, box.y + box.height / 2.0f, radius, badge.tone);
                    return right - 2.0f * radius;
                }
                const float height { badge_element::height_for({ .font_size = tab_badge_font_size }) * scale };
                const float width { std::min(badge_pill_width(context, badge.text, badge.tone, tab_badge_font_size, glyph), right - left) };
                draw_badge_pill(context, { right - width, box.y + (box.height - height) / 2.0f, width, height }, badge.text, badge.tone, tab_badge_font_size, glyph, badge.spinning);
                return right - width;
            }

            tab_item item_ {};
            tab_place place_ {};
            std::optional<tab_bar_style> style_override_ {};
            // 배치가 고른 스타일이다. 그리기는 이것을 본다 — 한 tree가 한 스타일로 자리를 잡고 그린다.
            tab_bar_style style_ {};
            // 앞·뒤 틈으로 받는다 (`tab_bar_config::move`·`receive`).
            bool gaps_ { false };
            // 견본이면 고정한 상태다.
            std::optional<pinned_state> pinned_ {};
            // 견본의 닫을 수 있는 탭이다 (단추 element 없이 그림만 그린다).
            bool preview_closable_ { false };
            ui_element* close_ { nullptr };
        };

        // 탭을 같은 폭으로 늘어놓는 레인이다. 폭은 막대가 배치 직전에 정한다 (`min_tab_width`로 줄 수 있다).
        class tab_lane_element final : public ui_element
        {
        public:
            explicit tab_lane_element(std::u8string owner)
                : ui_element { ui_element_id { ui_element_kind::tab_lane, std::move(owner) } }
            {
            }

            void add(std::unique_ptr<ui_element> tab)
            {
                add_child(std::move(tab));
            }

            void set_tab_width(const float width) noexcept
            {
                tab_width_ = width;
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float width { tab_width_ * positive_scale(context.scale) };
                float x { context.slot.x };
                for (const std::unique_ptr<ui_element>& child : children())
                {
                    child->arrange(context.for_child({ x, context.slot.y, width, context.slot.height }));
                    x += width;
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

        private:
            float tab_width_ { 0.0f };
        };

        // 막대 끝의 단추(넘침 메뉴·앞·뒤)가 함께 쓰는 바탕이다.
        // 아래로 지나가는 탭을 가리도록 막대 바탕으로 불투명하게 칠하고, 막대 바닥의 경계선을 그 밑에서도 잇는다.
        void draw_edge_button(draw_context& context, const ui_element& button, const tab_bar_style& style, const interaction_snapshot& interaction, const char32_t glyph)
        {
            const float scale { positive_scale(context.scale) };
            const rect_f box { button.bounds() };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(bar_background(style, context.palette)));
            const bool enabled { button.enabled() };
            if (enabled && (interaction.pressed == button.id() || interaction.hovered == button.id()))
            {
                const ui_color overlay { interaction.pressed == button.id() ? context.palette.button_pressed_background : context.palette.button_hover_background };
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(overlay));
            }
            draw_bottom_divider(context, box);
            if (context.codicon_typeface == nullptr)
                return;
            const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
            // 끝에 닿은 쪽 단추는 비활성 글자색이다 — 더 넘길 것이 없다.
            const SkPaint paint { solid_paint(enabled ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
            draw_centered_glyph(context.canvas, glyph, box, glyph_font, paint);
        }

        // 넘침 메뉴 버튼이다.
        class tab_overflow_element final : public ui_element
        {
        public:
            tab_overflow_element(std::u8string owner, const ui_action& action, std::u8string tooltip, const tab_bar_style* style)
                : ui_element { ui_element_id { ui_element_kind::tab_overflow, std::move(owner) } }
                , style_ { style }
            {
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, action);
                // 글리프뿐이라 이름의 원천이 이것뿐이다 — 비면 접근 tree에서 빠진다.
                set_tooltip(std::move(tooltip));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_edge_button(context, *this, *style_, interaction, codicons::icon_ellipsis);
            }

        private:
            // 막대가 배치에서 고른 스타일이다 (막대가 살아 있는 동안 유효하다).
            const tab_bar_style* style_ { nullptr };
        };

        // 한 탭씩 넘기는 앞·뒤 단추다.
        //  - Tab 자리가 아니다. 키보드는 ←/→로 탭을 옮기고 초점 되살리기가 띠를 흘린다 — 이 단추는 포인터의 지름길이다.
        //  - 보조 기술에는 이름(tooltip)이 있을 때 선다 (`overflow_tooltip`과 같은 규약).
        class tab_step_element final : public ui_element
        {
        public:
            tab_step_element(ui_element_id id, ui_action action, std::u8string tooltip, const tab_bar_style* style, const char32_t glyph)
                : ui_element { std::move(id) }
                , style_ { style }
                , glyph_ { glyph }
            {
                set_cursor(ui_cursor::hand);
                set_tab_stop(false);
                set_action(ui_trigger::left_click, std::move(action));
                set_tooltip(std::move(tooltip));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_edge_button(context, *this, *style_, interaction, glyph_);
            }

        private:
            const tab_bar_style* style_ { nullptr };
            char32_t glyph_ { 0 };
        };
    }

    float tab_step_offset(const float offset, const float tab_width, const float viewport, const float maximum, const int steps) noexcept
    {
        const float upper { std::max(0.0f, maximum) };
        float target { std::clamp(offset, 0.0f, upper) };
        if (tab_width <= 0.0f || steps == 0)
            return target;
        // 경계에 반 픽셀 못 미친 값은 경계로 본다 — 실수 오차로 이미 드러난 탭을 다시 넘기지 않는다.
        constexpr float epsilon { 0.5f };
        const int count { steps > 0 ? steps : -steps };
        for (int step = 0; step < count; ++step)
        {
            float next { target };
            if (steps > 0)
            {
                // 오른쪽 끝에 걸쳤거나 그 바로 뒤의 탭이 온전히 드러나는 자리다.
                const float index { std::floor((target + viewport + epsilon) / tab_width) };
                next = std::min(upper, std::max(target, (index + 1.0f) * tab_width - viewport));
            }
            else
            {
                // 왼쪽 끝에 걸쳤거나 그 바로 앞의 탭이 온전히 드러나는 자리다.
                const float index { std::ceil((target - epsilon) / tab_width) - 1.0f };
                next = std::max(0.0f, std::min(target, index * tab_width));
            }
            if (next == target)
                break;
            target = next;
        }
        return target;
    }

    float fitted_tab_width(const std::size_t count, const float tab_width, const float min_tab_width, const float available) noexcept
    {
        if (count == 0 || tab_width <= 0.0f)
            return tab_width;
        const float floor { min_tab_width > 0.0f && min_tab_width < tab_width ? min_tab_width : tab_width };
        return std::clamp(available / static_cast<float>(count), floor, tab_width);
    }

    tab_bar_element::tab_bar_element(tab_bar_config config)
        : ui_element { ui_element_id { ui_element_kind::tab_bar, config.owner } }
        , config_ { std::move(config) }
    {
        const bool previewing { config_.preview.has_value() };
        // 넘친 탭이 막대 밖으로 새지 않고 잘린 탭은 눌리지도 않는다.
        //  - 잘라내기는 띠가 이미 하지만, 막대는 넘침 버튼까지 아우르는 바깥
        //    경계라 자기 몫을 그대로 갖는다.
        set_clip_children(true);
        if (previewing == false)
        {
            // 탭 막대는 Tab에서 한 자리이고 그 안은 ←/→가 돈다.
            // 들어오면 선택된 탭에 선다 (앱 상태를 그대로 옮긴다).
            //  - 탭이 띠·레인 아래로 한 겹 더 들어가도 `focus_order`는 tree를 걸어
            //    모으므로 자리 판정이 바뀌지 않는다.
            set_focus_group(focus_axis::horizontal);
            set_focus_entry(ui_element_id { ui_element_kind::tab, config_.selected });
        }

        // 탭을 늘어놓는 레인이다. 간격도 여백도 없어 탭이 맞붙는다.
        auto lane { std::make_unique<tab_lane_element>(config_.owner) };
        // 탭 키의 순서다. 아래에서 항목을 옮겨 담기 전에 적어 둔다 — 탭과 막대 끝의 drop 판정이 함께 본다.
        std::vector<std::u8string> keys {};
        keys.reserve(config_.items.size());
        std::size_t selected_index { config_.items.size() };
        for (const tab_item& item : config_.items)
        {
            if (item.key == config_.selected && selected_index == config_.items.size())
                selected_index = keys.size();
            keys.push_back(item.key);
        }
        const tab_order order { std::make_shared<const std::vector<std::u8string>>(std::move(keys)) };
        const std::size_t count { config_.items.size() };
        std::size_t index { 0 };
        for (tab_item& item : config_.items)
        {
            const tab_place place {
                .index = index,
                .selected = item.key == config_.selected,
                .next_selected = index + 1 == selected_index,
                .last = index + 1 == count,
            };
            auto tab { std::make_unique<tab_element>(std::move(item), place, config_, id(), order) };
            last_tab_ = tab.get();
            lane->add(std::move(tab));
            ++index;
        }
        lane_ = lane.get();

        // 흘리기·잘라내기·다듬기는 띠의 몫이다.
        // 내용 폭은 담는 쪽이 알려 준다 — 측정 단계가 없다. 줄어든 폭은 배치에서 다시 알린다.
        strip_config strip {};
        strip.content_width = static_cast<float>(count) * config_.tab_width;
        strip.scroll_offset = config_.scroll_offset;
        if (previewing == false)
        {
            strip.scroll = config_.scroll;
            // 휠 한 칸에 탭 하나다. 칸을 탭 경계의 변화량으로 바꾸는 것은 막대다 — 실제 폭과 띠 폭을 아는 것이 막대다.
            if (config_.wheel_steps && config_.scroll != nullptr)
            {
                const tab_bar_element* const self { this };
                strip.step = [self, scroll = config_.scroll](const int steps) { return scroll(self->step_delta(steps)); };
            }
        }
        auto lane_strip { std::make_unique<strip_element>(ui_element_id { ui_element_kind::tab_strip, config_.owner }, strip) };
        lane_strip->set_content(std::move(lane));
        strip_ = lane_strip.get();
        add_child(std::move(lane_strip));

        // 견본에는 단추도 꼬리도 놓을 자리도 없다 — 그림만 남는다.
        if (previewing)
            return;

        // 띠보다 나중에 담아 위에 그려지고 hit도 먼저 가져간다.
        // 넘치는지는 arrange가 정하므로 그때까지 보이지 않는다.
        if (config_.overflow_mode == tab_overflow_mode::step && config_.scroll != nullptr)
        {
            const tab_bar_element* const self { this };
            const auto stepper = [self, scroll = config_.scroll](const int steps) -> ui_action {
                return [self, scroll, steps](const ui_action_context&) -> std::vector<input_action> { return { scroll(self->step_delta(steps)) }; };
            };
            const ui_element_id back_id { ui_element_kind::tab_step_back, config_.owner };
            const ui_element_id forward_id { ui_element_kind::tab_step_forward, config_.owner };
            auto back { std::make_unique<tab_step_element>(back_id, stepper(-1), config_.step_back_tooltip, &style_, codicons::icon_chevron_left) };
            auto forward { std::make_unique<tab_step_element>(forward_id, stepper(1), config_.step_forward_tooltip, &style_, codicons::icon_chevron_right) };
            back->set_visible(false);
            forward->set_visible(false);
            step_back_ = back.get();
            step_forward_ = forward.get();
            add_child(std::move(back));
            add_child(std::move(forward));
        }
        if (config_.overflow != nullptr)
        {
            auto button { std::make_unique<tab_overflow_element>(config_.owner, config_.overflow, config_.overflow_tooltip, &style_) };
            button->set_visible(false);
            overflow_ = button.get();
            add_child(std::move(button));
        }
        if (config_.trailing != nullptr)
        {
            if (std::unique_ptr<ui_element> trailing { config_.trailing() }; trailing != nullptr)
            {
                trailing_ = trailing.get();
                add_child(std::move(trailing));
            }
        }

        // 마지막 탭 뒤의 빈 곳도 끼울 틈이다 (마지막 탭의 뒤). 탭이 없는 막대는 막대 전체가 그 틈이다.
        //  - 탭 위의 자리는 탭이 먼저 답한다. 탭이 거절한 자리(순서가 그대로)는 여기로 내려오지만, 이 대상은 마지막
        //    탭의 오른쪽만 받으므로 그 거절을 뒤집지 않는다.
        if (config_.move != nullptr || config_.receive != nullptr)
        {
            const tab_bar_element* const self { this };
            const ui_element_id container { id() };
            const bool moving { config_.move != nullptr };
            const bool receiving { config_.receive != nullptr };
            drop_target end {};
            end.accepts = [container, order, moving, receiving](
                              const drag_payload& payload) { return (moving && own_tab(payload, container, *order)) || (receiving && foreign_tab(payload, container, *order)); };
            end.accepts_at = [self, container, order](const drag_payload& payload, const float x, const float y) {
                if (self->end_gap_contains(x, y) == false)
                    return false;
                return own_tab(payload, container, *order) == false || changes_order(*order, payload.dragged_owner, order->size());
            };
            end.on_drop = [self, container, order](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> {
                const std::u8string last { order->empty() ? std::u8string {} : order->back() };
                if (own_tab(payload, container, *order))
                    return { self->config_.move(payload.dragged_owner, last, drop_side::after) };
                return { self->config_.receive(payload, last, drop_side::after) };
            };
            end.custom_highlight = true;
            set_drop_target(std::move(end));
        }
    }

    float tab_bar_element::end_gap_x() const noexcept
    {
        return last_tab_ != nullptr ? last_tab_->bounds().x + last_tab_->bounds().width : strip_->bounds().x;
    }

    bool tab_bar_element::end_gap_contains(const float x, const float y) const noexcept
    {
        // 띠 안이어야 한다 — 넘침 버튼 위는 틈이 아니다.
        return strip_->bounds().contains(x, y) && x >= end_gap_x();
    }

    float tab_bar_element::maximum_scroll() const noexcept
    {
        return maximum_scroll_;
    }

    float tab_bar_element::scroll_offset() const noexcept
    {
        return config_.scroll_offset;
    }

    float tab_bar_element::tab_width() const noexcept
    {
        return tab_width_;
    }

    bool tab_bar_element::overflowing() const noexcept
    {
        return overflowing_;
    }

    float tab_bar_element::step_delta(const int steps) const noexcept
    {
        return tab_step_offset(config_.scroll_offset, tab_width_, viewport_, maximum_scroll_, steps) - config_.scroll_offset;
    }

    void tab_bar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { positive_scale(context.scale) };
        style_ = config_.style.has_value() ? *config_.style : context.app_style().tab_bar;
        const std::size_t count { config_.items.size() };
        const float total { context.slot.width };
        const float trailing_width { trailing_ != nullptr ? std::max(0.0f, config_.trailing_width) * scale : 0.0f };

        // 다 들어가지 않으면 먼저 탭을 같은 폭으로 줄이고, 최소 폭으로도 넘칠 때만 넘침이다.
        //  - 판정은 단추 폭을 떼기 **전**의 slot 폭으로 한다. 뗀 뒤에 재면
        //    "버튼이 있어서 넘친다"가 되어 자기참조다.
        const float smallest { fitted_tab_width(count, config_.tab_width, config_.min_tab_width, 0.0f) };
        overflowing_ = static_cast<float>(count) * smallest * scale + trailing_width > total;
        tab_width_ = overflowing_ ? smallest : fitted_tab_width(count, config_.tab_width, config_.min_tab_width, (total - trailing_width) / scale);
        const float lane_width { static_cast<float>(count) * tab_width_ * scale };

        // 넘칠 때만 끝의 단추들이 자리를 가져간다: [앞] 띠 [뒤] [넘침 메뉴] [꼬리].
        float left { context.slot.x };
        float right { context.slot.x + total };
        const float button_width { tab_overflow_width * scale };
        const auto take_right = [&right, &context](ui_element* const element, const float width) {
            right -= width;
            element->arrange(context.for_child({ right, context.slot.y, width, context.slot.height }));
        };
        if (overflowing_ && trailing_ != nullptr)
            take_right(trailing_, trailing_width);
        if (overflow_ != nullptr)
        {
            overflow_->set_visible(overflowing_);
            if (overflowing_)
                take_right(overflow_, button_width);
        }
        const bool stepping { overflowing_ && step_back_ != nullptr && step_forward_ != nullptr };
        if (step_back_ != nullptr && step_forward_ != nullptr)
        {
            step_back_->set_visible(stepping);
            step_forward_->set_visible(stepping);
        }
        float forward_x { right };
        if (stepping)
        {
            right -= button_width;
            forward_x = right;
            left += button_width;
        }

        // 남는 자리를 띠가 통째로 받는다. 흘리기·잘라내기·다듬기는 그 안에서 끝난다.
        //  - 레인이 실제로 좁아지므로 **마지막 탭이 버튼 밑을 지나지 않는다.**
        static_cast<tab_lane_element*>(lane_)->set_tab_width(tab_width_);
        strip_->set_content_width(static_cast<float>(count) * tab_width_);
        strip_->arrange(context.for_child({ left, context.slot.y, std::max(0.0f, right - left), context.slot.height }));
        viewport_ = std::max(0.0f, right - left) / scale;
        // 다듬은 값을 그대로 물려받아 앱이 같은 값을 상태에 되돌릴 수 있게 한다.
        maximum_scroll_ = strip_->maximum_scroll();
        config_.scroll_offset = strip_->scroll_offset();

        // 끝에 닿은 쪽 단추는 비활성이다. 흘린 값은 띠가 다듬은 뒤라야 안다.
        if (stepping)
        {
            step_back_->set_enabled(config_.scroll_offset > 0.0f);
            step_forward_->set_enabled(config_.scroll_offset < maximum_scroll_);
            step_back_->arrange(context.for_child({ context.slot.x, context.slot.y, button_width, context.slot.height }));
            step_forward_->arrange(context.for_child({ forward_x, context.slot.y, button_width, context.slot.height }));
        }

        // 넘치지 않으면 꼬리는 마지막 탭 바로 뒤에 선다 (막대 끝을 넘지 않는다).
        if (overflowing_ == false && trailing_ != nullptr)
        {
            const float x { context.slot.x + std::min(lane_width, std::max(0.0f, total - trailing_width)) };
            trailing_->arrange(context.for_child({ x, context.slot.y, trailing_width, context.slot.height }));
        }
    }

    void tab_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        // 이어지는 모양은 막대 바탕을 칠한다 — 선택된 탭이 다른 바탕으로 내용과 이어져야 한다.
        if (style_.variant != tab_bar_variant::underline)
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(bar_background(style_, context.palette)));
        // 막대 바닥의 경계선이다. 이어지는 모양의 선택된 탭은 그 위를 덮어 줄이 그 밑에서 끊긴다.
        draw_bottom_divider(context, box);
        draw_children(context, interaction);
        // 마지막 탭 뒤의 틈이 받는 자리면 그 틈에 세로 막대를 세운다. 띠가 자른 자리 밖(흘러 숨은 끝)에는 그리지 않는다.
        if (interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false && interaction.drag->hovered_drop_target == id())
        {
            const float scale { positive_scale(context.scale) };
            const rect_f lane { strip_->bounds() };
            const float marker { tab_gap_marker_width * scale };
            const float x { std::clamp(end_gap_x(), lane.x, std::max(lane.x, lane.x + lane.width - marker)) };
            draw_gap_marker(context, x, lane);
        }
    }

    access_info tab_bar_element::accessibility() const
    {
        // 견본은 그림 하나다. 이름은 앱이 붙인 것이고, 비면 접근 tree에서 접힌다.
        if (config_.preview.has_value())
            return { .role = config_.preview->name.empty() ? access_role::none : access_role::image, .name = config_.preview->name };
        // 이름은 `search_label` → 툴팁 순서다 (`ui_element::access_name`).
        // 막대에 툴팁을 달면 탭 오른쪽 빈 자리에서 글 상자가 뜬다.
        return { .role = access_role::tab_list, .name = access_name() };
    }
}
