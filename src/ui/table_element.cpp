#include "luil/ui/table_element.h"

#include "luil/generated/codicons.h"
#include "luil/ui/draw_primitives.h"
#include "ui/reorder_drop.h"
#include "ui/tree_expander.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace luil {
    namespace {
        // 표의 부품 자리표다. 표 owner와 부품 키 사이에 단위 구분자(U+001F)를 둔다 — 앱의 키에 들 수 없는
        // 글자라, 같은 화면의 다른 표나 같은 키의 다른 열과 겹치지 않는다.
        [[nodiscard]] std::u8string part_owner(const std::u8string& table, const std::u8string& part)
        {
            return table + u8"\u001f" + part;
        }

        // 열의 물리 픽셀 가로 자리다. 가로 스크롤만큼 왼쪽으로 민다.
        [[nodiscard]] rect_f column_rect(const rect_f& row, const table_column_span& span, const float scroll_x, const float scale) noexcept
        {
            return { row.x + (span.begin - scroll_x) * scale, row.y, span.width * scale, row.height };
        }

        // 칸 글 한 줄을 그린다. 끝 정렬은 글이 들어갈 때만 오른쪽에 붙고, 넘치면 시작에서 잘라 그린다 —
        // 넘친 숫자를 오른쪽에 붙이면 앞자리가 잘려 다른 숫자로 읽힌다.
        void draw_cell_text(SkCanvas& canvas, const std::u8string_view text, const rect_f& cell, const column_align align, const float scale, const SkFont& font, const SkPaint& paint)
        {
            if (text.empty())
                return;
            const float padding { table_cell_padding * scale };
            const float available { cell.width - padding * 2.0f };
            if (available <= 0.0f)
                return;
            float left { cell.x + padding };
            if (align == column_align::end)
                if (const float width { measure_text(text, font) }; width <= available)
                    left = cell.x + cell.width - padding - width;
            static_cast<void>(draw_text_within(canvas, text, left, cell.y + centered_text_baseline(font, cell.height), available, font, paint));
        }

        // 편집 중인 칸의 자리다. 앱의 편집기를 담고 초점을 그 안에 가둔다.
        //
        // 초점을 옮기는 길은 가둠의 진입·복귀다 (focus-entry-design.md). 편집기가 서면 controller가 본문에 있던
        // 초점을 거두고 편집기에 세우며, 편집이 끝나 이 자리가 사라지면 본문으로 돌려보낸다 — 앱은 초점을 다루지
        // 않는다. 같은 길을 dialog가 쓴다.
        //  - Enter·Esc는 이 자리의 키다. 편집기(텍스트 칸)가 쓰지 않는 Enter가 여기로 올라와 확정이 되고, Esc는 취소다.
        //    가둠의 Esc(`dismiss_action`)보다 키 처리기가 앞이라 Esc가 두 번 나지 않는다.
        class table_editor_host_element final : public ui_element
        {
        public:
            table_editor_host_element(ui_element_id id, std::unique_ptr<ui_element> editor, table_cell_ref cell, const table_cell_message_factory& commit,
                const table_cell_message_factory& cancel, ui_element_id body)
                : ui_element { std::move(id) }
            {
                set_focus_trap(true);
                set_focus_entry(editor->id());
                set_focus_return(std::move(body));
                add_child(std::move(editor));
                if (cancel != nullptr)
                    set_dismiss_action([cell, cancel](const ui_action_context&) -> std::vector<input_action> { return { cancel(cell) }; });
                set_key_handler([cell, commit, cancel](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
                    if (event.shortcut_modifier_down() || event.shift)
                        return std::nullopt;
                    if (event.key == key_code::enter && commit != nullptr)
                        return std::vector<input_action> { commit(cell) };
                    if (event.key == key_code::escape && cancel != nullptr)
                        return std::vector<input_action> { cancel(cell) };
                    return std::nullopt;
                });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                for (const std::unique_ptr<ui_element>& child : children())
                    child->arrange(context.for_child(context.slot));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }
        };

        // 체크 칸이 차지하는 폭이다 (논리 픽셀). 체크박스는 자기 bounds 안 4 들여서 그린다.
        constexpr float check_slot_width { check_box_size + 8.0f };

        // 행 하나의 칸들이다. 가상 목록의 행이 이것을 내용으로 담는다.
        //
        // 칸을 앱이 지었으면(`build_cell`) 그 element를 열 자리에 앉히고, 아니면 글 한 줄을 직접 그린다 —
        // 칸마다 label element를 두면 칸마다 자리표가 필요하고, 그 수가 행 수 × 열 수다.
        //  - 트리 열은 칸 앞에 깊이만큼 들여쓰고 삼각형을, 체크 열은 체크박스를 둔다. 글과 앱의 칸은 그 뒤에 선다.
        class table_row_element final : public ui_element
        {
        public:
            table_row_element(const table_element& table, const virtual_list_item& item, const std::size_t index, const bool selected)
                : ui_element { ui_element_id { ui_element_kind::table_row, part_owner(table.config().owner, item.key) } }
                , table_ { &table }
                , key_ { item.key }
                , depth_ { item.depth }
            {
                set_clip_children(true);
                const table_config& config { table.config() };
                texts_.resize(config.columns.size());
                leads_.resize(config.columns.size(), 0.0f);
                const ui_element_id body { ui_element_kind::virtual_list, config.owner };
                for (std::size_t column = 0; column < config.columns.size(); ++column)
                {
                    const table_column& info { config.columns[column] };
                    // 트리 열의 들여쓰기와 삼각형이다. 잎도 삼각형 자리를 비워 글이 줄을 맞춘다.
                    if (config.tree_column.empty() == false && info.key == config.tree_column)
                    {
                        leads_[column] += static_cast<float>(item.depth) * list_indent_step + list_expander_width;
                        if (item.expansion != list_expansion::none && table.expand_message() != nullptr)
                            add_part(std::make_unique<tree_expander_element>(ui_element_id { ui_element_kind::table_expander, part_owner(config.owner, item.key) }, item.key,
                                         item.expansion == list_expansion::expanded, table.expand_message(), body),
                                column, part_kind::expander);
                    }
                    // 체크 칸이다. 눌러도 초점은 본문에 남는다 — 체크박스가 초점을 가져가면 그 뒤의 ↓가 표의 것이 아니다.
                    if (config.check_column.empty() == false && info.key == config.check_column && config.check_state_of != nullptr)
                    {
                        const check_state state { config.check_state_of(item, index) };
                        check_config check {};
                        check.owner = part_owner(config.owner, item.key);
                        check.checked = state == check_state::checked;
                        check.mixed = state == check_state::mixed;
                        // 라벨이 칸 밖(행)에 있으므로 보조 기술의 이름은 행 이름이다.
                        check.tooltip = item.label;
                        if (config.toggle_check != nullptr && item.enabled)
                            check.toggle = [key = item.key, toggle = config.toggle_check](const ui_action_context&) -> std::vector<input_action> { return { toggle(key) }; };
                        auto box { std::make_unique<check_element>(std::move(check)) };
                        box->set_enabled(item.enabled);
                        box->set_tab_stop(false);
                        box->set_pointer_focus_target(body);
                        check_lead_[column] = leads_[column];
                        leads_[column] += check_slot_width;
                        add_part(std::move(box), column, part_kind::check);
                    }
                    // 편집 중인 칸이면 그 자리에 편집기가 선다. 글과 앱의 칸은 그리지 않는다.
                    if (config.editing.has_value() && config.editing->row == item.key && config.editing->column == info.key && config.build_editor != nullptr)
                        if (std::unique_ptr<ui_element> editor { config.build_editor(item, index, info) }; editor != nullptr)
                        {
                            add_part(std::make_unique<table_editor_host_element>(ui_element_id { ui_element_kind::table_editor, part_owner(config.owner, item.key) }, std::move(editor),
                                         *config.editing, config.commit_edit, config.cancel_edit, body),
                                column, part_kind::cell);
                            continue;
                        }
                    std::unique_ptr<ui_element> cell { config.build_cell != nullptr ? config.build_cell(item, index, info, selected) : nullptr };
                    if (cell != nullptr)
                    {
                        add_part(std::move(cell), column, part_kind::cell);
                        continue;
                    }
                    if (config.cell_text != nullptr)
                        texts_[column] = config.cell_text(item, index, config.columns[column]);
                    else if (column == 0u)
                        // 글을 주지 않은 표는 첫 열에 행 이름을 보인다 (가상 목록의 기본 행과 같다).
                        texts_[column] = item.label;
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const std::span<const std::unique_ptr<ui_element>> parts { children() };
                const std::vector<table_column_span>& spans { table_->spans() };
                for (std::size_t index = 0; index < parts.size(); ++index)
                {
                    const part_place& place { places_[index] };
                    if (place.column >= spans.size())
                    {
                        parts[index]->arrange(context.for_child({}));
                        continue;
                    }
                    const rect_f cell { column_rect(context.slot, spans[place.column], table_->scroll_x(), scale) };
                    switch (place.kind)
                    {
                    case part_kind::expander:
                        parts[index]->arrange(context.for_child(
                            { cell.x + (table_cell_padding + static_cast<float>(depth_) * list_indent_step) * scale, cell.y, list_expander_width * scale, cell.height }));
                        break;
                    case part_kind::check: {
                        // 체크박스는 자기 줄 높이를 정한다. 행 높이와 다르면 가운데에 앉힌다.
                        const float height { check_element::height_for({}) * scale };
                        const float left { cell.x + (table_cell_padding - 4.0f + check_lead_.at(place.column)) * scale };
                        parts[index]->arrange(context.for_child({ left, cell.y + (cell.height - height) / 2.0f, check_slot_width * scale, height }));
                        break;
                    }
                    case part_kind::cell: {
                        const float lead { leads_[place.column] * scale };
                        parts[index]->arrange(context.for_child({ cell.x + lead, cell.y, std::max(0.0f, cell.width - lead), cell.height }));
                        break;
                    }
                    }
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const std::vector<table_column_span>& spans { table_->spans() };
                const std::vector<table_column>& columns { table_->config().columns };
                const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
                for (std::size_t column = 0; column < texts_.size() && column < spans.size(); ++column)
                {
                    rect_f cell { column_rect(box, spans[column], table_->scroll_x(), scale) };
                    const float lead { leads_[column] * scale };
                    cell.x += lead;
                    cell.width = std::max(0.0f, cell.width - lead);
                    draw_cell_text(context.canvas, texts_[column], cell, columns[column].align, scale, font, foreground);
                }
                context.canvas.restore();
                draw_children(context, interaction);
                draw_cell_cursor(context, interaction, scale);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                // 구조다. 행 자신(DataItem)은 가상 목록의 행이 낸다.
                return {};
            }

        private:
            enum class part_kind
            {
                cell,
                expander,
                check,
            };

            // 자식 하나가 선 열과 그 쓰임이다 (자식 순서 그대로).
            struct part_place
            {
                std::size_t column { 0 };
                part_kind kind { part_kind::cell };
            };

            // 셀 커서의 테다. 행의 커서 표시와 같은 규칙으로 **본문에 키보드 초점이 있을 때만** 선다.
            void draw_cell_cursor(draw_context& context, const interaction_snapshot& interaction, const float scale) const
            {
                const table_config& config { table_->config() };
                if (config.cell_column.empty() || key_ != table_->cursor_key() || interaction.focus_visible == false)
                    return;
                if (interaction.focused != ui_element_id { ui_element_kind::virtual_list, config.owner })
                    return;
                const std::vector<table_column_span>& spans { table_->spans() };
                for (std::size_t column = 0; column < config.columns.size() && column < spans.size(); ++column)
                {
                    if (config.columns[column].key != config.cell_column)
                        continue;
                    const rect_f cell { column_rect(bounds(), spans[column], table_->scroll_x(), scale) };
                    SkPaint outline { solid_paint(context.palette.accent) };
                    outline.setStyle(SkPaint::kStroke_Style);
                    outline.setStrokeWidth(2.0f * scale);
                    context.canvas.drawRect(SkRect::MakeXYWH(cell.x + scale, cell.y + scale, cell.width - 2.0f * scale, cell.height - 2.0f * scale), outline);
                    return;
                }
            }

            void add_part(std::unique_ptr<ui_element> part, const std::size_t column, const part_kind kind)
            {
                places_.push_back({ column, kind });
                add_child(std::move(part));
            }

            const table_element* table_ { nullptr };
            std::u8string key_ {};
            int depth_ { 0 };
            std::vector<std::u8string> texts_ {};
            // 열마다 글과 앱의 칸이 밀리는 거리다 (들여쓰기·삼각형·체크박스, 논리 픽셀).
            std::vector<float> leads_ {};
            // 체크박스가 서는 거리다 (트리 열과 겹치면 삼각형 뒤).
            std::unordered_map<std::size_t, float> check_lead_ {};
            std::vector<part_place> places_ {};
        };

        // 머리글의 부품을 누르면 초점이 본문 목록에 선다.
        // 자리가 아닌 것을 누르면 초점이 거둬지는데(`apply_press_focus`), 정렬하려고 머리글을 누른 뒤 ↓가 죽으면
        // 그것은 표가 아니라 그림이다. 가상 목록의 행이 같은 이유로 목록을 가리킨다.
        [[nodiscard]] ui_element_id body_id(const std::u8string& table)
        {
            return { ui_element_kind::virtual_list, table };
        }

        // 열의 오른쪽 경계를 끄는 손잡이다.
        //  - 변화량만 나른다 (`split_handle_element`와 같은 규약). 폭을 가진 것은 앱이다.
        //  - 최소 폭 아래로 가는 몫은 미리 잘라 낸다 (`table_resize_delta`).
        //  - Tab의 자리가 아니다. 열마다 자리가 서면 표 하나에 자리가 열 수만큼 는다.
        class table_column_grip_element final : public ui_element
        {
        public:
            table_column_grip_element(ui_element_id id, const std::u8string& table, const table_column& column, const table_resize_message_factory& resize,
                const table_column_message_factory& finish)
                : ui_element { std::move(id) }
                , body_ { body_id(table) }
                , header_ { column.header }
            {
                set_tab_stop(false);
                set_cursor(ui_cursor::resize_horizontal);
                pointer_drag_target target {};
                // 끄는 동안 머리글이 다시 배치되며 손잡이가 경계를 따라 옮겨 간다.
                target.follows_pointer = true;
                target.on_move = [this, column, resize](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
                    const float delta { table_resize_delta(column, (current.x - previous.x) / scale_) };
                    if (delta == 0.0f)
                        return {};
                    return { resize(column.key, delta) };
                };
                if (finish != nullptr)
                    target.on_release = [key = column.key, finish](const ui_action_context&, drag_velocity) -> std::vector<input_action> { return { finish(key) }; };
                set_pointer_drag_target(std::move(target));
            }

            [[nodiscard]] const ui_element_id& pointer_focus_target() const noexcept override
            {
                return body_;
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                scale_ = context.scale > 0.0f ? context.scale : 1.0f;
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                if (interaction.hovered != id() && interaction.pressed != id())
                    return;
                const rect_f box { bounds() };
                const float line { 2.0f * scale_ };
                context.canvas.drawRect(SkRect::MakeXYWH(box.x + (box.width - line) / 2.0f, box.y, line, box.height), solid_paint(context.palette.accent));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::handle, .name = header_ };
            }

        private:
            ui_element_id body_ {};
            std::u8string header_ {};
            float scale_ { 1.0f };
        };

        // 머리글 칸이다. 눌러서 정렬을 청하고, 지금 정렬 방향을 화살표로 보인다.
        class table_header_cell_element final : public ui_element
        {
        public:
            table_header_cell_element(ui_element_id id, const table_config& config, const table_column& column)
                : ui_element { std::move(id) }
                , body_ { body_id(config.owner) }
                , header_ { column.header }
                , align_ { column.align }
                , sort_ { column.sort }
            {
                set_tab_stop(false);
                const ui_element_id header { ui_element_kind::table_header, config.owner };
                // 머리글 칸을 끌어 다른 칸에 놓으면 열 순서를 바꾸자는 메시지다. 규칙은 탭·목록 행과 한 벌이다
                // (reorder_drop.h) — 같은 표의 머리글 칸만 받고, 자기 자리는 받지 않는다.
                if (const auto& reorder { config.reorder_column }; reorder != nullptr)
                {
                    const ui_element* const self { this };
                    drag_source source {};
                    source.make_payload = [self, key = column.key, label = column.header, header](const ui_action_context& context) {
                        drag_payload payload {};
                        payload.source = context.element;
                        payload.container = header;
                        payload.dragged_owner = key;
                        payload.label = label;
                        payload.grab_offset_x = context.x - self->bounds().x;
                        payload.grab_offset_y = context.y - self->bounds().y;
                        return payload;
                    };
                    set_drag_source(std::move(source));
                    drop_target drop {};
                    drop.accepts = [key = column.key, header](const drag_payload& payload) { return accepts_reorder_drop(payload, ui_element_kind::table_header_cell, header, key); };
                    drop.on_drop = [key = column.key, reorder](const drag_payload& payload, const ui_action_context&) -> std::vector<input_action> {
                        return { reorder(payload.dragged_owner, key) };
                    };
                    set_drop_target(std::move(drop));
                }
                if (const auto& menu { config.column_menu }; menu != nullptr)
                    set_action(ui_trigger::right_click,
                        [key = column.key, menu](const ui_action_context& context) -> std::vector<input_action> { return { menu(key, context) }; });
                if (column.sortable == false || config.sort_request == nullptr)
                    return;
                set_cursor(ui_cursor::hand);
                set_action(ui_trigger::left_click,
                    [key = column.key, sort_request = config.sort_request](const ui_action_context&) -> std::vector<input_action> { return { sort_request(key) }; });
            }

            [[nodiscard]] const ui_element_id& pointer_focus_target() const noexcept override
            {
                return body_;
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                if (action(ui_trigger::left_click) != nullptr)
                    draw_hover_fill(context, box, id(), interaction, enabled());

                // 열 사이의 세로 선이다. 끌 수 있는 경계가 어디인지 보인다.
                context.canvas.drawRect(SkRect::MakeXYWH(box.x + box.width - scale, box.y + 4.0f * scale, scale, box.height - 8.0f * scale), solid_paint(context.palette.divider));

                // 정렬 화살표는 칸의 끝에 선다. 그만큼 글 자리가 준다.
                rect_f text_box { box };
                if (sort_ != sort_direction::none)
                {
                    const float glyph { 14.0f * scale };
                    const rect_f mark { box.x + box.width - table_cell_padding * scale - glyph, box.y, glyph, box.height };
                    const SkFont glyph_font { sk_ref_sp(context.codicon_typeface), glyph };
                    draw_centered_glyph(context.canvas, sort_ == sort_direction::ascending ? codicons::icon_arrow_up : codicons::icon_arrow_down, mark, glyph_font,
                        solid_paint(context.palette.primary_foreground));
                    text_box.width -= glyph + 4.0f * scale;
                }
                const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.small_font_size, scale) };
                const SkPaint foreground { solid_paint(sort_ != sort_direction::none ? context.palette.primary_foreground : context.palette.secondary_foreground) };
                draw_cell_text(context.canvas, header_, text_box, align_, scale, font, foreground);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::header_item, .name = header_ };
            }

        private:
            ui_element_id body_ {};
            std::u8string header_ {};
            column_align align_ { column_align::start };
            sort_direction sort_ { sort_direction::none };
        };

        // 머리글 줄이다. 칸과 경계 손잡이를 열 자리에 앉힌다.
        class table_header_element final : public ui_element
        {
        public:
            explicit table_header_element(const table_element& table)
                : ui_element { ui_element_id { ui_element_kind::table_header, table.config().owner } }
                , table_ { &table }
            {
                set_clip_children(true);
                const table_config& config { table.config() };
                for (const table_column& column : config.columns)
                    add_child(std::make_unique<table_header_cell_element>(ui_element_id { ui_element_kind::table_header_cell, part_owner(config.owner, column.key) }, config, column));
                // 손잡이는 칸 **뒤에** 담는다. 경계에 절반씩 걸쳐 다음 칸 위에도 서므로, 나중에 그려진 것이 hit에서 이긴다.
                for (std::size_t index = 0; index < config.columns.size(); ++index)
                {
                    const table_column& column { config.columns[index] };
                    if (column.resizable == false || config.resize_column == nullptr)
                        continue;
                    grip_columns_.push_back(index);
                    add_child(std::make_unique<table_column_grip_element>(ui_element_id { ui_element_kind::table_column_grip, part_owner(config.owner, column.key) }, config.owner,
                        column, config.resize_column, config.resize_column_end));
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const std::vector<table_column_span>& spans { table_->spans() };
                const std::span<const std::unique_ptr<ui_element>> parts { children() };
                const std::size_t cells { spans.size() };
                for (std::size_t index = 0; index < cells && index < parts.size(); ++index)
                    parts[index]->arrange(context.for_child(column_rect(context.slot, spans[index], table_->scroll_x(), scale)));
                const float grip { table_grip_width * scale };
                for (std::size_t index = cells; index < parts.size(); ++index)
                {
                    const rect_f cell { column_rect(context.slot, spans[grip_columns_[index - cells]], table_->scroll_x(), scale) };
                    parts[index]->arrange(context.for_child({ cell.x + cell.width - grip / 2.0f, cell.y, grip, cell.height }));
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
                draw_children(context, interaction);
                // 머리글과 본문 사이의 선이다.
                context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + box.height - scale, box.width, scale), solid_paint(context.palette.divider));
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::header, .name = table_->access_name() };
            }

        private:
            const table_element* table_ { nullptr };
            // 손잡이(칸 뒤의 자식들) 하나하나가 선 열이다.
            std::vector<std::size_t> grip_columns_ {};
        };
    } // namespace

    std::vector<table_column_span> table_column_spans(const std::span<const table_column> columns)
    {
        std::vector<table_column_span> spans {};
        spans.reserve(columns.size());
        float begin { 0.0f };
        for (const table_column& column : columns)
        {
            const float width { column.width > 0.0f ? column.width : 0.0f };
            spans.push_back({ begin, width });
            begin += width;
        }
        return spans;
    }

    float table_content_width(const std::span<const table_column> columns) noexcept
    {
        float total { 0.0f };
        for (const table_column& column : columns)
            total += column.width > 0.0f ? column.width : 0.0f;
        return total;
    }

    float table_resize_delta(const table_column& column, const float delta) noexcept
    {
        // 지금 폭이 이미 최소보다 작으면(앱이 그렇게 주었다) 줄이는 쪽만 막는다.
        const float room { std::min(0.0f, column.min_width - column.width) };
        return std::max(delta, room);
    }

    float table_clamp_scroll(const float scroll_x, const float content_width, const float viewport_width) noexcept
    {
        const float maximum { std::max(0.0f, content_width - viewport_width) };
        return std::clamp(scroll_x, 0.0f, maximum);
    }

    table_element::table_element(table_config config)
        : ui_element { ui_element_id { ui_element_kind::table, config.owner } }
        , config_ { std::move(config) }
    {
        spans_ = table_column_spans(config_.columns);
        content_width_ = table_content_width(config_.columns);

        auto header { std::make_unique<table_header_element>(*this) };
        header_ = header.get();
        add_child(std::move(header));

        // 본문은 가상 목록이다. 표가 정하는 것은 owner·행 내용·역할뿐이다.
        //  - 행 모델은 옮긴다. 표는 열만 읽고 행은 목록이 읽는다 — 두 벌로 두면 큰 모델이 frame마다 두 번 복사된다.
        virtual_list_config rows { std::move(config_.rows) };
        config_.rows = {};
        expand_ = rows.set_expanded;
        rows.owner = config_.owner;
        // 계층은 트리 열 안에 그린다. 행 앞에 따로 자리를 두지 않는다.
        rows.tree_gutter = false;
        // 셀 탐색이 켜지면 ←·→는 표가 먼저 해석한다. 셀 커서가 트리 열에 있을 때만 계층 키로 넘긴다 (`move_cell`).
        rows.tree_keys = config_.move_column == nullptr;
        rows.role = access_role::table;
        rows.row_role = access_role::data_item;
        // 행 내용이 표의 열 자리를 읽는다. 표는 그 행들을 담은 본문을 소유하므로 행보다 먼저 사라지지 않는다.
        rows.build_row = [this](const virtual_list_item& item, const std::size_t index, const bool selected) -> std::unique_ptr<ui_element> {
            return std::make_unique<table_row_element>(*this, item, index, selected);
        };
        auto body { std::make_unique<virtual_list_element>(std::move(rows)) };
        body_ = body.get();
        add_child(std::move(body));

        const bool checks { config_.check_column.empty() == false && config_.toggle_check != nullptr };
        if (config_.scroll_horizontal != nullptr || checks || config_.move_column != nullptr || config_.begin_edit != nullptr)
            set_key_handler([this](const key_pressed_event& event) { return handle_key(event); });
    }

    std::optional<std::vector<input_action>> table_element::handle_key(const key_pressed_event& event) const
    {
        // 본문 목록이 쓰지 않은 키가 여기로 올라온다 (key-propagation-design.md).
        //  - ←·→는 셀 탐색이 켜졌으면 열 이동, 아니면 가로 스크롤이다. 계층 목록의 ←·→는 본문이 먼저 쓴다.
        //  - Space는 커서 행의 체크다. 본문이 Space를 실행 키로 쓰면(`rows.activate`) 여기까지 오지 않는다.
        //  - F2는 커서 칸의 편집이다.
        if (event.shortcut_modifier_down() || event.shift)
            return std::nullopt;
        if (event.key == key_code::f2)
            return start_edit();
        if (event.key == key_code::space && config_.check_column.empty() == false && config_.toggle_check != nullptr)
        {
            const std::optional<std::size_t>& cursor { body_->cursor() };
            if (cursor.has_value() == false || *cursor >= body_->model().size() || body_->model()[*cursor].enabled == false)
                return std::vector<input_action> {};
            return std::vector<input_action> { config_.toggle_check(body_->model()[*cursor].key) };
        }
        if (event.key != key_code::arrow_left && event.key != key_code::arrow_right)
            return std::nullopt;
        const bool forward { event.key == key_code::arrow_right };
        if (config_.move_column != nullptr)
            return move_cell(forward);
        if (config_.scroll_horizontal == nullptr)
            return std::nullopt;
        const float step { forward ? table_key_scroll_step : -table_key_scroll_step };
        const float delta { table_clamp_scroll(scroll_x_ + step, content_width_, viewport_width_) - scroll_x_ };
        // 끝에 닿았으면 키만 삼킨다 — 끝에서 →를 누를 때마다 앱 단축키가 터지지 않게 한다.
        if (delta == 0.0f)
            return std::vector<input_action> {};
        return std::vector<input_action> { config_.scroll_horizontal(delta) };
    }

    std::optional<std::vector<input_action>> table_element::move_cell(const bool forward) const
    {
        const std::optional<std::size_t> current { column_index(config_.cell_column) };
        // 트리 열에 선 셀 커서는 계층 키가 먼저다 (탐색기와 같다). 갈 곳이 없을 때만 옆 열로 간다.
        if (current.has_value() && config_.tree_column.empty() == false && config_.columns[*current].key == config_.tree_column)
            if (std::optional<std::vector<input_action>> tree { body_->step_tree(forward) }; tree.has_value())
                return tree;
        if (config_.columns.empty())
            return std::nullopt;
        std::size_t target { 0 };
        if (current.has_value())
        {
            // 끝에서는 키만 삼킨다 (가로 스크롤 키와 같은 규약).
            if ((forward && *current + 1u >= config_.columns.size()) || (forward == false && *current == 0u))
                return std::vector<input_action> {};
            target = forward ? *current + 1u : *current - 1u;
        }
        std::vector<input_action> actions { config_.move_column(config_.columns[target].key) };
        // 옮긴 칸이 창 밖이면 들인다. 셀 커서는 앱 상태라 다음 frame에 선다 — 들이는 것은 지금 알고 있는 열 자리로 한다.
        if (const float delta { reveal_delta(target) }; delta != 0.0f && config_.scroll_horizontal != nullptr)
            actions.push_back(config_.scroll_horizontal(delta));
        return actions;
    }

    std::optional<std::vector<input_action>> table_element::start_edit() const
    {
        if (config_.begin_edit == nullptr)
            return std::nullopt;
        const std::optional<std::size_t>& cursor { body_->cursor() };
        if (cursor.has_value() == false || *cursor >= body_->model().size() || body_->model()[*cursor].enabled == false)
            return std::nullopt;
        // 셀 커서가 있으면 그 열, 없으면 첫 편집 가능 열이다.
        std::optional<std::size_t> column { column_index(config_.cell_column) };
        if (column.has_value() == false)
            for (std::size_t index = 0; index < config_.columns.size(); ++index)
                if (config_.columns[index].editable)
                {
                    column = index;
                    break;
                }
        if (column.has_value() == false || config_.columns[*column].editable == false)
            return std::nullopt;
        std::vector<input_action> actions { config_.begin_edit({ body_->model()[*cursor].key, config_.columns[*column].key }) };
        if (const float delta { reveal_delta(*column) }; delta != 0.0f && config_.scroll_horizontal != nullptr)
            actions.push_back(config_.scroll_horizontal(delta));
        return actions;
    }

    float table_element::reveal_delta(const std::size_t column) const noexcept
    {
        if (column >= spans_.size())
            return 0.0f;
        const float left { spans_[column].begin };
        const float right { left + spans_[column].width };
        if (left < scroll_x_)
            return left - scroll_x_;
        if (right > scroll_x_ + viewport_width_)
            // 창보다 넓은 열은 시작을 맞춘다.
            return std::min(left, right - viewport_width_) - scroll_x_;
        return 0.0f;
    }

    std::optional<std::size_t> table_element::column_index(const std::u8string_view key) const noexcept
    {
        if (key.empty())
            return std::nullopt;
        for (std::size_t index = 0; index < config_.columns.size(); ++index)
            if (config_.columns[index].key == key)
                return index;
        return std::nullopt;
    }

    std::u8string_view table_element::cursor_key() const noexcept
    {
        // 앱이 커서를 비워 두면 본문이 고른 행이나 첫 행으로 정한다. 그 다듬은 값을 따른다.
        const std::optional<std::size_t>& cursor { body_->cursor() };
        if (cursor.has_value() == false || *cursor >= body_->model().size())
            return {};
        return body_->model()[*cursor].key;
    }

    float table_element::scroll_x() const noexcept
    {
        return scroll_x_;
    }

    float table_element::content_width() const noexcept
    {
        return content_width_;
    }

    const virtual_list_element& table_element::body() const noexcept
    {
        return *body_;
    }

    const table_config& table_element::config() const noexcept
    {
        return config_;
    }

    const std::vector<table_column_span>& table_element::spans() const noexcept
    {
        return spans_;
    }

    const std::function<input_action(const std::u8string& key, bool expanded)>& table_element::expand_message() const noexcept
    {
        return expand_;
    }

    void table_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        viewport_width_ = context.slot.width / scale;
        scroll_x_ = table_clamp_scroll(config_.scroll_x, content_width_, viewport_width_);

        // 가로 휠의 임자다. 세로 휠은 안쪽 본문이 가지고, 가로 휠은 축이 다른 본문을 지나 여기로 온다
        // (`route_wheel`의 이벤트 짝).
        if (config_.scroll_horizontal != nullptr)
        {
            scroll_source source {};
            source.axis = scroll_axis::horizontal;
            source.scale = scale;
            source.at_start = scroll_x_ <= 0.0f;
            source.at_end = scroll_x_ >= std::max(0.0f, content_width_ - viewport_width_);
            source.scroll = [scroll = config_.scroll_horizontal, current = scroll_x_, content = content_width_, viewport = viewport_width_](const float delta) {
                // 범위 밖으로 가는 몫은 잘라 낸다. 앱이 다듬지 않아도 머리글과 본문이 빈 자리로 흐르지 않는다.
                return scroll(table_clamp_scroll(current + delta, content, viewport) - current);
            };
            set_scroll_source(std::move(source));
        }

        const float header_height { std::min(table_header_height * scale, context.slot.height) };
        header_->arrange(context.for_child({ context.slot.x, context.slot.y, context.slot.width, header_height }));
        body_->arrange(context.for_child({ context.slot.x, context.slot.y + header_height, context.slot.width, context.slot.height - header_height }));
    }

    void table_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    access_info table_element::accessibility() const
    {
        // 구조다. 머리글(Header)과 본문(DataGrid)이 이 자리로 올라온다.
        return {};
    }
} // namespace luil
