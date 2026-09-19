#include "luil/ui/list_element.h"

#include "ui/reorder_drop.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/stack_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 가지를 접었다 펴는 삼각형이다.
        // 행 안의 보조 버튼이라 Tab의 자리가 아니다.
        class list_expander_element final : public ui_element
        {
        public:
            list_expander_element(std::u8string key, const bool expanded, const list_config& config)
                : ui_element { ui_element_id { ui_element_kind::list_expander, std::move(key) } }
                , expanded_ { expanded }
                , set_expanded_ { config.set_expanded }
            {
                // 항목에 딸린 보조 버튼은 묶음 안에 두지 않는다 — 두면 ↑↓가 행과
                // 삼각형을 번갈아 지나고 "항목 안의 자리"라는 개념이 또 필요하다
                // (탭의 닫기 버튼과 같은 판정, focus-group-design.md).
                set_tab_stop(false);
                set_cursor(ui_cursor::hand);
                const std::u8string owner { id().owner };
                // 클릭은 토글이 먼저다. 없으면 절대 메시지에 지금 상태의 반대를
                // 담는다 — factory 하나로 클릭과 접근 실행이 함께 선다.
                if (const list_message_factory& toggle { config.toggle }; toggle != nullptr)
                    set_action(ui_trigger::left_click, [owner, &toggle](const ui_action_context&) -> std::vector<input_action> { return { toggle(owner) }; });
                else if (set_expanded_ != nullptr)
                {
                    const bool target { expanded == false };
                    set_action(ui_trigger::left_click, [this, owner, target](const ui_action_context&) -> std::vector<input_action> { return { set_expanded_(owner, target) }; });
                }
            }

            // 펼치기·접기는 절대 메시지만 탄다 — 토글 클릭으로 흘리면 오래된
            // 발행본을 본 같은 명령 둘이 두 번 뒤집는다 (그룹과 같은 갈래다).
            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                if (request.command != access_command::expand && request.command != access_command::collapse)
                    return ui_element::access_actions(request);
                if (set_expanded_ == nullptr)
                    return std::nullopt;
                return std::vector<input_action> { set_expanded_(id().owner, request.command == access_command::expand) };
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                const ui_color color { interaction.hovered == id() ? context.palette.primary_foreground : context.palette.secondary_foreground };
                const char32_t glyph { expanded_ ? codicons::icon_chevron_down : codicons::icon_chevron_right };
                draw_centered_glyph(context.canvas, glyph, bounds(), glyph_font, solid_paint(color));
            }

        private:
            bool expanded_ { false };
            // 펼침의 절대 메시지다 (없으면 빈 함수).
            // 목록의 config는 부모 element가 들고 있어 참조로도 안전하지만, 접근
            // 실행이 행을 거치지 않고 이 요소에 직접 물을 수 있어 사본을 든다.
            std::function<input_action(const std::u8string& key, bool expanded)> set_expanded_ {};
        };

        // 목록의 줄 하나다.
        // 누르면 고르자는 메시지를 내고, 삼각형을 누르면 접었다 펴자는 메시지를 낸다.
        class list_row_element final : public ui_element
        {
        public:
            list_row_element(list_item item, const bool selected, const bool reserve_expander, const list_config& config)
                : ui_element { ui_element_id { ui_element_kind::list_row, item.key } }
                , item_ { std::move(item) }
                , selected_ { selected }
                , reserve_expander_ { reserve_expander }
                , handle_ { config.reorder != nullptr }
            {
                const list_message_factory& select { config.select };
                set_enabled(item_.enabled);
                // 묶음 안에서 글자로 찾을 때 보는 글이다 (행에 적힌 이름과 같은 것).
                set_search_label(item_.label);
                // 삼각형은 가지에만 선다. 잎은 자리만 비운다.
                if ((config.toggle != nullptr || config.set_expanded != nullptr) && item_.expansion != list_expansion::none)
                {
                    auto expander { std::make_unique<list_expander_element>(item_.key, item_.expansion == list_expansion::expanded, config) };
                    expander_ = expander.get();
                    add_child(std::move(expander));
                }
                if (handle_)
                {
                    // 잡은 지점과 행 원점의 차이는 배치가 정하므로 잡는 순간의 bounds에서 읽는다.
                    const ui_element* const self { this };
                    const std::u8string key { item_.key };
                    std::u8string label { item_.label };
                    const list_reorder_message_factory& reorder { config.reorder };
                    // 이 행이 속한 목록이다. 놓는 쪽이 같은 목록인지를 이것으로 가른다.
                    const ui_element_id container { ui_element_kind::list, config.owner };
                    drag_source source {};
                    source.make_payload = [self, key, label, container](const ui_action_context& context) {
                        drag_payload payload {};
                        payload.source = context.element;
                        payload.container = container;
                        payload.dragged_owner = key;
                        payload.label = label;
                        payload.grab_offset_x = context.x - self->bounds().x;
                        payload.grab_offset_y = context.y - self->bounds().y;
                        return payload;
                    };
                    set_drag_source(std::move(source));

                    drop_target drop {};
                    // 같은 목록의 행만 받는다. 자기 자리에 놓는 것은 이동이 아니므로 받지
                    // 않는다 (강조도 뜨지 않는다) — 규칙은 탭과 한 벌이다 (reorder_drop.h).
                    drop.accepts = [key, container](const drag_payload& payload) { return accepts_reorder_drop(payload, ui_element_kind::list_row, container, key); };
                    drop.on_drop = [key, &reorder](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> { return { reorder(payload.dragged_owner, key) }; };
                    set_drop_target(std::move(drop));
                    // 평소 모양(grab)은 drag 역할에서 라이브러리가 고른다.
                    set_active_cursor(config.row_active_cursor);
                }

                // 고르는 액션이 없으면 자리도 아니다.
                // 파생마다 키보드를 켜고 끄는 규칙을 따로 두지 않는 것이 이 자리다
                // (list-view-design.md).
                if (select == nullptr)
                    return;
                const std::u8string key { item_.key };
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click, [key, &select](const ui_action_context&) -> std::vector<input_action> { return { select(key) }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                if (expander_ == nullptr)
                    return;
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                expander_->arrange(context.for_child({ context.slot.x + expander_offset(scale), context.slot.y, list_expander_width * scale, context.slot.height }));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const bool dragged { interaction.drag.has_value() && interaction.drag->payload.dragged_owner == item_.key };
                const bool drop_here { interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false && interaction.drag->hovered_drop_target == id() };

                // 놓을 자리 · 고른 행 · 눌린 행 · hover의 순서다.
                // 고름은 앱 상태라 포인터 상태보다 오래가고, 놓을 자리는 지금 끄는
                // 손이 묻는 것이라 그보다도 앞선다.
                ui_color background { 0 };
                bool fill { false };
                if (drop_here)
                {
                    background = context.palette.drop_target_background;
                    fill = true;
                }
                else if (selected_)
                {
                    // 고른 행의 모양은 공유 primitive 하나다 — 가상 목록의 행과 앱이 지은
                    // 행이 같은 함수를 써야 한 화면의 고름이 한 모양이다.
                    // 끌려 나간 행은 제자리에 흐리게 남는다 (hover 채움과 같은 규칙).
                    if (dragged)
                        context.canvas.saveLayerAlphaf(&shape, 0.35f);
                    draw_row_selection(context, box);
                    if (dragged)
                        context.canvas.restore();
                }
                else if (interaction.pressed == id())
                {
                    background = context.palette.button_pressed_background;
                    fill = true;
                }
                else if (interaction.hovered == id())
                {
                    background = context.palette.button_hover_background;
                    fill = true;
                }
                if (fill)
                {
                    SkPaint paint { solid_paint(background) };
                    // 끌려 나간 행은 제자리에 흐리게 남는다.
                    if (dragged)
                        paint.setAlphaf(0.35f);
                    context.canvas.drawRect(shape, paint);
                }

                // 잡는 손잡이는 깊이 밖의 칸이다 — 행의 것이지 tree 계층의 것이 아니다.
                if (handle_)
                {
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    const SkPaint dim { solid_paint(context.palette.secondary_foreground) };
                    draw_centered_glyph(context.canvas, codicons::icon_gripper, { box.x + 8.0f * scale, box.y, 16.0f * scale, box.height }, glyph_font, dim);
                }

                float text_left { box.x + text_offset(scale) };
                const SkPaint foreground { solid_paint(item_.enabled ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                if (item_.icon != 0)
                {
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    const SkPaint glyph { solid_paint(item_.enabled ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
                    draw_centered_glyph(context.canvas, item_.icon, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, glyph);
                    text_left += 22.0f * scale;
                }

                const float text_width { box.x + box.width - 8.0f * scale - text_left };
                if (text_width > 0.0f)
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), context.metrics.body_font_size * scale };
                    static_cast<void>(draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }
                draw_children(context, interaction);
            }

            [[nodiscard]] std::optional<std::vector<input_action>> access_actions(const access_request& request) const override
            {
                // 행의 클릭은 **고르기**다. 펼침은 삼각형이 쥔다.
                if (request.command != access_command::expand && request.command != access_command::collapse)
                    return ui_element::access_actions(request);
                return expander_ != nullptr ? expander_->access_actions(request) : std::nullopt;
            }

            [[nodiscard]] access_info accessibility() const override
            {
                access_info info { .role = access_role::list_item, .name = item_.label, .selected = selected_ };
                // 펼침 상태는 가지에만 있다 — 잎에 "접힘"을 말하면 거짓이 된다.
                if (item_.expansion != list_expansion::none)
                    info.expanded = item_.expansion == list_expansion::expanded;
                return info;
            }

        private:
            // 삼각형이 서는 자리의 왼쪽이다 (행 원점 기준, 물리 픽셀).
            // 깊이가 미는 것은 이 값 하나라, 삼각형과 글이 같은 만큼 함께 밀린다.
            [[nodiscard]] float expander_offset(const float scale) const noexcept
            {
                return (8.0f + (handle_ ? list_handle_width : 0.0f) + static_cast<float>(item_.depth) * list_indent_step) * scale;
            }

            // 앞 글리프와 글이 시작하는 자리다.
            // 목록이 접힘을 쓰면 **잎도** 삼각형 폭을 비워 글이 줄을 맞춘다.
            [[nodiscard]] float text_offset(const float scale) const noexcept
            {
                return expander_offset(scale) + (reserve_expander_ ? list_expander_width * scale : 0.0f);
            }

            list_item item_ {};
            bool selected_ { false };
            bool reserve_expander_ { false };
            bool handle_ { false };
            // 잎이거나 목록이 접힘을 쓰지 않으면 nullptr다.
            ui_element* expander_ { nullptr };
        };
    } // namespace

    list_element::list_element(list_config config)
        : ui_element { ui_element_id { ui_element_kind::list, config.owner } }
        , config_ { std::move(config) }
    {
        // 목록은 Tab에서 한 자리이고 그 안은 ↑↓가 돈다.
        // 들어오면 고른 행에 선다 (앱 상태를 그대로 옮긴다).
        //  - 행이 창·레인 아래로 두 겹 들어가도 `focus_order`는 tree를 걸어
        //    모으므로 자리 판정이 바뀌지 않는다 (탭 막대와 같다).
        set_focus_group(focus_axis::vertical);
        set_focus_entry(ui_element_id { ui_element_kind::list_row, config_.selected });

        // 행을 늘어놓는 레인이다. 간격도 여백도 없어 행이 맞붙는다.
        stack_config lane_config {};
        auto lane { std::make_unique<stack_element>(ui_element_id { ui_element_kind::list_lane, config_.owner }, lane_config) };
        // 접힘을 쓰는 목록이면 잎도 삼각형 자리를 비운다 (글이 줄을 맞춘다).
        const bool reserve_expander { config_.toggle != nullptr || config_.set_expanded != nullptr };
        for (list_item& item : config_.items)
        {
            const bool selected { item.enabled && item.key == config_.selected };
            lane->add(std::make_unique<list_row_element>(std::move(item), selected, reserve_expander, config_), config_.row_height);
        }
        content_height_ = static_cast<float>(config_.items.size()) * config_.row_height;

        // 흘리기·잘라내기·다듬기는 창의 몫이다.
        // 내용 높이는 담는 쪽이 알려 준다 — 측정 단계가 없다.
        scroll_view_config view {};
        view.content_height = content_height_;
        view.scroll_offset = config_.scroll_offset;
        auto scroll { std::make_unique<scroll_view_element>(ui_element_id { ui_element_kind::list_scroll, config_.owner }, view) };
        scroll->set_content(std::move(lane));
        view_ = scroll.get();
        add_child(std::move(scroll));

        // 막대는 스크롤 메시지를 만들 수 있을 때만 선다 ("없는 것은 두지 않는다").
        // 내용·창 높이와 스크롤 값은 slot이 정해져야 알 수 있어 `arrange`가 채운다.
        if (config_.scroll == nullptr)
            return;
        scrollbar_config bar {};
        bar.scroll = config_.scroll;
        bar.scroll_to = config_.scroll_to;
        auto scrollbar { std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::list_scrollbar, config_.owner }, std::move(bar)) };
        // 목록 안의 막대는 **Tab의 자리가 아니다.** 목록 자신이 ↑/↓·Home/End를
        // 가진 묶음이라, 막대까지 자리가 되면 같은 목록에 자리가 둘 선다.
        //  - 홀로 서는 막대는 자기 자리를 자처한다. 거절은 담는 쪽의 몫이라는
        //    기존 규칙 그대로다 (탭 막대·메뉴·토스트가 같은 자리에서 거절한다).
        scrollbar->set_tab_stop(false);
        scrollbar_ = scrollbar.get();
        add_child(std::move(scrollbar));
    }

    float list_element::content_height() const noexcept
    {
        return content_height_;
    }

    float list_element::maximum_scroll() const noexcept
    {
        return maximum_scroll_;
    }

    float list_element::scroll_offset() const noexcept
    {
        return config_.scroll_offset;
    }

    float list_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        return view_->scroll_delta_to_reveal(target);
    }

    void list_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // 막대가 오른쪽 끝의 자리를 가져가고 창이 나머지를 받는다.
        //  - 창이 실제로 좁아지므로 **행의 글이 막대 밑을 지나지 않는다.**
        float view_width { context.slot.width };
        if (scrollbar_ != nullptr)
            view_width -= list_scrollbar_width * scale;
        if (view_width < 0.0f)
            view_width = 0.0f;
        view_->arrange(context.for_child({ context.slot.x, context.slot.y, view_width, context.slot.height }));

        // 다듬은 값을 그대로 물려받아 앱이 같은 값을 상태에 되돌릴 수 있게 한다.
        maximum_scroll_ = view_->maximum_scroll();
        config_.scroll_offset = view_->scroll_offset();

        // **흘릴 것이 있으면 이 목록이 그 창이다** (`scroll_area_element`와 같은 판정).
        // 휠과 초점 되살리기가 표 없이 임자를 찾고, 보조 기술의 "이 자리를 화면에
        // 들여라"(ScrollItem)도 같은 길을 탄다 — 창을 세우지 않으면 화면 밖으로
        // 밀린 행에 그 명령이 설 자리가 없다 (accessibility-action-design.md).
        if (config_.scroll != nullptr)
        {
            if (maximum_scroll_ > 0.0f)
            {
                scroll_source source {};
                source.scroll = config_.scroll;
                source.scale = scale;
                set_scroll_source(std::move(source));
            }
            else
                set_scroll_source(std::nullopt);
        }

        if (scrollbar_ == nullptr)
            return;
        // 목록에 이름이 있으면 막대도 그 이름으로 읽힌다 — 앱이 손댈 수 없는
        // 안쪽 부품이라 이름을 물려주는 것이 유일한 길이다 (`ui_element::access_name`).
        scrollbar_->set_access_name(access_name());

        // 막대가 재는 창 높이는 **창이 실제로 받은 높이**다. 목록이 다시 계산하면
        // 같은 식이 두 곳에 살고 언젠가 어긋난다 (`clamp_scroll`의 규칙과 같다).
        scrollbar_->set_metrics(content_height_, context.slot.height / scale, config_.scroll_offset);
        const float bar_width { list_scrollbar_width * scale };
        scrollbar_->arrange(context.for_child({ context.slot.x + context.slot.width - bar_width, context.slot.y, bar_width, context.slot.height }));
    }

    void list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 목록 자체는 바탕을 칠하지 않는다.
        // 표면 색과 모서리는 담는 쪽(panel)의 몫이다.
        draw_children(context, interaction);
        // 가장자리는 행 위에 겹친다. `arrange`가 다듬은 offset과 최대치를 그대로 쓴다.
        draw_scroll_edges(context, bounds(), config_.scroll_offset, maximum_scroll_, config_.edges);
    }
    access_info list_element::accessibility() const
    {
        // 이름은 `search_label` → 툴팁 순서다 (`ui_element::access_name`).
        // 목록에 툴팁을 달면 행 사이 빈 자리에서 글 상자가 뜬다.
        return { .role = access_role::list, .name = access_name() };
    }
} // namespace luil
