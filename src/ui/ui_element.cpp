#include "luil/ui/ui_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <utility>

namespace luil {
    bool rect_f::contains(const float point_x, const float point_y) const noexcept
    {
        return point_x >= x && point_x < x + width && point_y >= y && point_y < y + height;
    }

    interaction_snapshot interaction_for_surface(interaction_snapshot view, const std::u8string& surface)
    {
        // 값마다 자기 표식이 있고, 표식이 이 표면을 가리키지 않으면 그 값은
        // 여기서 일어난 일이 아니다. **값과 표식을 함께 비운다** — 표식만 남으면
        // 거른 결과가 그 자체로는 옳지 않은 snapshot이 된다.
        if (view.hovered_surface != surface)
        {
            view.hovered = {};
            view.hovered_surface.clear();
            // tooltip 지연의 기준 시각도 그 hover의 것이다.
            view.hover_started_at.reset();
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
        if (view.drag.has_value() && view.drag->surface != surface)
            view.drag.reset();
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

    const std::u8string& ui_element::search_label() const noexcept
    {
        return search_label_;
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

    bool ui_element::interactive() const noexcept
    {
        if (tooltip_.empty() == false || cursor_ != ui_cursor::inherit || drag_source_.has_value() || drop_target_.has_value() || pointer_drag_target_.has_value())
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

    void ui_element::set_search_label(std::u8string text)
    {
        search_label_ = std::move(text);
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

    const ui_element* ui_element::find_drop_target(const float x, const float y, const drag_payload& payload) const
    {
        if (visible_ == false)
            return nullptr;

        // 잘려 보이지 않는 자리는 놓을 자리도 아니다 (hit test와 같은 규칙).
        if (clip_children_ && bounds_.contains(x, y) == false)
            return nullptr;

        // 뒤에 추가된 자식이 위에 그려지므로 역순으로 검사한다.
        for (std::size_t index = children_.size(); index > 0; --index)
        {
            const ui_element* const target { children_[index - 1]->find_drop_target(x, y, payload) };
            if (target != nullptr)
                return target;
        }

        // 일반 hit test와 달리 drop 대상이 아닌 element는 조용히 지나친다.
        if (enabled_ && bounds_.contains(x, y) && drop_target_.has_value() && drop_target_->accepts && drop_target_->accepts(payload))
            return this;
        return nullptr;
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

    std::optional<text_input_snapshot> ui_element::text_input() const
    {
        return std::nullopt;
    }

    std::optional<rect_f> ui_element::text_span_bounds(const text_span_query&, const text_measurer&) const
    {
        return std::nullopt;
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
        const std::u8string& name { search_label_.empty() ? tooltip_ : search_label_ };
        if (name.empty())
            return {};
        return { .role = access_role::button, .name = name };
    }

    std::optional<std::vector<input_action>> ui_element::access_actions(const access_request& request) const
    {
        // 값 정하기와 펼치기·접기는 **절대 명령**이라 기본이 없다. 클릭은
        // 뒤집기라, 오래된 발행본을 보고 흘리면 같은 명령 둘이 두 번 뒤집는다 —
        // 목표 상태를 나르는 factory를 가진 요소만 재정의로 답한다
        // (accessibility-action-design.md).
        if (request.command == access_command::set_value || request.command == access_command::expand || request.command == access_command::collapse)
            return std::nullopt;
        const ui_action* const pressed { action(ui_trigger::left_click) };
        if (pressed == nullptr)
            return std::nullopt;
        // 클릭과 같은 액션을 같은 규칙으로 낸다.
        // 좌표는 요소의 한가운데다 (포인터가 관여하지 않았다).
        const rect_f box { bounds() };
        return (*pressed)(ui_action_context { id(), box.x + box.width / 2.0f, box.y + box.height / 2.0f, false });
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
