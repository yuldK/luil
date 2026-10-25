#include "luil/ui/ui_element.h"

#include "luil/ui/ui_tree.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace luil {
    const ui_element_id& ui_element::pointer_focus_target() const noexcept
    {
        return pointer_focus_target_ != ui_element_id {} ? pointer_focus_target_ : id();
    }

    void ui_element::set_pointer_focus_target(ui_element_id target)
    {
        pointer_focus_target_ = std::move(target);
    }

    const ui_style& arrange_context::app_style() const noexcept
    {
        return style != nullptr ? *style : default_ui_style();
    }

    bool rect_f::contains(const float point_x, const float point_y) const noexcept
    {
        return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
    }

    std::vector<rect_f> bands_outside(const rect_f& outer, const rect_f& inner)
    {
        const float outer_right { outer.x + outer.width };
        const float outer_bottom { outer.y + outer.height };
        const float left { std::clamp(inner.x, outer.x, outer_right) };
        const float right { std::clamp(inner.x + inner.width, left, outer_right) };
        const float top { std::clamp(inner.y, outer.y, outer_bottom) };
        const float bottom { std::clamp(inner.y + inner.height, top, outer_bottom) };
        std::vector<rect_f> bands {};
        const auto keep = [&bands](const rect_f& band) {
            if (band.width > 0.0f && band.height > 0.0f)
                bands.push_back(band);
        };
        keep({ outer.x, outer.y, outer.width, top - outer.y });
        keep({ outer.x, bottom, outer.width, outer_bottom - bottom });
        keep({ outer.x, top, left - outer.x, bottom - top });
        keep({ right, top, outer_right - right, bottom - top });
        return bands;
    }

    interaction_snapshot interaction_for_surface(interaction_snapshot view, const std::u8string& surface)
    {
        // 값마다 자기 표식이 있고, 표식이 이 표면을 가리키지 않으면 그 값은
        // 여기서 일어난 일이 아니다. **값과 표식을 함께 비운다** — 표식만 남으면
        // 거른 결과가 그 자체로는 옳지 않은 snapshot이 된다.
        if (view.tooltip.has_value() && view.tooltip->surface != surface)
            view.tooltip.reset();
        if (view.hovered_surface != surface)
        {
            view.hovered = {};
            view.hovered_surface.clear();
            // tooltip 지연의 기준 시각과 놓을 자리도 그 hover의 것이다.
            view.hover_started_at.reset();
            view.hover_point.reset();
        }
        if (view.pressed_surface != surface)
        {
            view.pressed = {};
            view.pressed_surface.clear();
        }
        if (view.focused_surface != surface)
        {
            // `focused_input`은 초점이 텍스트 박스일 때의 같은 값이라 함께 간다.
            // caret 깜빡임의 위상과 초점 테도 그 초점에 매인 것이다.
            view.focused = {};
            view.focused_input = {};
            view.focused_surface.clear();
            view.focus_started_at.reset();
            view.focus_visible = false;
        }
        if (view.menu_surface != surface)
        {
            view.menu_highlight = {};
            view.menu_surface.clear();
        }
        // 끌기의 표식은 `drag_visual` 안에 있어 값과 함께 사라진다.
        //  - 끌기는 표면 둘에 걸친다. 출발 표면은 끌리는 항목의 흐린 표시를, 포인터가 놓인 표면은 ghost와 강조를
        //    그린다. 둘 다 아닌 표면에는 없다.
        //  - 포인터가 이 표면 위에 없으면 그 자리의 것(좌표·강조)을 지우고 `outside`로 알린다. 남겨 두면 다른 창
        //    좌표의 ghost가 이 창의 같은 자리에 그려지고, 다른 tree의 id가 이 tree의 같은 id를 강조한다.
        if (view.drag.has_value())
        {
            drag_visual& drag { *view.drag };
            const bool origin { drag.surface == surface };
            const bool pointer { drag.pointer_on() == surface && drag.outside == false };
            if (origin == false && pointer == false)
                view.drag.reset();
            else if (pointer == false)
            {
                drag.pointer_surface.reset();
                drag.outside = true;
                drag.hovered_drop_target = {};
            }
        }
        return view;
    }

    ui_element::ui_element(ui_element_id id) noexcept
        : id_ { std::move(id) }
    {}

    const ui_element_id& ui_element::id() const noexcept
    {
        return id_;
    }

    const rect_f& ui_element::bounds() const noexcept
    {
        return bounds_;
    }

    bool ui_element::arranged() const noexcept
    {
        return arranged_;
    }

    bool ui_element::enabled() const noexcept
    {
        return enabled_;
    }

    bool ui_element::visible() const noexcept
    {
        return visible_;
    }

    const std::u8string& ui_element::tooltip() const noexcept
    {
        return tooltip_;
    }

    const tooltip_config* ui_element::tooltip_content() const noexcept
    {
        if (tooltip_content_ == nullptr)
            return nullptr;
        const ui_tree* const content { tooltip_content_->content.get() };
        if (content == nullptr || content->root() == nullptr)
            return nullptr;
        const ui_element& root { *content->root() };
        const rect_f box { root.bounds() };
        return root.visible() && root.arranged() && std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height) && box.width > 0.0f && box.height > 0.0f
            ? tooltip_content_.get()
            : nullptr;
    }

    bool ui_element::has_tooltip() const noexcept
    {
        return tooltip_.empty() == false || tooltip_content() != nullptr;
    }

    const std::u8string& ui_element::search_label() const noexcept
    {
        return search_label_;
    }

    const std::u8string& ui_element::access_name() const noexcept
    {
        // 화면에 뜨지 않는 이름이 먼저다 — 툴팁은 눈으로 읽는 글이라 컨테이너에
        // 달면 그 위에 글 상자가 뜬다 (헤더의 설명).
        if (access_name_.empty() == false)
            return access_name_;
        return search_label_.empty() ? tooltip_ : search_label_;
    }

    const ui_action* ui_element::action(const ui_trigger trigger) const noexcept
    {
        const ui_action& stored { actions_[static_cast<std::size_t>(trigger)] };
        return stored ? &stored : nullptr;
    }

    const drag_source* ui_element::drag() const noexcept
    {
        return drag_source_.has_value() ? &*drag_source_ : nullptr;
    }

    const drop_target* ui_element::drop() const noexcept
    {
        return drop_target_.has_value() ? &*drop_target_ : nullptr;
    }

    const pointer_drag_target* ui_element::pointer_drag() const noexcept
    {
        return pointer_drag_target_.has_value() ? &*pointer_drag_target_ : nullptr;
    }

    const key_step_target* ui_element::key_step() const noexcept
    {
        return key_step_target_.has_value() ? &*key_step_target_ : nullptr;
    }

    const zoom_source* ui_element::zoom() const noexcept
    {
        return zoom_source_.has_value() ? &*zoom_source_ : nullptr;
    }

    void ui_element::set_zoom_source(std::optional<zoom_source> source)
    {
        zoom_source_ = std::move(source);
    }

    const scroll_source* ui_element::scroll() const noexcept
    {
        return scroll_source_.has_value() ? &*scroll_source_ : nullptr;
    }

    const pan_handoff_target* ui_element::pan_handoff() const noexcept
    {
        return pan_handoff_.has_value() ? &*pan_handoff_ : nullptr;
    }

    void ui_element::set_pan_handoff(std::optional<pan_handoff_target> target)
    {
        pan_handoff_ = std::move(target);
    }

    const key_search_target* ui_element::key_search() const noexcept
    {
        return key_search_target_.has_value() ? &*key_search_target_ : nullptr;
    }

    const key_capture_target* ui_element::key_capture() const noexcept
    {
        return key_capture_target_.has_value() && key_capture_target_->on_key != nullptr ? &*key_capture_target_ : nullptr;
    }

    const key_handler* ui_element::key_handler_of() const noexcept
    {
        return key_handler_ != nullptr ? &key_handler_ : nullptr;
    }

    bool ui_element::interactive() const noexcept
    {
        if (has_tooltip() || cursor_ != ui_cursor::inherit || drag_source_.has_value() || drop_target_.has_value() || pointer_drag_target_.has_value())
            return true;
        for (const ui_action& stored : actions_)
            if (stored)
                return true;
        return false;
    }

    std::span<const std::unique_ptr<ui_element>> ui_element::children() const noexcept
    {
        return children_;
    }

    void ui_element::set_bounds(const rect_f& bounds) noexcept
    {
        bounds_ = bounds;
        // 모든 `arrange` 구현이 지나는 한 곳이라 여기서 표시를 세운다.
        arranged_ = true;
    }

    void ui_element::set_enabled(const bool value) noexcept
    {
        enabled_ = value;
    }

    void ui_element::set_visible(const bool value) noexcept
    {
        visible_ = value;
    }

    void ui_element::set_tooltip(std::u8string text)
    {
        tooltip_ = std::move(text);
    }

    void ui_element::set_tooltip_content(tooltip_config config)
    {
        const auto nonnegative = [](const float value) { return std::isfinite(value) && value > 0.0f ? value : 0.0f; };
        config.delay = std::max(config.delay, std::chrono::milliseconds::zero());
        config.padding = nonnegative(config.padding);
        config.gap = nonnegative(config.gap);
        config.corner_radius = nonnegative(config.corner_radius);
        config.shadow = std::min(nonnegative(config.shadow), 1.0f);
        tooltip_content_ = config.content != nullptr ? std::make_unique<tooltip_config>(std::move(config)) : nullptr;
    }

    void ui_element::set_search_label(std::u8string text)
    {
        search_label_ = std::move(text);
    }

    void ui_element::set_access_name(std::u8string text)
    {
        access_name_ = std::move(text);
    }

    void ui_element::set_action(const ui_trigger trigger, ui_action action)
    {
        actions_[static_cast<std::size_t>(trigger)] = std::move(action);
    }

    void ui_element::clear_action(const ui_trigger trigger) noexcept
    {
        actions_[static_cast<std::size_t>(trigger)] = {};
    }

    void ui_element::set_drag_source(std::optional<drag_source> source)
    {
        drag_source_ = std::move(source);
    }

    void ui_element::set_drop_target(std::optional<drop_target> target)
    {
        drop_target_ = std::move(target);
    }

    void ui_element::set_pointer_drag_target(std::optional<pointer_drag_target> target)
    {
        pointer_drag_target_ = std::move(target);
    }

    void ui_element::set_key_step_target(std::optional<key_step_target> target)
    {
        // 포인터 hit 판정(`interactive`)은 넓히지 않는다.
        // 키로 조작할 수 있다는 것이 "포인터가 여기서 멈춘다"를 뜻하지 않는다.
        key_step_target_ = std::move(target);
    }

    void ui_element::set_scroll_source(std::optional<scroll_source> source)
    {
        // `set_key_step_target`과 같은 이유로 `interactive`를 넓히지 않는다.
        // 흘릴 수 있다는 것이 "포인터가 여기서 멈춘다"를 뜻하지 않는다 — 휠은
        // hit 대상이 아니라 좌표를 **덮는** 컨테이너를 찾는다.
        scroll_source_ = std::move(source);
    }

    void ui_element::set_key_search_target(std::optional<key_search_target> target)
    {
        key_search_target_ = std::move(target);
    }

    void ui_element::set_key_capture(std::optional<key_capture_target> target)
    {
        key_capture_target_ = std::move(target);
    }

    const hover_dwell_target* ui_element::hover_dwell() const noexcept
    {
        return hover_dwell_target_.has_value() && hover_dwell_target_->on_dwell != nullptr ? &*hover_dwell_target_ : nullptr;
    }

    const hover_region_target* ui_element::hover_region() const noexcept
    {
        return hover_region_target_.has_value() ? &*hover_region_target_ : nullptr;
    }

    void ui_element::set_hover_dwell(std::optional<hover_dwell_target> target)
    {
        hover_dwell_target_ = std::move(target);
    }

    void ui_element::set_hover_region(std::optional<hover_region_target> target)
    {
        hover_region_target_ = std::move(target);
    }

    void ui_element::set_access_handler(access_handler handler)
    {
        access_handler_ = std::move(handler);
    }

    void ui_element::set_key_handler(key_handler handler)
    {
        key_handler_ = std::move(handler);
    }

    bool ui_element::clip_children() const noexcept
    {
        return clip_children_;
    }

    bool ui_element::tab_stop() const noexcept
    {
        // 지정하지 않았으면 "누를 수 있으면 자리다".
        return tab_stop_.value_or(action(ui_trigger::left_click) != nullptr);
    }

    focus_axis ui_element::focus_group() const noexcept
    {
        return focus_group_;
    }

    const ui_element_id& ui_element::focus_entry() const noexcept
    {
        return focus_entry_;
    }

    bool ui_element::takes_tab() const noexcept
    {
        return takes_tab_;
    }

    bool ui_element::focus_trap() const noexcept
    {
        return focus_trap_;
    }

    const ui_element_id& ui_element::focus_return() const noexcept
    {
        return focus_return_;
    }

    const ui_action* ui_element::dismiss_action() const noexcept
    {
        return dismiss_action_ ? &dismiss_action_ : nullptr;
    }

    const ui_action* ui_element::focus_leave_action() const noexcept
    {
        return focus_leave_action_ ? &focus_leave_action_ : nullptr;
    }

    bool ui_element::default_button() const noexcept
    {
        return default_button_;
    }

    bool ui_element::focusable() const noexcept
    {
        return enabled_ && visible_ && arranged_ && tab_stop();
    }

    bool ui_element::hit_opaque() const noexcept
    {
        return hit_opaque_;
    }

    void ui_element::set_hit_opaque(const bool value) noexcept
    {
        hit_opaque_ = value;
    }

    bool ui_element::hover_only() const noexcept
    {
        return hover_only_;
    }

    void ui_element::set_hover_only(const bool value) noexcept
    {
        hover_only_ = value;
    }

    void ui_element::set_cursor_idle_hide(const std::chrono::milliseconds delay) noexcept
    {
        cursor_idle_hide_ = delay > std::chrono::milliseconds { 0 } ? delay : std::chrono::milliseconds { 0 };
    }

    std::chrono::milliseconds ui_element::cursor_idle_hide() const noexcept
    {
        return cursor_idle_hide_;
    }

    bool ui_element::caption_client() const noexcept
    {
        return caption_client_;
    }

    void ui_element::set_caption_client(const bool value) noexcept
    {
        caption_client_ = value;
    }

    void ui_element::set_tab_stop(const bool value) noexcept
    {
        tab_stop_ = value;
    }

    void ui_element::set_focus_group(const focus_axis axis) noexcept
    {
        focus_group_ = axis;
    }

    void ui_element::set_focus_entry(ui_element_id id)
    {
        focus_entry_ = std::move(id);
    }

    void ui_element::set_takes_tab(const bool value) noexcept
    {
        takes_tab_ = value;
    }

    void ui_element::set_focus_trap(const bool value) noexcept
    {
        focus_trap_ = value;
    }

    void ui_element::set_focus_return(ui_element_id id)
    {
        focus_return_ = std::move(id);
    }

    void ui_element::set_default_button(const bool value) noexcept
    {
        default_button_ = value;
    }

    void ui_element::set_dismiss_action(ui_action action)
    {
        dismiss_action_ = std::move(action);
    }

    void ui_element::set_focus_leave_action(ui_action action)
    {
        focus_leave_action_ = std::move(action);
    }

    void ui_element::set_clip_children(const bool value) noexcept
    {
        clip_children_ = value;
    }

    const ui_element* ui_element::hit_test(const float x, const float y) const
    {
        if (visible_ == false)
            return nullptr;

        // 자르는 컨테이너는 잘려 보이지 않는 자식이 눌리지도 않아야 한다.
        if (clip_children_ && bounds_.contains(x, y) == false)
            return nullptr;

        // 뒤에 추가된 자식이 위에 그려지므로 역순으로 검사한다.
        // 스크롤로 자식이 부모 slot 밖에 걸칠 수 있어 자식 탐색은 자기 bounds로 막지 않는다.
        for (std::size_t index = children_.size(); index > 0; --index)
        {
            const ui_element* const hit { children_[index - 1]->hit_test(x, y) };
            if (hit != nullptr)
                return hit;
        }

        // hit_opaque는 상호작용 없이도 hit를 흡수한다.
        // 위에 뜬 표면의 빈 자리 클릭이 아래로 새지 않는다.
        if ((interactive() || hit_opaque_) && bounds_.contains(x, y))
            return this;
        return nullptr;
    }

    bool drag_payload::carries(const std::u8string_view owner) const noexcept
    {
        if (owner.empty())
            return false;
        return dragged_owner == owner || std::find(dragged_owners.begin(), dragged_owners.end(), owner) != dragged_owners.end();
    }

    bool drag_payload::carries(const ui_element_id& from, const std::u8string_view owner) const noexcept
    {
        if (container != ui_element_id {} && container != from)
            return false;
        return carries(owner);
    }

    std::size_t drag_payload::dragged_count() const noexcept
    {
        if (dragged_owners.empty() == false)
            return dragged_owners.size();
        return dragged_owner.empty() ? 0u : 1u;
    }

    const ui_element* ui_element::find_drop_target(const float x, const float y, const drag_payload& payload) const
    {
        return drop_hit_test(x, y, payload).target;
    }

    drop_hit_result ui_element::drop_hit_test(const float x, const float y, const drag_payload& payload) const
    {
        if (visible_ == false)
            return {};

        // 잘려 보이지 않는 자리는 놓을 자리도 아니다 (hit test와 같은 규칙).
        if (clip_children_ && bounds_.contains(x, y) == false)
            return {};

        // 뒤에 추가된 자식이 위에 그려지므로 역순으로 검사한다.
        for (std::size_t index = children_.size(); index > 0; --index)
        {
            const drop_hit_result result { children_[index - 1]->drop_hit_test(x, y, payload) };
            if (result.target != nullptr || result.blocked)
                return result;
        }

        // 일반 hit test와 달리 drop 대상이 아닌 element는 조용히 지나친다.
        // 자리로 가르는 대상(`accepts_at`)이 거절한 자리는 받지 않은 것이라 아래로 내려간다.
        if (enabled_ && bounds_.contains(x, y) && drop_target_.has_value() && drop_target_->accepts && drop_target_->accepts(payload)
            && (drop_target_->accepts_at == nullptr || drop_target_->accepts_at(payload, x, y)))
            return { this, false };
        if (hit_opaque_ && bounds_.contains(x, y))
            return { nullptr, true };
        return {};
    }

    std::optional<std::chrono::steady_clock::time_point> ui_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(context);
        static_cast<void>(interaction);
        return std::nullopt;
    }

    std::optional<std::size_t> ui_element::offset_at(float, const text_measurer&) const
    {
        return std::nullopt;
    }

    std::optional<std::size_t> ui_element::offset_at_point(const float x, float, const text_measurer& measurer) const
    {
        return offset_at(x, measurer);
    }

    std::optional<text_caret_position> ui_element::caret_at_point(const float x, const float y, const text_measurer& measurer) const
    {
        const std::optional<std::size_t> offset { offset_at_point(x, y, measurer) };
        if (offset.has_value() == false)
            return std::nullopt;
        return text_caret_position { *offset, false };
    }

    std::optional<text_input_snapshot> ui_element::text_input() const
    {
        return std::nullopt;
    }

    std::optional<rect_f> ui_element::text_span_bounds(const text_span_query&, const text_measurer&) const
    {
        return std::nullopt;
    }

    std::vector<std::size_t> ui_element::text_line_starts() const
    {
        return {};
    }

    float ui_element::scroll_delta_to_reveal(const rect_f&) const
    {
        // 기본은 "나는 흘리지 않는다"다.
        // 자르기만 하는 컨테이너도 여기 남는다 — 자를 뿐 옮길 값이 없다.
        return 0.0f;
    }

    access_info ui_element::accessibility() const
    {
        // 누를 수 있고 이름이 있으면 단추로 읽힌다.
        // 이름 없는 단추는 스크린 리더에 소음이라(scrim의 바깥 클릭 액션이 그렇다)
        // 이름이 있는 것만 승격한다.
        if (action(ui_trigger::left_click) == nullptr)
            return {};
        const std::u8string& name { access_name() };
        if (name.empty())
            return {};
        return { .role = access_role::button, .name = name };
    }

    std::optional<std::vector<input_action>> ui_element::access_actions(const access_request& request) const
    {
        // 앱이 받겠다고 한 명령이 먼저다. 답하지 않으면 아래의 기본으로 물러선다.
        if (access_handler_ != nullptr)
            if (std::optional<std::vector<input_action>> handled { access_handler_(request) }; handled.has_value())
                return handled;
        // 고른 것에 더하기·빼기는 목표 상태를 나르는 명령이라 클릭(뒤집기)으로 대신하지 않는다. 받는 앱이 없으면
        // 할 수 없는 일이다.
        // 값 정하기와 펼치기·접기는 **절대 명령**이라 기본이 없다. 클릭은
        // 뒤집기라, 오래된 발행본을 보고 흘리면 같은 명령 둘이 두 번 뒤집는다 —
        // 목표 상태를 나르는 factory를 가진 요소만 재정의로 답한다
        // (accessibility-action-design.md).
        if (request.command == access_command::set_value || request.command == access_command::expand || request.command == access_command::collapse
            || request.command == access_command::add_to_selection || request.command == access_command::remove_from_selection)
            return std::nullopt;
        const ui_action* const pressed { action(ui_trigger::left_click) };
        if (pressed == nullptr)
            return std::nullopt;
        // 클릭과 같은 액션을 같은 규칙으로 낸다.
        // 좌표는 요소의 한가운데다 (포인터가 관여하지 않았다).
        const rect_f box { bounds() };
        return (*pressed)(ui_action_context { id(), box.x + box.width / 2.0f, box.y + box.height / 2.0f, false });
    }

    std::optional<size_f> ui_element::preferred_size(const measure_context& context, const float available_width) const
    {
        static_cast<void>(context);
        static_cast<void>(available_width);
        return std::nullopt;
    }

    const access_item_model* ui_element::access_items() const noexcept
    {
        return nullptr;
    }

    ui_cursor ui_element::cursor_at(const float x, const float y, const interaction_snapshot& interaction) const
    {
        static_cast<void>(x);
        static_cast<void>(y);

        // 잡고 끄는 중인지다.
        // drag & drop은 출발 element가, pointer drag는 눌린 element가 주인이다.
        const bool holding {
            (interaction.drag.has_value() && interaction.drag->payload.source == id()) || (interaction.pressed == id() && pointer_drag_target_.has_value()),
        };
        if (holding && active_cursor_ != ui_cursor::inherit)
            return active_cursor_;
        if (cursor_ != ui_cursor::inherit)
            return cursor_;

        // 지정이 없으면 역할이 정한다.
        // 끌 수 있는 것은 잡는 모양이고 텍스트 박스는 글자 모양이다.
        if (drag_source_.has_value())
            return holding ? ui_cursor::grabbing : ui_cursor::grab;
        if (text_input().has_value())
            return ui_cursor::text;
        return ui_cursor::inherit;
    }

    ui_cursor ui_element::cursor() const noexcept
    {
        return cursor_;
    }

    ui_cursor ui_element::active_cursor() const noexcept
    {
        return active_cursor_;
    }

    void ui_element::set_cursor(const ui_cursor cursor) noexcept
    {
        cursor_ = cursor;
    }

    void ui_element::set_active_cursor(const ui_cursor cursor) noexcept
    {
        active_cursor_ = cursor;
    }

    void ui_element::add_child(std::unique_ptr<ui_element> child)
    {
        children_.push_back(std::move(child));
    }

    void ui_element::draw_children(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 자르는 컨테이너는 자식을 그리는 동안만 canvas를 좁힌다.
        if (clip_children_)
            context.canvas.save();
        if (clip_children_)
            context.canvas.clipRect(SkRect::MakeXYWH(bounds_.x, bounds_.y, bounds_.width, bounds_.height));
        for (const std::unique_ptr<ui_element>& child : children_)
            if (child->visible())
                child->draw(context, interaction);
        if (clip_children_)
            context.canvas.restore();
    }
} // namespace luil
