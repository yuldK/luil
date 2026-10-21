#include "luil/ui/virtual_list_element.h"

#include "luil/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace luil {
    namespace {
        // 항목 하나가 차지하는 높이다 (논리 픽셀).
        // 0은 "없음"이라 기본값으로 물러선다 (`stack_item`과 같은 규칙).
        [[nodiscard]] float item_height(const virtual_list_item& item, const float row_height) noexcept
        {
            return item.height > 0.0f ? item.height : row_height;
        }

        // 그 항목이 키보드가 설 수 있는 자리인가.
        [[nodiscard]] bool selectable(const virtual_list_item& item) noexcept
        {
            return item.enabled;
        }

        // `from`에서 시작해 `direction` 방향으로 `count`칸 옮긴 활성 항목이다.
        // 끝에 닿으면 거기서 멈춘다 (돌지 않는다).
        [[nodiscard]] std::optional<std::size_t> stepped(const std::span<const virtual_list_item> items, const std::size_t from, const int direction, const int count) noexcept
        {
            std::optional<std::size_t> best {};
            std::size_t current { from };
            for (int moved = 0; moved < count; ++moved)
            {
                bool advanced { false };
                // 비활성 항목은 칸으로 세지 않는다 — 건너뛰는 것이지 멈추는 것이 아니다.
                while (true)
                {
                    if (direction < 0)
                    {
                        if (current == 0u)
                            break;
                        --current;
                    }
                    else
                    {
                        if (current + 1u >= items.size())
                            break;
                        ++current;
                    }
                    if (selectable(items[current]))
                    {
                        advanced = true;
                        break;
                    }
                }
                if (advanced == false)
                    break;
                best = current;
            }
            return best;
        }

        // 처음(또는 끝)의 활성 항목이다.
        [[nodiscard]] std::optional<std::size_t> edge_item(const std::span<const virtual_list_item> items, const bool front) noexcept
        {
            for (std::size_t step = 0; step < items.size(); ++step)
            {
                const std::size_t index { front ? step : items.size() - 1u - step };
                if (selectable(items[index]))
                    return index;
            }
            return std::nullopt;
        }

        // 행 하나다.
        //
        // 내용은 앱이 짓고 자리표·선택 표시·누름·글자 탐색 이름은 이 행이 쥔다.
        // 그 나눔이 요점이다 — 앱이 행을 통째로 만들면 앱마다 선택과 키보드를
        // 다시 짜고 그중 하나를 반드시 틀린다 (list-view-design.md).
        class virtual_row_element final : public ui_element
        {
        public:
            virtual_row_element(ui_element_id id, ui_element_id list, std::u8string label, const bool enabled, const bool selected, const bool cursor, const access_role role)
                : ui_element { std::move(id) }
                , list_ { std::move(list) }
                , label_ { std::move(label) }
                , role_ { role }
                , selected_ { selected }
                , cursor_ { cursor }
            {
                set_enabled(enabled);
                // 행은 Tab의 자리가 아니다. 자리는 목록 자신이다 — 창에 걸치지
                // 않는 행은 tree에 없으므로 자리가 될 수 없고, 걸치는 것만 자리가
                // 되면 자리의 수가 스크롤에 따라 달라진다.
                set_tab_stop(false);
                set_search_label(label_);
            }

            void set_content(std::unique_ptr<ui_element> content)
            {
                if (content == nullptr)
                    return;
                add_child(std::move(content));
            }

            [[nodiscard]] const ui_element_id& pointer_focus_target() const noexcept override
            {
                return list_;
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                for (const std::unique_ptr<ui_element>& child : children())
                    child->arrange(context.for_child(context.slot));
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect shape { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                // 함께 끌리는 행과 놓을 자리다 (`list_element`와 같은 규칙). 키는 목록 안에서만 유일하므로 출발 목록까지 견준다.
                const bool dragged { interaction.drag.has_value() && interaction.drag->payload.carries(list_, id().owner) };
                const bool drop_here { interaction.drag.has_value() && interaction.drag->payload.suppress_drop_highlight == false && interaction.drag->hovered_drop_target == id() };

                // 놓을 자리 · 고른 행 · 눌린 행 · hover의 순서다 (`list_element`와 같은 규칙).
                // 고름은 앱 상태라 포인터 상태보다 오래가고, 놓을 자리는 지금 끄는 손이 묻는 것이라 그보다도 앞선다.
                ui_color background { 0 };
                bool fill { false };
                if (drop_here)
                {
                    background = context.palette.drop_target_background;
                    fill = true;
                }
                else if (selected_)
                {
                    // 고른 행의 모양은 `list_element`와 같은 primitive 하나다.
                    // 끌려 나간 행은 제자리에 흐리게 남는다.
                    if (dragged)
                        context.canvas.saveLayerAlphaf(&shape, 0.35f);
                    draw_row_selection(context, box);
                    if (dragged)
                        context.canvas.restore();
                }
                else if (enabled() && interaction.pressed == id())
                {
                    background = context.palette.button_pressed_background;
                    fill = true;
                }
                else if (enabled() && interaction.hovered == id())
                {
                    background = context.palette.button_hover_background;
                    fill = true;
                }
                if (fill)
                {
                    SkPaint paint { solid_paint(background) };
                    // 고르지 않은 행 하나를 끌어도 제자리에 흐리게 남는다 (끄는 동안 hover가 그 행에 머문다).
                    if (dragged && drop_here == false)
                        paint.setAlphaf(0.35f);
                    context.canvas.drawRect(shape, paint);
                }

                // 커서는 **초점이 목록에 있을 때만** 보인다.
                // 초점 테는 `ui_tree`가 목록 전체에 두르므로, 그 안의 어느 행에
                // 서 있는지는 이 표시가 말한다 — 둘 다 테면 한 화면에서
                // 구별되지 않는다 (기본 버튼의 채움과 초점 테를 가른 것과 같은 판단이다).
                if (cursor_ && interaction.focus_visible && interaction.focused == list_)
                {
                    SkPaint outline { solid_paint(context.palette.accent) };
                    outline.setStyle(SkPaint::kStroke_Style);
                    outline.setStrokeWidth(scale);
                    const float inset { scale * 0.5f };
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x + inset, box.y + inset, box.width - scale, box.height - scale), outline);
                }

                // 앱이 내용을 짓지 않았으면 이름 한 줄이 기본 행이다.
                // 안쪽 여백은 `list_row_element`의 글 자리와 같은 값이라, 두 목록을
                // 나란히 놓아도 글의 왼쪽 끝이 맞는다.
                if (children().empty())
                {
                    const SkFont font { sk_ref_sp(context.ui_typeface), scaled_font_size(context.metrics.body_font_size, scale) };
                    const SkPaint foreground { solid_paint(enabled() ? context.palette.primary_foreground : context.palette.disabled_foreground) };
                    const float text_left { box.x + 8.0f * scale };
                    if (const float text_width { box.x + box.width - 8.0f * scale - text_left }; text_width > 0.0f)
                        static_cast<void>(draw_text_within(context.canvas, label_, text_left, box.y + centered_text_baseline(font, box.height), text_width, font, foreground));
                }

                draw_children(context, interaction);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = role_, .name = label_, .selected = selected_ };
            }

        private:
            // 이 행을 담은 목록의 자리표다.
            // 커서 표시는 **그 목록에 초점이 있을 때만** 선다 — kind만 견주면 한 화면에
            // 놓인 다른 가상 목록의 커서까지 함께 그려진다 (자리표는 언제나 전부를 견준다).
            ui_element_id list_ {};
            std::u8string label_ {};
            access_role role_ { access_role::list_item };
            bool selected_ { false };
            bool cursor_ { false };
        };

        // 지어진 행들을 모델 자리에 그대로 앉히는 레인이다.
        //
        // `stack_element`가 아니라 이것인 이유는 **비어 있는 앞뒤** 때문이다.
        // 스택은 자식을 차례로 쌓으므로 첫 자식이 언제나 맨 위에 서는데, 가상
        // 목록의 첫 자식은 3만 번째 행일 수 있다. 자리를 모델이 정하므로 레인은
        // 받은 slot의 원점에 각 행의 시작점을 더하기만 한다.
        class virtual_lane_element final : public ui_element
        {
        public:
            explicit virtual_lane_element(ui_element_id id) noexcept
                : ui_element { std::move(id) }
            {}

            void add_row(std::unique_ptr<ui_element> row, const virtual_list_span& span)
            {
                spans_.push_back(span);
                add_child(std::move(row));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const std::span<const std::unique_ptr<ui_element>> rows { children() };
                for (std::size_t index = 0; index < rows.size(); ++index)
                {
                    const virtual_list_span& span { spans_[index] };
                    rows[index]->arrange(context.for_child({ context.slot.x, context.slot.y + span.begin * scale, context.slot.width, span.length * scale }));
                }
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

        private:
            std::vector<virtual_list_span> spans_ {};
        };
    } // namespace

    float virtual_list_content_height(const std::span<const virtual_list_item> items, const float row_height) noexcept
    {
        float total { 0.0f };
        for (const virtual_list_item& item : items)
            total += item_height(item, row_height);
        return total;
    }

    virtual_list_span virtual_list_row_span(const std::span<const virtual_list_item> items, const float row_height, const std::size_t index) noexcept
    {
        if (index >= items.size())
            return {};
        float begin { 0.0f };
        for (std::size_t step = 0; step < index; ++step)
            begin += item_height(items[step], row_height);
        return { begin, item_height(items[index], row_height) };
    }

    virtual_list_range virtual_list_visible_range(
        const std::span<const virtual_list_item> items, const float row_height, const float scroll_offset, const float viewport_height, const int overscan) noexcept
    {
        if (items.empty() || viewport_height <= 0.0f)
            return {};
        const float top { scroll_offset > 0.0f ? scroll_offset : 0.0f };
        const float bottom { top + viewport_height };

        // 반열린 구간이라 경계에 정확히 닿은 행은 **다음** 행의 것이다
        // (`animation_frame_at`이 장 경계에 세운 규칙과 같다). 그러지 않으면
        // 창 높이가 행 높이의 배수일 때 언제나 한 줄을 더 짓는다.
        std::size_t begin { items.size() };
        std::size_t end { 0 };
        float offset { 0.0f };
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            const float height { item_height(items[index], row_height) };
            const float row_bottom { offset + height };
            // 높이가 0인 행은 어떤 창에도 걸치지 않는다 (그리는 것이 없다).
            if (height > 0.0f && row_bottom > top && offset < bottom)
            {
                if (index < begin)
                    begin = index;
                end = index + 1u;
            }
            offset = row_bottom;
        }
        if (begin >= end)
            return {};

        const std::size_t margin { overscan > 0 ? static_cast<std::size_t>(overscan) : 0u };
        begin = begin > margin ? begin - margin : 0u;
        end = end + margin < items.size() ? end + margin : items.size();
        return { begin, end };
    }

    std::optional<std::size_t> virtual_list_step_target(const std::span<const virtual_list_item> items, const std::optional<std::size_t> from, const value_step step, const int page_rows) noexcept
    {
        if (items.empty())
            return std::nullopt;
        if (step == value_step::minimum)
            return edge_item(items, true);
        if (step == value_step::maximum)
            return edge_item(items, false);

        // 서 있던 자리를 모르면 첫 활성 항목에서 시작한 것으로 본다.
        // 그러면 아직 아무 데도 서지 않은 목록에서 ↓ 한 번이 첫 항목을 집는다.
        const std::optional<std::size_t> start { from.has_value() && *from < items.size() ? from : edge_item(items, true) };
        if (start.has_value() == false)
            return std::nullopt;
        if (from.has_value() == false || *from >= items.size())
            return start;

        const int rows { page_rows > 0 ? page_rows : 1 };
        switch (step)
        {
        case value_step::decrease:
            return stepped(items, *start, -1, 1);
        case value_step::increase:
            return stepped(items, *start, 1, 1);
        case value_step::decrease_page:
            return stepped(items, *start, -1, rows);
        case value_step::increase_page:
            return stepped(items, *start, 1, rows);
        case value_step::minimum:
        case value_step::maximum:
            break;
        }
        return std::nullopt;
    }

    std::optional<std::size_t> virtual_list_search_target(
        const std::span<const virtual_list_item> items, const std::optional<std::size_t> from, const std::u8string_view query, const bool first) noexcept
    {
        if (items.empty() || query.empty())
            return std::nullopt;
        const std::size_t current { from.has_value() && *from < items.size() ? *from : 0u };
        const std::size_t start { first ? current + 1u : current };
        for (std::size_t step = 0; step < items.size(); ++step)
        {
            const std::size_t index { (start + step) % items.size() };
            const virtual_list_item& item { items[index] };
            if (selectable(item) && std::u8string_view { item.label }.starts_with(query))
                return index;
        }
        return std::nullopt;
    }

    virtual_list_element::virtual_list_element(virtual_list_config config)
        : ui_element(ui_element_id { ui_element_kind::virtual_list, config.owner })
        , config_(std::move(config))
    {
        content_height_ = virtual_list_content_height(model(), config_.row_height);

        // 커서는 앱이 이름 댄 항목이고, 없으면 고른 항목이며, 그것도 없으면
        // 첫 활성 항목이다. 세 단계가 한곳에 있어야 키보드·그리기·접근성이
        // 같은 행을 가리킨다.
        const std::u8string& wanted { config_.cursor.empty() ? config_.selected : config_.cursor };
        if (wanted.empty() == false)
            for (std::size_t index = 0; index < model().size(); ++index)
                if (model()[index].key == wanted)
                {
                    cursor_ = index;
                    break;
                }
        if (cursor_.has_value() == false)
            cursor_ = edge_item(model(), true);

        // 흘리기·막대·치수·휠은 영역이 한다.
        // 목록이 더하는 것은 **모델 위의 키보드**뿐이다.
        scroll_area_config area {};
        area.owner = config_.owner;
        area.content_height = content_height_;
        area.scroll_offset = config_.scroll_offset;
        area.scroll = config_.scroll;
        area.scroll_to = config_.scroll_to;
        area.bar = config_.bar;
        area.edges = config_.edges;
        // 막대는 Tab의 자리가 아니다. 목록 자신이 ↑/↓·Page·Home/End를 가진 자리라,
        // 막대까지 자리가 되면 같은 목록에 자리가 둘 선다 (`list_element`와 같은 판단).
        area.bar_tab_stop = false;
        auto scroll { std::make_unique<scroll_area_element>(std::move(area)) };
        area_ = scroll.get();
        auto lane { std::make_unique<virtual_lane_element>(ui_element_id { ui_element_kind::virtual_list_lane, config_.owner }) };
        lane_ = lane.get();
        area_->set_content(std::move(lane));
        add_child(std::move(scroll));

        // 목록이 Tab의 자리다 — 안의 행이 아니다.
        // `tab_stop`의 기본값("누를 수 있으면 자리")은 여기서 답이 될 수 없다.
        // 목록에는 누를 것이 없고, 누를 수 있는 행은 tree에서 나고 사라진다.
        set_tab_stop(moves_cursor() || config_.activate != nullptr);

        // Space·Enter가 커서 행을 실행한다.
        //
        // controller는 초점을 가진 element의 `left_click`을 그 두 키로 실행한다
        // (`process_activation_key`). 그래서 이 액션은 **키보드의 문**이고,
        // 포인터로는 닿지 않는다 — `hit_test`가 목록 자신을 답하지 않는다.
        if (config_.activate != nullptr)
            set_action(ui_trigger::left_click, [this](const ui_action_context&) -> std::vector<input_action> {
                if (cursor_.has_value() == false || *cursor_ >= model().size() || model()[*cursor_].enabled == false)
                    return {};
                return { config_.activate(model()[*cursor_].key) };
            });

        // Ctrl+A와 Ctrl+Space다. 둘 다 **목록의 키**라 초점이 행 안의 부품에 있어도 그 부품이 쓰지 않으면
        // 여기까지 올라온다 (key-propagation-design.md). 쓰지 않는 키는 nullopt로 흘린다.
        if (config_.multiple_selection && (config_.select_all != nullptr || config_.toggle_at_cursor != nullptr))
            set_key_handler([this](const key_pressed_event& event) -> std::optional<std::vector<input_action>> {
                if (event.alt || event.meta)
                    return std::nullopt;
                if (event.key == key_code::key_a && event.primary_shortcut_down() && event.shift == false && config_.select_all != nullptr)
                    return std::vector<input_action> { config_.select_all() };
                if (event.key == key_code::space && event.control && event.shift == false && config_.toggle_at_cursor != nullptr)
                {
                    // 커서가 없거나 비활성 항목이면 삼키기만 한다 — 그 자리에서 앱 단축키가 대신 실행되면 안 된다.
                    if (cursor_.has_value() == false || *cursor_ >= model().size() || model()[*cursor_].enabled == false)
                        return std::vector<input_action> {};
                    return std::vector<input_action> { config_.toggle_at_cursor(model()[*cursor_].key) };
                }
                return std::nullopt;
            });

        if (moves_cursor() == false)
            return;

        // 화살표·Page·Home/End다. 묶음보다 앞에 선 경로라 감싼 묶음이 있어도
        // 목록이 먼저 가진다 (`process_step_key`가 `process_group_key`보다 앞이다).
        //  - `move_with`가 있으면 수식 키의 뜻까지 받는다. Ctrl+화살표도 이 목록의 키가 된다.
        //  - `move`만 있으면 지금처럼 걸음만 받는다 (Shift는 보이지 않고 Ctrl+화살표는 앱 단축키다).
        key_step_target steps {};
        steps.axis = focus_axis::vertical;
        const auto step_to = [this](const value_step step, const virtual_list_move_context& context) -> std::optional<std::vector<input_action>> {
            const std::optional<std::size_t> target { virtual_list_step_target(model(), cursor_, step, page_rows(step)) };
            if (target.has_value() == false)
                return std::vector<input_action> {};
            return move_cursor(*target, context);
        };
        if (config_.move_with != nullptr)
            steps.on_step_with = [step_to](const value_step step, const key_step_context& keys) { return step_to(step, { step, keys.extend, keys.keep_selection }); };
        else
            steps.on_step = [step_to](const value_step step) { return step_to(step, { step, false, false }); };
        set_key_step_target(std::move(steps));

        // 글자 탐색이다. 질의를 잇고 끊는 규칙은 controller가 쥐고, 모델에서
        // 무엇이 맞는지는 목록이 답한다.
        //  - `first`(같은 글자를 거듭 쳐 도는가)도 controller가 준다. 질의의 길이로
        //    되짚으면 UTF-8에서 한글 한 글자가 세 byte라 첫 글자부터 "이어 친 글자"가
        //    되고, 같은 글자를 거듭 쳐도 후보가 돌지 않는다.
        key_search_target search {};
        search.on_search = [this](const std::u8string_view query, const bool first) -> std::optional<std::vector<input_action>> {
            const std::optional<std::size_t> target { virtual_list_search_target(model(), cursor_, query, first) };
            if (target.has_value() == false)
                return std::nullopt;
            return move_cursor(*target, {});
        };
        set_key_search_target(std::move(search));
    }

    int virtual_list_element::page_rows(const value_step step) const noexcept
    {
        // 커서가 선 자리에서 창 하나에 들어가는 줄 수다.
        //
        // **기본 높이로 나누지 않는다.** 높이가 섞인 모델(`virtual_list_item::height`)에서
        // 그렇게 재면 200 픽셀짜리 행들이 늘어선 자리에서 Page 한 번이 다섯 화면을
        // 건너뛴다 — 화면 하나만큼 움직인다는 Page의 뜻이 지켜지지 않는다.
        // 커서에서부터 실제 높이를 더해 가며 세면 뜻이 그대로 남는다.
        //  - 배치 전에는 창을 모르므로 한 줄로 본다. 키가 멈추지는 않는다.
        if (viewport_height_ <= 0.0f || model().empty() || (step != value_step::decrease_page && step != value_step::increase_page))
            return 1;
        float used { 0.0f };
        int rows { 0 };
        std::size_t index { cursor_.value_or(0u) };
        const bool backwards { step == value_step::decrease_page };
        while (true)
        {
            // 행 시작점 사이의 거리를 센다. 위로 갈 때는 이전 행의 높이다.
            if (backwards)
            {
                if (index == 0u)
                    break;
                --index;
                used += item_height(model()[index], config_.row_height);
            }
            else
            {
                if (index + 1u >= model().size())
                    break;
                used += item_height(model()[index], config_.row_height);
                ++index;
            }
            if (used > viewport_height_)
                break;
            // stepped()도 활성 행만 이동 칸으로 센다. 높이는 모든 행이 차지한다.
            if (model()[index].enabled)
                ++rows;
        }
        return rows > 0 ? rows : 1;
    }

    bool virtual_list_element::moves_cursor() const noexcept
    {
        return config_.move != nullptr || config_.move_with != nullptr;
    }

    std::vector<input_action> virtual_list_element::move_cursor(const std::size_t index, const virtual_list_move_context& context) const
    {
        if (moves_cursor() == false || index >= model().size())
            return {};
        std::vector<input_action> actions {};
        // **이미 그 자리면 커서 메시지를 내지 않는다.** Home을 첫 항목에서 누르면
        // 옮길 것이 없는데도 앱이 깨어나 tree를 통째로 다시 짓는다 — 아래 스크롤의
        // 0 방벽과 같은 이유다. 다만 키는 여전히 목록이 가지고(빈 목록도 "삼켰다"이다),
        // **화면은 들인다** — 커서는 제자리여도 스크롤로 밀려 나가 있을 수 있다.
        //  - `move_with`는 예외다. 커서가 제자리여도 고른 것이 바뀌는 걸음(그냥·Shift)이 있으므로 낸다. 커서만
        //    옮기는 Ctrl 걸음만 제자리에서 멈춘다.
        const bool moved { cursor_.has_value() == false || *cursor_ != index };
        if (config_.move_with != nullptr)
        {
            if (moved || context.keep_selection == false || context.extend)
                actions.push_back(config_.move_with(model()[index].key, context));
        }
        else if (moved)
            actions.push_back(config_.move(model()[index].key));

        // 옮긴 자리를 함께 화면 안으로 들인다.
        //
        // **되살리기를 앱에 미루지 않는다.** `on_focus_moved`는 초점이 옮겨 갔을
        // 때 불리는데, 여기서 초점은 목록에 그대로 서 있다 — 옮겨 간 것은 앱
        // 상태인 커서뿐이라 그 계기가 아예 오지 않는다. 얼마나 흘릴지를 아는
        // 것은 모델과 창 높이를 함께 쥔 이쪽뿐이다.
        if (config_.scroll == nullptr)
            return actions;
        const virtual_list_span span { virtual_list_row_span(model(), config_.row_height, index) };
        // 지금 흘러간 자리는 **다듬은 값**이다. 앱이 준 원값으로 재면 범위를 벗어난
        // 상태에서 키를 눌렀을 때 화면에 보이는 것과 다른 자리를 기준으로 삼는다.
        const float offset { area_->metrics().scroll_offset };
        const float delta { luil::scroll_delta_to_reveal(span.begin, span.length, offset, viewport_height_) };
        // 이미 보이면 메시지를 내지 않는다 — 0짜리 스크롤이 logic을 깨워 tree를
        // 통째로 다시 짓는다 (`route_reveal`의 방벽과 같은 자리).
        if (delta != 0.0f)
            actions.push_back(config_.scroll(delta));
        return actions;
    }

    float virtual_list_element::content_height() const noexcept
    {
        return content_height_;
    }

    const scroll_metrics& virtual_list_element::metrics() const noexcept
    {
        return area_->metrics();
    }

    const virtual_list_range& virtual_list_element::realized() const noexcept
    {
        return realized_;
    }

    const std::optional<std::size_t>& virtual_list_element::cursor() const noexcept
    {
        return cursor_;
    }

    const std::vector<virtual_list_item>& virtual_list_element::model() const noexcept
    {
        // 공유 모델이 있으면 그것이다. 모델을 읽는 자리는 전부 여기를 지난다 — 한 자리라도 `items`를 직접 읽으면
        // 공유 모델을 준 목록에서 그 자리만 빈 모델을 본다.
        return config_.shared_items != nullptr ? *config_.shared_items : config_.items;
    }

    float virtual_list_element::scroll_delta_to_reveal(const rect_f& target) const
    {
        // 커스텀 행 안의 컨트롤이 초점을 받았을 때의 되살리기다.
        // 안쪽 영역이 답한다 (그쪽이 다시 안쪽 창에 넘긴다).
        return area_->scroll_delta_to_reveal(target);
    }

    const ui_element* virtual_list_element::hit_test(const float x, const float y) const
    {
        // 자식만 본다 — 목록 자신은 답이 아니다.
        //
        // 기본 구현은 자식을 먼저 훑고 자기 bounds를 검사하는데, `activate`가 세운
        // `left_click` 때문에 목록이 `interactive()`가 되어 그 검사가 목록을 답한다.
        // 그러면 행 아래 빈 자리를 누른 것이 **커서 행의 실행**이 된다 — 누른 자리와
        // 실행된 자리가 다른, 설명할 수 없는 클릭이다. 그 액션은 키보드의 문이지
        // 포인터의 문이 아니다.
        //
        // **숨긴 목록은 답하지 않는다.** 기본 구현의 첫 줄이 그 검사인데, 자식
        // 훑기만 가져오면서 함께 빠지기 쉽다. 부모는 보이는지 묻지 않고 자식의
        // `hit_test`를 부르므로, 빠뜨리면 접힌 탭의 목록이 그려지지도 않은 채 눌린다.
        if (visible() == false)
            return nullptr;
        const std::span<const std::unique_ptr<ui_element>> parts { children() };
        for (std::size_t index = parts.size(); index > 0u; --index)
            if (const ui_element* const found { parts[index - 1u]->hit_test(x, y) }; found != nullptr)
                return found;
        return nullptr;
    }

    void virtual_list_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        viewport_height_ = context.slot.height / scale;

        // 지을 행을 정하는 것은 **배치 시점**이다. 창 높이를 알아야 어디까지
        // 걸치는지 답할 수 있고, 창 높이는 slot이 정해져야 안다.
        //  - `arrange`가 자식을 만드는 유일한 자리다. tree는 아직 게시되지
        //    않았으므로 만드는 것이 허용된다 (`tab_bar_element`가 넘침 버튼의
        //    보임을 자기 arrange에서 정하는 것과 같은 걸음이다).
        // 행은 **높이가 있는 첫 배치**에서 한 번만 짓는다.
        //  - 자식은 tree 하나에 한 벌이라 두 번 지으면 같은 자리표가 둘 선다.
        //  - 높이가 0인 배치를 계기로 삼지 않는 이유는, 담는 쪽이 자리를 잡기
        //    전에 한 번 0으로 배치해 볼 수 있어서다. 그때 지으면 아무 행도 걸치지
        //    않아 빈 목록이 굳는다.
        if (realized_built_ == false && context.slot.height > 0.0f)
        {
            realized_built_ = true;
            const float offset { clamp_scroll(content_height_, viewport_height_, config_.scroll_offset) };
            realized_ = virtual_list_visible_range(model(), config_.row_height, offset, viewport_height_, config_.overscan);
            build_rows();
        }
        // 이름은 안쪽 영역을 지나 막대까지 이어진다 (`list_element`와 같은 줄).
        area_->set_access_name(access_name());
        area_->arrange(context.for_child(context.slot));
    }

    void virtual_list_element::build_rows()
    {
        auto* const lane { static_cast<virtual_lane_element*>(lane_) };
        // 다중 선택의 키들이다. 모델 전체가 골라져 있어도 지어지는 행만 묻는다.
        std::unordered_set<std::u8string_view> chosen {};
        if (config_.multiple_selection)
            chosen.insert(config_.selection.begin(), config_.selection.end());
        const auto realize = [this, lane, &chosen](const std::size_t index, const float begin) {
            const virtual_list_item& item { model()[index] };
            const bool selected { item.enabled && (item.key == config_.selected || chosen.contains(item.key)) };
            const bool at_cursor { cursor_.has_value() && *cursor_ == index };
            auto row { std::make_unique<virtual_row_element>(ui_element_id { ui_element_kind::virtual_list_row, item.key }, id(), item.label, item.enabled, selected, at_cursor, config_.row_role) };
            if ((config_.select != nullptr || config_.pick != nullptr || moves_cursor()) && item.enabled)
            {
                // 누른 행은 **고르는 것이자 커서가 서는 곳**이다.
                //
                // 커서를 함께 옮기지 않으면, 누른 다음 누른 화살표가 눌린 행이 아니라
                // 커서가 있던 옛 자리에서 출발한다 — 화면에서는 "방금 누른 곳에서
                // 한 칸"으로 보여야 하는 몸짓이 엉뚱한 데로 뛴다. 그것을 앱이 자기
                // `select` 처리에서 챙기게 두면 반드시 한 앱이 빠뜨린다.
                //  - 이미 커서가 선 행이면 커서 메시지를 내지 않는다 (`move_cursor`의
                //    방벽과 같은 이유).
                //  - **factory를 값으로 담는다.** 지역 참조를 잡으면 그 참조가 죽은 뒤에
                //    액션이 실행되고(행은 tree가 사는 동안 산다), 목록을 가리키는 `this`를
                //    잡으면 행이 목록보다 오래 살 수 없다는 사실에 기대게 된다.
                //  - `move_with`만 든 목록은 커서만 옮기는 문맥(`keep_selection`)으로 낸다. 고르기는 위의 `pick`·`select`가
                //    이미 정했다.
                row->set_action(ui_trigger::left_click,
                    [key = item.key, select = config_.select, pick = config_.pick, move = at_cursor ? virtual_list_message_factory {} : config_.move,
                        move_with = at_cursor || config_.move != nullptr ? virtual_list_move_with_factory {} : config_.move_with](
                        const ui_action_context& context) -> std::vector<input_action> {
                        std::vector<input_action> actions {};
                        if (pick != nullptr)
                            actions.push_back(pick(key, context));
                        else if (select != nullptr)
                            actions.push_back(select(key));
                        if (move != nullptr)
                            actions.push_back(move(key));
                        else if (move_with != nullptr)
                            actions.push_back(move_with(key, virtual_list_move_context { std::nullopt, false, true }));
                        return actions;
                    });
            }
            // 두 번 누름은 그 행의 키로 앱에 간다. 누름과 같은 까닭으로 factory를 값으로 담는다.
            if (config_.double_click != nullptr && item.enabled)
                row->set_action(ui_trigger::double_click, [key = item.key, double_click = config_.double_click](const ui_action_context&) -> std::vector<input_action> {
                    return { double_click(key) };
                });
            // 우클릭은 그 행의 키와 문맥으로 앱에 간다. 누름과 같은 까닭으로 factory를 값으로 담는다.
            if (config_.context_menu != nullptr && item.enabled)
                row->set_action(ui_trigger::right_click,
                    [key = item.key, context_menu = config_.context_menu](const ui_action_context& context) -> std::vector<input_action> { return { context_menu(key, context) }; });
            // 행을 끌고 행에 놓는 것은 앱의 factory다 (여러 항목을 함께 끄는 drag & drop).
            if (config_.drag != nullptr && item.enabled)
            {
                drag_source source {};
                source.make_payload = [key = item.key, list = id(), drag = config_.drag](const ui_action_context& context) {
                    drag_payload payload { drag(key, context) };
                    if (payload.source == ui_element_id {})
                        payload.source = context.element;
                    // 출발 목록이다. 받는 쪽이 같은 키의 다른 목록 행과 가른다 (`drag_payload::carries`).
                    if (payload.container == ui_element_id {})
                        payload.container = list;
                    return payload;
                };
                row->set_drag_source(std::move(source));
            }
            if (config_.accepts_drop != nullptr && config_.drop != nullptr && item.enabled)
                row->set_drop_target(drop_target {
                    [key = item.key, accepts = config_.accepts_drop](const drag_payload& payload) { return accepts(key, payload); },
                    [key = item.key, drop = config_.drop](const drag_payload& payload, const ui_action_context& context) -> std::vector<input_action> { return { drop(key, payload, context) }; },
                });
            // 다중 선택의 더하기·빼기는 앱의 메시지다. 고르기는 행의 누름(기본 실행)이 맡는다.
            //  - 앱이 그 메시지를 주지 않았으면 `pick`의 Ctrl+누름으로 대신한다. 포인터로 하나 더 고르거나 빼는 몸짓이
            //    그것이라, 보조 기술도 같은 길을 탄다. 그냥 누름(이것 하나만)으로는 대신하지 않는다.
            //  - 이미 그 상태인 명령은 `plan_access_request`가 먼저 거르므로 Ctrl+누름의 뒤집기가 목표 상태와 어긋나지
            //    않는다 — 고르지 않은 행에만 더하기가, 고른 행에만 빼기가 온다.
            //  - 둘 다 없으면 지금처럼 무효다.
            const virtual_list_message_factory add { config_.add_to_selection };
            const virtual_list_message_factory remove { config_.remove_from_selection };
            const virtual_list_pick_factory pick { config_.pick };
            if (config_.multiple_selection && item.enabled && (add != nullptr || remove != nullptr || pick != nullptr))
                row->set_access_handler([key = item.key, row_id = row->id(), add, remove, pick](const access_request& request) -> std::optional<std::vector<input_action>> {
                    const bool adding { request.command == access_command::add_to_selection };
                    if (adding == false && request.command != access_command::remove_from_selection)
                        return std::nullopt;
                    if (adding && add != nullptr)
                        return std::vector<input_action> { add(key) };
                    if (adding == false && remove != nullptr)
                        return std::vector<input_action> { remove(key) };
                    if (pick == nullptr)
                        return std::nullopt;
                    // 보조 기술이 시킨 것이라 좌표는 없다.
                    ui_action_context context { row_id };
                    context.control = true;
                    return std::vector<input_action> { pick(key, context) };
                });
            // 기본 행의 글은 행이 **직접** 그린다.
            // `label_element`를 자식으로 두면 그 자식에게도 자리표가 필요한데, 항목
            // 키로 지으면 같은 키를 보여 주는 두 목록에서 겹치고(`duplicate_ids`),
            // 비워 두면 모든 행의 글이 한 자리표를 나눠 갖는다. 글 한 줄을 그리려고
            // 자리표 문제를 만들 이유가 없다 (`list_row_element`도 같은 자리에서
            // 글을 직접 그린다).
            if (config_.build_row != nullptr)
                row->set_content(config_.build_row(item, index, selected));
            lane->add_row(std::move(row), { begin, item_height(item, config_.row_height) });
        };

        // 창에 걸치지 않아도 짓는 행이다.
        //  - 커서 행: 보조 기술이 "지금 어디에 서 있는가"를 물었을 때 답할 자리가 있어야 하고, 커스텀 행 안의
        //    컨트롤이 스크롤 한 번에 사라지지 않는다.
        //  - 고른 행: 보조 기술의 선택 읽기(`GetSelection`)와 선택 알림은 tree에 선 행만 본다. 창 밖의 고른 행이
        //    없으면 고른 것을 셋 두고 흘린 순간 하나만 골라진 목록으로 읽힌다. 창에 가까운 것부터
        //    `virtual_list_offscreen_selection_limit`개까지다 — 모두 고르기에 행이 모델만큼 지어지지 않게 한다.
        const bool outside_window_cursor { cursor_.has_value() && (*cursor_ < realized_.begin || *cursor_ >= realized_.end) };
        std::vector<std::size_t> offscreen {};
        if (outside_window_cursor)
            offscreen.push_back(*cursor_);
        if (config_.selected.empty() == false || chosen.empty() == false)
        {
            std::vector<std::size_t> selected {};
            for (std::size_t index = 0; index < model().size(); ++index)
            {
                if (index >= realized_.begin && index < realized_.end)
                    continue;
                if (outside_window_cursor && index == *cursor_)
                    continue;
                const virtual_list_item& item { model()[index] };
                if (item.enabled && (item.key == config_.selected || chosen.contains(item.key)))
                    selected.push_back(index);
            }
            if (selected.size() > virtual_list_offscreen_selection_limit)
            {
                const auto distance = [this](const std::size_t index) { return index < realized_.begin ? realized_.begin - index : index + 1u - realized_.end; };
                const auto nearer = [&distance](const std::size_t left, const std::size_t right) { return distance(left) < distance(right); };
                std::nth_element(selected.begin(), selected.begin() + static_cast<std::ptrdiff_t>(virtual_list_offscreen_selection_limit), selected.end(), nearer);
                selected.resize(virtual_list_offscreen_selection_limit);
            }
            offscreen.insert(offscreen.end(), selected.begin(), selected.end());
        }
        std::sort(offscreen.begin(), offscreen.end());

        // 행은 모델 차례로 짓는다 — 보조 기술이 형제를 따라 읽는 차례가 화면의 차례와 같다.
        // 앞부분의 합은 한 번만 더한다. 매 행마다 0부터 더하면 목록 끝의 수십 행을 짓는 비용이 제곱으로 는다.
        const std::size_t last { std::max(realized_.end, offscreen.empty() ? std::size_t { 0 } : offscreen.back() + 1u) };
        auto next_offscreen { offscreen.begin() };
        float begin { 0.0f };
        for (std::size_t index = 0; index < last; ++index)
        {
            const bool in_window { index >= realized_.begin && index < realized_.end };
            if (in_window || (next_offscreen != offscreen.end() && *next_offscreen == index))
                realize(index, begin);
            if (next_offscreen != offscreen.end() && *next_offscreen == index)
                ++next_offscreen;
            begin += item_height(model()[index], config_.row_height);
        }
    }

    void virtual_list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 목록 자체는 바탕을 칠하지 않는다.
        // 표면 색과 모서리는 담는 쪽(panel)의 몫이다 (`list_element`와 같다).
        draw_children(context, interaction);
    }

    access_info virtual_list_element::accessibility() const
    {
        access_info info { .role = config_.role, .name = access_name() };
        info.multiple_selection = config_.multiple_selection;
        // 커서가 선 항목의 이름을 목록의 "지금 값"으로 답한다.
        // 형제 순회는 지어진 행 안이라, 창 밖의 항목은 그 순회로 닿지 않는다 —
        // 그래서 지금 어디에 서 있는지는 목록 자신이 말해야 한다.
        if (cursor_.has_value() && *cursor_ < model().size())
            info.value = model()[*cursor_].label;
        return info;
    }
} // namespace luil
