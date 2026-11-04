#include "luil/ui/calendar_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/field_popup.h"
#include "luil/ui/text_block.h"
#include "ui/default_text.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cmath>
#include <memory>
#include <span>
#include <utility>

namespace luil {
    namespace {
        using std::chrono::year_month;
        using std::chrono::year_month_day;

        // 날 칸 안쪽의 여백이다 (논리 픽셀). 이웃한 고른 날·오늘 테가 붙어 보이지 않는다.
        constexpr float day_inset { 2.0f };
        // 오늘 테의 굵기다 (논리 픽셀). 고른 날이면 그 안쪽에 같은 굵기의 테를 하나 더 긋는다.
        constexpr float today_ring_width { 1.5f };
        // 머리 줄 단추 글리프의 크기다 (논리 픽셀).
        constexpr float nav_glyph_size { 14.0f };
        // 비활성의 내용 불투명도다 (색 고르기와 같다).
        constexpr float disabled_content_alpha { 0.4f };
        // 날짜 칸의 글 왼쪽 여백과 아이콘 자리 폭이다 (논리 픽셀). 드롭다운과 같다.
        constexpr float field_inset { 8.0f };
        constexpr float field_icon_width { 18.0f };
        constexpr float field_icon_size { 14.0f };

        [[nodiscard]] std::u8string part_owner(const std::u8string_view owner, const std::u8string_view part)
        {
            std::u8string result { owner };
            result += u8'\u001f';
            result += part;
            return result;
        }

        [[nodiscard]] float cell_size_of(const calendar_config& config) noexcept
        {
            return config.cell_size > 0.0f && std::isfinite(config.cell_size) ? config.cell_size : calendar_cell_size;
        }

        [[nodiscard]] SkRect sk_rect(const rect_f& box) noexcept
        {
            return SkRect::MakeXYWH(box.x, box.y, box.width, box.height);
        }

        // 칸 가운데에 글을 놓는다. 칸보다 넓으면 왼쪽부터 말줄임한다.
        void draw_centered_text(SkCanvas& canvas, const std::u8string_view text, const rect_f& box, const SkFont& font, const SkPaint& paint)
        {
            if (text.empty() || box.width <= 0.0f || box.height <= 0.0f)
                return;
            const float width { measure_text(text, font) };
            const float baseline { box.y + centered_text_baseline(font, box.height) };
            if (width <= box.width)
                draw_text(canvas, text, box.x + (box.width - width) / 2.0f, baseline, font, paint);
            else
                static_cast<void>(draw_text_within(canvas, text, box.x, baseline, box.width, font, paint));
        }

        // 날의 십진수 글이다. 숫자 글자를 직접 지어 로케일에 달리지 않는다.
        [[nodiscard]] std::u8string day_number(const year_month_day day)
        {
            const unsigned value { static_cast<unsigned>(day.day()) };
            std::u8string text {};
            if (value >= 10u)
                text += static_cast<char8_t>(u8'0' + value / 10u % 10u);
            text += static_cast<char8_t>(u8'0' + value % 10u);
            return text;
        }

        // 날 칸 하나다. 모양은 root가 build 때 정한 값이고 이 element는 자리와 그리기, 접근만 든다.
        class calendar_day final : public ui_element
        {
        public:
            struct look
            {
                year_month_day day {};
                // 보이는 달의 날인가. 이웃 달의 날은 흐리게 그린다.
                bool in_month { false };
                // 범위 안이고 `available`이 참인가.
                bool selectable { false };
                bool selected { false };
                bool today { false };
                std::u8string name {};
            };

            calendar_day(ui_element_id id, look look)
                : ui_element { std::move(id) }
                , look_ { std::move(look) }
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
                if (box.width <= 0.0f || box.height <= 0.0f)
                    return;
                const float inset { day_inset * scale };
                const SkRect face { SkRect::MakeXYWH(box.x + inset, box.y + inset, box.width - 2.0f * inset, box.height - 2.0f * inset) };
                const float radius { context.metrics.control_corner_radius * scale };
                const bool pressable { enabled() && action(ui_trigger::left_click) != nullptr };

                // 고른 날은 강조색 채움이다. 누를 수 있는 날만 hover·눌림을 칠한다.
                if (look_.selected)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(face, radius, radius), solid_paint(context.palette.accent));
                else if (pressable && interaction.pressed == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(face, radius, radius), solid_paint(context.palette.button_pressed_background));
                else if (pressable && interaction.hovered == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(face, radius, radius), solid_paint(context.palette.button_hover_background));

                // 오늘은 강조색 테다. 고른 날과 겹치면 채움 안쪽에 강조 위 글색의 테를 더해 색 하나로만 가르지 않는다.
                if (look_.today)
                {
                    const float ring { today_ring_width * scale };
                    SkPaint outer { solid_paint(context.palette.accent) };
                    outer.setStyle(SkPaint::kStroke_Style);
                    outer.setStrokeWidth(ring);
                    outer.setAntiAlias(true);
                    const SkRect outer_line { face.makeInset(ring / 2.0f, ring / 2.0f) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(outer_line, radius, radius), outer);
                    if (look_.selected)
                    {
                        SkPaint inner { solid_paint(context.palette.accent_foreground) };
                        inner.setStyle(SkPaint::kStroke_Style);
                        inner.setStrokeWidth(ring);
                        inner.setAntiAlias(true);
                        const SkRect inner_line { face.makeInset(ring * 1.5f, ring * 1.5f) };
                        context.canvas.drawRRect(SkRRect::MakeRectXY(inner_line, radius, radius), inner);
                    }
                }

                ui_color text_color { context.palette.primary_foreground };
                if (look_.selected)
                    text_color = context.palette.accent_foreground;
                else if (look_.selectable == false)
                    text_color = context.palette.disabled_foreground;
                else if (look_.in_month == false)
                    text_color = context.palette.secondary_foreground;
                const SkFont font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
                draw_centered_text(context.canvas, day_number(look_.day), box, font, solid_paint(text_color));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::list_item, .name = look_.name, .selected = look_.selected };
            }

        private:
            look look_ {};
        };

        // 이전 달·다음 달 단추다. 글리프 하나와 hover·눌림 배경을 그린다.
        class calendar_nav final : public ui_element
        {
        public:
            calendar_nav(ui_element_id id, const char32_t glyph, std::u8string name)
                : ui_element { std::move(id) }
                , glyph_ { glyph }
                , name_ { std::move(name) }
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
                if (box.width <= 0.0f || box.height <= 0.0f)
                    return;
                const float inset { day_inset * scale };
                draw_hover_fill(context, { box.x + inset, box.y + inset, box.width - 2.0f * inset, box.height - 2.0f * inset }, id(), interaction, enabled(), context.metrics.control_corner_radius);
                const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), nav_glyph_size * scale };
                draw_centered_glyph(context.canvas, glyph_, box, glyph_font, solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::button, .name = name_ };
            }

        private:
            char32_t glyph_ { 0 };
            std::u8string name_ {};
        };

        // 날 칸 42개를 6주 x 7일로 놓는 격자다. 선택 container(list)이고 키 처리기가 여기 선다.
        class calendar_grid final : public ui_element
        {
        public:
            calendar_grid(ui_element_id id, std::u8string name)
                : ui_element { std::move(id) }
                , name_ { std::move(name) }
            {
            }

            void add(std::unique_ptr<ui_element> day)
            {
                add_child(std::move(day));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float width { context.slot.width / 7.0f };
                const float height { context.slot.height / 6.0f };
                const std::span<const std::unique_ptr<ui_element>> days { children() };
                for (std::size_t index { 0 }; index < days.size(); ++index)
                {
                    const float x { context.slot.x + static_cast<float>(index % 7u) * width };
                    const float y { context.slot.y + static_cast<float>(index / 7u) * height };
                    days[index]->set_enabled(enabled());
                    days[index]->arrange(context.for_child({ x, y, width, height }));
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::list, .name = name_ };
            }

        private:
            std::u8string name_ {};
        };
    }

    calendar_element::calendar_element(calendar_config config)
        : ui_element { ui_element_id { ui_element_kind::calendar, config.owner } }
        , config_ { std::move(config) }
    {
        // 그린 커서다. 틀린 커서는 시작 커서로, 범위 밖 커서는 범위로 접는다. 값(`value`)은 앱 상태라 그대로 둔다.
        cursor_ = config_.cursor.ok() ? clamp_date(config_.cursor, config_.bounds) : calendar_start_cursor(config_.value, config_.today, config_.bounds);
        const year_month month { cursor_.year(), cursor_.month() };
        const bool interactive { config_.select != nullptr || config_.move_to != nullptr };

        // 이전·다음 단추는 한 달 앞뒤 같은 날(말일로 접음)로 커서를 옮긴다. 그 달 전체가 범위 밖이면 누를 수 없다.
        //  - 범위의 두 끝은 달력 끝을 범위로 접은 날이다.
        const std::function<input_action(year_month_day)>& turn { config_.turn_month != nullptr ? config_.turn_month : config_.move_to };
        if (turn != nullptr)
        {
            const year_month_day low { clamp_date(year_month_day { std::chrono::year { 1 }, std::chrono::January, std::chrono::day { 1 } }, config_.bounds) };
            const year_month_day high { clamp_date(year_month_day { std::chrono::year { 9999 }, std::chrono::December, std::chrono::day { 31 } }, config_.bounds) };
            const year_month before { month - std::chrono::months { 1 } };
            const year_month after { month + std::chrono::months { 1 } };
            previous_blocked_ = year_month_day { std::chrono::year_month_day_last { before.year(), std::chrono::month_day_last { before.month() } } } < low;
            next_blocked_ = year_month_day { after / std::chrono::day { 1 } } > high;
            const auto make_nav = [this, &turn](const std::u8string_view part, const char32_t glyph, const std::u8string_view name, const year_month_day target) {
                auto button { std::make_unique<calendar_nav>(ui_element_id { ui_element_kind::calendar_nav, part_owner(config_.owner, part) }, glyph, std::u8string { name }) };
                button->set_tooltip(std::u8string { name });
                button->set_cursor(ui_cursor::hand);
                button->set_action(ui_trigger::left_click, [this, message = turn, target](const ui_action_context&) { return emit_move(message, target); });
                return button;
            };
            const std::u8string_view previous_name { default_text::text_or_default(config_.text.previous_month, default_text::calendar_previous_month()) };
            auto previous { make_nav(u8"prev", codicons::icon_chevron_left, previous_name, clamp_date(add_months_clamped(cursor_, -1), config_.bounds)) };
            previous_ = previous.get();
            add_child(std::move(previous));
            const std::u8string_view next_name { default_text::text_or_default(config_.text.next_month, default_text::calendar_next_month()) };
            auto next { make_nav(u8"next", codicons::icon_chevron_right, next_name, clamp_date(add_months_clamped(cursor_, 1), config_.bounds)) };
            next_ = next.get();
            add_child(std::move(next));
        }

        auto grid { std::make_unique<calendar_grid>(ui_element_id { ui_element_kind::calendar_grid, config_.owner }, month_title()) };
        if (interactive)
            grid->set_key_handler([this](const key_pressed_event& event) { return handle_key(event); });
        const ui_element_id cursor_id { day_id(config_.owner, cursor_) };
        for (const year_month_day day : calendar_month_grid(month, config_.first_day))
        {
            calendar_day::look look {};
            look.day = day;
            look.in_month = day.year() == cursor_.year() && day.month() == cursor_.month();
            look.selectable = selectable(day);
            look.selected = config_.value.has_value() && *config_.value == day;
            look.today = config_.today.has_value() && *config_.today == day;
            look.name = day_name(day);
            auto cell { std::make_unique<calendar_day>(day_id(config_.owner, day), std::move(look)) };
            const bool pressable { config_.select != nullptr && selectable(day) };
            if (pressable)
            {
                // 커서가 아닌 날의 클릭은 커서를 먼저 그 날로 옮긴다. 앱의 초점 요청이 누른 날에 초점을 세운다.
                cell->set_action(ui_trigger::left_click, [this, day](const ui_action_context&) {
                    std::vector<input_action> actions {};
                    if (day != cursor_ && config_.move_to != nullptr)
                        actions.push_back(config_.move_to(day));
                    actions.push_back(config_.select(day));
                    return actions;
                });
                cell->set_cursor(ui_cursor::hand);
            }
            else if (interactive)
            {
                // 고를 수 없는 날도 누름을 받아(hit) 포인터 초점이 커서 날에 남는다. 받지 않으면 누름이 뒤의 판까지 가 초점이 지워진다.
                cell->set_cursor(ui_cursor::arrow);
            }
            // 커서 날만 Tab 자리다. 고를 수 없는 날이어도 자리라 커서가 그 날에 설 수 있다 (결정 6).
            if (day == cursor_)
                cell->set_tab_stop(interactive);
            else
            {
                cell->set_tab_stop(false);
                if (interactive)
                    cell->set_pointer_focus_target(cursor_id);
            }
            grid->add(std::move(cell));
        }
        grid_ = grid.get();
        add_child(std::move(grid));
    }

    size_f calendar_element::size_for(const calendar_config& config) noexcept
    {
        const float cell { cell_size_of(config) };
        return { cell * 7.0f, cell + calendar_weekday_height + cell * 6.0f };
    }

    ui_element_id calendar_element::day_id(const std::u8string_view owner, const year_month_day day)
    {
        return { ui_element_kind::calendar_day, part_owner(owner, format_iso_date(day)) };
    }

    bool calendar_element::selectable(const year_month_day day) const
    {
        return calendar_contains(config_.bounds, day) && (config_.available == nullptr || config_.available(day));
    }

    std::u8string calendar_element::month_title() const
    {
        const year_month month { cursor_.year(), cursor_.month() };
        return config_.text.month_title != nullptr ? config_.text.month_title(month) : default_text::calendar_month_title(month);
    }

    std::u8string calendar_element::day_name(const year_month_day day) const
    {
        // 꼬리의 차례(오늘, 고를 수 없음)와 쉼표는 요소가 정한다. 낱말은 앱이 덮는다.
        std::u8string name { config_.text.day_name != nullptr ? config_.text.day_name(day) : default_text::calendar_day_name(day) };
        if (config_.today.has_value() && *config_.today == day)
        {
            name += u8", ";
            name += default_text::text_or_default(config_.text.today, default_text::calendar_today());
        }
        if (selectable(day) == false)
        {
            name += u8", ";
            name += default_text::text_or_default(config_.text.unavailable, default_text::calendar_unavailable());
        }
        return name;
    }

    std::vector<input_action> calendar_element::emit_move(const std::function<input_action(year_month_day)>& message, const year_month_day target) const
    {
        // 범위 끝에서 더 갈 곳이 없으면 빈 목록이다. 키가 앱 단축키로 새지 않는다.
        if (message == nullptr || target == cursor_)
            return {};
        return { message(target) };
    }

    std::optional<std::vector<input_action>> calendar_element::handle_key(const key_pressed_event& event) const
    {
        // Ctrl·Alt·Meta를 든 키는 앱 단축키다. Shift는 PageUp·PageDown에서만 뜻이 있다 (한 해).
        if (event.shortcut_modifier_down())
            return std::nullopt;
        const bool page { event.key == key_code::page_up || event.key == key_code::page_down };
        if (event.shift && page == false)
            return std::nullopt;
        // 초점 날이 고를 수 있으면 Enter·Space는 실행 키(초점 날의 클릭)가 먼저 가져간다. 여기 온 것은 고를 수 없는 날이라
        // 빈 목록으로 삼켜 기본 단추로 가지 않게 한다.
        if (event.key == key_code::enter || event.key == key_code::space)
            return config_.select != nullptr ? std::optional<std::vector<input_action>> { std::vector<input_action> {} } : std::nullopt;
        if (config_.move_to == nullptr)
            return std::nullopt;
        std::optional<calendar_move> move {};
        if (event.key == key_code::arrow_left)
            move = calendar_move::previous_day;
        else if (event.key == key_code::arrow_right)
            move = calendar_move::next_day;
        else if (event.key == key_code::arrow_up)
            move = calendar_move::previous_week;
        else if (event.key == key_code::arrow_down)
            move = calendar_move::next_week;
        else if (event.key == key_code::page_up)
            move = event.shift ? calendar_move::previous_year : calendar_move::previous_month;
        else if (event.key == key_code::page_down)
            move = event.shift ? calendar_move::next_year : calendar_move::next_month;
        else if (event.key == key_code::home)
            move = calendar_move::month_start;
        else if (event.key == key_code::end)
            move = calendar_move::month_end;
        if (move.has_value() == false)
            return std::nullopt;
        return emit_move(config_.move_to, calendar_moved(cursor_, *move, config_.bounds));
    }

    void calendar_element::arrange(const arrange_context& context)
    {
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
        const float cell { cell_size_of(config_) * scale_ };
        const float weekday { calendar_weekday_height * scale_ };
        const float x { context.slot.x };
        const float y { context.slot.y };
        set_bounds({ x, y, cell * 7.0f, cell + weekday + cell * 6.0f });
        title_box_ = { x + cell, y, cell * 5.0f, cell };
        weekday_box_ = { x, y + cell, cell * 7.0f, weekday };

        // 비활성은 root의 것이라 자식이 따라간다. 단추는 그 달이 범위 밖이어도 비활성이다.
        const auto place = [this, &context](ui_element* const part, const rect_f& box, const bool open) {
            if (part == nullptr)
                return;
            part->set_enabled(enabled() && open);
            part->arrange(context.for_child(box));
        };
        place(previous_, { x, y, cell, cell }, previous_blocked_ == false);
        place(next_, { x + cell * 6.0f, y, cell, cell }, next_blocked_ == false);
        place(grid_, { x, y + cell + weekday, cell * 7.0f, cell * 6.0f }, true);
    }

    void calendar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 비활성은 내용 전체를 옅게 그린다 (색 고르기와 같다).
        const bool dim { enabled() == false };
        if (dim)
            context.canvas.saveLayerAlphaf(nullptr, disabled_content_alpha);
        const SkFont title_font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
        draw_centered_text(context.canvas, month_title(), title_box_, title_font, solid_paint(context.palette.primary_foreground));
        const SkFont weekday_font { make_text_font(context, { .size = context.metrics.small_font_size, .weight = context.metrics.font_weights.small_text }) };
        const SkPaint weekday_paint { solid_paint(context.palette.secondary_foreground) };
        const float column { weekday_box_.width / 7.0f };
        const unsigned first { config_.first_day.ok() ? config_.first_day.c_encoding() : 0u };
        for (unsigned index { 0 }; index < 7u; ++index)
        {
            const unsigned encoding { (first + index) % 7u };
            const std::u8string_view name { default_text::text_or_default(config_.text.weekdays[encoding], default_text::calendar_weekday_short(std::chrono::weekday { encoding })) };
            draw_centered_text(context.canvas, name, { weekday_box_.x + static_cast<float>(index) * column, weekday_box_.y, column, weekday_box_.height }, weekday_font, weekday_paint);
        }
        draw_children(context, interaction);
        if (dim)
            context.canvas.restore();
    }

    access_info calendar_element::accessibility() const
    {
        return { .role = access_role::group, .name = std::u8string { default_text::text_or_default(config_.text.name, default_text::calendar_name()) } };
    }

    date_field_element::date_field_element(date_field_config config)
        : ui_element { ui_element_id { ui_element_kind::date_field, config.owner } }
        , config_ { std::move(config) }
    {
        if (config_.set_open == nullptr)
            return;
        // 누름은 build 때의 상태 반대를 담아 절대 메시지 factory를 부른다. 보조 기술의 펼치기·접기도 같은 factory다.
        set_action(ui_trigger::left_click, [set_open = config_.set_open, target = config_.open == false](const ui_action_context&) -> std::vector<input_action> { return { set_open(target) }; });
        set_cursor(ui_cursor::hand);
        // 칸에서는 ↓가 달리 쓰이지 않는다. 드롭다운 단추의 관례대로 ↓도 연다.
        set_key_handler(field_popup_key_handler(config_.open, config_.set_open, true));
        // 열린 동안 키보드 초점이 같은 표면 안에서 칸을 떠나면 닫는다. popup의 날로 옮긴 초점은 떠남이 아니다.
        if (config_.open)
            set_focus_leave_action([close = config_.set_open](const ui_action_context&) { return std::vector<input_action> { close(false) }; });
    }

    std::u8string date_field_element::value_text() const
    {
        if (config_.value.has_value() == false || config_.value->ok() == false)
            return {};
        return config_.format != nullptr ? config_.format(*config_.value) : default_text::date_field_text(*config_.value);
    }

    void date_field_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void date_field_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        if (box.width <= 0.0f || box.height <= 0.0f)
            return;
        const SkRect body { sk_rect(box) };
        const float radius { context.metrics.control_corner_radius * scale };

        // 드롭다운과 같은 표면·테두리 규칙이다. popup이 떠 있으면 테두리를 강조한다.
        ui_color background { context.palette.input_background };
        if (enabled() && interaction.pressed == id())
            background = context.palette.button_pressed_background;
        else if (enabled() && interaction.hovered == id())
            background = context.palette.button_hover_background;
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));
        SkPaint border { solid_paint(config_.open ? context.palette.accent : context.palette.input_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        // 오른쪽의 달력 아이콘이다. 열림은 테두리가 알리므로 아이콘은 바꾸지 않는다.
        const float icon_width { field_icon_width * scale };
        const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), field_icon_size * scale };
        const SkPaint glyph { solid_paint(enabled() ? context.palette.secondary_foreground : context.palette.disabled_foreground) };
        draw_centered_glyph(context.canvas, codicons::icon_calendar, { box.x + box.width - icon_width - 4.0f * scale, box.y, icon_width, box.height }, glyph_font, glyph);

        const float inset { field_inset * scale };
        const float text_width { box.width - inset - icon_width - 8.0f * scale };
        if (text_width <= 0.0f)
            return;
        const std::u8string text { value_text() };
        const bool empty { text.empty() };
        const std::u8string_view shown { empty ? default_text::text_or_default(config_.placeholder, default_text::date_field_placeholder()) : std::u8string_view { text } };
        ui_color text_color { context.palette.primary_foreground };
        if (empty)
            text_color = context.palette.secondary_foreground;
        else if (enabled() == false)
            text_color = context.palette.disabled_foreground;
        const SkFont font { make_text_font(context, { .size = context.metrics.body_font_size, .weight = context.metrics.font_weights.body_text }) };
        static_cast<void>(draw_text_within(context.canvas, shown, box.x + inset, box.y + centered_text_baseline(font, box.height), text_width, font, solid_paint(text_color)));
    }

    access_info date_field_element::accessibility() const
    {
        // 이름은 "무엇을 고르는 칸인가", 값은 표시 글이다. 값이 없으면 값 글도 비어 있다 (placeholder는 값이 아니다).
        access_info info { .role = access_role::combo_box, .expanded = config_.open };
        info.name = std::u8string { default_text::text_or_default(config_.name, default_text::date_field_name()) };
        info.value = value_text();
        return info;
    }

    std::optional<std::vector<input_action>> date_field_element::access_actions(const access_request& request) const
    {
        if (request.command != access_command::expand && request.command != access_command::collapse)
            return ui_element::access_actions(request);
        // 펼치기·접기는 절대 메시지만 탄다. 클릭(뒤집기)으로 흘리면 오래된 발행본을 본 같은 명령 둘이 열었다 도로 닫는다.
        if (config_.set_open == nullptr)
            return std::nullopt;
        return std::vector<input_action> { config_.set_open(request.command == access_command::expand) };
    }
}
