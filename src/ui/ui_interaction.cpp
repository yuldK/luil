#include "luil/ui/ui_interaction.h"
#include "luil/ui/menu_element.h"

#include "luil/messaging/envelope.h"
#include "luil/ui/zoom_view_element.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <variant>

namespace luil {
    namespace {
        const menu_element* menu_in_element(const ui_element* element) noexcept
        {
            if (element == nullptr)
                return nullptr;
            if (const auto* menu { dynamic_cast<const menu_element*>(element) }; menu != nullptr)
                return menu;
            for (const auto& child : element->children())
                if (const auto* menu { menu_in_element(child.get()) }; menu != nullptr)
                    return menu;
            return nullptr;
        }
    } // namespace

    namespace {
        // 놓는 순간의 속도를 재는 창이다. 이보다 오래된 이동은 속도에 넣지 않는다.
        constexpr std::chrono::milliseconds motion_window { 100 };
        constexpr std::chrono::milliseconds inertia_interval { 16 };
        constexpr float inertia_minimum_velocity { 80.0f };
        constexpr float inertia_maximum_velocity { 6000.0f };
        constexpr float inertia_stop_velocity { 20.0f };
        // 실제 경과 시간으로 감쇠해 입력 빈도나 갱신 횟수가 이동 거리를 바꾸지 않는다.
        constexpr float inertia_decay { 5.0f };

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
        }

        [[nodiscard]] const ui_element* wheel_owner_at(const ui_element& element, float x, float y);
        [[nodiscard]] const ui_element* live_touch_handle(const ui_tree& tree, const ui_element_id& id, float x, float y);

        [[nodiscard]] const ui_element* live_zoom_view(const ui_tree& tree, const ui_element_id& id, const float x, const float y)
        {
            const ui_element* const view { tree.find(id) };
            if (view == nullptr || view->zoom() == nullptr || tree.within_focus_trap(id) == false)
                return nullptr;
            const ui_element* const over { tree.root() != nullptr ? wheel_owner_at(*tree.root(), x, y) : nullptr };
            if (over == nullptr)
                return nullptr;
            // 내용이 이동해 안쪽 창이 누른 자리 밑으로 와도 잡은 보기는 바뀌지 않는다.
            // 그 밖의 방벽·다른 가지가 덮었으면 끝낸다.
            return over->id() == id || (tree.within(id, over->id()) && (over->zoom() != nullptr || over->scroll() != nullptr)) ? view : nullptr;
        }

        [[nodiscard]] bool within_zoom_view(const ui_element& element, const ui_element_id& id, bool inside = false)
        {
            inside = inside || element.zoom() != nullptr;
            if (element.id() == id)
                return inside;
            for (const std::unique_ptr<ui_element>& child : element.children())
                if (within_zoom_view(*child, id, inside))
                    return true;
            return false;
        }

        void post_with_retry(messaging::channel<app_message>& inbox, const app_message& message)
        {
            // 앱 메시지는 버리지 않는다.
            // 가득 찬 inbox에는 짧게 물러났다 다시 시도하고 닫힌 inbox(종료 중)만 조용히 포기한다.
            while (true)
            {
                app_message attempt { message };
                const messaging::post_result result { inbox.post(std::move(attempt)) };
                if (result != messaging::post_result::channel_full)
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            }
        }

        [[nodiscard]] float distance_between(const float from_x, const float from_y, const float to_x, const float to_y) noexcept
        {
            const float delta_x { to_x - from_x };
            const float delta_y { to_y - from_y };
            return std::sqrt(delta_x * delta_x + delta_y * delta_y);
        }
    } // namespace

    interaction_controller::interaction_controller(interaction_policy* const policy, interaction_config config) noexcept
        : policy_ { policy }
        , config_ { std::move(config) }
    {
        // 시작 설정의 검증은 창이 한다 (시작 실패로 알린다). 여기 닿은 잘못된 값은
        // 기본값으로 물러선다 — 0의 시간 창이 모든 접촉을 끌기로 만들지 않게 한다.
        if (valid_touch_gesture_config(config_.touch) == false)
            config_.touch = {};
    }

    std::optional<text_input_target> interaction_controller::text_target(const ui_element_kind kind) const
    {
        if (policy_ == nullptr)
            return std::nullopt;
        return policy_->text_target_of(kind);
    }

    input_action interaction_controller::text_edit_action(const text_input_target target, const text::text_edit_command command, const bool extend) const
    {
        if (policy_ == nullptr)
            return {};
        text_edit_request request {};
        request.target = target;
        request.command = command;
        request.extend = extend;
        return policy_->make_text_edit_action(request);
    }

    void interaction_controller::set_tree(std::shared_ptr<const ui_tree> tree)
    {
        tree_ = std::move(tree);
        clear_gone_surface_gestures();

        // 사라진 element를 가리키는 초점·강조는 새 tree를 받는 즉시 거둔다.
        // snapshot을 곧바로 읽는 렌더러가 없어진 텍스트 박스의 caret을 기다리지 않게 한다.
        update_focus();

        // 포인터가 머문 자리에서 내용이 스크롤로 흐르면 커서 아래 element가 바뀐다.
        // 새 tree를 받을 때 마지막 위치로 hover를 다시 판정해
        // tooltip이 이전 대상을 따라 쓸려 다니지 않게 한다.
        // 끌기 중에는 hover가 잡은 대상에 남아야 하므로 건드리지 않는다.
        if (pointer_inside_ && pointer_drag_id_ == ui_element_id {} && snapshot_.drag.has_value() == false)
            update_hover(last_pointer_x_, last_pointer_y_, last_pointer_time_, false);

        // 끄는 동안 내용이 흘렀으면(가장자리 자동 스크롤) 포인터 밑의 놓을 자리도 바뀐다. 포인터가 멈춰 있어도
        // 강조가 새 내용을 따라가게 한다. 포인터가 주 창 위에 있을 때만이다 — 다른 창의 자리는 그 창의 tree가 온다.
        if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() && snapshot_.drag->pointer_on().empty())
            relocate_drag();
        settle_auto_scroll();
        resume_auto_scroll();
    }

    void interaction_controller::set_surface_trees(surface_tree_list surfaces)
    {
        surface_trees_ = std::move(surfaces);

        // 사라진 표면에 매인 몸짓을 **먼저** 거둔다.
        // 아래 hover 재판정이 `snapshot_.drag`를 보므로 이 순서여야 한다 —
        // 죽은 끌기가 남으면 그 뒤로 hover가 얼어붙는다.
        clear_gone_surface_gestures();

        // 메뉴 popup이 열리고 닫히는 edge도 여기서 드러난다.
        update_focus();

        // 표면 내용이 바뀌었으면 그 위에 머문 포인터의 hover도 다시 판정한다.
        // 주 tree를 받을 때와 같은 규칙이다.
        if (pointer_inside_ && last_pointer_surface_.empty() == false && pointer_drag_id_ == ui_element_id {} && snapshot_.drag.has_value() == false)
            update_hover(last_pointer_x_, last_pointer_y_, last_pointer_time_, false);

        // 주 tree를 받을 때와 같이 흐른 내용 위의 놓을 자리를 다시 찾는다.
        if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() && snapshot_.drag->pointer_on().empty() == false)
            relocate_drag();
        settle_auto_scroll();
        resume_auto_scroll();
    }

    void interaction_controller::clear_gone_surface_gestures() noexcept
    {
        // 누름은 시작한 표면에 매인다 (`pressed_surface_`).
        // 그 표면이 사라지면 잡은 대상을 다시 찾을 tree가 없어, 남은 끌기 대상이
        // 이후의 이동을 전부 조기 반환으로 삼킨다.
        //  - 주 창(빈 id)은 표면 목록의 임자가 아니라 여기서 보지 않는다.
        //  - 터치 접촉은 누름보다 오래 산다 (스크롤로 바뀐 뒤에도). 따로 거둔다.
        if (touch_.has_value() && surface_tree(touch_->surface) == nullptr)
            cancel_touch();
        if (inertia_.has_value() && surface_tree(inertia_->surface) == nullptr)
            inertia_.reset();
        if (pointer_contact_.has_value() && surface_tree(pointer_contact_->surface) == nullptr)
            cancel_pointer_gesture();
        if (pressed_surface_.empty() == false && surface_tree(pressed_surface_) == nullptr)
        {
            text_drag_id_ = {};
            clear_press();
        }

        // 끌기의 표식은 `snapshot_.drag` 안에 있어 함께 사라진다 —
        // 따로 든 멤버였다면 여기서 빠뜨릴 수 있는 자리다.
        //  - 주 창의 끌기는 `surface_tree`가 주 tree로 답하므로 그대로 남는다.
        //    끌기를 시작한 자리는 이미 clear_press로 비워져 위 검사에 걸리지 않는다.
        //  - **출발 표면의 소멸은 취소다** (놓기가 아니다 — `on_unclaimed_drop`도 부르지 않는다). 그 창이 쥔 포인터
        //    캡처가 창과 함께 풀려 뗌이 어디에 떨어질지 알 수 없다. 마지막 탭을 끌어낸 창을 앱이 닫는 것은 놓기의
        //    메시지를 받은 **뒤**다 (multi-window-design.md).
        //  - 포인터가 가 있던 다른 표면이 사라진 것은 끌기를 죽이지 않는다. 그 자리의 강조만 거두고 다음 이동이 새
        //    자리를 찾는다.
        if (snapshot_.drag.has_value() && surface_tree(snapshot_.drag->surface) == nullptr)
            abandon_drag();
        else if (snapshot_.drag.has_value() && snapshot_.drag->outside == false && surface_tree(snapshot_.drag->pointer_on()) == nullptr)
            snapshot_.drag->hovered_drop_target = {};
    }

    const ui_tree* interaction_controller::surface_tree(const std::u8string& surface) const noexcept
    {
        if (surface.empty())
            return tree_.get();
        for (const auto& [id, tree] : surface_trees_)
            if (id == surface)
                return tree.get();
        return nullptr;
    }

    std::vector<input_action> interaction_controller::process(const raw_input_event& event)
    {
        // 활성 표면은 `update_focus`의 **전제**라 그보다 먼저 선다.
        // 뒤에 두면 그 `update_focus`가 옛 활성 표면으로 진입을 판정해, 방금
        // 떠난 창의 가둠이 초점을 곧바로 되잡는다 — 거둠과 진입이 서로를 지우는
        // 그 자리다 (active-surface-design.md).
        //  - 활성 표면은 **얻음만** 넣는다. 상실로 비우지 않는 이유는 다른 앱으로
        //    넘어간 동안에도 "돌아가면 이 창"이 여전히 참이기 때문이다 (3.2).
        if (const auto* const gained { std::get_if<surface_focus_gained_event>(&event) }; gained != nullptr)
            active_surface_ = gained->surface;
        update_focus();
        // 초점을 옮기는 자리가 넷이다 (Tab·화살표·Home/End·글자 탐색).
        // 되살리기를 그 넷에 각각 심으면 언젠가 하나를 빠뜨리므로, 옮겨진 사실을
        // **여기 한 곳에서** 전후 비교로 센다 (focus-reveal-design.md).
        const ui_element_id focused_before { snapshot_.focused };
        std::vector<input_action> actions {};
        actions = std::visit(
            [this](const auto& value) -> std::vector<input_action> {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<value_type, pointer_moved_event>)
                    return process_move(value);
                else if constexpr (std::is_same_v<value_type, pointer_pressed_event>)
                    return process_press(value);
                else if constexpr (std::is_same_v<value_type, pointer_released_event>)
                    return process_release(value);
                else if constexpr (std::is_same_v<value_type, pointer_left_event>)
                {
                    // **떠난 표면의 것만** 거둔다. popup은 앵커 창 위에 떠 있어
                    // 두 표면의 떠남과 들어옴이 늘 잇달아 오고, 늦게 도착한
                    // 떠남이 방금 다른 표면에서 얻은 hover를 지운다.
                    if (snapshot_.hovered_surface == value.surface)
                    {
                        snapshot_.hovered = {};
                        snapshot_.hovered_surface.clear();
                        snapshot_.hover_started_at.reset();
                    }
                    // 포인터 자리의 기억도 그 표면의 것일 때만 버린다.
                    // 이미 다른 표면으로 옮겨 갔으면 그 기억이 최신이고,
                    // 버리면 그 표면의 tree가 다시 올 때 hover 재판정이 멈춘다.
                    if (last_pointer_surface_ == value.surface)
                        pointer_inside_ = false;
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, surface_focus_lost_event>)
                {
                    if (inertia_.has_value() && inertia_->surface == value.surface)
                        inertia_.reset();
                    // 주 창과 보조 창의 focus 메시지는 연달아 올 수 있다.
                    // 떠난 표면이 지금 초점을 가진 표면일 때만 거둬, 늦은
                    // 메시지가 새 창에서 막 얻은 텍스트 초점을 지우지 않는다.
                    if (snapshot_.focused_surface == value.surface)
                        clear_focus();
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, surface_focus_gained_event>)
                {
                    // 값은 `process`의 첫 줄이 이미 넣었다 — 순서가 뜻을 가지는
                    // 유일한 이벤트라 그 자리에 있다.
                    //  - 초점을 여기서 옮기지는 않는다. 진입은 여전히 "초점이
                    //    없을 때"의 술어라, 활성 창이 바뀌었다고 서 있던 초점을
                    //    빼앗지 않는다. 떠난 창의 초점은 짝인 상실이 거둔다
                    //    (active-surface-design.md).
                    static_cast<void>(value);
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, file_drag_entered_event>)
                {
                    inertia_.reset();
                    process_file_drag_entered(value);
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, file_drag_moved_event>)
                {
                    process_file_drag_moved(value);
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, file_drag_left_event>)
                {
                    // 놓기도 표시로는 떠남이다 — 실행은 UI thread가 이미 했다.
                    // 내부 끌기는 건드리지 않는다 (파일이 실린 것만 거둔다).
                    if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() == false)
                        snapshot_.drag.reset();
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, mouse_wheel_event>)
                {
                    inertia_.reset();
                    // 스크롤 뒤 tree가 다시 오면 이 자리로 hover를 재판정한다.
                    pointer_inside_ = true;
                    last_pointer_x_ = value.x;
                    last_pointer_y_ = value.y;
                    last_pointer_time_ = value.time;
                    last_pointer_surface_ = value.surface;

                    // 위로 굴리면 내용이 위로 간다.
                    // 라우팅(어느 목록·pane을 스크롤 할지)은 앱 정책의 몫이며,
                    // popup 위의 휠은 그 popup의 tree로 판단한다.
                    const float delta { -(value.delta / 120.0f) * input_wheel_scroll_step };
                    const ui_tree* const tree { surface_tree(value.surface) };
                    if (tree == nullptr)
                        return {};
                    const ui_element* const owner { tree->root() != nullptr ? wheel_owner_at(*tree->root(), value.x, value.y) : nullptr };
                    // 보기 안의 스크롤도 공통 라우팅을 탄다. 텍스트 정책만 가진 앱이
                    // 빈 on_wheel 기본값 때문에 안쪽 창의 휠을 잃으면 안 된다.
                    if (owner != nullptr && within_zoom_view(*tree->root(), owner->id()))
                        return route_wheel(*tree, value, delta);
                    return policy_ != nullptr ? policy_->on_wheel(*tree, value, delta) : route_wheel(*tree, value, delta);
                }
                else if constexpr (std::is_same_v<value_type, key_pressed_event>)
                {
                    inertia_.reset();
                    return process_key(value);
                }
                else if constexpr (std::is_same_v<value_type, access_focus_event>)
                {
                    inertia_.reset();
                    return process_access_focus(value);
                }
                else if constexpr (std::is_same_v<value_type, pointer_cancelled_event>)
                    return process_cancel(value);
                else if constexpr (std::is_same_v<value_type, focus_reveal_event>)
                {
                    // 초점이 그 표면에 있을 때만이다. 초점이 옮겨 간 것은 아니라 아래의 전후
                    // 비교로는 서지 않으므로 여기서 직접 묻는다.
                    const ui_tree* const tree { surface_tree(value.surface) };
                    if (policy_ == nullptr || tree == nullptr || snapshot_.focused == ui_element_id {} || snapshot_.focused_surface != value.surface)
                        return {};
                    return policy_->on_focus_moved(*tree, snapshot_.focused);
                }
                else if constexpr (std::is_same_v<value_type, character_typed_event>)
                {
                    // 문자 입력은 초점을 가진 텍스트 박스로만 간다.
                    // 받을 값인지는 앱 logic이 거른다.
                    const std::optional<text_input_target> target { text_target(snapshot_.focused_input.kind) };
                    // 텍스트 박스가 없으면 그 글자는 **묶음 안에서 항목을 찾는** 데 쓴다.
                    // 순서가 이 자리에 이미 서 있다 — 칸이 먼저이고, 글자 탐색은 그 다음이다.
                    if (target.has_value() == false)
                    {
                        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
                        const ui_element* const focused { tree != nullptr ? tree->find(snapshot_.focused) : nullptr };
                        const zoom_source* const source { focused != nullptr && focused->focusable() ? focused->zoom() : nullptr };
                        if (source != nullptr && source->on_key)
                        {
                            key_pressed_event key {};
                            key.key = value.character == U'+' || value.character == U'=' ? key_code::zoom_in
                                : value.character == U'-'                                ? key_code::zoom_out
                                : value.character == U'0'                                ? key_code::key_0
                                                                                         : key_code::none;
                            if (key.key != key_code::none)
                                if (auto actions { source->on_key(key) }; actions.has_value())
                                    return std::move(*actions);
                        }
                        return process_typeahead(value);
                    }
                    if (const auto document { focused_text_input() }; document.has_value() && document->read_only)
                        return {};
                    if (value.character == U'\b')
                        return { text_edit_action(*target, text::text_edit_command::backspace) };
                    // Ctrl+Backspace는 WM_CHAR가 U+007F로 준다.
                    if (value.character == 0x7Fu)
                        return delete_word(*target, true);
                    // 나머지 제어 문자는 키 경로(Ctrl+C 등)가 이미 다뤘다.
                    if (value.character < U' ')
                        return {};

                    text_edit_request request {};
                    request.target = *target;
                    request.command = text::text_edit_command::insert;
                    request.text = text::text_edit_encode_utf8(value.character);
                    return { policy_->make_text_edit_action(request) };
                }
                else
                    return {};
            },
            event);

        // **키보드로** 옮겨진 초점만 되살린다. 누른 자리는 이미 보인다 —
        // 마우스와 키보드를 가르는 값이 `focus_visible`로 이미 서 있어
        // 새 상태를 세우지 않는다 (keyboard-focus-design.md).
        if (policy_ != nullptr && snapshot_.focus_visible && snapshot_.focused != focused_before && snapshot_.focused != ui_element_id {})
            if (const ui_tree* const tree { surface_tree(snapshot_.focused_surface) }; tree != nullptr)
                for (input_action& action : policy_->on_focus_moved(*tree, snapshot_.focused))
                    actions.push_back(std::move(action));
        // 끌기가 끝났으면(뗌·취소·Esc) 가장자리 흐름도 멈춘다.
        settle_auto_scroll();
        return actions;
    }

    const interaction_snapshot& interaction_controller::snapshot() const noexcept
    {
        return snapshot_;
    }

    std::vector<input_action> interaction_controller::process_move(const pointer_moved_event& event)
    {
        // 터치는 hover를 남기지 않는다. 포인터 자리의 기억도 마우스·펜의 것이다.
        if (event.device == pointer_device::touch)
            return process_touch_move(event);

        // **포인터가 실제로 움직였는가.** Win32는 창이 바뀌거나 눌린 뒤에 같은 자리의 이동
        // 메시지를 다시 보낸다. 그 메시지로 tooltip 시계를 세우면 손대지 않은 자리에 tooltip이 선다.
        const bool moved { pointer_inside_ == false || event.x != last_pointer_x_ || event.y != last_pointer_y_ || event.surface != last_pointer_surface_ };
        pointer_inside_ = true;
        last_pointer_x_ = event.x;
        last_pointer_y_ = event.y;
        last_pointer_time_ = event.time;
        last_pointer_surface_ = event.surface;

        // 터치가 조작을 쥐고 있으면 마우스·펜의 이동은 hover만 바꾼다.
        // 잡은 전용 조작(스크롤 막대)을 다른 장치가 끌면 안 된다.
        if (touch_.has_value())
        {
            update_hover(event.x, event.y, event.time, moved);
            return {};
        }

        if (pointer_contact_.has_value() && (pointer_contact_->device != event.device || pointer_contact_->id != event.pointer_id || pointer_contact_->surface != event.surface))
        {
            update_hover(event.x, event.y, event.time, moved);
            return {};
        }

        // 잡은 대상은 누름이 시작된 표면에서 다시 찾는다.
        // 캡처 중에는 이벤트도 같은 표면에서 온다.
        const ui_tree* const pressed_tree { surface_tree(pressed_surface_) };

        if (view_drag_.has_value())
        {
            const view_drag previous { *view_drag_ };
            const ui_element* const owner { pressed_tree != nullptr ? live_zoom_view(*pressed_tree, previous.id, previous.start_x, previous.start_y) : nullptr };
            if (owner == nullptr || owner->id() != previous.id || owner->zoom()->pan_by == nullptr)
            {
                cancel_pointer_gesture();
                return {};
            }
            view_drag_->x = event.x;
            view_drag_->y = event.y;
            const zoom_point delta { (event.x - previous.x) / positive_scale(owner->zoom()->scale), (event.y - previous.y) / positive_scale(owner->zoom()->scale) };
            if (view_drag_->edge.has_value())
                record_view_pan(*view_drag_->edge, delta, event.time, event.x, event.y);
            return delta != zoom_point {} ? std::vector<input_action> { owner->zoom()->pan_by(delta) } : std::vector<input_action> {};
        }

        // 텍스트 박스를 잡고 있으면 이동이 선택 범위를 늘린다.
        // 포인터가 칸을 벗어나도 이어진다.
        //  - 같은 id로 다시 찾는다.
        if (text_drag_id_ != ui_element_id {})
        {
            const ui_element* const target { pressed_tree != nullptr ? pressed_tree->find(text_drag_id_) : nullptr };
            if (target == nullptr)
                return {};
            return place_text_caret(*target, event.x, event.y, true, false);
        }

        // 스크롤 막대를 잡고 있으면 이동은 전부 그 element의 몫이다.
        // tree가 다시 빌드되어도 같은 id로 찾아 이어서 끈다.
        if (pointer_drag_id_ != ui_element_id {})
        {
            const pointer_drag_target* const handler { live_drag_handler(pressed_tree, pointer_drag_id_, pressed_x_, pressed_y_) };
            if (handler == nullptr)
            {
                cancel_pointer_gesture();
                return {};
            }
            if (handler->on_move == nullptr)
            {
                track_auto_scroll(pressed_surface_, event.x, event.y, event.time);
                return {};
            }

            const ui_action_context previous { action_context(pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, pointer_drag_modifiers_) };
            const ui_action_context current { action_context(pointer_drag_id_, event.x, event.y, modifiers_of(event)) };
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            pointer_drag_modifiers_ = modifiers_of(event);
            pointer_drag_motion_.add(event.time, event.x, event.y);
            std::vector<input_action> actions { handler->on_move(previous, current) };
            track_auto_scroll(pressed_surface_, event.x, event.y, event.time);
            return actions;
        }

        // drag 중이면 ghost 위치와 수락 중인 drop 대상만 갱신한다.
        // drop 대상은 포인터가 실제로 놓인 표면에서 찾는다 — 끌고 간 다른 창이면 그 창의 tree다
        // (multi-window-design.md).
        if (snapshot_.drag.has_value())
        {
            const drag_location at { locate_drag(event) };
            update_drag(at);
            update_hover(event.x, event.y, event.time, moved);
            track_auto_scroll(at.surface, at.x, at.y, event.time);
            return {};
        }

        const ui_tree* const tree { surface_tree(event.surface) };
        if (tree == nullptr)
        {
            update_hover(event.x, event.y, event.time, moved);
            return {};
        }

        // 시작 거리를 둔 손잡이는 그 거리를 넘는 순간 끌기가 된다. 그 전에 떼면 클릭이다.
        if (pointer_drag_pending_)
        {
            const ui_element* const target { pressed_tree != nullptr ? live_touch_handle(*pressed_tree, pressed_id_, pressed_x_, pressed_y_) : nullptr };
            const pointer_drag_target* const handler { target != nullptr ? target->pointer_drag() : nullptr };
            if (handler == nullptr || (handler->start_distance > 0.0f) == false)
                // 손잡이가 사라졌다. 남은 것은 보통 누름이다 (뗌의 클릭 판정이 대상을 다시 본다).
                pointer_drag_pending_ = false;
            else if (distance_between(pressed_x_, pressed_y_, event.x, event.y) >= handler->start_distance)
            {
                pointer_drag_pending_ = false;
                pointer_drag_id_ = pressed_id_;
                pointer_drag_held_ = *handler;
                pointer_drag_x_ = pressed_x_;
                pointer_drag_y_ = pressed_y_;
                pointer_drag_modifiers_ = pressed_modifiers_;
                pointer_drag_motion_ = {};
                // 끌기는 클릭이 아니다. 앞 클릭의 연타 기록을 지워 끌기 뒤의 클릭이 그것과 더블 클릭으로 묶이지 않게 한다.
                last_click_id_ = {};
                last_click_surface_.clear();
                click_streak_ = 0;
                // 누름은 누른 자리와 누른 순간의 수식 키다. 넘은 이동 자체도 끌기라 이어서 그 이동을 보낸다.
                std::vector<input_action> actions {};
                if (handler->on_press)
                    actions = handler->on_press(action_context(pointer_drag_id_, pressed_x_, pressed_y_, pressed_modifiers_));
                for (input_action& action : process_move(event))
                    actions.push_back(std::move(action));
                return actions;
            }
            else
            {
                update_hover(event.x, event.y, event.time, moved);
                return {};
            }
        }

        // 눌린 채 임계 거리를 넘으면 클릭 대신 drag가 시작된다.
        if (drag_candidate_ && pressed_button_ == pointer_button::left && distance_between(pressed_x_, pressed_y_, event.x, event.y) >= config_.drag_start_distance)
        {
            const ui_element* const source { pressed_tree != nullptr ? pressed_tree->find(pressed_id_) : nullptr };
            if (source != nullptr && source->enabled() && source->drag() != nullptr && source->drag()->make_payload)
            {
                // 자리는 누른 자리이고 수식 키는 끌기가 시작되는 이 이동의 순간이다.
                const ui_action_context context { action_context(pressed_id_, pressed_x_, pressed_y_, modifiers_of(event)) };
                // 끌기는 지금 이벤트가 난 표면에서 선다 — 캡처 중이라 누른 표면과 같다.
                // 그 표면이 사라지면 끌기도 죽는다. 포인터가 다른 창으로 가면 표시는 그 창으로 옮겨 간다
                // (multi-window-design.md).
                begin_drag(*source->drag(), context, event.x, event.y, event.surface);
                clear_press();
                const drag_location at { locate_drag(event) };
                update_drag(at);
                track_auto_scroll(at.surface, at.x, at.y, event.time);
            }
            drag_candidate_ = false;
        }

        update_hover(event.x, event.y, event.time, moved);
        return {};
    }

    template<typename event_type>
    interaction_controller::drag_location interaction_controller::locate_drag(const event_type& event) const
    {
        // 플랫폼이 포인터 밑을 다시 찾아 실었다. 캡처는 누른 창이 쥐고 있어 좌표가 그 창 기준이라, 다른 창 위인지는
        // 화면에서 찾아야만 안다 (`pointer_over`).
        if (event.over.has_value())
        {
            if (event.over->outside)
                return { event.surface, event.x, event.y, true };
            return { event.over->surface, event.over->x, event.over->y, false };
        }
        // 싣지 않았으면 이벤트의 표면이 포인터 밑이다. 그 표면의 내용 밖이면 창 밖이다.
        const ui_tree* const tree { surface_tree(event.surface) };
        const ui_element* const root { tree != nullptr ? tree->root() : nullptr };
        const bool outside { root != nullptr && root->bounds().contains(event.x, event.y) == false };
        return { event.surface, event.x, event.y, outside };
    }

    void interaction_controller::begin_drag(const drag_source& source, const ui_action_context& context, const float x, const float y, const std::u8string& surface)
    {
        // payload를 먼저 짓는다 — 짓는 앱 콜백이 던지면 반쯤 선 끌기가 남지 않는다.
        drag_payload payload { source.make_payload(context) };
        drag_unclaimed_ = source.on_unclaimed_drop;
        snapshot_.drag = drag_visual { std::move(payload), x, y, {}, surface };
    }

    void interaction_controller::update_drag(const drag_location& at)
    {
        drag_visual& drag { *snapshot_.drag };
        drag.x = at.x;
        drag.y = at.y;
        drag.outside = at.outside;
        // 출발 표면이면 비워 둔다 — 비어 있는 것이 "출발 표면"의 뜻이라 한 값에 두 표현이 생기지 않는다.
        if (at.surface == drag.surface)
            drag.pointer_surface.reset();
        else
            drag.pointer_surface = at.surface;
        drag.hovered_drop_target = {};
        if (at.outside)
            return;
        if (const ui_tree* const tree { surface_tree(at.surface) }; tree != nullptr)
            if (const ui_element* const over { tree->find_drop_target(at.x, at.y, drag.payload) }; over != nullptr)
                drag.hovered_drop_target = over->id();
    }

    void interaction_controller::relocate_drag()
    {
        const drag_visual& drag { *snapshot_.drag };
        update_drag(drag_location { drag.pointer_on(), drag.x, drag.y, drag.outside });
    }

    std::vector<input_action> interaction_controller::finish_drag(const drag_location& at, const modifier_keys modifiers, const std::optional<screen_point> screen)
    {
        std::vector<input_action> actions {};
        const drag_visual drag { *snapshot_.drag };
        const auto unclaimed { std::move(drag_unclaimed_) };
        abandon_drag();
        clear_press();
        // 놓을 자리는 포인터가 놓인 표면에서 찾는다. 창 밖이면 찾을 tree가 없다.
        const ui_tree* const tree { at.outside ? nullptr : surface_tree(at.surface) };
        const drop_hit_result hit { tree != nullptr ? tree->drop_hit_test(at.x, at.y, drag.payload) : drop_hit_result {} };
        if (hit.target != nullptr)
        {
            if (hit.target->drop()->on_drop)
                actions = hit.target->drop()->on_drop(drag.payload, action_context(hit.target->id(), at.x, at.y, modifiers));
            return actions;
        }
        // 받는 대상이 없는 놓기다. 불투명 표면이 가린 자리는 빈 자리가 아니라 막힌 자리라 알리지 않는다.
        if (hit.blocked || unclaimed == nullptr)
            return actions;
        drag_release release {};
        release.surface = at.surface;
        release.x = at.x;
        release.y = at.y;
        release.outside = at.outside;
        release.screen = screen;
        release.control = modifiers.control;
        release.shift = modifiers.shift;
        release.alt = modifiers.alt;
        return unclaimed(drag.payload, release);
    }

    void interaction_controller::abandon_drag() noexcept
    {
        snapshot_.drag.reset();
        drag_unclaimed_ = nullptr;
    }

    // OS 파일 끌기의 표시 상태다 (os-dragdrop-design.md).
    // 수락의 답과 놓기의 실행은 UI thread가 같은 tree에 직접 묻고, 여기는
    // 스냅샷의 수락 대상 강조만 세운다. 내부 끌기와는 서로 배타다 —
    // OS 끌기 동안은 OLE가 포인터를 잡아 press·move가 오지 않는다.
    void interaction_controller::process_file_drag_entered(const file_drag_entered_event& event)
    {
        drag_payload payload {};
        // ghost는 출발한 쪽(탐색기)이 그린다 — 우리는 강조만 얹는다.
        payload.custom_visual = true;
        payload.files = event.files;
        // 파일 끌기도 들어온 표면에 매인다 — 다른 창으로 넘어가면 그 창이 자기
        // entered를 낸다 (multi-window-design.md).
        drag_visual visual { std::move(payload), event.x, event.y, {}, event.surface };
        if (const ui_tree* const tree { surface_tree(event.surface) }; tree != nullptr)
            if (const ui_element* const over { tree->find_drop_target(event.x, event.y, visual.payload) }; over != nullptr)
                visual.hovered_drop_target = over->id();
        snapshot_.drag = std::move(visual);
        // 밖에서 온 끌기의 놓기는 UI thread가 직접 실행한다. 안에서 시작한 끌기의 알림 곳이 남아 있을 자리가 아니다.
        drag_unclaimed_ = nullptr;
    }

    void interaction_controller::process_file_drag_moved(const file_drag_moved_event& event)
    {
        // 들어온 적 없는 이동은 버린다. 내부 끌기도 건드리지 않는다.
        if (snapshot_.drag.has_value() == false || snapshot_.drag->payload.files.empty())
            return;
        snapshot_.drag->x = event.x;
        snapshot_.drag->y = event.y;
        snapshot_.drag->hovered_drop_target = {};
        const ui_tree* const tree { surface_tree(event.surface) };
        if (tree == nullptr)
            return;
        if (const ui_element* const over { tree->find_drop_target(event.x, event.y, snapshot_.drag->payload) }; over != nullptr)
            snapshot_.drag->hovered_drop_target = over->id();
    }

    std::vector<input_action> route_wheel(const ui_tree& tree, const float x, const float y, const float delta, const std::span<const scroll_route> routes)
    {
        for (const scroll_route& route : routes)
        {
            if (route.scroll == nullptr)
                continue;
            const ui_element* const target { tree.find(route.id) };
            if (target == nullptr || target->bounds().contains(x, y) == false)
                continue;
            return { route.scroll(delta) };
        }
        return {};
    }

    std::vector<input_action> route_reveal(const ui_tree& tree, const ui_element_id& target, const std::span<const scroll_route> routes)
    {
        const ui_element* const focused { tree.find(target) };
        if (focused == nullptr)
            return {};
        for (const scroll_route& route : routes)
        {
            // 품는가를 묻는 것이 휠과 갈리는 전부다 (휠은 좌표가 덮는가를 묻는다).
            if (route.scroll == nullptr || tree.within(route.id, target) == false)
                continue;
            const ui_element* const viewport { tree.find(route.id) };
            if (viewport == nullptr)
                continue;
            // 얼마나 흘릴지는 그 창이 답한다 — 배율을 아는 것이 그쪽뿐이다.
            //  - 이미 보이면 0이고, 그때는 메시지를 내지 않는다. 없으면 화살표를
            //    누를 때마다 logic이 깨어나 tree를 통째로 다시 짓는다.
            const float delta { viewport->scroll_delta_to_reveal(focused->bounds()) };
            if (delta == 0.0f)
                return {};
            return { route.scroll(delta) };
        }
        return {};
    }

    namespace {
        // 좌표를 덮는 가장 안쪽·가장 위의 element 중, 휠이 거기서 멈추는 것이다.
        //
        // 답은 둘 중 하나다: 흘리는 컨테이너이거나, **흘리지는 않지만 휠을
        // 삼키는 것**(scrim·메뉴 같은 `hit_opaque`)이다. 두 답을 한 함수가 내는
        // 이유는 찾는 방법이 같아서다 — 위에 떠 있는 것부터 안쪽으로 내려가다
        // 처음 걸리는 것이 임자다. 무엇이 걸렸는지는 부르는 쪽이 가른다.
        //  - **`hit_opaque`를 보는 것이 방벽이다.** 보지 않으면 modal dialog가
        //    떠 있는 동안 scrim 위에서 굴린 휠이 뒤의 화면을 흘린다. 포인터를
        //    막는 것이 modal의 몫이라면(modal-dialog-design.md) 휠도 포인터다.
        //  - 자식을 역순으로 보는 것과 자르는 컨테이너 밖을 보지 않는 것은
        //    `ui_element::hit_test`와 같은 규칙이다.
        //  - 다만 `interactive()`는 묻지 않는다. 흘리는 창은 누를 것이 없어도
        //    휠을 받는다.
        //  - 비활성인 가지는 통째로 지나친다. 흐리게 그려 놓고 굴러가면 그것은
        //    비활성이 아니다.
        [[nodiscard]] const ui_element* wheel_owner_at(const ui_element& element, const float x, const float y)
        {
            if (element.visible() == false || element.enabled() == false)
                return nullptr;
            if (element.clip_children() && element.bounds().contains(x, y) == false)
                return nullptr;
            const std::span<const std::unique_ptr<ui_element>> children { element.children() };
            for (std::size_t index = children.size(); index > 0u; --index)
                if (const ui_element* const found { wheel_owner_at(*children[index - 1u], x, y) }; found != nullptr)
                    return found;
            if (element.bounds().contains(x, y) == false)
                return nullptr;
            const scroll_source* const source { element.scroll() };
            if ((source != nullptr && source->scroll != nullptr) || element.zoom() != nullptr || element.hit_opaque())
                return &element;
            return nullptr;
        }

        // 터치 끌기의 임자다. 휠의 탐색과 같은 규칙(역순·clip·비활성 가지·
        // `hit_opaque` 방벽)이고, 다른 것은 **축이 맞는 것만 임자**라는 점이다.
        // 축이 다른 흘리는 컨테이너는 방벽이 아니라 지나친다 — 가로 탭 막대가
        // 세로 화면의 세로 손짓을 삼키지 않는다.
        //  - 그래도 그 컨테이너가 `hit_opaque`면 방벽이다. 막는 것은 축과 무관하다.
        template<typename accepts_type>
        [[nodiscard]] const ui_element* pan_owner_at(const ui_element& element, const float x, const float y, const accepts_type& accepts)
        {
            if (element.visible() == false || element.enabled() == false)
                return nullptr;
            if (element.clip_children() && element.bounds().contains(x, y) == false)
                return nullptr;
            const std::span<const std::unique_ptr<ui_element>> children { element.children() };
            for (std::size_t index = children.size(); index > 0u; --index)
                if (const ui_element* const found { pan_owner_at(*children[index - 1u], x, y, accepts) }; found != nullptr)
                    return found;
            if (element.bounds().contains(x, y) == false)
                return nullptr;
            if (accepts(element) || element.hit_opaque())
                return &element;
            return nullptr;
        }

        // 뿌리에서 `id`까지의 길이다 (뿌리가 앞, 대상이 뒤).
        // 찾지 못하면 `path`는 비어 있다.
        [[nodiscard]] bool collect_ancestry(const ui_element& element, const ui_element_id& id, std::vector<const ui_element*>& path)
        {
            path.push_back(&element);
            if (element.id() == id)
                return true;
            for (const std::unique_ptr<ui_element>& child : element.children())
                if (collect_ancestry(*child, id, path))
                    return true;
            path.pop_back();
            return false;
        }

        // 대상이 그 창 안에서 실제로 보이는 세로 구간이다.
        //
        // 겹치는 데가 없으면 창 전체다 — 아직 들이지 못한 대상은 "이 창 어딘가에
        // 설 것"이므로 다음 겹에는 창을 통째로 이름 대는 것이 맞다.
        //  - 그 창의 축만 본다. 다른 축은 창의 자리를 그대로 물려주어 바깥 겹의
        //    판정이 안쪽 창의 그 축 자리를 쓰게 한다.
        [[nodiscard]] rect_f visible_part_of(const rect_f& target, const rect_f& viewport, const scroll_axis axis) noexcept
        {
            if (axis == scroll_axis::horizontal)
            {
                const float left { target.x > viewport.x ? target.x : viewport.x };
                const float target_right { target.x + target.width };
                const float viewport_right { viewport.x + viewport.width };
                const float right { target_right < viewport_right ? target_right : viewport_right };
                if (right <= left)
                    return viewport;
                return { left, viewport.y, right - left, viewport.height };
            }
            const float top { target.y > viewport.y ? target.y : viewport.y };
            const float target_bottom { target.y + target.height };
            const float viewport_bottom { viewport.y + viewport.height };
            const float bottom { target_bottom < viewport_bottom ? target_bottom : viewport_bottom };
            if (bottom <= top)
                return viewport;
            return { viewport.x, top, viewport.width, bottom - top };
        }
    } // namespace

    std::vector<input_action> route_wheel(const ui_tree& tree, const float x, const float y, const float delta)
    {
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return {};
        const ui_element* const owner { wheel_owner_at(*root, x, y) };
        if (owner == nullptr)
            return {};
        const scroll_source* const source { owner->scroll() };
        // 흘리지 않고 삼키기만 하는 것이 걸렸으면 아무 일도 하지 않는다.
        if (source == nullptr || source->scroll == nullptr)
            return {};
        return { source->scroll(delta) };
    }

    const ui_element* zoom_owner_at(const ui_tree& tree, const float x, const float y)
    {
        const ui_element* const owner { tree.root() != nullptr ? wheel_owner_at(*tree.root(), x, y) : nullptr };
        return owner != nullptr && owner->zoom() != nullptr ? owner : nullptr;
    }

    std::vector<input_action> route_wheel(const ui_tree& tree, const mouse_wheel_event& event, const float scroll_delta)
    {
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return {};
        // 가로 휠(틸트 휠·터치패드 가로 밀기)은 **가로 창의 것**이다. 세로 창을 흘리지 않는다.
        //  - 축이 다른 흘리는 창은 방벽이 아니라 지나친다 — 세로 목록 안에 선 포인터의 가로 휠이 그 목록을
        //    감싼 가로 띠에 간다 (터치 끌기의 `pan_owner_at`과 같은 규칙). `hit_opaque` 방벽은 그대로 막는다.
        //  - 확대 보기는 두 축을 다 받는다.
        //  - 세로 휠은 지금처럼 축을 묻지 않는다. 가로 띠 위에서 굴린 세로 휠이 그 띠를 흘리는 것이 앱이 표로
        //    지어 오던 동작이다 (`scroll_source::axis`).
        const auto takes_horizontal = [](const ui_element& element) {
            const scroll_source* const source { element.scroll() };
            return element.zoom() != nullptr || (source != nullptr && source->scroll != nullptr && source->axis == scroll_axis::horizontal);
        };
        const ui_element* const owner { event.horizontal ? pan_owner_at(*root, event.x, event.y, takes_horizontal) : wheel_owner_at(*root, event.x, event.y) };
        if (owner == nullptr)
            return {};
        if (const zoom_source* const source { owner->zoom() }; source != nullptr)
        {
            const float scale { positive_scale(source->scale) };
            if (event.control || (source->wheel == zoom_wheel_mode::zoom && event.horizontal == false))
            {
                if (source->zoom_by == nullptr || event.delta == 0.0f)
                    return {};
                const rect_f& box { owner->bounds() };
                const zoom_point anchor { (event.x - box.x - box.width / 2.0f) / scale, (event.y - box.y - box.height / 2.0f) / scale };
                return { source->zoom_by(std::pow(1.2f, std::clamp(event.delta / 120.0f, -64.0f, 64.0f)), anchor) };
            }
            if (source->pan_by == nullptr || scroll_delta == 0.0f)
                return {};
            // 가로 휠 양수는 오른쪽이다. 세로 양수는 위쪽이라 내용 이동 부호가 갈린다.
            const float delta { event.horizontal ? scroll_delta : -scroll_delta };
            return { source->pan_by(event.horizontal || event.shift ? zoom_point { delta, 0.0f } : zoom_point { 0.0f, delta }) };
        }
        const scroll_source* const source { owner->scroll() };
        if (source == nullptr || source->scroll == nullptr)
            return {};
        // 가로 휠 양수는 오른쪽으로 가자는 뜻이라 offset이 는다. `scroll_delta`는 세로 휠의 부호(위로 굴림 = 음수)로
        // 온 값이므로 가로에서는 뒤집는다.
        return { source->scroll(event.horizontal ? -scroll_delta : scroll_delta) };
    }

    std::optional<pan_target> route_pan(const ui_tree& tree, const float x, const float y, const scroll_axis axis)
    {
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return std::nullopt;
        const auto accepts = [axis](const ui_element& element) {
            const scroll_source* const source { element.scroll() };
            return source != nullptr && source->scroll != nullptr && source->axis == axis;
        };
        const ui_element* const owner { pan_owner_at(*root, x, y, accepts) };
        // 막기만 하는 것이 걸렸으면 없다.
        if (owner == nullptr || accepts(*owner) == false)
            return std::nullopt;
        const scroll_source& source { *owner->scroll() };
        return pan_target { owner->id(), axis, positive_scale(source.scale), source.scroll, source.at_start, source.at_end };
    }

    std::optional<pan_target> route_pan(const ui_tree& tree, const float x, const float y, const scroll_axis axis, const std::span<const pan_route> routes)
    {
        const ui_element* const root { tree.root() };
        if (root == nullptr)
            return std::nullopt;
        const auto route_of = [axis, routes](const ui_element& element) -> const pan_route* {
            for (const pan_route& route : routes)
                if (route.id == element.id() && route.axis == axis && route.scroll != nullptr)
                    return &route;
            return nullptr;
        };
        const ui_element* const owner { pan_owner_at(*root, x, y, [&route_of](const ui_element& element) { return route_of(element) != nullptr; }) };
        const pan_route* const route { owner != nullptr ? route_of(*owner) : nullptr };
        if (route == nullptr)
            return std::nullopt;
        return pan_target { route->id, axis, positive_scale(route->scale), route->scroll, route->at_start, route->at_end };
    }

    bool valid_touch_gesture_config(const touch_gesture_config& config) noexcept
    {
        const auto distance = [](const float value) { return std::isfinite(value) && value > 0.0f; };
        return distance(config.minimum_pinch_distance) && distance(config.pan_start_distance) && distance(config.press_move_tolerance) && config.pan_start_time.count() > 0
            && config.long_press_time.count() > 0 && config.pan_start_time < config.long_press_time;
    }

    std::vector<input_action> route_reveal(const ui_tree& tree, const ui_element_id& target)
    {
        const ui_element* const root { tree.root() };
        const ui_element* const focused { tree.find(target) };
        if (root == nullptr || focused == nullptr)
            return {};
        std::vector<const ui_element*> path {};
        if (collect_ancestry(*root, target, path) == false || path.size() < 2u)
            return {};

        // 안쪽 창부터 바깥으로 **이어서** 들인다.
        //
        // 한 겹만 보고 끝내면 겹친 창에서 초점이 화면 밖에 남는다 — 행은 안쪽
        // 목록 안에서 보이는데 그 목록이 바깥 판에서 밀려 나가 있는 경우다.
        //
        // 다음 겹의 대상은 안쪽 스크롤을 적용한 뒤 이 창에서 보이는 부분이다.
        //  - 겹치는 부분을 쓰는 것이 요점이다. 창 전체를 그대로 넘기면, 안쪽 창이
        //    바깥 창보다 **길** 때 `scroll_delta_to_reveal`이 앞 끝을 맞춰 0을
        //    답한다 (layout_metrics.h: 대상이 창보다 길면 앞쪽 끝을 맞춘다). 그러면
        //    800px짜리 안쪽 목록의 맨 아래 행이 300px 바깥 창에서 잘린 채로 남고
        //    아무 메시지도 나오지 않는다.
        //  - **전부 보이면 빈 목록이다.** 이것이 방벽이다 — 없으면 화살표를 누를
        //    때마다 0짜리 스크롤 메시지가 logic을 깨워 tree를 다시 짓는다.
        std::vector<input_action> actions {};
        rect_f box { focused->bounds() };
        for (std::size_t index = path.size() - 1u; index > 0u; --index)
        {
            const ui_element* const viewport { path[index - 1u] };
            const scroll_source* const source { viewport->scroll() };
            if (source == nullptr || source->scroll == nullptr)
                continue;
            // 얼마나 흘릴지는 그 창이 답한다 (표 있는 짝과 같은 줄).
            if (const float delta { viewport->scroll_delta_to_reveal(box) }; delta != 0.0f)
            {
                actions.push_back(source->scroll(delta));
                (source->axis == scroll_axis::horizontal ? box.x : box.y) -= delta * positive_scale(source->scale);
            }
            box = visible_part_of(box, viewport->bounds(), source->axis);
        }
        return actions;
    }

    void apply_text_edit(text::text_edit_state& state, const text_edit_request& request, const text_insert_filter& filter)
    {
        if (request.sequence != 0)
            state.applied_sequence = request.sequence;
        // affinity는 caret 놓기만 세운다. 글을 바꾸거나 caret을 글자·낱말로 옮기면 그 자리는 윗줄 끝이 아니다.
        state.caret_upstream = false;
        const auto filtered { [&filter](const std::u8string& value) { return filter != nullptr ? filter(value) : value; } };
        switch (request.command)
        {
        case text::text_edit_command::insert:
            text::text_edit_insert(state, filtered(request.text));
            return;
        case text::text_edit_command::backspace:
            text::text_edit_backspace(state);
            return;
        case text::text_edit_command::delete_forward:
            text::text_edit_delete_forward(state);
            return;
        case text::text_edit_command::delete_word_left:
            text::text_edit_delete_word_left(state);
            return;
        case text::text_edit_command::delete_word_right:
            text::text_edit_delete_word_right(state);
            return;
        case text::text_edit_command::undo:
            static_cast<void>(text::text_edit_undo(state));
            return;
        case text::text_edit_command::redo:
            static_cast<void>(text::text_edit_redo(state));
            return;
        case text::text_edit_command::move_left:
            text::text_edit_move(state, text::text_edit_motion::left, request.extend);
            return;
        case text::text_edit_command::move_right:
            text::text_edit_move(state, text::text_edit_motion::right, request.extend);
            return;
        case text::text_edit_command::move_word_left:
            text::text_edit_move(state, text::text_edit_motion::word_left, request.extend);
            return;
        case text::text_edit_command::move_word_right:
            text::text_edit_move(state, text::text_edit_motion::word_right, request.extend);
            return;
        case text::text_edit_command::move_line_start:
            text::text_edit_move(state, text::text_edit_motion::line_start, request.extend);
            return;
        case text::text_edit_command::move_line_end:
            text::text_edit_move(state, text::text_edit_motion::line_end, request.extend);
            return;
        case text::text_edit_command::select_all:
            text::text_edit_select_all(state);
            return;
        case text::text_edit_command::select_word:
            text::text_edit_select_word(state, request.offset);
            return;
        case text::text_edit_command::cut:
            static_cast<void>(text::text_edit_erase_selection(state));
            return;
        case text::text_edit_command::clear:
            text::text_edit_set_text(state, {}, false);
            return;
        case text::text_edit_command::replace_all:
            text::text_edit_replace_all(state, filtered(request.text), request.offset);
            if (request.anchor.has_value())
                state.anchor = text::text_edit_clamp_offset(state.text, *request.anchor);
            return;
        case text::text_edit_command::place_caret:
            text::text_edit_place_caret(state, request.offset, request.extend);
            state.caret_upstream = request.upstream && state.caret == request.offset;
            return;
        }
    }

    text_input_view make_text_input_view(const text::text_edit_state& state, const std::optional<text_composition_event>& composition, const text_input_target target)
    {
        text_input_view view {};
        view.text = state.text;
        view.caret = state.caret;
        view.anchor = state.anchor;
        view.caret_upstream = state.caret_upstream;
        view.applied_sequence = state.applied_sequence;
        if (composition.has_value() == false || composition->target != target)
            return view;

        view.composing = true;
        view.composition_text = composition->text;
        view.composition_caret = composition->caret;
        view.composing_begin = composition->composing_begin;
        view.composing_end = composition->composing_end;
        return view;
    }

    std::u8string_view search_query(const text_input_view& value, const std::function<bool(std::u8string_view)>& any_match)
    {
        const std::u8string_view displayed { value.displayed_text() };
        // 조합 중이 아니면 고를 것이 하나뿐이다.
        // 확정된 글이 비어 있어도 마찬가지다 — 빈 질의로 물러서면 "전부"가 된다.
        if (value.composing == false || value.text.empty() || any_match == nullptr)
            return displayed;
        if (any_match(displayed))
            return displayed;
        // 조합 중인 글자를 뺀 자리로 물러선다.
        return value.text;
    }

    std::vector<input_action> interaction_controller::process_press(const pointer_pressed_event& event)
    {
        // 가운데 버튼은 선택·초점·끌기 상태를 바꾸지 않는다.
        if (event.button == pointer_button::middle)
            return {};
        inertia_.reset();
        if (event.device == pointer_device::touch)
            return process_touch_press(event);
        // 실제 마우스·펜의 새 누름은 진행 중인 터치 조작을 취소하고 시작한다.
        // 취소된 접촉의 나머지 이벤트는 id가 맞는 접촉이 없어 삼켜진다.
        if (touch_.has_value())
            cancel_touch();
        last_view_tap_.reset();
        // 새 누름은 기존 조작을 인계받는다. 옛 선택·손잡이·drop 후보를 섞지 않는다.
        cancel_pointer_gesture();

        const ui_tree* const tree { surface_tree(event.surface) };
        if (tree == nullptr)
            return {};

        const ui_element* const hit { tree->hit_test(event.x, event.y) };
        if (hit == nullptr || hit->enabled() == false)
        {
            // 빈 곳이나 비활성 element를 누른 것도 "다른 곳을 누른" 것이라
            // 텍스트 초점을 함께 거둔다. hit가 없다고 초점을 남기면
            // 배경을 눌러도 caret이 깜빡이고 타이핑이 옛 박스로 간다.
            clear_press();
            clear_focus();
            return {};
        }
        pressed_surface_ = event.surface;
        pressed_modifiers_ = modifiers_of(event);
        pointer_contact_ = pointer_contact { event.device, event.pointer_id, event.surface, event.button };

        // 앱이 「어느 판을 만졌는가」를 듣는다. 이 누름의 다른 액션보다 앞선다.
        std::vector<input_action> actions {};
        if (policy_ != nullptr && event.button == pointer_button::left)
            actions = policy_->on_press(*tree, *hit, event);
        const auto with_press = [&actions](std::vector<input_action> more) {
            for (input_action& action : more)
                actions.push_back(std::move(action));
            return std::move(actions);
        };

        apply_press_focus(*tree, hit, event.surface, event.time);
        const ui_element* const view { zoom_owner_at(*tree, event.x, event.y) };
        // 마우스·펜은 빈 곳만 이동한다. 내용의 클릭·선택·항목 끌기는 그대로 둔다.
        if (event.button == pointer_button::left && view != nullptr && hit->id() == view->id() && view->zoom()->pan_by)
        {
            view_drag_ = view_drag { view->id(), event.x, event.y, event.x, event.y, std::nullopt };
            // 펜만 경계 쓸기를 잰다. 마우스 끌기는 쪽을 넘기는 몸짓이 아니다.
            if (event.device == pointer_device::pen && view->zoom()->edge_swipe && view->zoom()->pan_room)
                view_drag_->edge = view_edge_track { view->zoom()->pan_room, {}, {}, motion_track { { { event.time, event.x, event.y } } }, false };
            return actions;
        }

        // 텍스트 박스는 누른 자리로 caret이 가고, 누른 채 끌면 범위가 잡힌다.
        // 임계 시간·거리 안의 두 번째 누름은 그 자리의 낱말을 고른다.
        const std::optional<text_input_target> hit_target { text_target(hit->id().kind) };
        if (event.button == pointer_button::left && hit_target.has_value())
        {
            // 연타는 표면을 넘지 않는다 — 좌표가 표면마다 자기 client 기준이라
            // 표면을 빼면 다른 창의 같은 자리가 같은 자리로 보인다. 장치도 넘지 않는다.
            const bool same_target { hit->id() == last_click_id_ && event.surface == last_click_surface_ && event.device == last_click_device_ };
            const bool repeat { same_target && event.time - last_click_time_ <= config_.double_click_time };
            const bool same_spot { repeat && distance_between(last_click_x_, last_click_y_, event.x, event.y) <= config_.double_click_distance };
            click_streak_ = same_spot ? click_streak_ + 1 : 1;
            last_click_id_ = hit->id();
            last_click_device_ = event.device;
            last_click_surface_ = event.surface;
            last_click_time_ = event.time;
            last_click_x_ = event.x;
            last_click_y_ = event.y;

            pressed_id_ = hit->id();
            pressed_button_ = event.button;
            pressed_x_ = event.x;
            pressed_y_ = event.y;
            drag_candidate_ = false;
            snapshot_.pressed = hit->id();
            snapshot_.pressed_surface = event.surface;
            // 두 번째 누름은 낱말, 세 번째부터는 전부다.
            // Shift+클릭은 선택을 늘린다.
            if (click_streak_ >= 3)
            {
                text_drag_id_ = {};
                return with_press({ text_edit_action(*hit_target, text::text_edit_command::select_all) });
            }
            const bool word { click_streak_ == 2 };
            text_drag_id_ = word ? ui_element_id {} : hit->id();
            return with_press(place_text_caret(*hit, event.x, event.y, word == false && event.shift, word));
        }

        pressed_id_ = hit->id();
        pressed_button_ = event.button;
        pressed_x_ = event.x;
        pressed_y_ = event.y;
        drag_candidate_ = event.button == pointer_button::left && hit->drag() != nullptr;
        if (event.button == pointer_button::left)
        {
            snapshot_.pressed = hit->id();
            snapshot_.pressed_surface = event.surface;
        }

        // 스크롤 막대는 누른 순간부터 끌기가 시작된다.
        // 임계 거리를 두지 않는다.
        const pointer_drag_target* const handler { hit->pointer_drag() };
        if (event.button == pointer_button::left && handler != nullptr && handler->start_distance > 0.0f)
        {
            // 시작 거리를 둔 손잡이는 그 거리를 넘을 때까지 보통 누름이다 — 떼면 클릭이다.
            // 끌기가 이기므로 같은 element의 drag & drop 후보는 세우지 않는다.
            pointer_drag_pending_ = true;
            drag_candidate_ = false;
            return actions;
        }
        if (event.button == pointer_button::left && handler != nullptr)
        {
            pointer_drag_id_ = hit->id();
            pointer_drag_held_ = *handler;
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            pointer_drag_modifiers_ = modifiers_of(event);
            pointer_drag_motion_ = {};
            if (handler->on_press)
                return with_press(handler->on_press(action_context(pointer_drag_id_, event.x, event.y, pointer_drag_modifiers_)));
        }
        return actions;
    }

    void interaction_controller::apply_press_focus(const ui_tree& tree, const ui_element* const hit, const std::u8string& surface, const std::chrono::steady_clock::time_point time)
    {
        if ((hit != nullptr && hit->preserves_pointer_focus()) || (tree.root() != nullptr && tree.root()->preserves_pointer_focus()))
            return;
        // 자리인 element를 누르면 초점을 주고, 아니면 거둔다.
        // 초점 시각은 caret 깜빡임의 위상 기준이고, 눌린 표면이 초점의 표면이다.
        //  - 텍스트 박스는 앱 정책이 정하므로(`text_target_of`) 자리 판정과 따로 묻는다.
        //    자리가 아니어도 텍스트 박스면 초점을 준다 — 문자가 갈 곳이 있어야 한다.
        //  - 눌러서 잡은 초점에는 테를 그리지 않는다 (`focus_visible`).
        //  - 빈 곳이나 비활성 element를 누른 것도 "다른 곳을 누른" 것이라 거둔다.
        if (hit == nullptr || hit->enabled() == false)
        {
            clear_focus();
            return;
        }
        const std::optional<text_input_target> hit_target { text_target(hit->id().kind) };
        // 초점을 가진 텍스트 칸 **안**을 누른 것은 "다른 곳을 누른" 것이 아니다.
        //  - 칸이 자기 안에 둔 부품(지우기 버튼)은 자리도 텍스트 대상도 아니라
        //    그냥 두면 누를 때마다 caret이 사라져 지우고 이어 칠 수가 없다.
        //  - 초점을 그 부품으로 **옮기지도 않는다.** 초점은 그대로 칸에 남는다 —
        //    부품은 누르는 자리일 뿐 글이 갈 곳이 아니다.
        const bool inside_focused_input { snapshot_.focused_input != ui_element_id {} && tree.within(snapshot_.focused_input, hit->id()) };
        const ui_element_id& focus_id { hit->pointer_focus_target() };
        const ui_element* const pointer_focus { focus_id == hit->id() ? hit : tree.find(focus_id) };
        if (hit_target.has_value() || (pointer_focus != nullptr && pointer_focus->focusable()))
        {
            snapshot_.focused = hit_target.has_value() ? hit->id() : pointer_focus->id();
            snapshot_.focused_input = hit_target.has_value() ? hit->id() : ui_element_id {};
            snapshot_.focused_surface = surface;
            snapshot_.focus_started_at = time;
            snapshot_.focus_visible = false;
        }
        else if (inside_focused_input == false)
        {
            clear_focus();
        }
    }

    std::vector<input_action> interaction_controller::process_release(const pointer_released_event& event)
    {
        // 가운데 버튼은 선택·초점·끌기 상태를 바꾸지 않는다.
        if (event.button == pointer_button::middle)
            return {};
        if (event.device == pointer_device::touch)
            return process_touch_release(event);
        // 터치가 조작을 쥔 동안의 마우스·펜 뗌은 짝인 누름이 없다
        // (그 장치의 새 누름은 터치를 먼저 취소했다).
        if (touch_.has_value())
            return {};

        if (pointer_contact_.has_value() == false || pointer_contact_->device != event.device || pointer_contact_->id != event.pointer_id || pointer_contact_->button != event.button)
            return {};
        if (pointer_contact_->surface != event.surface)
        {
            // 같은 포인터가 모르는 표면에서 끝났으면 기존 계약대로 조작만 거둔다.
            // 그 좌표로 drop·클릭을 만들지는 않는다. 살아 있는 다른 표면의 뗌은 삼킨다.
            if (surface_tree(event.surface) == nullptr)
                cancel_pointer_gesture();
            return {};
        }
        pointer_contact_.reset();
        if (view_drag_.has_value())
        {
            const view_drag previous { *view_drag_ };
            view_drag_.reset();
            clear_press();
            const ui_tree* const tree { surface_tree(event.surface) };
            const ui_element* const owner { tree != nullptr ? live_zoom_view(*tree, previous.id, previous.start_x, previous.start_y) : nullptr };
            if (owner == nullptr || owner->id() != previous.id || owner->zoom()->pan_by == nullptr)
                return {};
            const float scale { positive_scale(owner->zoom()->scale) };
            const zoom_point delta { (event.x - previous.x) / scale, (event.y - previous.y) / scale };
            std::vector<input_action> actions {};
            if (delta != zoom_point {})
                actions.push_back(owner->zoom()->pan_by(delta));
            if (previous.edge.has_value())
            {
                view_edge_track edge { *previous.edge };
                record_view_pan(edge, delta, event.time, event.x, event.y);
                if (std::optional<input_action> swipe { finish_view_edge(edge, *owner) }; swipe.has_value())
                    actions.push_back(std::move(*swipe));
            }
            return actions;
        }

        // tree가 없어도 **거두기까지는 반드시 닿는다.**
        // 뗌은 잡은 것을 놓는 유일한 계기인데, 표면이 사라진 뒤의 합성 뗌
        // (HWND 파괴·capture 상실)은 바로 그 사라진 표면 id로 온다. 여기서
        // 먼저 돌아서면 남은 `text_drag_id_`가 그 다음 포인터 이동을 통째로
        // 삼킨다 (multi-window-design.md).
        const ui_tree* const tree { surface_tree(event.surface) };

        // 텍스트 범위 선택을 놓는 것은 클릭이 아니다.
        // 선택은 이미 잡혀 있다.
        if (text_drag_id_ != ui_element_id {} && event.button == pointer_button::left)
        {
            text_drag_id_ = {};
            clear_press();
            return {};
        }

        // 스크롤 막대를 놓는 것은 클릭이 아니다.
        // 끌기만 끝낸다.
        if (pointer_drag_id_ != ui_element_id {} && event.button == pointer_button::left)
        {
            // 끝을 듣는 손잡이에는 끝을 알린다 (시트의 멈춤점).
            //  - 포인터를 잃은 끝(`cancelled`)에는 뗀 자리도 수식 키도 없다. 마지막 이동의 값으로 알린다.
            std::vector<input_action> actions {};
            if (event.cancelled)
                actions = end_pointer_drag(pointer_drag_x_, pointer_drag_y_, {}, pointer_drag_modifiers_);
            else
                actions = end_pointer_drag(event.x, event.y, pointer_drag_motion_.velocity_at(event.time), modifiers_of(event));
            pointer_drag_id_ = {};
            clear_press();
            return actions;
        }

        // drag를 끝낸다.
        // 수락하는 대상 위에서만 drop 액션이 실행된다.
        if (snapshot_.drag.has_value())
        {
            // 포인터를 잃었으면 놓은 것이 아니다. 끌기만 거둔다.
            if (event.cancelled)
            {
                cancel_pointer_gesture();
                return {};
            }
            if (event.button == pointer_button::left)
                return finish_drag(locate_drag(event), modifiers_of(event), event.screen);
            return {};
        }

        // 포인터를 잃은 끝은 클릭이 아니다.
        if (event.cancelled)
        {
            clear_press();
            return {};
        }

        // 여기부터는 tree를 요구한다 (hit test와 클릭 판정).
        // 거둘 것은 위에서 이미 거뒀으므로 누름만 정리하고 물러선다.
        if (tree == nullptr)
        {
            clear_press();
            return {};
        }

        const ui_element* const hit { tree->hit_test(event.x, event.y) };
        const ui_element_id pressed { pressed_id_ };
        const pointer_button pressed_button { pressed_button_ };
        clear_press();

        // 클릭은 같은 대상 위에서의 누름과 뗌이다.
        // 누른 뒤 벗어나면 아무 일도 없다.
        if (hit == nullptr || hit->enabled() == false || (hit->id() == pressed) == false || event.button != pressed_button)
            return {};

        // 키보드 탐색 초점 등 앱 쪽 입력 상태를 클릭에 잇는다.
        if (policy_ != nullptr)
            policy_->on_click(*hit);

        // 더블 클릭: 같은 대상을 임계 시간·거리 안에 다시
        // 왼쪽 클릭했고 등록된 액션이 있을 때만이다.
        // 등록이 없으면 왼쪽 클릭 두 번으로 처리한다.
        //  - **표면도 같아야 한다.** id는 tree 안에서만 안정적이고 좌표는 표면마다
        //    자기 client 기준이라, 표면을 빼면 A창을 누른 직후 B창의 같은 id를 같은
        //    자리에서 누르는 것이 연타가 된다 (multi-window-design.md).
        ui_trigger trigger { event.button == pointer_button::right ? ui_trigger::right_click : ui_trigger::left_click };
        if (trigger == ui_trigger::left_click && hit->id() == last_click_id_ && event.surface == last_click_surface_ && event.device == last_click_device_
            && event.time - last_click_time_ <= config_.double_click_time && distance_between(last_click_x_, last_click_y_, event.x, event.y) <= config_.double_click_distance
            && hit->action(ui_trigger::double_click) != nullptr)
        {
            trigger = ui_trigger::double_click;
            last_click_id_ = {};
            last_click_surface_.clear();
        }
        else if (trigger == ui_trigger::left_click)
        {
            last_click_id_ = hit->id();
            last_click_device_ = event.device;
            last_click_surface_ = event.surface;
            last_click_time_ = event.time;
            last_click_x_ = event.x;
            last_click_y_ = event.y;
        }
        return run_trigger(*hit, trigger, event.x, event.y, modifiers_of(event));
    }

    namespace {
        // 누를 때 잡은 element가 지금 tree에서도 그 자리의 임자인가.
        // 사라졌거나 숨었거나 비활성이 됐거나 새 modal이 덮었으면 nullptr다 —
        // 그 접촉의 뗌은 액션 없이 끝난다.
        [[nodiscard]] const ui_element* live_touch_target(const ui_tree& tree, const ui_element_id& id, const float x, const float y)
        {
            const ui_element* const target { tree.find(id) };
            if (target == nullptr || target->enabled() == false || tree.visibly_contains(id) == false)
                return nullptr;
            const ui_element* const hit { tree.hit_test(x, y) };
            return hit != nullptr && hit->id() == id ? target : nullptr;
        }

        // 누른 손잡이가 아직 살아 있는가다. 손가락을 따라 자리를 옮기는 손잡이(`follows_pointer`)는 누른 자리가 아니라
        // id·가시성·가둠으로 본다. 그 밖의 손잡이는 누른 자리를 덮은 것이 끊는다 (`live_touch_target`).
        [[nodiscard]] const ui_element* live_touch_handle(const ui_tree& tree, const ui_element_id& id, const float x, const float y)
        {
            const ui_element* const target { tree.find(id) };
            const pointer_drag_target* const handler { target != nullptr ? target->pointer_drag() : nullptr };
            if (handler != nullptr && handler->follows_pointer)
                return target->enabled() && tree.visibly_contains(id) && tree.within_focus_trap(id) ? target : nullptr;
            return live_touch_target(tree, id, x, y);
        }

        // (x, y)를 덮고 `axis`로 끌기를 넘겨받는 가장 안쪽 컨테이너다. 막기만 하는 것이 먼저 걸리면 없다.
        [[nodiscard]] const ui_element* handoff_owner_at(const ui_tree& tree, const float x, const float y, const scroll_axis axis)
        {
            const ui_element* const root { tree.root() };
            if (root == nullptr)
                return nullptr;
            const auto accepts = [axis](const ui_element& element) {
                const pan_handoff_target* const target { element.pan_handoff() };
                return target != nullptr && target->drag != nullptr && target->axis == axis;
            };
            const ui_element* const owner { pan_owner_at(*root, x, y, accepts) };
            return owner != nullptr && accepts(*owner) ? owner : nullptr;
        }

        // 넘겨받은 컨테이너가 아직 살아 있는가다. 시트는 손가락을 따라 내려가 누른 자리를 떠나므로 id로 찾는다.
        [[nodiscard]] const pan_handoff_target* live_handoff(const ui_tree& tree, const ui_element_id& id, const scroll_axis axis)
        {
            const ui_element* const owner { tree.find(id) };
            const pan_handoff_target* const target { owner != nullptr ? owner->pan_handoff() : nullptr };
            if (target == nullptr || target->drag == nullptr || target->axis != axis || owner->enabled() == false || tree.visibly_contains(id) == false || tree.within_focus_trap(id) == false)
                return nullptr;
            return target;
        }

        // 손가락이 축 방향으로 움직인 만큼 그 컨테이너를 흘린다.
        // 손가락을 위로 올리면 내용도 따라 올라간다 — offset 증가다 (휠의 양수와 같다).
        [[nodiscard]] std::vector<input_action> pan_step(const pan_target& pan, const float from_x, const float from_y, const float to_x, const float to_y)
        {
            const float along { pan.axis == scroll_axis::horizontal ? from_x - to_x : from_y - to_y };
            const float delta { along / pan.scale };
            if (delta == 0.0f || pan.scroll == nullptr)
                return {};
            return { pan.scroll(delta) };
        }
    } // namespace

    bool interaction_controller::pointer_gesture_active() const noexcept
    {
        const bool internal_drag { snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() };
        return pointer_contact_.has_value() || pressed_id_ != ui_element_id {} || text_drag_id_ != ui_element_id {} || pointer_drag_id_ != ui_element_id {} || internal_drag;
    }

    std::optional<pan_target> interaction_controller::resolve_pan(const ui_tree& tree, const float x, const float y, const scroll_axis axis)
    {
        if (policy_ != nullptr)
            return policy_->pan_target_at(tree, x, y, axis);
        return route_pan(tree, x, y, axis);
    }

    std::vector<input_action> interaction_controller::process_touch_press(const pointer_pressed_event& event)
    {
        if (touch_.has_value())
        {
            touch_contact& contact { *touch_ };
            if (contact.id == event.pointer_id || contact.surface != event.surface || contact.view == ui_element_id {} || contact.second.has_value() || contact.config.pinch_enabled == false
                || (contact.phase != touch_phase::pending && contact.phase != touch_phase::view_pan))
                return {};
            const ui_tree* const tree { surface_tree(contact.surface) };
            const ui_element* const owner { tree != nullptr ? live_zoom_view(*tree, contact.view, contact.start_x, contact.start_y) : nullptr };
            const ui_element* const second { tree != nullptr ? zoom_owner_at(*tree, event.x, event.y) : nullptr };
            if (owner == nullptr || second == nullptr || owner->id() != contact.view || second->id() != contact.view || owner->zoom()->zoom_by == nullptr || owner->zoom()->pan_by == nullptr)
                return {};
            contact.second = touch_contact::second_contact { event.pointer_id, event.x, event.y };
            contact.phase = touch_phase::pinching;
            contact.pinched = true;
            if (contact.edge.has_value())
                contact.edge->pinched = true;
            // 핀치 앞의 탭과 핀치 뒤의 탭은 짝이 아니다.
            last_view_tap_.reset();
            contact.moved_action = true;
            clear_press();
            return {};
        }
        // 컨트롤 조작은 한 접촉이 한다. 이미 쥔 접촉이 있으면 추가 손가락이고,
        // 마우스·펜이 조작 중이면 그쪽이 임자다. 둘 다 삼킨다.
        if (touch_.has_value() || pointer_gesture_active())
            return {};
        const ui_tree* const tree { surface_tree(event.surface) };
        if (tree == nullptr)
            return {};

        touch_contact contact {};
        contact.id = event.pointer_id;
        contact.surface = event.surface;
        contact.config = config_.touch;
        contact.scale = positive_scale(event.scale);
        contact.pressed_at = event.time;
        contact.start_x = event.x;
        contact.start_y = event.y;
        contact.last_x = event.x;
        contact.last_y = event.y;
        contact.modifiers = modifiers_of(event);
        // 흘릴 후보가 어느 축에도 없으면 시간 창 동안 끌기를 보류할 까닭이 없다.
        //  - 클릭 대상이 없는 여백·일반 글 위에서도 후보는 선다 (휠과 같은 탐색이다).
        //  - 끌기를 넘겨받는 컨테이너(시트)도 후보다. 안에 흘리는 창이 없는 짧은 시트도 끌어 내린다.
        const auto pan_candidate
            = [this, tree, &event](const scroll_axis axis) { return resolve_pan(*tree, event.x, event.y, axis).has_value() || handoff_owner_at(*tree, event.x, event.y, axis) != nullptr; };
        contact.pan_open = contact.config.pan_enabled && (pan_candidate(scroll_axis::vertical) || pan_candidate(scroll_axis::horizontal));

        // 초점은 아직 옮기지 않는다. 스크롤이 될 접촉이 칸의 초점과 IME를 빼앗으면
        // 안 된다 — 탭이 확정되는 뗌에서 옮긴다.
        const ui_element* const hit { tree->hit_test(event.x, event.y) };
        const ui_element* const view { zoom_owner_at(*tree, event.x, event.y) };
        if (view != nullptr && (hit == nullptr || (hit->enabled() && hit->pointer_drag() == nullptr && hit->drag() == nullptr)) && view->zoom()->pan_by)
        {
            contact.view = view->id();
            contact.scale = positive_scale(view->zoom()->scale);
            contact.pan_open = false;
            // 경계 쓸기를 알리는 보기만 잰다. 기준은 누를 때의 상태다.
            if (view->zoom()->edge_swipe && view->zoom()->pan_room)
                contact.edge = view_edge_track { view->zoom()->pan_room, {}, {}, motion_track { { { event.time, event.x, event.y } } }, false };
            if (hit == nullptr || hit->id() == view->id())
                contact.phase = touch_phase::view_pan;
        }
        pressed_surface_ = event.surface;
        if (hit == nullptr || hit->enabled() == false)
        {
            touch_ = std::move(contact);
            return {};
        }
        contact.target = hit->id();
        pressed_modifiers_ = contact.modifiers;
        // 누름 관찰은 터치도 누르는 순간이다. 탭이 될지 스크롤이 될지는 아직 모른다.
        std::vector<input_action> actions {};
        if (policy_ != nullptr)
            actions = policy_->on_press(*tree, *hit, event);
        pressed_id_ = hit->id();
        pressed_button_ = pointer_button::left;
        pressed_x_ = event.x;
        pressed_y_ = event.y;
        snapshot_.pressed = hit->id();
        snapshot_.pressed_surface = event.surface;

        const pointer_drag_target* const handler { hit->pointer_drag() };
        // 시작 거리를 둔 손잡이는 터치 몸짓의 규칙을 따른다 — 탭·스크롤·길게 누르기가 먼저이고, 스크롤이 될 수
        // 없게 된 뒤 허용치를 넘기면 끌기가 선다 (대기 단계의 일반 끌기 자리).
        if (handler == nullptr || handler->start_distance > 0.0f)
        {
            touch_ = std::move(contact);
            return actions;
        }
        // 전용 손잡이는 누르는 즉시 조작이 시작된다. 스크롤 후보가 아니다.
        contact.phase = touch_phase::handle;
        contact.pan_open = false;
        touch_ = std::move(contact);
        pointer_drag_id_ = hit->id();
        pointer_drag_held_ = *handler;
        pointer_drag_x_ = event.x;
        pointer_drag_y_ = event.y;
        pointer_drag_modifiers_ = modifiers_of(event);
        // 조작이 이미 시작됐으므로 초점은 마우스처럼 누를 때 옮긴다.
        apply_press_focus(*tree, hit, event.surface, event.time);
        if (handler->on_press == nullptr)
            return actions;
        std::vector<input_action> moved { handler->on_press(action_context(pointer_drag_id_, event.x, event.y, pointer_drag_modifiers_)) };
        // 누른 자리로 뛰는 것도 실제 이동이다. 그 뒤로는 길게 누르기가 아니다.
        if (moved.empty() == false)
            touch_->moved_action = true;
        for (input_action& action : moved)
            actions.push_back(std::move(action));
        return actions;
    }

    std::vector<input_action> interaction_controller::process_touch_move(const pointer_moved_event& event)
    {
        // 쥔 접촉의 것만 본다. 추가 손가락·취소된 접촉의 이동은 삼킨다.
        if (touch_.has_value() == false || touch_->surface != event.surface || (touch_->id != event.pointer_id && (touch_->second.has_value() == false || touch_->second->id != event.pointer_id)))
            return {};
        touch_contact& contact { *touch_ };
        // 길게 누르기로 메뉴를 연 접촉이다. 손을 뗄 때까지 스크롤·끌기로 바뀌지 않는다.
        if (contact.long_press == long_press_state::fired)
            return {};
        // 둘째 손가락의 이동도 같은 키보드의 상태다.
        contact.modifiers = modifiers_of(event);
        const ui_tree* const tree { surface_tree(contact.surface) };
        // 거리는 누른 표면의 배율로 나눈 논리 픽셀이다.
        // 가장 먼 거리를 남기므로 이동 이벤트를 몇 번에 나눠 받았는지가 판정을 바꾸지 않는다.
        const float travel { distance_between(contact.start_x, contact.start_y, event.x, event.y) / contact.scale };
        contact.max_distance = std::max(contact.max_distance, travel);

        std::vector<input_action> actions {};
        switch (contact.phase)
        {
        case touch_phase::view_pan:
        case touch_phase::pinching: {
            const ui_element* const owner { tree != nullptr ? live_zoom_view(*tree, contact.view, contact.start_x, contact.start_y) : nullptr };
            if (owner == nullptr || owner->id() != contact.view || owner->zoom()->pan_by == nullptr || (contact.second.has_value() && owner->zoom()->zoom_by == nullptr))
            {
                cancel_touch();
                return {};
            }
            const zoom_source& source { *owner->zoom() };
            const float scale { positive_scale(source.scale) };
            if (contact.second.has_value())
            {
                const float old_x { (contact.last_x + contact.second->x) / 2.0f };
                const float old_y { (contact.last_y + contact.second->y) / 2.0f };
                const float old_distance { distance_between(contact.last_x, contact.last_y, contact.second->x, contact.second->y) / scale };
                if (event.pointer_id == contact.id)
                {
                    contact.last_x = event.x;
                    contact.last_y = event.y;
                }
                else
                {
                    contact.second->x = event.x;
                    contact.second->y = event.y;
                }
                const float x { (contact.last_x + contact.second->x) / 2.0f };
                const float y { (contact.last_y + contact.second->y) / 2.0f };
                const float distance { distance_between(contact.last_x, contact.last_y, contact.second->x, contact.second->y) / scale };
                const zoom_point delta { (x - old_x) / scale, (y - old_y) / scale };
                if (delta != zoom_point {})
                    actions.push_back(source.pan_by(delta));
                // 서로 거의 겹친 접촉은 이동만 한다. 떨어진 뒤의 첫 이동에서 새 기준이 선다.
                if (old_distance >= contact.config.minimum_pinch_distance && distance >= contact.config.minimum_pinch_distance && distance != old_distance)
                {
                    const rect_f& box { owner->bounds() };
                    const zoom_point anchor { (x - box.x - box.width / 2.0f) / scale, (y - box.y - box.height / 2.0f) / scale };
                    actions.push_back(source.zoom_by(distance / old_distance, anchor));
                }
            }
            else
            {
                const zoom_point delta { (event.x - contact.last_x) / scale, (event.y - contact.last_y) / scale };
                if (delta != zoom_point {})
                    actions.push_back(source.pan_by(delta));
                if (contact.edge.has_value())
                    record_view_pan(*contact.edge, delta, event.time, event.x, event.y);
                contact.last_x = event.x;
                contact.last_y = event.y;
            }
            if (actions.empty() == false)
                contact.moved_action = true;
            return actions;
        }
        case touch_phase::handoff: {
            const pan_handoff_target* const target { tree != nullptr ? live_handoff(*tree, contact.handoff, contact.handoff_axis) : nullptr };
            if (target == nullptr)
            {
                cancel_touch();
                return {};
            }
            const float along { contact.handoff_axis == scroll_axis::horizontal ? event.x - contact.last_x : event.y - contact.last_y };
            if (along != 0.0f)
                actions.push_back(target->drag(along / positive_scale(target->scale)));
            break;
        }
        case touch_phase::handle: {
            const pointer_drag_target* const handler { live_drag_handler(tree, contact.target, contact.start_x, contact.start_y) };
            if (handler == nullptr)
            {
                cancel_touch();
                return {};
            }
            if (handler->on_move == nullptr)
                break;
            const ui_action_context previous { action_context(pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, pointer_drag_modifiers_) };
            const ui_action_context current { action_context(pointer_drag_id_, event.x, event.y, contact.modifiers) };
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            pointer_drag_modifiers_ = contact.modifiers;
            actions = handler->on_move(previous, current);
            if (actions.empty() == false)
                contact.moved_action = true;
            track_auto_scroll(contact.surface, event.x, event.y, event.time);
            break;
        }
        case touch_phase::dragging:
            if (tree == nullptr || live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) == nullptr)
            {
                cancel_touch();
                return {};
            }
            if (snapshot_.drag.has_value())
            {
                const drag_location at { locate_drag(event) };
                update_drag(at);
                track_auto_scroll(at.surface, at.x, at.y, event.time);
            }
            break;
        case touch_phase::panning: {
            // 매 이동마다 새 tree에서 같은 자리를 다시 묻는다. 같은 임자·같은 축이
            // 답할 때만 그 최신 메시지로 잇고, 사라졌거나 가려졌으면 취소한다.
            //  - 경계에 닿았다고 뒤의 컨테이너로 넘기지 않는다 (후속 범위다).
            const scroll_axis axis { contact.pan->axis };
            const std::optional<pan_target> pan { tree != nullptr ? resolve_pan(*tree, contact.start_x, contact.start_y, axis) : std::nullopt };
            if (pan.has_value() == false || pan->id != contact.pan->id)
            {
                cancel_touch();
                return {};
            }
            contact.pan = pan;
            actions = pan_step(*contact.pan, contact.last_x, contact.last_y, event.x, event.y);
            break;
        }
        case touch_phase::pending: {
            if (contact.view != ui_element_id {} && travel >= contact.config.press_move_tolerance)
            {
                contact.phase = touch_phase::view_pan;
                clear_press();
                // 시간 창 없이 누른 자리부터의 이동을 한 번에 보낸다.
                return process_touch_move(event);
            }
            if (contact.max_distance > contact.config.press_move_tolerance)
            {
                // 허용치를 넘은 접촉은 더 이상 클릭이 아니다. 누름 표시를 거둔다.
                snapshot_.pressed = {};
                snapshot_.pressed_surface.clear();
            }
            if (contact.pan_open && event.time - contact.pressed_at > contact.config.pan_start_time)
                contact.pan_open = false;
            if (contact.pan_open && travel >= contact.config.pan_start_distance && tree != nullptr)
            {
                // 축은 누른 자리부터의 이동으로 정한다. 그 축으로 흐르는 가장 안쪽
                // 후보가 임자이고, 없으면 이 접촉은 더는 스크롤이 아니다.
                const float delta_x { event.x - contact.start_x };
                const float delta_y { event.y - contact.start_y };
                const scroll_axis axis { std::abs(delta_x) > std::abs(delta_y) ? scroll_axis::horizontal : scroll_axis::vertical };
                std::optional<pan_target> pan { resolve_pan(*tree, contact.start_x, contact.start_y, axis) };
                // 바깥 컨테이너(시트)가 넘겨받는 것은 이 순간 한 번만 정한다. 안쪽 창이 없거나, 처음에 있는 창을
                // 처음 쪽(손가락이 아래·오른쪽)으로 끄는 때다. 안쪽 창이 그 컨테이너 밖이면 컨테이너의 일이 아니다.
                const ui_element* handoff { handoff_owner_at(*tree, contact.start_x, contact.start_y, axis) };
                if (handoff != nullptr && pan.has_value() && tree->within(handoff->id(), pan->id) == false)
                    handoff = nullptr;
                const float toward { axis == scroll_axis::horizontal ? delta_x : delta_y };
                if (handoff != nullptr && (pan.has_value() == false || (pan->at_start && toward > 0.0f)))
                {
                    contact.phase = touch_phase::handoff;
                    contact.handoff = handoff->id();
                    contact.handoff_axis = axis;
                    clear_press();
                    // 누른 자리부터 쌓인 이동을 한 번에 넘긴다 (스크롤의 시작과 같다).
                    const pan_handoff_target& target { *handoff->pan_handoff() };
                    actions.push_back(target.drag(toward / positive_scale(target.scale)));
                    break;
                }
                if (pan.has_value())
                {
                    contact.phase = touch_phase::panning;
                    contact.pan = std::move(pan);
                    // 첫 이동만 받고 놓아도 누른 자리부터 속도를 잴 수 있어야 한다.
                    contact.motion.samples.insert(contact.motion.samples.begin(), { contact.pressed_at, contact.start_x, contact.start_y });
                    // 스크롤이 된 접촉은 클릭이 아니다. 누름은 거두되 접촉은 남는다.
                    clear_press();
                    // 시작할 때는 누른 자리부터 쌓인 이동을 한 번에 반영한다.
                    actions = pan_step(*contact.pan, contact.start_x, contact.start_y, event.x, event.y);
                    break;
                }
                contact.pan_open = false;
            }
            // 스크롤이 될 수 없게 된 뒤에야 일반 끌기를 본다 — 빠른 쓸기는
            // 스크롤이고, 오래 잡았다가 옮기는 것이 끌기다.
            if (contact.pan_open == false && travel >= contact.config.press_move_tolerance && contact.target != ui_element_id {} && tree != nullptr)
            {
                const ui_element* const source { live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) };
                if (source == nullptr)
                {
                    cancel_touch();
                    return {};
                }
                if (const pointer_drag_target* const handler { source->pointer_drag() }; handler != nullptr && handler->start_distance > 0.0f)
                {
                    // 시작 거리를 둔 손잡이다. 마우스처럼 누른 자리의 누름과 넘은 이동을 잇달아 보낸다.
                    contact.phase = touch_phase::handle;
                    contact.moved_action = true;
                    pointer_drag_id_ = contact.target;
                    pointer_drag_held_ = *handler;
                    pointer_drag_x_ = contact.start_x;
                    pointer_drag_y_ = contact.start_y;
                    pointer_drag_modifiers_ = pressed_modifiers_;
                    // 끌기는 탭이 아니다. 앞 탭의 연타 기록을 지운다 (마우스와 같다).
                    last_click_id_ = {};
                    last_click_surface_.clear();
                    click_streak_ = 0;
                    snapshot_.pressed = contact.target;
                    snapshot_.pressed_surface = contact.surface;
                    // 조작이 시작됐으므로 초점은 지금 옮긴다 (누르는 즉시 시작하는 손잡이는 누를 때 옮긴다).
                    apply_press_focus(*tree, source, contact.surface, event.time);
                    if (handler->on_press)
                        actions = handler->on_press(action_context(pointer_drag_id_, contact.start_x, contact.start_y, pressed_modifiers_));
                    if (handler->on_move)
                    {
                        const ui_action_context previous { action_context(pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, pointer_drag_modifiers_) };
                        const ui_action_context current { action_context(pointer_drag_id_, event.x, event.y, contact.modifiers) };
                        pointer_drag_x_ = event.x;
                        pointer_drag_y_ = event.y;
                        pointer_drag_modifiers_ = contact.modifiers;
                        for (input_action& action : handler->on_move(previous, current))
                            actions.push_back(std::move(action));
                    }
                    track_auto_scroll(contact.surface, event.x, event.y, event.time);
                }
                else if (source->drag() != nullptr && source->drag()->make_payload)
                {
                    const ui_action_context context { action_context(contact.target, contact.start_x, contact.start_y, contact.modifiers) };
                    begin_drag(*source->drag(), context, event.x, event.y, contact.surface);
                    clear_press();
                    contact.phase = touch_phase::dragging;
                    const drag_location at { locate_drag(event) };
                    update_drag(at);
                    track_auto_scroll(at.surface, at.x, at.y, event.time);
                }
            }
            break;
        }
        }
        contact.motion.add(event.time, event.x, event.y);
        contact.last_x = event.x;
        contact.last_y = event.y;
        return actions;
    }

    std::vector<input_action> interaction_controller::process_touch_release(const pointer_released_event& event)
    {
        if (touch_.has_value() == false || touch_->surface != event.surface || (touch_->id != event.pointer_id && (touch_->second.has_value() == false || touch_->second->id != event.pointer_id)))
            return {};
        if (touch_->phase == touch_phase::view_pan || touch_->phase == touch_phase::pinching)
        {
            const pointer_moved_event moved {
                .x = event.x,
                .y = event.y,
                .time = event.time,
                .surface = event.surface,
                .device = pointer_device::touch,
                .pointer_id = event.pointer_id,
                .in_contact = true,
            };
            std::vector<input_action> actions { process_touch_move(moved) };
            if (touch_.has_value() == false)
                return actions;
            if (touch_->second.has_value())
            {
                if (touch_->id == event.pointer_id)
                {
                    touch_->id = touch_->second->id;
                    touch_->last_x = touch_->second->x;
                    touch_->last_y = touch_->second->y;
                }
                touch_->second.reset();
                touch_->phase = touch_phase::view_pan;
            }
            else
            {
                const ui_tree* const tree { surface_tree(event.surface) };
                if (touch_->moved_action == false)
                {
                    const ui_element* const view { tree != nullptr ? zoom_owner_at(*tree, touch_->start_x, touch_->start_y) : nullptr };
                    if (view != nullptr && view->id() == touch_->view)
                        apply_press_focus(*tree, view, event.surface, event.time);
                }
                // 핀치가 섞이지 않은 한 접촉만 경계 쓸기를 알린다. 보기는 최신 tree에서 다시 찾는다.
                if (touch_->edge.has_value() && touch_->edge->pinched == false && tree != nullptr)
                    if (const ui_element* const owner { live_zoom_view(*tree, touch_->view, touch_->start_x, touch_->start_y) }; owner != nullptr)
                        if (std::optional<input_action> swipe { finish_view_edge(*touch_->edge, *owner) }; swipe.has_value())
                            actions.push_back(std::move(*swipe));
                // 빈 곳을 움직이지 않고 뗀 것은 탭이다. 빈 곳의 이동은 떨림에도 곧장 나가므로 이동 액션이 아니라
                // 허용치로 가른다.
                if (touch_->pinched == false && touch_->max_distance <= touch_->config.press_move_tolerance)
                {
                    if (std::optional<std::vector<input_action>> zoom { view_double_tap(*touch_, event) }; zoom.has_value())
                        for (input_action& action : *zoom)
                            actions.push_back(std::move(action));
                }
                else
                    last_view_tap_.reset();
                cancel_touch();
            }
            return actions;
        }
        touch_contact contact { std::move(*touch_) };
        touch_.reset();
        contact.modifiers = modifiers_of(event);
        // 확대 보기 안의 탭만 두 번 탭의 짝을 잇는다. 스크롤·끌기·손잡이·메뉴와 보기 밖의 탭은 짝을 끊는다.
        if (contact.phase != touch_phase::pending || contact.view == ui_element_id {} || contact.long_press == long_press_state::fired)
            last_view_tap_.reset();
        // 누르고 있는 동안 이미 우클릭을 냈다. 뗌은 클릭도 탭도 아니다.
        if (contact.long_press == long_press_state::fired)
        {
            clear_press();
            return {};
        }
        const ui_tree* const tree { surface_tree(contact.surface) };
        contact.max_distance = std::max(contact.max_distance, distance_between(contact.start_x, contact.start_y, event.x, event.y) / contact.scale);
        const bool still { contact.max_distance <= contact.config.press_move_tolerance };

        switch (contact.phase)
        {
        case touch_phase::panning: {
            // 정상적인 뗌의 마지막 좌표 변화도 반영한다. 클릭은 아니다.
            clear_press();
            const std::optional<pan_target> pan { tree != nullptr ? resolve_pan(*tree, contact.start_x, contact.start_y, contact.pan->axis) : std::nullopt };
            if (pan.has_value() == false || pan->id != contact.pan->id)
                return {};
            std::vector<input_action> actions { pan_step(*pan, contact.last_x, contact.last_y, event.x, event.y) };
            contact.motion.add(event.time, event.x, event.y);
            const drag_velocity measured { contact.motion.velocity_at(event.time) };
            const float velocity { -(pan->axis == scroll_axis::horizontal ? measured.x : measured.y) / positive_scale(pan->scale) };
            if (contact.config.inertia_enabled && config_.touch.inertia_enabled && std::isfinite(velocity) && std::abs(velocity) >= inertia_minimum_velocity
                && (velocity > 0.0f ? pan->at_end == false : pan->at_start == false))
                inertia_
                    = inertial_pan { pan->id, contact.surface, pan->axis, contact.start_x, contact.start_y, std::clamp(velocity, -inertia_maximum_velocity, inertia_maximum_velocity), event.time };
            return actions;
        }
        case touch_phase::dragging:
            if (tree == nullptr || live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) == nullptr)
            {
                abandon_drag();
                clear_press();
                return {};
            }
            if (snapshot_.drag.has_value())
                return finish_drag(locate_drag(event), contact.modifiers, event.screen);
            clear_press();
            return {};
        case touch_phase::handoff: {
            // 마지막 좌표 변화를 넘기고 끝을 알린다.
            clear_press();
            const pan_handoff_target* const target { tree != nullptr ? live_handoff(*tree, contact.handoff, contact.handoff_axis) : nullptr };
            if (target == nullptr)
                return {};
            std::vector<input_action> actions {};
            const float along { contact.handoff_axis == scroll_axis::horizontal ? event.x - contact.last_x : event.y - contact.last_y };
            if (along != 0.0f)
                actions.push_back(target->drag(along / positive_scale(target->scale)));
            for (input_action& action : end_touch_drag(contact, event.x, event.y, contact.motion.velocity_at(event.time), along))
                actions.push_back(std::move(action));
            return actions;
        }
        case touch_phase::handle: {
            // 손잡이를 놓는 것은 클릭이 아니다 (마우스와 같다). 움직이지 않고
            // 오래 눌렀으면 길게 누르기만 남는다. 끝을 듣는 손잡이에는 먼저 끝을 알린다.
            //  - 끝을 알린 뒤에 누름을 거둔다. 창 밖으로 흘러 지어지지 않은 손잡이는 쥐어 둔 handler로 끝을 듣는데,
            //    `clear_press`가 그것을 함께 지운다.
            std::vector<input_action> actions { tree != nullptr ? end_touch_drag(contact, event.x, event.y, contact.motion.velocity_at(event.time)) : std::vector<input_action> {} };
            clear_press();
            if (still == false || contact.moved_action || tree == nullptr)
                return actions;
            const ui_element* const target { live_touch_handle(*tree, contact.target, contact.start_x, contact.start_y) };
            // 보통은 누르고 있는 동안 `advance`가 이미 냈다. 여기는 pump가 그 시각에
            // 깨기 전에 뗌이 먼저 온 경우다.
            if (target == nullptr || contact.config.long_press_enabled == false || contact.long_press == long_press_state::unavailable
                || event.time - contact.pressed_at < contact.config.long_press_time || has_long_press_action(*target) == false)
                return actions;
            if (policy_ != nullptr)
                policy_->on_click(*target);
            for (input_action& action : run_long_press(contact, *target, event.x, event.y))
                actions.push_back(std::move(action));
            return actions;
        }
        case touch_phase::view_pan:
        case touch_phase::pinching:
            return {};
        case touch_phase::pending:
            clear_press();
            // 허용치를 넘었지만 스크롤도 끌기도 되지 않은 접촉은 클릭으로 되돌리지 않는다.
            if (still == false)
            {
                last_view_tap_.reset();
                return {};
            }
            return finish_touch_tap(contact, event);
        }
        return {};
    }

    std::optional<std::chrono::steady_clock::time_point> interaction_controller::next_deadline() const noexcept
    {
        const std::optional<std::chrono::steady_clock::time_point> gesture { gesture_deadline() };
        const std::optional<std::chrono::steady_clock::time_point> flow { auto_scroll_deadline() };
        std::optional<std::chrono::steady_clock::time_point> result { gesture };
        for (const auto candidate : { flow, submenu_deadline() })
            if (candidate.has_value() && (result.has_value() == false || *candidate < *result))
                result = candidate;
        return result;
    }

    std::optional<std::chrono::steady_clock::time_point> interaction_controller::gesture_deadline() const noexcept
    {
        if (inertia_.has_value())
            return inertia_->updated_at + inertia_interval;
        if (touch_.has_value() == false)
            return std::nullopt;
        const touch_contact& contact { *touch_ };
        // 움직이지 않은 대기 접촉과 아직 이동 액션을 내지 않은 손잡이만 길게 누를 수 있다.
        // 빈 곳에는 우클릭할 대상이 없다.
        const bool eligible_phase { contact.phase == touch_phase::pending || (contact.phase == touch_phase::handle && contact.moved_action == false) };
        if (eligible_phase == false || contact.long_press != long_press_state::waiting || contact.config.long_press_enabled == false || contact.target == ui_element_id {}
            || contact.max_distance > contact.config.press_move_tolerance)
            return std::nullopt;
        return contact.pressed_at + contact.config.long_press_time;
    }

    std::vector<input_action> interaction_controller::advance(const std::chrono::steady_clock::time_point now)
    {
        // 가장자리 흐름은 터치 판정과 따로 간다 (손가락으로 끄는 동안에도 흐른다).
        std::vector<input_action> actions { advance_auto_scroll(now) };
        if (const auto deadline { submenu_deadline() }; deadline.has_value() && now >= *deadline)
        {
            opened_hover_ = snapshot_.hovered;
            opened_hover_surface_ = snapshot_.hovered_surface;
            opened_hover_at_ = snapshot_.hover_started_at;
            if (const auto* menu { hovered_menu() }; menu != nullptr)
                if (auto opened { menu->open_submenu(snapshot_.hovered.owner) }; opened.has_value())
                    for (auto& action : *opened)
                        actions.push_back(std::move(action));
        }
        const auto with_flow = [&actions](std::vector<input_action> more) {
            for (input_action& action : more)
                actions.push_back(std::move(action));
            return std::move(actions);
        };
        const std::optional<std::chrono::steady_clock::time_point> deadline { gesture_deadline() };
        if (deadline.has_value() == false || now < *deadline)
            return actions;
        if (inertia_.has_value())
            return with_flow(advance_inertia(now));
        touch_contact& contact { *touch_ };
        const ui_tree* const tree { surface_tree(contact.surface) };
        const ui_element* const target { tree != nullptr ? live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) : nullptr };
        // 대상이 사라졌거나 가려졌으면 뗌의 탭 판정도 같은 이유로 액션이 없다. 우클릭이
        // 없는 대상은 길게 눌러도 그냥 탭이다 — 뗌이 정한다.
        if (target == nullptr || has_long_press_action(*target) == false)
        {
            contact.long_press = long_press_state::unavailable;
            return actions;
        }
        contact.long_press = long_press_state::fired;
        // 메뉴가 열리면 누름 표시는 거둔다. 접촉은 뗄 때까지 쥐고 있는다.
        clear_press();
        // 탭처럼 초점부터 옮긴다 (텍스트 칸이면 그 칸이 메뉴의 대상이다). 손잡이는
        // 누를 때 이미 옮겼다.
        if (contact.phase == touch_phase::pending)
            apply_press_focus(*tree, target, contact.surface, now);
        // 우클릭이 된 접촉은 연속 탭의 기록에서도 빠진다.
        last_click_id_ = {};
        last_click_surface_.clear();
        click_streak_ = 0;
        if (policy_ != nullptr)
            policy_->on_click(*target);
        return with_flow(run_long_press(contact, *target, contact.last_x, contact.last_y));
    }

    std::vector<input_action> interaction_controller::advance_inertia(const std::chrono::steady_clock::time_point now)
    {
        inertial_pan& motion { *inertia_ };
        const ui_tree* const tree { surface_tree(motion.surface) };
        const std::optional<pan_target> pan { tree != nullptr ? resolve_pan(*tree, motion.x, motion.y, motion.axis) : std::nullopt };
        if (pan.has_value() == false || pan->id != motion.id || pan->axis != motion.axis || pan->scroll == nullptr || (motion.velocity > 0.0f ? pan->at_end : pan->at_start))
        {
            inertia_.reset();
            return {};
        }
        // 감쇠 곡선을 적분해 늦게 깨어나도 한 번의 갱신으로 같은 거리를 보낸다.
        const float until_stop { std::log(std::abs(motion.velocity) / inertia_stop_velocity) / inertia_decay };
        const float elapsed { std::chrono::duration<float>(now - motion.updated_at).count() };
        const float seconds { std::min(elapsed, until_stop) };
        const float next_velocity { motion.velocity * std::exp(-inertia_decay * seconds) };
        const float delta { (motion.velocity - next_velocity) / inertia_decay };
        motion.velocity = next_velocity;
        motion.updated_at = now;
        if (elapsed >= until_stop)
            inertia_.reset();
        return { pan->scroll(delta) };
    }

    float drag_auto_scroll_speed(const float position, const float start, const float end, const float scale, const drag_auto_scroll_config& config) noexcept
    {
        // 창이 띠 둘보다 좁으면 반씩 나눈다 — 가운데에서는 어느 쪽으로도 흐르지 않는다.
        const float band { std::min(config.edge_band * positive_scale(scale), (end - start) / 2.0f) };
        if (std::isfinite(position) == false || std::isfinite(band) == false || band <= 0.0f || std::isfinite(config.maximum_speed) == false || config.maximum_speed <= 0.0f)
            return 0.0f;
        // 띠의 안쪽 끝에서 0, 창 가장자리에서 절반, 가장자리 밖으로 띠 폭만큼 나가면 최대다.
        constexpr float full_depth { 2.0f };
        if (position < start + band)
            return -config.maximum_speed * std::min((start + band - position) / band, full_depth) / full_depth;
        if (position > end - band)
            return config.maximum_speed * std::min((position - (end - band)) / band, full_depth) / full_depth;
        return 0.0f;
    }

    namespace {
        // 흘릴 창 하나와 그 속도다 (논리 픽셀/초).
        struct auto_scroll_flow
        {
            const ui_element* window { nullptr };
            float speed { 0.0f };
        };

        // `anchor`를 감싼 흘리는 창 가운데 축마다 지금 흘러야 할 가장 안쪽 창이다.
        //  - 안쪽 창이 그쪽 끝에 닿았거나 포인터가 그 창의 띠 밖이면 바깥 창을 본다 — 목록이 끝에 닿은 뒤에는
        //    목록을 품은 판이 흐른다.
        //  - 끝은 발행본이 알린다 (`scroll_source::at_start`·`at_end`). 앱이 다듬은 끝이 다음 tree로 오면 멈춘다.
        [[nodiscard]] std::vector<auto_scroll_flow> auto_scroll_flows(const ui_tree& tree, const ui_element_id& anchor, const float x, const float y, const drag_auto_scroll_config& config)
        {
            std::vector<auto_scroll_flow> flows {};
            const ui_element* const root { tree.root() };
            std::vector<const ui_element*> path {};
            if (root == nullptr || anchor == ui_element_id {} || collect_ancestry(*root, anchor, path) == false)
                return flows;
            bool decided[2] { false, false };
            for (auto it { path.rbegin() }; it != path.rend(); ++it)
            {
                const ui_element& window { **it };
                const scroll_source* const source { window.scroll() };
                if (source == nullptr || source->scroll == nullptr || window.visible() == false || window.enabled() == false)
                    continue;
                const bool horizontal { source->axis == scroll_axis::horizontal };
                if (decided[horizontal ? 1 : 0])
                    continue;
                const rect_f& box { window.bounds() };
                const float start { horizontal ? box.x : box.y };
                const float length { horizontal ? box.width : box.height };
                const float speed { drag_auto_scroll_speed(horizontal ? x : y, start, start + length, source->scale, config) };
                if (speed == 0.0f || (speed < 0.0f ? source->at_start : source->at_end))
                    continue;
                decided[horizontal ? 1 : 0] = true;
                flows.push_back({ &window, speed });
            }
            return flows;
        }

        // (x, y)를 덮는 가장 안쪽의 흘리는 창이다. 막기만 하는 것이 먼저 걸리면 없다.
        [[nodiscard]] const ui_element* scroll_window_at(const ui_tree& tree, const float x, const float y)
        {
            const ui_element* const root { tree.root() };
            if (root == nullptr)
                return nullptr;
            const auto accepts = [](const ui_element& element) {
                const scroll_source* const source { element.scroll() };
                return source != nullptr && source->scroll != nullptr;
            };
            const ui_element* const owner { pan_owner_at(*root, x, y, accepts) };
            return owner != nullptr && accepts(*owner) ? owner : nullptr;
        }

        // `id`를 감싼 가장 안쪽의 흘리는 창이다 (`id` 자신도 창이면 그것이다).
        [[nodiscard]] const ui_element* scroll_window_around(const ui_tree& tree, const ui_element_id& id)
        {
            const ui_element* const root { tree.root() };
            std::vector<const ui_element*> path {};
            if (root == nullptr || collect_ancestry(*root, id, path) == false)
                return nullptr;
            for (auto it { path.rbegin() }; it != path.rend(); ++it)
                if (const scroll_source* const source { (*it)->scroll() }; source != nullptr && source->scroll != nullptr)
                    return *it;
            return nullptr;
        }

        // `outer`가 `inner`를 감싸는가 (같으면 참이다).
        [[nodiscard]] bool encloses(const ui_tree& tree, const ui_element_id& outer, const ui_element_id& inner)
        {
            const ui_element* const root { tree.root() };
            std::vector<const ui_element*> path {};
            if (root == nullptr || collect_ancestry(*root, inner, path) == false)
                return false;
            return std::ranges::any_of(path, [&outer](const ui_element* const element) { return element->id() == outer; });
        }
    } // namespace

    const pointer_drag_target* interaction_controller::live_drag_handler(const ui_tree* const tree, const ui_element_id& id, const float x, const float y) const
    {
        if (tree == nullptr || id == ui_element_id {})
            return nullptr;
        if (const ui_element* const target { live_touch_handle(*tree, id, x, y) }; target != nullptr && target->pointer_drag() != nullptr)
            return target->pointer_drag();
        // 자동 스크롤을 켠 손잡이는 흘리는 창 안에 있어 내용이 흐르면 누른 자리를 떠난다. 누른 자리를 덮은 것으로
        // 보면 한 행만 흘러도 끌기가 끊긴다.
        if (pointer_drag_held_.has_value() == false || pointer_drag_held_->auto_scroll == false)
            return nullptr;
        if (const ui_element* const moved { tree->find(id) }; moved != nullptr)
        {
            if (moved->enabled() == false || tree->within_focus_trap(id) == false)
                return nullptr;
            return moved->pointer_drag();
        }
        // 창 밖으로 흘러 지어지지 않았다 (가상 목록). 흘리던 창이 남아 있으면 쥐어 둔 handler로 잇는다 — 창까지
        // 사라졌으면 화면이 바뀐 것이라 끊는다.
        const bool window_alive { auto_scroll_.has_value() && auto_scroll_->handle && auto_scroll_->handle_id == id && tree->find(auto_scroll_->anchor) != nullptr };
        return window_alive ? &*pointer_drag_held_ : nullptr;
    }

    void interaction_controller::track_auto_scroll(const std::u8string& surface, const float x, const float y, const std::chrono::steady_clock::time_point time)
    {
        const ui_tree* const tree { surface_tree(surface) };
        if (tree == nullptr)
        {
            auto_scroll_.reset();
            return;
        }
        auto_scroll_state next {};
        next.surface = surface;
        next.x = x;
        next.y = y;
        if (pointer_drag_id_ != ui_element_id {})
        {
            // 손잡이는 켠 것만 흐른다.
            const pointer_drag_target* const handler { live_drag_handler(tree, pointer_drag_id_, pressed_x_, pressed_y_) };
            if (handler == nullptr || handler->auto_scroll == false)
            {
                auto_scroll_.reset();
                return;
            }
            next.handle = true;
            next.handle_id = pointer_drag_id_;
            // 흘릴 창은 손잡이를 감싼 창이다. 손잡이는 내용과 함께 흘러 창 밖에서 지어지지 않을 수 있으므로(가상 목록)
            // 지어져 있을 때 찾은 창을 이어 쓴다.
            const bool continuing { auto_scroll_.has_value() && auto_scroll_->handle && auto_scroll_->handle_id == pointer_drag_id_ && auto_scroll_->surface == surface };
            if (const ui_element* const window { scroll_window_around(*tree, pointer_drag_id_) }; window != nullptr)
                next.anchor = window->id();
            else if (continuing)
                next.anchor = auto_scroll_->anchor;
            else
                next.anchor = pointer_drag_id_;
        }
        else if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() && config_.auto_scroll.drag_and_drop)
        {
            // 포인터 밑의 흘리는 창이 기준이다. 창 밖이면 직전 창(처음에는 끌기를 시작한 자리)이 이어진다 —
            // 목록 위로 끌고 나가도 그 목록이 위로 흐른다.
            //  - 포인터가 직전 창을 감싼 바깥 창(목록을 품은 판) 위로 나간 것도 그 창 밖이다. 바깥 창으로 갈아타면
            //    목록 위로 끌고 나간 순간 목록은 멈추고 판이 받는다.
            //  - 직전 창과 남남인 창(옆 목록)에 들어가면 그 창으로 옮겨 간다.
            const bool continuing { auto_scroll_.has_value() && auto_scroll_->handle == false && auto_scroll_->surface == surface };
            const ui_element* const window { scroll_window_at(*tree, x, y) };
            if (window != nullptr && (continuing == false || encloses(*tree, window->id(), auto_scroll_->anchor) == false))
                next.anchor = window->id();
            else if (continuing)
                next.anchor = auto_scroll_->anchor;
            else
                next.anchor = snapshot_.drag->payload.source;
        }
        else
        {
            auto_scroll_.reset();
            return;
        }
        next.flowing = auto_scroll_flows(*tree, next.anchor, x, y, config_.auto_scroll).empty() == false;
        // 흐르던 중이면 시계를 잇는다. 새로 흐르기 시작하면 이 이벤트의 시각부터 잰다.
        next.updated_at = next.flowing && auto_scroll_.has_value() && auto_scroll_->flowing ? auto_scroll_->updated_at : time;
        auto_scroll_ = std::move(next);
    }

    std::optional<std::chrono::steady_clock::time_point> interaction_controller::auto_scroll_deadline() const noexcept
    {
        if (auto_scroll_.has_value() == false || auto_scroll_->flowing == false)
            return std::nullopt;
        const std::chrono::milliseconds interval { config_.auto_scroll.interval.count() > 0 ? config_.auto_scroll.interval : std::chrono::milliseconds { 16 } };
        return auto_scroll_->updated_at + interval;
    }

    std::vector<input_action> interaction_controller::advance_auto_scroll(const std::chrono::steady_clock::time_point now)
    {
        const std::optional<std::chrono::steady_clock::time_point> deadline { auto_scroll_deadline() };
        if (deadline.has_value() == false || now < *deadline)
            return {};
        auto_scroll_state& state { *auto_scroll_ };
        if (state.resuming)
        {
            state.resuming = false;
            state.updated_at = now;
            return {};
        }
        const float seconds { std::chrono::duration<float>(now - state.updated_at).count() };
        state.updated_at = now;
        const ui_tree* const tree { surface_tree(state.surface) };
        std::vector<input_action> actions {};
        if (tree != nullptr)
            for (const auto_scroll_flow& flow : auto_scroll_flows(*tree, state.anchor, state.x, state.y, config_.auto_scroll))
                if (const float delta { flow.speed * seconds }; delta != 0.0f)
                    actions.push_back(flow.window->scroll()->scroll(delta));
        if (actions.empty())
        {
            state.flowing = false;
            return actions;
        }
        // 내용이 흘렀으니 포인터가 멈춰 있어도 손잡이에 같은 자리의 이동을 알린다. 좌표는 표면 좌표다 — 스크롤
        // 메시지가 먼저 나가므로 앱이 자기 스크롤 위치로 옮기면 흐른 만큼 늘어난 자리가 된다.
        if (state.handle && pointer_drag_id_ == state.handle_id)
        {
            const pointer_drag_target* const handler { live_drag_handler(tree, pointer_drag_id_, pressed_x_, pressed_y_) };
            if (handler != nullptr && handler->on_move != nullptr)
            {
                const ui_action_context previous { action_context(pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, pointer_drag_modifiers_) };
                const ui_action_context current { action_context(pointer_drag_id_, state.x, state.y, pointer_drag_modifiers_) };
                pointer_drag_x_ = state.x;
                pointer_drag_y_ = state.y;
                for (input_action& action : handler->on_move(previous, current))
                    actions.push_back(std::move(action));
            }
        }
        return actions;
    }

    void interaction_controller::settle_auto_scroll() noexcept
    {
        if (auto_scroll_.has_value() == false)
            return;
        bool alive { false };
        if (auto_scroll_->handle)
            alive = pointer_drag_id_ == auto_scroll_->handle_id;
        else
            alive = snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty() && snapshot_.drag->pointer_on() == auto_scroll_->surface;
        if (alive == false)
            auto_scroll_.reset();
    }

    void interaction_controller::resume_auto_scroll()
    {
        if (auto_scroll_.has_value() == false || auto_scroll_->flowing)
            return;
        const ui_tree* const tree { surface_tree(auto_scroll_->surface) };
        if (tree == nullptr || auto_scroll_flows(*tree, auto_scroll_->anchor, auto_scroll_->x, auto_scroll_->y, config_.auto_scroll).empty())
            return;
        auto_scroll_->flowing = true;
        auto_scroll_->resuming = true;
    }

    std::vector<input_action> interaction_controller::run_long_press(const touch_contact& contact, const ui_element& target, const float x, const float y)
    {
        std::vector<input_action> actions { run_trigger(target, ui_trigger::right_click, x, y, contact.modifiers) };
        if (target.action(ui_trigger::right_click) == nullptr && config_.text_edit_menu && text_target(target.id().kind).has_value() && target.text_input().has_value())
        {
            if (target.text_input()->composing == false)
                actions = place_text_caret(target, x, y, false, true);
            actions.emplace_back(ui_command::text_edit_menu);
        }
        // 메뉴가 실제로 열리는 순간이다. 아무것도 내지 않은 우클릭에는 울리지 않는다.
        if (contact.config.long_press_haptic && actions.empty() == false)
            actions.insert(actions.begin(), input_action { haptic_request { haptic_feedback::long_press } });
        return actions;
    }

    bool interaction_controller::has_long_press_action(const ui_element& target) const
    {
        return target.action(ui_trigger::right_click) != nullptr || (config_.text_edit_menu && text_target(target.id().kind).has_value() && target.text_input().has_value());
    }

    std::vector<input_action> interaction_controller::finish_touch_tap(const touch_contact& contact, const pointer_released_event& event)
    {
        const ui_tree* const tree { surface_tree(contact.surface) };
        if (tree == nullptr)
            return {};
        // 빈 곳을 탭한 것은 "다른 곳을 누른" 것이다 (마우스는 누를 때 거둔다).
        if (contact.target == ui_element_id {})
        {
            clear_focus();
            return {};
        }
        const ui_element* const target { live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) };
        if (target == nullptr)
            return {};
        apply_press_focus(*tree, target, contact.surface, event.time);

        // 길게 누르기는 보통 누르고 있는 동안 `advance`가 낸다. 여기는 pump가 그 시각에
        // 깨기 전에 뗌이 먼저 온 경우다 — 같은 판정을 뗌의 시각으로 한다.
        // 우클릭 액션이 없으면 그냥 탭이다.
        if (contact.config.long_press_enabled && contact.long_press == long_press_state::waiting && event.time - contact.pressed_at >= contact.config.long_press_time && has_long_press_action(*target))
        {
            // 우클릭이 된 접촉은 연속 탭의 기록에서도 빠진다.
            last_click_id_ = {};
            last_click_surface_.clear();
            click_streak_ = 0;
            if (policy_ != nullptr)
                policy_->on_click(*target);
            return run_long_press(contact, *target, event.x, event.y);
        }

        // 연속 탭은 대상·표면·장치로 잇는다. 접촉마다 바뀌는 포인터 id는 보지 않는다.
        //  - 거리 한계는 터치 허용치다. 마우스의 몇 픽셀로는 두 번 탭이 서지 않는다.
        const bool same_target { contact.target == last_click_id_ && contact.surface == last_click_surface_ && last_click_device_ == pointer_device::touch };
        const bool close_by { distance_between(last_click_x_, last_click_y_, event.x, event.y) <= contact.config.press_move_tolerance * contact.scale };
        const bool repeat { same_target && close_by && event.time - last_click_time_ <= config_.double_click_time };
        const auto remember = [&] {
            last_click_id_ = contact.target;
            last_click_device_ = pointer_device::touch;
            last_click_surface_ = contact.surface;
            last_click_time_ = event.time;
            last_click_x_ = event.x;
            last_click_y_ = event.y;
        };

        // 텍스트 칸의 탭은 caret을 놓는다 (두 번은 낱말, 세 번부터 전부).
        // 마우스처럼 칸의 누름은 클릭 액션이 아니다.
        if (const std::optional<text_input_target> text { text_target(target->id().kind) }; text.has_value())
        {
            // 텍스트 칸의 두 번 탭은 낱말 고르기다. 보기의 확대와 짝을 이루지 않는다.
            last_view_tap_.reset();
            click_streak_ = repeat ? click_streak_ + 1 : 1;
            remember();
            if (click_streak_ >= 3)
                return { text_edit_action(*text, text::text_edit_command::select_all) };
            return place_text_caret(*target, event.x, event.y, false, click_streak_ == 2);
        }

        // 확대 보기 안의 탭이다. 내용의 더블 클릭이 먼저이고, 그것이 없으면 짝을 이룬 둘째 탭이 보기의 확대다.
        // 첫 탭은 늦추지 않고 아래에서 그대로 클릭으로 나간다.
        if (contact.view != ui_element_id {})
        {
            if (repeat && target->action(ui_trigger::double_click) != nullptr)
                last_view_tap_.reset();
            else if (std::optional<std::vector<input_action>> zoom { view_double_tap(contact, event) }; zoom.has_value())
            {
                last_click_id_ = {};
                last_click_surface_.clear();
                click_streak_ = 0;
                return std::move(*zoom);
            }
        }

        if (policy_ != nullptr)
            policy_->on_click(*target);
        ui_trigger trigger { ui_trigger::left_click };
        if (repeat && target->action(ui_trigger::double_click) != nullptr)
        {
            trigger = ui_trigger::double_click;
            last_click_id_ = {};
            last_click_surface_.clear();
        }
        else
            remember();
        return run_trigger(*target, trigger, event.x, event.y, modifiers_of(event));
    }

    std::vector<input_action> interaction_controller::process_cancel(const pointer_cancelled_event& event)
    {
        if (event.device == pointer_device::touch)
        {
            if (touch_.has_value() && touch_->surface == event.surface && (touch_->id == event.pointer_id || (touch_->second.has_value() && touch_->second->id == event.pointer_id)))
            {
                // 끌기를 쥔 접촉이면 끝을 알린다. 시트가 끄던 자리에 남지 않고 멈춤점으로 돌아간다.
                std::vector<input_action> actions { end_touch_drag(*touch_, touch_->last_x, touch_->last_y, {}) };
                cancel_touch();
                last_view_tap_.reset();
                return actions;
            }
            return {};
        }
        // 장치만 같아도 다른 포인터·표면의 취소일 수 있다.
        if (touch_.has_value() || pointer_contact_.has_value() == false || pointer_contact_->device != event.device || pointer_contact_->id != event.pointer_id
            || pointer_contact_->surface != event.surface)
            return {};
        // 손잡이를 쥐고 있었으면 끝을 알린다 (터치 취소와 같다).
        std::vector<input_action> actions { end_pointer_drag(pointer_drag_x_, pointer_drag_y_, {}, pointer_drag_modifiers_) };
        cancel_pointer_gesture();
        return actions;
    }

    void interaction_controller::cancel_pointer_gesture() noexcept
    {
        view_drag_.reset();
        pointer_contact_.reset();
        text_drag_id_ = {};
        if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty())
            abandon_drag();
        clear_press();
    }

    std::optional<std::vector<input_action>> interaction_controller::view_double_tap(const touch_contact& contact, const pointer_released_event& event)
    {
        const ui_tree* const tree { surface_tree(contact.surface) };
        const ui_element* const owner { tree != nullptr ? live_zoom_view(*tree, contact.view, contact.start_x, contact.start_y) : nullptr };
        if (owner == nullptr || owner->id() != contact.view || owner->zoom()->double_tap == nullptr)
        {
            last_view_tap_.reset();
            return std::nullopt;
        }
        // 같은 보기·표면에서, 앞 탭을 뗀 뒤 더블 클릭 시간 안에 눌렀고, 터치 허용치 안의 자리다 (연속 탭과 같은 한계).
        const bool same_view { last_view_tap_.has_value() && last_view_tap_->view == contact.view && last_view_tap_->surface == contact.surface };
        const bool soon { same_view && contact.pressed_at >= last_view_tap_->time && contact.pressed_at - last_view_tap_->time <= config_.double_click_time };
        const bool near { soon && distance_between(last_view_tap_->x, last_view_tap_->y, event.x, event.y) <= contact.config.press_move_tolerance * contact.scale };
        if (near == false)
        {
            last_view_tap_ = view_tap { contact.view, contact.surface, event.time, event.x, event.y };
            return std::nullopt;
        }
        last_view_tap_.reset();
        const float scale { positive_scale(owner->zoom()->scale) };
        const rect_f& box { owner->bounds() };
        return owner->zoom()->double_tap({ (event.x - box.x - box.width / 2.0f) / scale, (event.y - box.y - box.height / 2.0f) / scale });
    }

    std::vector<input_action> interaction_controller::end_touch_drag(const touch_contact& contact, const float x, const float y, const drag_velocity velocity, const float final_delta)
    {
        const ui_tree* const tree { surface_tree(contact.surface) };
        if (tree == nullptr)
            return {};
        if (contact.phase == touch_phase::handoff)
        {
            const pan_handoff_target* const target { live_handoff(*tree, contact.handoff, contact.handoff_axis) };
            if (target == nullptr || target->release == nullptr)
                return {};
            const float along { contact.handoff_axis == scroll_axis::horizontal ? velocity.x : velocity.y };
            const float scale { positive_scale(target->scale) };
            return { target->release(final_delta / scale, along / scale) };
        }
        if (contact.phase != touch_phase::handle)
            return {};
        const pointer_drag_target* const handler { live_drag_handler(tree, contact.target, contact.start_x, contact.start_y) };
        if (handler == nullptr || handler->on_release == nullptr)
            return {};
        return handler->on_release(action_context(contact.target, x, y, contact.modifiers), velocity);
    }

    std::vector<input_action> interaction_controller::end_pointer_drag(const float x, const float y, const drag_velocity velocity, const modifier_keys modifiers) const
    {
        if (pointer_drag_id_ == ui_element_id {})
            return {};
        const pointer_drag_target* const handler { live_drag_handler(surface_tree(pressed_surface_), pointer_drag_id_, pressed_x_, pressed_y_) };
        if (handler == nullptr || handler->on_release == nullptr)
            return {};
        return handler->on_release(action_context(pointer_drag_id_, x, y, modifiers), velocity);
    }

    void interaction_controller::record_view_pan(view_edge_track& edge, const zoom_point delta, const std::chrono::steady_clock::time_point time, const float x, const float y)
    {
        if (delta != zoom_point {} && edge.pan_room)
        {
            // 앱이 이동을 하나씩 다듬는 것과 같은 차례로 견준다. 합을 한 번에 견주면 끝에 막혔다 돌아온 접촉이
            // 막힌 몫을 잃는다.
            const zoom_point room { edge.pan_room(edge.moved, delta) };
            edge.moved = { edge.moved.x + room.x, edge.moved.y + room.y };
            edge.blocked = { edge.blocked.x + delta.x - room.x, edge.blocked.y + delta.y - room.y };
        }
        edge.motion.add(time, x, y);
    }

    void interaction_controller::motion_track::add(const std::chrono::steady_clock::time_point time, const float x, const float y)
    {
        samples.push_back({ time, x, y });
        const auto stale { std::ranges::find_if(samples, [time](const sample& value) { return time - value.time <= motion_window; }) };
        samples.erase(samples.begin(), stale);
    }

    drag_velocity interaction_controller::motion_track::velocity_at(const std::chrono::steady_clock::time_point time) const noexcept
    {
        // 놓은 시각에서 100ms 안의 표본만 본다. 멈췄다 놓았으면 남는 것이 없어 0이다.
        const auto fresh { std::ranges::find_if(samples, [time](const sample& value) { return time - value.time <= motion_window; }) };
        if (std::distance(fresh, samples.end()) < 2)
            return {};
        const sample& first { *fresh };
        const sample& last { samples.back() };
        const float seconds { std::chrono::duration<float>(last.time - first.time).count() };
        if (seconds <= 0.0f)
            return {};
        return { (last.x - first.x) / seconds, (last.y - first.y) / seconds };
    }

    std::optional<input_action> interaction_controller::finish_view_edge(const view_edge_track& edge, const ui_element& owner)
    {
        const zoom_source* const source { owner.zoom() };
        if (source == nullptr || source->edge_swipe == nullptr || edge.motion.samples.empty())
            return std::nullopt;
        // 남은 표본의 처음과 끝으로 잰 속도다. 뗌도 표본으로 들어가므로 뗌 직전에 멈췄다면 표본이 하나만 남아 0이다.
        const drag_velocity measured { edge.motion.velocity_at(edge.motion.samples.back().time) };
        const float scale { positive_scale(source->scale) };
        const zoom_point velocity { measured.x / scale, measured.y / scale };
        const zoom_edge_swipe swipe { judge_edge_swipe(source->edge_swipe_config, edge.moved, edge.blocked, velocity) };
        if (swipe.horizontal == 0 && swipe.vertical == 0)
            return std::nullopt;
        return source->edge_swipe(swipe);
    }

    void interaction_controller::cancel_touch() noexcept
    {
        if (touch_.has_value() && touch_->phase == touch_phase::dragging)
            abandon_drag();
        touch_.reset();
        clear_press();
    }

    bool interaction_controller::set_touch_config(const touch_gesture_config& config) noexcept
    {
        if (valid_touch_gesture_config(config) == false)
            return false;
        config_.touch = config;
        if (config.pan_enabled == false || config.inertia_enabled == false)
            inertia_.reset();
        if (touch_.has_value())
        {
            if (config.inertia_enabled == false)
                touch_->config.inertia_enabled = false;
            const touch_phase phase { touch_->phase };
            const bool pan_off { touch_->view == ui_element_id {} && touch_->config.pan_enabled && config.pan_enabled == false && (phase == touch_phase::pending || phase == touch_phase::panning) };
            const bool long_press_off { touch_->config.long_press_enabled && config.long_press_enabled == false && phase == touch_phase::pending };
            // 손잡이 조작은 계속하지만 그 접촉의 메뉴 후보는 즉시 끈다.
            // 다시 켜도 이미 내려가 있는 손가락에 메뉴 자격을 새로 주지 않는다.
            if (phase == touch_phase::handle && config.long_press_enabled == false)
                touch_->config.long_press_enabled = false;
            const bool pinch_off { touch_->config.pinch_enabled && config.pinch_enabled == false && phase == touch_phase::pinching };
            if (config.pinch_enabled == false)
                touch_->config.pinch_enabled = false;
            if (pan_off || long_press_off || pinch_off)
                cancel_touch();
        }
        return true;
    }

    std::vector<input_action> interaction_controller::place_text_caret(const ui_element& element, const float x, const float y, const bool extend, const bool select_word) const
    {
        const std::optional<text_input_target> target { text_target(element.id().kind) };
        if (target.has_value() == false || policy_ == nullptr)
            return {};
        const std::optional<text_caret_position> position { element.caret_at_point(x, y, config_.measure_text) };
        if (position.has_value() == false)
            return {};

        // 암호 칸의 낱말 고르기는 칸 전체다. 낱말 경계가 공백 자리를 알린다.
        const std::optional<text_input_snapshot> value { element.text_input() };
        const bool whole { select_word && value.has_value() && value->secret };
        text_edit_request request {};
        request.target = *target;
        request.command = whole ? text::text_edit_command::select_all : select_word ? text::text_edit_command::select_word : text::text_edit_command::place_caret;
        request.offset = position->offset;
        request.extend = extend;
        // 넘겨 접은 줄의 끝을 누르면 caret이 그 줄에 붙는다 (낱말 고르기는 caret을 놓지 않는다).
        request.upstream = request.command == text::text_edit_command::place_caret && position->upstream;
        return { policy_->make_text_edit_action(request) };
    }

    const ui_element* interaction_controller::focused_text_element() const
    {
        const ui_tree* const focused_tree { surface_tree(snapshot_.focused_surface) };
        return focused_tree != nullptr ? focused_tree->find(snapshot_.focused_input) : nullptr;
    }

    std::vector<input_action> interaction_controller::move_text_caret_by_lines(const text_input_snapshot& value, const float rows, const bool to_line_end, const bool extend) const
    {
        // 지금 caret의 자리를 칸에 묻고, 그 줄에서 위아래로 옮긴 자리를 포인터처럼 다시 묻는다.
        // 줄과 글자의 자리를 아는 것은 칸뿐이라 판정이 클릭과 한 길이다 (`offset_at_point`).
        //  - 줄을 오가는 동안 짧은 줄을 지나도 처음의 가로 자리를 기억하지 않는다. 그 자리는 앱 상태에 없다.
        const ui_element* const element { focused_text_element() };
        if (element == nullptr)
            return {};
        const std::optional<rect_f> caret { element->text_span_bounds({ value.text, value.caret, value.caret, value.caret }, config_.measure_text) };
        if (caret.has_value() == false)
            return {};
        const float middle { caret->y + caret->height / 2.0f };
        if (rows == 0.0f)
            return place_text_caret(*element, to_line_end ? std::numeric_limits<float>::max() : std::numeric_limits<float>::lowest(), middle, extend, false);
        return place_text_caret(*element, caret->x, middle + rows * caret->height, extend, false);
    }

    std::vector<input_action> interaction_controller::follow_text_caret()
    {
        const ui_element* const element { focused_text_element() };
        const std::optional<text_input_snapshot> value { element != nullptr ? element->text_input() : std::nullopt };
        if (value.has_value() == false || value->multiline == false)
        {
            followed_caret_.reset();
            return {};
        }
        // 조합 중에는 조합 글이 섞인 표시 문서와 그 caret이다 — 그려지는 줄이 그것이다.
        const std::u8string_view document { value->composing ? value->composition_text : value->text };
        const std::size_t caret { value->composing ? value->composition_caret : value->caret };
        const followed_caret now { snapshot_.focused_input, caret, value->composing == false && value->caret_upstream, std::hash<std::u8string_view> {}(document) };
        if (followed_caret_ == now)
            return {};
        followed_caret_ = now;

        // 칸 안의 흘리는 창이 들인다. 흘릴 것이 없는 창은 scroll source를 세우지 않으므로 할 일도 없다.
        const ui_element* window { nullptr };
        const std::function<void(const ui_element&)> find_window = [&window, &find_window](const ui_element& node) {
            for (const std::unique_ptr<ui_element>& child : node.children())
            {
                if (window != nullptr)
                    return;
                if (child->scroll() != nullptr && child->scroll()->scroll != nullptr)
                    window = child.get();
                else
                    find_window(*child);
            }
        };
        find_window(*element);
        const std::optional<rect_f> line { element->text_span_bounds({ document, caret, caret, caret }, config_.measure_text) };
        if (line.has_value() == false)
            return {};
        std::vector<input_action> actions {};
        if (window != nullptr)
        {
            const float delta { window->scroll_delta_to_reveal(*line) };
            if (delta != 0.0f)
                actions.push_back(window->scroll()->scroll(delta));
        }
        if (element->scroll() != nullptr && element->scroll()->axis == scroll_axis::horizontal)
        {
            const float delta { element->scroll_delta_to_reveal(*line) };
            if (delta != 0.0f)
                actions.push_back(element->scroll()->scroll(delta));
        }
        return actions;
    }

    std::optional<text_input_snapshot> interaction_controller::focused_text_input() const
    {
        // 초점 표면의 tree가 들고 있다.
        const ui_tree* const focused_tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { focused_tree != nullptr ? focused_tree->find(snapshot_.focused_input) : nullptr };
        return element != nullptr ? element->text_input() : std::nullopt;
    }

    std::vector<input_action> interaction_controller::copy_focused_selection(const text_input_target target, const bool erase) const
    {
        // 선택이 없으면 아무 일도 하지 않는다.
        // 암호 칸은 복사도 잘라내기도 하지 않는다 — 클립보드는 다른 앱과 클립보드 기록이 읽는다. 복사 없이
        // 지우기만 하는 잘라내기는 사용자가 기대한 일이 아니라 그것도 하지 않는다.
        const std::optional<text_input_snapshot> value { focused_text_input() };
        if (value.has_value() == false || value->secret || value->caret == value->anchor)
            return {};

        const std::size_t begin { value->caret < value->anchor ? value->caret : value->anchor };
        const std::size_t end { value->caret < value->anchor ? value->anchor : value->caret };
        std::vector<input_action> actions {};
        actions.push_back(input_action { clipboard_copy_request { std::u8string { value->text.substr(begin, end - begin) } } });
        if (erase)
            actions.push_back(text_edit_action(target, text::text_edit_command::cut));
        return actions;
    }

    std::vector<input_action> interaction_controller::delete_word(const text_input_target target, const bool left) const
    {
        const std::optional<text_input_snapshot> value { focused_text_input() };
        if (value.has_value() == false || value->secret == false)
            return { text_edit_action(target, left ? text::text_edit_command::delete_word_left : text::text_edit_command::delete_word_right) };
        // 암호 칸은 낱말 경계를 쓰지 않는다. 고른 구간이 있으면 그것만, 없으면 그쪽 끝까지 지운다.
        const text::text_edit_command erase { left ? text::text_edit_command::backspace : text::text_edit_command::delete_forward };
        if (value->caret != value->anchor)
            return { text_edit_action(target, erase) };
        return { text_edit_action(target, left ? text::text_edit_command::move_line_start : text::text_edit_command::move_line_end, true), text_edit_action(target, erase) };
    }

    std::optional<std::vector<input_action>> interaction_controller::process_text_input_key(const key_pressed_event& event)
    {
        const std::optional<text_input_target> focused { text_target(snapshot_.focused_input.kind) };
        if (focused.has_value() == false)
            return std::nullopt;
        const text_input_target target { *focused };
        const bool primary_shortcut { event.primary_shortcut_down() };
        const bool word_navigation { event.word_navigation_down() };
        const text_input_snapshot value { focused_text_input().value_or(text_input_snapshot {}) };
        if (value.navigation_keys_to_parent && value.composing == false && event.shortcut_modifier_down() == false
            && (event.key == key_code::arrow_up || event.key == key_code::arrow_down || event.key == key_code::page_up
                || event.key == key_code::page_down || event.key == key_code::home || event.key == key_code::end || event.key == key_code::enter))
            return std::nullopt;
        if (value.read_only)
        {
            const bool edits { event.key == key_code::delete_forward
                || (event.key == key_code::enter && event.shortcut_modifier_down() == false) || (event.key == key_code::insert && event.shift)
                || (primary_shortcut && (event.key == key_code::key_v || event.key == key_code::key_x
                    || event.key == key_code::key_z || event.key == key_code::key_y))
            };
            if (edits)
                return std::vector<input_action> {};
        }
        // 암호 칸의 낱말 이동은 칸 끝으로 간다. 낱말 경계가 공백 자리를 알린다.
        const bool whole_line { word_navigation && value.secret };

        // 여러 줄 칸에서는 줄을 다루는 키의 뜻이 바뀐다 (`text_area_element`). 나머지 키는 한 줄 칸과 같다.
        if (value.multiline)
            switch (event.key)
            {
            case key_code::enter: {
                // 줄 바꿈이다. 기본 버튼으로 가지 않는다 — 여러 줄 칸의 Enter는 글자의 자리다.
                //  - Ctrl·Alt를 함께 누른 Enter는 앱 단축키로 흐른다 (보내기·확인을 거기 두는 앱이 흔하다).
                if (event.shortcut_modifier_down() || policy_ == nullptr)
                    return std::nullopt;
                text_edit_request request {};
                request.target = target;
                request.command = text::text_edit_command::insert;
                request.text = u8"\n";
                return std::vector<input_action> { policy_->make_text_edit_action(request) };
            }
            case key_code::arrow_up:
            case key_code::arrow_down:
                return move_text_caret_by_lines(value, event.key == key_code::arrow_up ? -1.0f : 1.0f, false, event.shift);
            case key_code::page_up:
            case key_code::page_down: {
                // 한 쪽은 창에 드는 줄 수에서 한 줄을 남긴 만큼이다 — 넘긴 뒤에도 앞 쪽의 끝 줄이 보인다.
                const ui_element* const element { focused_text_element() };
                const std::optional<rect_f> caret { element != nullptr ? element->text_span_bounds({ value.text, value.caret, value.caret, value.caret }, config_.measure_text) : std::nullopt };
                if (caret.has_value() == false || caret->height <= 0.0f)
                    return std::vector<input_action> {};
                const float rows { std::max(1.0f, std::floor(element->bounds().height / caret->height) - 1.0f) };
                return move_text_caret_by_lines(value, event.key == key_code::page_up ? -rows : rows, false, event.shift);
            }
            case key_code::home:
            case key_code::end:
                // Ctrl+Home·End는 글의 처음과 끝이다 — 아래의 한 줄 칸 경로가 곧 그것이다.
                if (primary_shortcut)
                    break;
                return move_text_caret_by_lines(value, 0.0f, event.key == key_code::end, event.shift);
            default:
                break;
            }

        switch (event.key)
        {
        case key_code::arrow_left:
            if (whole_line)
                return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_start, event.shift) };
            return std::vector<input_action> { text_edit_action(target, word_navigation ? text::text_edit_command::move_word_left : text::text_edit_command::move_left, event.shift) };
        case key_code::arrow_right:
            if (whole_line)
                return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_end, event.shift) };
            return std::vector<input_action> { text_edit_action(target, word_navigation ? text::text_edit_command::move_word_right : text::text_edit_command::move_right, event.shift) };
        case key_code::home:
            return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_start, event.shift) };
        case key_code::end:
            return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_end, event.shift) };
        case key_code::delete_forward:
            // Shift+Delete는 선택 영역을 잘라내고, Ctrl+Delete는 오른쪽 낱말을 지운다.
            if (event.shift && event.control == false)
                return copy_focused_selection(target, true);
            if (word_navigation)
                return delete_word(target, false);
            return std::vector<input_action> { text_edit_action(target, text::text_edit_command::delete_forward) };
        case key_code::insert:
            // Ctrl+Insert는 복사, Shift+Insert는 붙여넣기다.
            if (event.control)
                return copy_focused_selection(target, false);
            if (event.shift)
                return std::vector<input_action> { input_action { clipboard_paste_request { target, value.multiline } } };
            return std::nullopt;
        case key_code::key_z:
            if (primary_shortcut)
                return std::vector<input_action> { text_edit_action(target, event.shift ? text::text_edit_command::redo : text::text_edit_command::undo) };
            return std::nullopt;
        case key_code::key_y:
            if (primary_shortcut)
                return std::vector<input_action> { text_edit_action(target, text::text_edit_command::redo) };
            return std::nullopt;
        case key_code::key_a:
            if (primary_shortcut)
                return std::vector<input_action> { text_edit_action(target, text::text_edit_command::select_all) };
            return std::nullopt;
        case key_code::key_c:
        case key_code::key_x:
            if (primary_shortcut == false)
                return std::nullopt;
            return copy_focused_selection(target, event.key == key_code::key_x);
        case key_code::key_v:
            if (primary_shortcut)
                return std::vector<input_action> { input_action { clipboard_paste_request { target, value.multiline } } };
            return std::nullopt;
        default:
            break;
        }
        return std::nullopt;
    }

    std::vector<input_action> interaction_controller::process_key(const key_pressed_event& event)
    {
        // 컨텍스트 메뉴가 열려 있으면 ↑/↓/Enter/Esc는 메뉴의 몫이다.
        // 목록 탐색으로 흘러가지 않는다.
        // 메뉴는 주 tree에 있을 수도, popup tree에 있을 수도 있다.
        const std::optional<menu_kinds> menu { policy_ != nullptr ? policy_->menu() : std::nullopt };
        if (menu.has_value())
            if (const menu_location host { find_menu(*menu) }; host.tree != nullptr)
            {
                if (std::optional<std::vector<input_action>> handled { process_menu_key(event, *menu, host) }; handled.has_value())
                    return std::move(*handled);
                // 메뉴가 갖지 않은 키는 그 안에 초점을 가진 텍스트 박스까지만 간다
                // (검색 칸이 있는 메뉴). 메뉴가 전부 삼키면 caret도 옮길 수 없다.
                //  - 앱 정책으로는 여전히 흐르지 않는다. 메뉴 뒤의 목록이
                //    키를 받으면 안 되는 것이 위 규칙의 뜻이다.
                if (std::optional<std::vector<input_action>> typed { process_text_input_key(event) }; typed.has_value())
                    return std::move(*typed);
                return {};
            }

        // Space와 Enter는 초점을 가진 컨트롤을 실행한다.
        //  - **초점이 이긴다.** Enter가 초점 테와 다른 자리를 실행하면 테가
        //    거짓말을 한다 (enter-default-design.md). 기본 버튼은 초점이
        //    그것을 받지 못할 때만 나선다 — 아래 `process_default_key`다.
        if (std::optional<std::vector<input_action>> activated { process_activation_key(event) }; activated.has_value())
            return std::move(*activated);

        // Tab은 초점을 옮긴다.
        // 텍스트 박스 안에서도 "다음 자리로 나간다"는 뜻이라 박스보다 먼저 본다.
        //  - 자리가 하나도 없으면 삼키지 않고 앱 정책으로 흘려보낸다.
        //  - 지금 자리가 **Tab을 자기 것으로 쓰면**(코드 칸) 옮기지 않는다.
        //    빠져나가는 길은 앱이 만든다 (focus-group-design.md).
        if (event.key == key_code::tab && focus_takes_tab() == false && move_focus(event.shift == false, event.surface, event.time))
            return {};

        // 초점을 가진 텍스트 박스가 먼저 가져간다.
        // 소비하지 않은 키만 앱 정책의 키 라우팅으로 흐른다.
        if (std::optional<std::vector<input_action>> handled { process_text_input_key(event) }; handled.has_value())
            return std::move(*handled);

        // 초점이 값을 가진 element면 화살표·Page·Home/End는 **그 값의 것**이다.
        // **묶음보다 앞이다** — 값은 초점이 선 자리에서 바뀌고 묶음은 초점을 옮긴다.
        // 텍스트 박스 다음인 것은 묶음과 같은 이유다 (칸 안의 ←/→는 caret의 것이다).
        const ui_tree* const zoom_tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const zoom_focus { zoom_tree != nullptr ? zoom_tree->find(snapshot_.focused) : nullptr };
        const zoom_source* const zoom { zoom_focus != nullptr && zoom_focus->focusable() ? zoom_focus->zoom() : nullptr };
        if (zoom != nullptr && zoom->on_key)
            if (auto actions { zoom->on_key(event) }; actions.has_value())
                return std::move(*actions);

        if (std::optional<std::vector<input_action>> stepped { process_step_key(event) }; stepped.has_value())
            return std::move(*stepped);

        // 초점이 묶음 안이면 방향이 맞는 화살표가 그 안을 돈다.
        // **텍스트 박스 다음**이다 — 묶음 안의 칸에서 ←/→는 caret의 것이다.
        if (std::optional<std::vector<input_action>> moved { process_group_key(event) }; moved.has_value())
            return std::move(*moved);

        // drag 중의 Esc는 취소가 먼저다.
        // 앱 정책까지 가지 않는다.
        if (event.key == key_code::escape && snapshot_.drag.has_value())
        {
            abandon_drag();
            return {};
        }

        // 위의 소비자들이 갖지 않은 키는 초점에서 root 쪽으로 흐른다 (key-propagation-design.md).
        //  - **가장 작은 단위가 먼저다.** 행 안의 부품에 초점이 있어도 그 부품이 쓰지 않는 Ctrl+A는 목록이 받는다.
        //  - **가둠의 Esc와 기본 버튼보다 앞이다.** dialog 안에서 목록을 연 콤보는 Esc로 목록만 닫아야 하고,
        //    그것은 콤보가 소비하는 Esc다. 소비하지 않은 Esc만 dialog로 간다.
        //  - 끌기 취소 Esc보다는 뒤다. 잡고 있는 손이 언제나 먼저다.
        if (std::optional<std::vector<input_action>> handled { process_handler_key(event) }; handled.has_value())
            return std::move(*handled);

        // 가둠(modal dialog)이 떠 있으면 Esc는 그 dialog의 것이다.
        // **메뉴와 끌기 다음이다** — dialog 안에서 연 드롭다운은 Esc로 메뉴만
        // 닫히고 dialog는 남아야 하고, 끌고 있는 손이 그보다 우선이다.
        if (std::optional<std::vector<input_action>> dismissed { process_dismiss_key(event) }; dismissed.has_value())
            return std::move(*dismissed);

        // 초점이 받지 못한 Enter는 기본 버튼의 것이다.
        // **가둠의 Esc와 같은 높이에 선다** — 두 키가 같은 자리에서 갈린다
        // (enter-default-design.md).
        if (std::optional<std::vector<input_action>> confirmed { process_default_key(event) }; confirmed.has_value())
            return std::move(*confirmed);

        if (policy_ == nullptr)
            return {};
        return policy_->on_key(tree_.get(), event, snapshot_);
    }

    std::optional<std::vector<input_action>> interaction_controller::process_handler_key(const key_pressed_event& event)
    {
        if (snapshot_.focused == ui_element_id {})
            return std::nullopt;
        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        if (tree == nullptr)
            return std::nullopt;
        for (const ui_element* const element : tree->ancestry_of(snapshot_.focused))
        {
            // 비활성 element는 묻지 않고 건너뛴다. 그 위는 여전히 묻는다 — 흐리게 그린 행 위의
            // 목록은 살아 있다 (`process_step_key`가 초점 element만 거르는 것과 같은 판단이다).
            if (element->enabled() == false)
                continue;
            const key_handler* const handler { element->key_handler_of() };
            if (handler == nullptr)
                continue;
            if (std::optional<std::vector<input_action>> actions { (*handler)(event) }; actions.has_value())
                return actions;
        }
        return std::nullopt;
    }

    std::optional<std::vector<input_action>> interaction_controller::process_default_key(const key_pressed_event& event)
    {
        // 수정자와 함께면 앱 단축키다 (실행 키와 같은 판정).
        if (event.key != key_code::enter || event.shortcut_modifier_down())
            return std::nullopt;

        // 초점이 있으면 그 표면에서, 없으면 **키가 온 표면**에서 찾는다
        // (Esc·Tab과 같은 규칙).
        const bool has_focus { snapshot_.focused != ui_element_id {} };
        const ui_tree* const tree { surface_tree(has_focus ? snapshot_.focused_surface : event.surface) };
        const ui_element* const button { tree != nullptr ? tree->default_button() : nullptr };
        // 없거나 지금 실행할 수 없으면 Enter는 그대로 앱으로 흐른다.
        if (button == nullptr || button->enabled() == false || button->action(ui_trigger::left_click) == nullptr)
            return std::nullopt;

        // 클릭과 같은 경로로 낸다 — 좌표는 그 버튼의 한가운데다 (Space·Esc와 같은 규칙).
        const rect_f box { button->bounds() };
        if (policy_ != nullptr)
            policy_->on_click(*button);
        return run_trigger(*button, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, modifiers_of(event));
    }

    std::optional<std::vector<input_action>> interaction_controller::process_dismiss_key(const key_pressed_event& event)
    {
        if (event.key != key_code::escape)
            return std::nullopt;

        // 초점이 있으면 그 표면에서, 없으면 **키가 온 표면**에서 찾는다
        // (Tab 순회와 같은 규칙).
        const bool has_focus { snapshot_.focused != ui_element_id {} };
        const ui_tree* const tree { surface_tree(has_focus ? snapshot_.focused_surface : event.surface) };
        const ui_element* const trap { tree != nullptr ? tree->focus_trap() : nullptr };
        const ui_action* const dismiss { trap != nullptr ? trap->dismiss_action() : nullptr };
        // 빈 액션이 "이 계기로는 닫지 않는다"다. Esc가 그대로 앱으로 흐른다.
        if (dismiss == nullptr)
            return std::nullopt;

        // 좌표는 가둠의 한가운데다 (포인터가 관여하지 않았다 — Space와 같은 규칙).
        const rect_f box { trap->bounds() };
        return (*dismiss)(ui_action_context { trap->id(), box.x + box.width / 2.0f, box.y + box.height / 2.0f });
    }

    const menu_element* interaction_controller::hovered_menu() const noexcept
    {
        const ui_tree* const tree { surface_tree(snapshot_.hovered_surface) };
        const auto* const menu { tree != nullptr ? menu_in_element(tree->root()) : nullptr };
        return menu != nullptr && menu->enabled() && menu->has_submenu(snapshot_.hovered.owner) ? menu : nullptr;
    }

    std::optional<std::chrono::steady_clock::time_point> interaction_controller::submenu_deadline() const noexcept
    {
        const auto* const menu { hovered_menu() };
        if (menu == nullptr || snapshot_.hover_started_at.has_value() == false
            || (opened_hover_ == snapshot_.hovered && opened_hover_surface_ == snapshot_.hovered_surface && opened_hover_at_ == snapshot_.hover_started_at)
            || menu->config().opened_key == snapshot_.hovered.owner)
            return std::nullopt;
        return *snapshot_.hover_started_at + menu->config().hover_delay;
    }

    std::optional<std::vector<input_action>> interaction_controller::process_menu_key(const key_pressed_event& event, const menu_kinds& kinds, const menu_location& host)
    {
        const ui_tree& tree { *host.tree };
        const menu_element* const menu { menu_in_element(tree.root()) };
        if (menu != nullptr && menu->config().depth > 0 && snapshot_.menu_surface != host.surface)
            for (const auto& item : menu->config().items)
                if (item.enabled)
                {
                    snapshot_.menu_highlight = { kinds.item, item.key };
                    snapshot_.menu_surface = host.surface;
                    break;
                }
        if (menu != nullptr && event.shortcut_modifier_down() == false)
        {
            if (event.key == key_code::arrow_right || event.key == key_code::enter)
                if (auto opened { menu->open_submenu(snapshot_.menu_highlight.owner) }; opened.has_value())
                    return opened;
            if ((event.key == key_code::arrow_left || event.key == key_code::escape) && menu->config().depth > 0)
            {
                snapshot_.menu_highlight = { kinds.item, menu->config().anchor_key };
                snapshot_.menu_surface = menu->config().parent_surface;
                const auto* const close { &menu->config().close_submenu };
                return *close ? (*close)({ snapshot_.menu_highlight }) : std::vector<input_action> {};
            }
        }
        switch (event.key)
        {
        case key_code::arrow_down:
        case key_code::arrow_up: {
            const std::vector<ui_element_id> items { tree.ids_of_kind(kinds.item) };
            if (items.empty())
                return std::vector<input_action> {};

            // 현재 강조 위치다.
            // 없으면 아래는 처음부터, 위는 끝부터 시작한다.
            std::size_t current { items.size() };
            for (std::size_t index = 0; index < items.size(); ++index)
                if (items[index] == snapshot_.menu_highlight)
                    current = index;

            // 비활성 항목은 건너뛴다.
            // 끝에 닿으면 제자리에 머문다.
            const auto item_enabled = [&tree](const ui_element_id& id) {
                const ui_element* const element { tree.find(id) };
                return element != nullptr && element->enabled();
            };
            // 강조와 그것이 사는 표면은 짝으로 선다 — 표식이 없으면 같은 키의
            // 메뉴를 연 다른 popup에도 같은 강조가 그려진다.
            if (event.key == key_code::arrow_down)
            {
                for (std::size_t index = current == items.size() ? 0 : current + 1; index < items.size(); ++index)
                    if (item_enabled(items[index]))
                    {
                        snapshot_.menu_highlight = items[index];
                        snapshot_.menu_surface = host.surface;
                        break;
                    }
            }
            else
            {
                for (std::size_t index = current == items.size() ? items.size() : current; index > 0; --index)
                    if (item_enabled(items[index - 1]))
                    {
                        snapshot_.menu_highlight = items[index - 1];
                        snapshot_.menu_surface = host.surface;
                        break;
                    }
            }
            return std::vector<input_action> {};
        }
        case key_code::enter: {
            // Ctrl·Alt가 눌린 Enter는 앱 단축키다 — 실행 키가 되는 것은 Shift뿐이다 (Space·Enter 실행과 같은 규약).
            if (event.shortcut_modifier_down())
                return std::nullopt;
            if (snapshot_.menu_highlight == ui_element_id {})
                return std::vector<input_action> {};
            const ui_element* const item { tree.find(snapshot_.menu_highlight) };
            if (item == nullptr || item->enabled() == false)
                return std::vector<input_action> {};
            const rect_f box { item->bounds() };
            return run_trigger(*item, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, modifiers_of(event));
        }
        case key_code::escape:
            return policy_ != nullptr ? policy_->close_menu() : std::vector<input_action> {};
        default:
            break;
        }
        // 메뉴의 키가 아니다.
        return std::nullopt;
    }

    interaction_controller::menu_location interaction_controller::find_menu(const menu_kinds& kinds) const
    {
        // kind로만 찾는다. `menu_config::owner`를 채운 메뉴는 root id가
        // `{ menu, owner }`라 owner 없는 id로는 닿지 않는다 — 정책이 주는 것은
        // kind 짝이지 owner가 아니므로, owner가 무엇이든 그 kind가 서 있으면 열린 것이다.
        const auto holds_menu = [&kinds](const ui_tree& tree) { return tree.ids_of_kind(kinds.container).empty() == false; };
        menu_location result {};
        std::size_t depth { 0 };
        const auto consider = [&](const ui_tree* tree, const std::u8string& surface) {
            if (tree == nullptr || holds_menu(*tree) == false)
                return;
            const auto* const menu { menu_in_element(tree->root()) };
            const std::size_t level { menu != nullptr ? menu->config().depth : 0 };
            if (result.tree == nullptr || level > depth)
            {
                result = { tree, surface };
                depth = level;
            }
        };
        consider(tree_.get(), {});
        for (const auto& [id, tree] : surface_trees_)
            consider(tree.get(), id);
        return result;
    }

    std::vector<input_action> interaction_controller::run_trigger(const ui_element& element, const ui_trigger trigger, const float x, const float y, const modifier_keys modifiers)
    {
        const ui_action* const action { element.action(trigger) };
        if (action == nullptr)
            return {};
        return (*action)(action_context(element.id(), x, y, modifiers));
    }

    ui_action_context interaction_controller::action_context(const ui_element_id& element, const float x, const float y, const modifier_keys modifiers)
    {
        return ui_action_context { element, x, y, modifiers.control, modifiers.shift, modifiers.alt };
    }

    void interaction_controller::update_hover(const float x, const float y, const std::chrono::steady_clock::time_point time, const bool moved)
    {
        // hover는 포인터가 마지막으로 있던 표면의 tree로 판정한다.
        const ui_tree* const tree { surface_tree(last_pointer_surface_) };
        const ui_element* const hit { tree != nullptr ? tree->hit_test(x, y) : nullptr };
        const ui_element_id hovered { hit != nullptr ? hit->id() : ui_element_id {} };
        // **표면도 함께 본다.** 두 표면에 같은 id가 사는 것이 유효하므로(같은
        // 목록을 보이는 두 창) id만 비교하면 A창의 행에서 B창의 같은 키 행으로
        // 옮겨 간 것을 "그대로"로 읽어 표식만 낡는다.
        const bool none { hovered == ui_element_id {} };
        if (hovered == snapshot_.hovered && (none || last_pointer_surface_ == snapshot_.hovered_surface))
        {
            const auto* const menu { hovered_menu() };
            if (moved && menu != nullptr && menu->config().opened_key.empty() == false && menu->config().opened_key != hovered.owner)
                snapshot_.hover_started_at = time;
            // 내용이 바뀌어 포인터 밑에 온 element는 시계 없이 서 있다. 그 위에서 포인터를
            // 움직이면 그때부터 잰다 — 사람이 그 자리를 가리켰다는 첫 증거다.
            if (moved && none == false && snapshot_.hover_started_at.has_value() == false)
                snapshot_.hover_started_at = time;
            return;
        }
        snapshot_.hovered = hovered;
        if (none)
        {
            snapshot_.hovered_surface.clear();
            snapshot_.hover_started_at.reset();
        }
        else
        {
            snapshot_.hovered_surface = last_pointer_surface_;
            // **tooltip의 시계는 포인터가 움직여 들어왔을 때만 선다.** 포인터가 머문 채
            // 화면이 다시 지어져(스크롤·클릭 뒤의 배치 변화) 밑의 element가 바뀐 것은 hover
            // 강조만 옮긴다. 마지막 이동의 옛 시각으로 시계를 세우면 지연이 이미 지나 있어,
            // 가리키지도 않은 element의 tooltip이 곧바로 선다.
            if (moved)
                snapshot_.hover_started_at = time;
            else
                snapshot_.hover_started_at.reset();
        }
    }

    std::optional<std::vector<input_action>> interaction_controller::process_activation_key(const key_pressed_event& event)
    {
        // 수정자와 함께라면 앱 단축키다 (Ctrl+Space·Ctrl+Enter 등).
        const bool space { event.key == key_code::space };
        if ((space == false && event.key != key_code::enter) || event.shortcut_modifier_down())
            return std::nullopt;
        // 초점이 텍스트 박스면 **두 키가 갈린다.**
        //  - Space는 글자다. 문자 경로(`character_typed_event`)가 그것을 먹으므로
        //    키는 여기서 끝난다 — 앱 단축키로 흘려보내면 글을 치는 동안 그 단축키가
        //    함께 실행된다.
        //  - Enter는 글자가 아니다. 삼키지 않고 아래로 보내 **기본 버튼**이 받게
        //    한다 — 칸에 커서를 둔 채 Enter로 확인하는 동작이 그래야 선다
        //    (enter-default-design.md).
        if (snapshot_.focused_input != ui_element_id {})
            return space ? std::optional<std::vector<input_action>> { std::vector<input_action> {} } : std::nullopt;
        if (snapshot_.focused == ui_element_id {})
            return std::nullopt;

        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { tree != nullptr ? tree->find(snapshot_.focused) : nullptr };
        if (element == nullptr || element->enabled() == false || element->action(ui_trigger::left_click) == nullptr)
            return std::nullopt;

        // 클릭과 같은 경로로 낸다 — 같은 액션이 같은 규칙으로 나가야 한다.
        // 좌표는 그 element의 한가운데다 (포인터가 관여하지 않았다).
        const rect_f box { element->bounds() };
        if (policy_ != nullptr)
            policy_->on_click(*element);
        return run_trigger(*element, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, modifiers_of(event));
    }

    std::vector<input_action> interaction_controller::process_access_focus(const access_focus_event& event)
    {
        const ui_tree* const tree { surface_tree(event.surface) };
        const ui_element* const element { tree != nullptr ? tree->find(event.target) : nullptr };
        // 자리가 아니면 세우지 않는다 (`focusable`이 비활성·숨음·배치 없음을 함께 본다).
        if (element == nullptr || element->focusable() == false)
            return {};
        // 키보드로 옮긴 것과 같은 부류라 테가 그려진다 — 보조 기술의 초점이 눈에
        // 보이지 않으면 곁에서 보는 사람이 따라갈 수 없다.
        set_keyboard_focus(event.surface, event.target, event.time);
        return {};
    }

    bool interaction_controller::move_focus(const bool forward, const std::u8string& key_surface, const std::chrono::steady_clock::time_point time)
    {
        // 초점이 있으면 그 표면에서 돌고, 없으면 **키가 온 표면**에서 시작한다.
        //  - 논리 초점이 이기는 쪽이 popup을 살린다. popup은 keyboard focus를
        //    받지 못해 앵커 창이 키를 나르므로, 표식을 검문으로 쓰면 popup 안
        //    검색 칸이 죽는다 (key-surface-routing-design.md).
        const bool has_focus { snapshot_.focused != ui_element_id {} };
        const std::u8string surface { has_focus ? snapshot_.focused_surface : key_surface };
        const ui_tree* const tree { surface_tree(surface) };
        if (tree == nullptr)
            return false;
        std::vector<ui_element_id> order { tree->focus_order() };
        // 순서는 앱이 다시 정할 수 있다 (걸러 내기도 한다).
        // 기본은 그리기 순서 그대로다.
        if (policy_ != nullptr)
            order = policy_->order_focus(*tree, std::move(order));
        if (order.empty())
            return false;

        // 초점이 없거나 지금 목록에 없으면 끝에서 시작한다.
        // 있으면 한 칸 옮기고 끝에서 처음으로 돈다.
        std::size_t next { forward ? 0u : order.size() - 1u };
        if (has_focus)
            if (const auto found { std::find(order.begin(), order.end(), snapshot_.focused) }; found != order.end())
            {
                const std::size_t index { static_cast<std::size_t>(found - order.begin()) };
                next = forward ? (index + 1u) % order.size() : (index + order.size() - 1u) % order.size();
            }

        set_keyboard_focus(surface, order[next], time);
        return true;
    }

    void interaction_controller::set_keyboard_focus(const std::u8string& surface, const ui_element_id& id, const std::optional<std::chrono::steady_clock::time_point> time)
    {
        snapshot_.focused = id;
        // 텍스트 박스인지는 앱 정책이 정한다 (`text_target_of`).
        snapshot_.focused_input = text_target(id.kind).has_value() ? id : ui_element_id {};
        snapshot_.focused_surface = surface;
        snapshot_.focus_started_at = time;
        // 키보드로 옮긴 초점이라 테를 그린다.
        snapshot_.focus_visible = true;
    }

    bool interaction_controller::focus_takes_tab() const
    {
        if (snapshot_.focused == ui_element_id {})
            return false;
        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { tree != nullptr ? tree->find(snapshot_.focused) : nullptr };
        return element != nullptr && element->takes_tab();
    }

    std::vector<input_action> interaction_controller::process_typeahead(const character_typed_event& event)
    {
        // 제어 문자는 글이 아니다 (Backspace·Esc는 키 경로가 이미 다뤘다).
        if (event.character < U' ' || event.character == 0x7Fu || snapshot_.focused == ui_element_id {})
            return {};
        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        if (tree == nullptr)
            return {};
        // 초점을 가진 element가 자기 모델을 아는 목록이면 그것이 **먼저다**
        // (가상 목록). `process_step_key`가 묶음보다 앞에 선 것과 같은 규칙이다 —
        // 초점이 선 element 자신의 글자 쓰임이 감싼 묶음보다 앞선다. 뒤에 두면
        // 목록을 감싼 묶음이 글자를 통째로 가져가 창에 걸친 행만 찾는다.
        const ui_element* const focused_element { tree->find(snapshot_.focused) };
        //  - 비활성 element는 묻지 않는다. `process_step_key`가 이미 그렇게 하고,
        //    흐리게 그려 둔 목록이 글자만 삼키면 화살표는 죽었는데 글자는 사는
        //    반쪽 상태가 된다.
        const key_search_target* const search { focused_element != nullptr && focused_element->enabled() ? focused_element->key_search() : nullptr };
        // 묶음 밖에서는 글자로 갈 곳이 없다 — 그 글자는 앱의 것이 아니라 그냥 사라진다.
        const focus_group_scope scope { tree->focus_group_of(snapshot_.focused) };
        if (search == nullptr && (scope.axis == focus_axis::none || scope.members.empty()))
            return {};

        // 앞의 글자를 잊는 계기는 둘이다.
        //  - 시간이 끊겼다. controller는 시계를 조회하지 않으므로 문턱을 재는 값은
        //    이벤트가 실어 온 시각뿐이다 (그래서 `character_typed_event`에 time이 있다).
        //  - 초점이 **다른 길로** 옮겨 갔다 (Tab·화살표·클릭). 질의는 그것으로 옮겨 온
        //    자리에만 붙는다.
        if (event.time - last_typeahead_time_ > config_.typeahead_reset_time || snapshot_.focused != typeahead_focus_)
            typeahead_query_.clear();

        // 첫 글자면 **다음** 항목부터 찾는다 — 같은 글자를 거듭 치면 그 글자로
        // 시작하는 항목들을 돈다. 이어 친 글자면 **지금** 항목부터 찾는다 —
        // 글을 더 적은 것이지 다음으로 가자는 뜻이 아니다.
        const bool first { typeahead_query_.empty() };
        typeahead_query_ += text::text_edit_encode_utf8(event.character);
        last_typeahead_time_ = event.time;

        // 모델을 아는 목록은 자기가 답한다. 초점은 그 컨테이너에 그대로 서 있고
        // (옮길 자리가 tree에 없을 수 있다), 옮기는 것은 앱 상태인 커서다 —
        // 그래서 답이 자리표가 아니라 메시지다.
        if (search != nullptr)
        {
            typeahead_focus_ = snapshot_.focused;
            if (search->on_search == nullptr)
                return {};
            // `first`는 controller가 안다 — 질의를 잇고 끊는 것이 이쪽이라 element가
            // 질의의 길이로 되짚으면 UTF-8 한 글자를 여러 글자로 센다.
            std::optional<std::vector<input_action>> moved { search->on_search(typeahead_query_, first) };
            // 맞는 것이 없어도 글자는 이 element가 가진다 — 질의는 남겨 두어
            // 다음 글자가 이어 붙는다 (묶음의 글자 탐색과 같은 규약).
            return moved.has_value() ? std::move(*moved) : std::vector<input_action> {};
        }

        const auto found { std::find(scope.members.begin(), scope.members.end(), snapshot_.focused) };
        const std::size_t current { found != scope.members.end() ? static_cast<std::size_t>(found - scope.members.begin()) : 0u };
        const std::size_t count { scope.members.size() };
        const std::size_t start { first ? current + 1u : current };
        for (std::size_t step = 0; step < count; ++step)
        {
            const ui_element_id& candidate { scope.members[(start + step) % count] };
            const ui_element* const element { tree->find(candidate) };
            if (element == nullptr || element->search_label().starts_with(typeahead_query_) == false)
                continue;
            if (candidate != snapshot_.focused)
                set_keyboard_focus(snapshot_.focused_surface, candidate, event.time);
            typeahead_focus_ = candidate;
            // 맞은 것이 지금 자리여도 글자는 묶음이 가진다 — 앱으로 새지 않는다.
            return {};
        }
        // 맞는 것이 없으면 자리는 그대로다. 질의는 남겨 두어 다음 글자가 이어 붙는다.
        typeahead_focus_ = snapshot_.focused;
        return {};
    }

    std::optional<std::vector<input_action>> interaction_controller::process_step_key(const key_pressed_event& event)
    {
        if (snapshot_.focused == ui_element_id {})
            return std::nullopt;
        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { tree != nullptr ? tree->find(snapshot_.focused) : nullptr };
        if (element == nullptr || element->enabled() == false)
            return std::nullopt;
        const key_step_target* const target { element->key_step() };
        if (target == nullptr || (target->on_step == nullptr && target->on_step_with == nullptr))
            return std::nullopt;
        // 수정자와 함께라면 앱 단축키다 (묶음과 같은 규칙).
        //  - 예외는 수식 키의 뜻을 받는 element의 Ctrl이다 (`key_step_target::on_step_with`). 목록에서 Ctrl+↓는
        //    "선택을 두고 커서만 옮기기"라 그 목록의 것이다. Alt·Meta는 그래도 앱의 것이다.
        const bool with_context { target->on_step_with != nullptr };
        if (with_context ? (event.alt || event.meta) : event.shortcut_modifier_down())
            return std::nullopt;

        // 화살표는 축으로 갈린다. Page·Home·End에는 방향이 없어 축을 걸지 않는다 —
        // 가릴 것이 없는 키다 (묶음의 Home/End와 같은 판단이다).
        const auto axis_takes = [axis = target->axis](const bool horizontal) { return axis == focus_axis::both || (horizontal ? axis == focus_axis::horizontal : axis == focus_axis::vertical); };
        std::optional<value_step> step {};
        switch (event.key)
        {
        case key_code::arrow_left:
            if (axis_takes(true))
                step = value_step::decrease;
            break;
        case key_code::arrow_right:
            if (axis_takes(true))
                step = value_step::increase;
            break;
        case key_code::arrow_up:
            if (axis_takes(false))
                step = value_step::decrease;
            break;
        case key_code::arrow_down:
            if (axis_takes(false))
                step = value_step::increase;
            break;
        case key_code::page_up:
            step = value_step::decrease_page;
            break;
        case key_code::page_down:
            step = value_step::increase_page;
            break;
        case key_code::home:
            step = value_step::minimum;
            break;
        case key_code::end:
            step = value_step::maximum;
            break;
        default:
            break;
        }
        if (step.has_value() == false)
            return std::nullopt;
        // element의 nullopt는 "그 걸음은 내 것이 아니다"라 키가 그대로 흐른다.
        if (with_context)
            return target->on_step_with(*step, key_step_context { event.shift, event.control });
        return target->on_step(*step);
    }

    std::optional<std::vector<input_action>> interaction_controller::process_group_key(const key_pressed_event& event)
    {
        // 수정자와 함께라면 앱 단축키다.
        if (event.shortcut_modifier_down() || snapshot_.focused == ui_element_id {})
            return std::nullopt;
        const bool horizontal { event.key == key_code::arrow_left || event.key == key_code::arrow_right };
        const bool vertical { event.key == key_code::arrow_up || event.key == key_code::arrow_down };
        // Home/End에는 방향이 없다. 축이 선 묶음이면 어느 축이든 이 키의 임자다 —
        // "처음"과 "끝"을 정하는 것은 묶음의 **순서**이지 화살표의 방향이 아니다.
        const bool jump { event.key == key_code::home || event.key == key_code::end };
        if (horizontal == false && vertical == false && jump == false)
            return std::nullopt;

        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        if (tree == nullptr)
            return std::nullopt;
        const focus_group_scope scope { tree->focus_group_of(snapshot_.focused) };
        // 방향이 맞지 않는 화살표는 묶음의 것이 아니다 — 그대로 앱으로 흐른다.
        const bool matching_axis { scope.axis == focus_axis::both || (horizontal && scope.axis == focus_axis::horizontal) || (vertical && scope.axis == focus_axis::vertical) };
        const bool mine { jump ? scope.axis != focus_axis::none : matching_axis };
        if (mine == false)
            return std::nullopt;

        const auto found { std::find(scope.members.begin(), scope.members.end(), snapshot_.focused) };
        if (found == scope.members.end() || scope.members.size() < 2u)
            return std::nullopt;

        // 곧장 끝으로 간다 — **돌지 않는다.** 순환은 화살표의 것이고, 처음/끝이
        // 도는 순간 Home과 End가 같은 자리를 가리켜 둘 중 하나가 뜻을 잃는다.
        //  - 이미 그 자리에 있어도 키는 묶음이 가진다. 첫 항목에서만 Home이 앱으로
        //    새어 화면이 함께 맨 위로 뛰는 일이 없어야 한다.
        if (jump)
        {
            const ui_element_id& target { event.key == key_code::home ? scope.members.front() : scope.members.back() };
            if (target != snapshot_.focused)
                set_keyboard_focus(snapshot_.focused_surface, target, event.time);
            return std::vector<input_action> {};
        }

        // 끝에서 돈다 (Tab 순환과 같은 규칙).
        const bool forward { event.key == key_code::arrow_right || event.key == key_code::arrow_down };
        const std::size_t index { static_cast<std::size_t>(found - scope.members.begin()) };
        const std::size_t next { forward ? (index + 1u) % scope.members.size() : (index + scope.members.size() - 1u) % scope.members.size() };
        set_keyboard_focus(snapshot_.focused_surface, scope.members[next], event.time);
        return std::vector<input_action> {};
    }

    void interaction_controller::update_focus()
    {
        // 초점은 누르거나 Tab으로 옮겨야 생기고, **자리가 자기를 이름 지을 때**도
        // 생긴다 (아래 진입). 계기가 하나 느는 것이지 임자가 바뀌는 것이 아니다 —
        // 이름을 짓는 것은 여전히 앱이고 tree가 나른다 (focus-entry-design.md).
        //
        // 초점을 가진 element가 자기 표면의 tree에서 사라지면(dialog가 닫혔다,
        // 보조 창이 닫혔다) 초점을 거둔다.
        // 종류마다 edge를 따로 세지 않고 element의 존재만 본다.
        //
        // **가둠(modal) 밖에 있으면 사라진 것과 같다.** dialog를 연 버튼은 scrim
        // 뒤에 남아 눌러서는 닿을 수 없는데, 초점을 쥔 채로 두면 Space가 그것을
        // 다시 실행하고 초점 테가 scrim 위에 떠 보인다.
        //  - 가둠 밖을 막는 검사를 Space·화살표·텍스트 경로마다 흩어 두면 언젠가
        //    하나를 빠뜨린다. 여기서 거두면 그 아래가 전부 "초점 없음"의 이미
        //    옳은 길을 탄다 (modal-dialog-design.md).
        //
        // **숨었거나 비활성이 된 것도 사라진 것과 같다.** `find`는 숨긴 가지도 답하고
        // id는 그대로 남으므로, 존재만 보면 다음 tree에서 숨긴 버튼이 Enter로
        // 실행되고 비활성으로 만든 칸에 글자가 계속 간다. 조상까지 보이는가
        // (`visibly_contains`)와 지금 활성인가를 여기서 함께 본다 — 초점을 **줄 때**의
        // 조건(`focusable`·`enabled`)이 초점을 **쥐고 있는 동안**에도 참이어야 한다.
        if (snapshot_.focused != ui_element_id {})
        {
            const ui_tree* const focused_tree { surface_tree(snapshot_.focused_surface) };
            const ui_element* const focused_element { focused_tree != nullptr ? focused_tree->find(snapshot_.focused) : nullptr };
            if (focused_element == nullptr || focused_element->enabled() == false || focused_tree->visibly_contains(snapshot_.focused) == false
                || focused_tree->within_focus_trap(snapshot_.focused) == false)
                clear_focus();
        }

        // 가둠은 **활성 표면**의 것이다 — 사용자가 보고 있는 창의 가둠이 임자다.
        // 가둠이 막는 것은 키보드이고 키보드는 활성 창에 있으므로, 활성 창이 아닌
        // 가둠은 지금 아무도 가두고 있지 않다 (active-surface-design.md).
        //  - focus-entry-design.md이 "표면 둘이 동시에 가두면 누가 임자인가"로
        //    미뤄 둔 자리의 답이다.
        //  - 활성 표면의 창이 닫혔으면 nullptr이라 진입이 저절로 멈춘다.
        const ui_tree* const active_tree { surface_tree(active_surface_) };
        const ui_element* const trap { active_tree != nullptr ? active_tree->focus_trap() : nullptr };

        // 되돌아갈 자리는 **가둠이 살아 있는 동안** 한 칸에 적어 둔다.
        // 가둠이 사라진 tree에는 그 이름이 없다 — 이름을 든 element가 함께 사라진다.
        //  - 중첩 가둠은 "그리기 순서의 마지막이 임자"라는 기존 규칙이 그대로 스택
        //    노릇을 한다. 안쪽이 사라지면 바깥이 다시 임자가 되어 자기 값으로 덮는다
        //    (focus-entry-design.md).
        //  - 어느 표면의 가둠이었는지도 함께 적는다. 되돌리기가 남의 창에서
        //    터지지 않게 하는 유일한 근거다 (3.4).
        if (trap != nullptr)
        {
            focus_return_ = trap->focus_return();
            focus_return_surface_ = active_surface_;
        }

        // 진입 — **초점이 없고 가둠이 서 있고 그 가둠이 자리를 이름 지었으면
        // 거기 세운다.** 명령이 아니라 술어다. 그래서 되풀이 적용이 공짜로
        // 풀리고(세우고 나면 전제가 거짓이다), 사용자가 가둠 안 어디든 눌러
        // 초점을 잡으면 그 뒤로 다시 발화하지 않는다 (focus-entry-design.md).
        //  - 바로 위에서 거두는 코드가 판 구멍을 **같은 함수의 다음 줄**이 메운다.
        //    가둠 안에서 "초점 없음"은 키가 갈 곳이 없는 죽은 상태라, 그 구멍을
        //    사용자의 Tab이 아니라 여기서 메운다 (2.2·3.3).
        //  - 시각은 비운다 — `set_tree`에는 시각 인자가 없고 controller는 시계를
        //    조회하지 않는다.
        //  - 자리를 세우는 표면은 그 가둠이 사는 표면, 곧 활성 표면이다. 여기에
        //    빈 값(주 창)을 못 박아 두었던 것이 보조 창의 가둠이 아무 자리에도
        //    초점을 세우지 못한 이유였다 (active-surface-design.md).
        //  - **테는 끈다.** 계기가 키보드가 아니라 tree라, 테를 켜면 마우스로 연
        //    dialog에도 테가 미리 서서 화면이 먼저 말을 건다. 테의 뜻("키보드로
        //    옮긴 초점")은 keyboard-focus-design.md의 그 규칙 그대로이고,
        //    Enter·Space·화살표는 테 없이도 이 초점으로 간다. 첫 Tab이 테를 켠다
        //    (focus-entry-design.md의 개정).
        if (trap != nullptr && snapshot_.focused == ui_element_id {})
            if (const std::optional<ui_element_id> entry { active_tree->focus_trap_entry() }; entry.has_value())
            {
                set_keyboard_focus(active_surface_, *entry, std::nullopt);
                snapshot_.focus_visible = false;
            }

        // 되돌리기 — 가둠이 사라졌고 적어 둔 자리가 있으면 거기 세우고 한 칸을 비운다.
        // 진입과 짝이라 같은 함수 안에 나란히 선다.
        //  - 테는 진입과 달리 **켠 채 둔다.** 진입 자리는 방금 뜬 dialog가 "키가
        //    여기로 온다"를 화면으로 이미 말하지만, 돌아온 자리는 아무것도 말하지
        //    않는다 — 테가 없으면 Space가 어디를 다시 실행할지 근거가 없다.
        //  - 그 자리가 지금 초점을 받을 수 없으면(화면이 바뀌었다) 초점 없음으로
        //    남는다. 한 칸은 어느 쪽이든 비운다 — 낡은 이름을 들고 있지 않는다.
        //  - **적어 둔 표면이 지금 활성일 때만** 발화한다. 그 조건이 없으면 보조
        //    창으로 넘어간 순간 "활성 표면에 가둠이 없다"가 참이 되어, 아직 서
        //    있는 주 창 가둠의 되돌리기가 터진다 (active-surface-design.md).
        if (trap == nullptr && focus_return_ != ui_element_id {} && focus_return_surface_ == active_surface_)
        {
            const ui_element_id target { std::exchange(focus_return_, ui_element_id {}) };
            focus_return_surface_.clear();
            const ui_element* const element { active_tree != nullptr ? active_tree->find(target) : nullptr };
            if (element != nullptr && element->focusable())
                set_keyboard_focus(active_surface_, target, std::nullopt);
        }

        // 컨텍스트 메뉴가 열리고 닫힐 때마다 키보드 강조를 지운다.
        // 새 메뉴는 강조 없이 시작한다 (첫 ↓가 첫 활성 항목을 고른다).
        const std::optional<menu_kinds> menu { policy_ != nullptr ? policy_->menu() : std::nullopt };
        const bool menu_open { menu.has_value() && find_menu(*menu).tree != nullptr };
        if (menu_open != context_menu_open_)
        {
            context_menu_open_ = menu_open;
            snapshot_.menu_highlight = {};
            snapshot_.menu_surface.clear();
        }
    }

    void interaction_controller::clear_focus() noexcept
    {
        snapshot_.focused = {};
        snapshot_.focused_input = {};
        snapshot_.focused_surface.clear();
        snapshot_.focus_started_at.reset();
        snapshot_.focus_visible = false;
    }

    void interaction_controller::clear_press() noexcept
    {
        pointer_drag_id_ = {};
        pointer_drag_held_.reset();
        pointer_drag_pending_ = false;
        pressed_id_ = {};
        pressed_button_ = pointer_button::none;
        pressed_surface_.clear();
        drag_candidate_ = false;
        snapshot_.pressed = {};
        snapshot_.pressed_surface.clear();
    }

    std::vector<input_action> interaction_controller::cancel_dropped_gestures()
    {
        inertia_.reset();
        // 유실분에 뗌이 있었어도 끌던 시트가 그 자리에 남지 않게 끝을 알린다. 터치 손잡이도 `pointer_drag_id_`를 쓰므로
        // 터치 접촉이 있으면 그쪽으로 한 번만 알린다.
        std::vector<input_action> actions {};
        if (touch_.has_value())
            actions = end_touch_drag(*touch_, touch_->last_x, touch_->last_y, {});
        else
            actions = end_pointer_drag(pointer_drag_x_, pointer_drag_y_, {}, pointer_drag_modifiers_);
        // 터치 접촉도 거둔다. 남은 손가락의 이벤트는 쥔 접촉이 없어 삼켜진다.
        touch_.reset();
        last_view_tap_.reset();
        view_drag_.reset();
        pointer_contact_.reset();
        clear_press();
        text_drag_id_ = {};
        abandon_drag();
        auto_scroll_.reset();
        return actions;
    }

    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot,
        messaging::latest_slot<surface_tree_list>& surface_tree_slot, messaging::channel<app_message>& app_inbox, messaging::latest_slot<interaction_snapshot>& interaction_slot,
        const std::function<void(ui_command)>& execute_ui_command, interaction_policy* const policy, interaction_config config, const std::function<void(app_ui_command)>& execute_app_ui_command,
        const std::function<void(clipboard_request)>& execute_clipboard, messaging::latest_slot<touch_gesture_config>* const touch_slot, const std::function<void(haptic_feedback)>& execute_haptic)
    {
        interaction_controller controller { policy, std::move(config) };
        std::uint64_t tree_version { 0 };
        std::uint64_t surface_version { 0 };
        std::uint64_t touch_version { 0 };
        interaction_snapshot published {};
        messaging::envelope<raw_input_event> received {};
        std::uint64_t next_sequence { 0 };
        const auto dispatch = [&](std::vector<input_action> actions) {
            for (input_action& action : actions)
                if (const auto* const message { std::get_if<app_message>(&action) }; message != nullptr)
                {
                    if (message->empty() == false)
                        post_with_retry(app_inbox, *message);
                }
                else if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr && execute_ui_command)
                {
                    // 편집 메뉴는 UI thread가 초점 snapshot에서 대상을 찾는다. 명령의 wake보다 먼저 게시한다.
                    if (*command == ui_command::text_edit_menu)
                    {
                        published = controller.snapshot();
                        static_cast<void>(interaction_slot.publish(published));
                    }
                    execute_ui_command(*command);
                }
                else if (auto* const app_command { std::get_if<app_ui_command>(&action) }; app_command != nullptr && execute_app_ui_command)
                    execute_app_ui_command(std::move(*app_command));
                else if (auto* const copy { std::get_if<clipboard_copy_request>(&action) }; copy != nullptr && execute_clipboard)
                    execute_clipboard(clipboard_request { std::move(*copy) });
                else if (const auto* const paste { std::get_if<clipboard_paste_request>(&action) }; paste != nullptr && execute_clipboard)
                    execute_clipboard(clipboard_request { *paste });
                else if (auto* const files { std::get_if<clipboard_files_copy_request>(&action) }; files != nullptr && execute_clipboard)
                    execute_clipboard(clipboard_request { std::move(*files) });
                else if (const auto* const read { std::get_if<clipboard_files_paste_request>(&action) }; read != nullptr && execute_clipboard)
                    execute_clipboard(clipboard_request { *read });
                else if (const auto* const haptic { std::get_if<haptic_request>(&action) }; haptic != nullptr && execute_haptic)
                    execute_haptic(haptic->feedback);
        };
        while (true)
        {
            // 시간이 흘러야 일어날 판정(길게 누르기·관성 스크롤)이 있으면 그 시각까지만 기다린다.
            std::chrono::milliseconds wait { 250 };
            if (const auto deadline { controller.next_deadline() }; deadline.has_value())
            {
                const auto remaining { std::chrono::ceil<std::chrono::milliseconds>(*deadline - std::chrono::steady_clock::now()) };
                wait = std::clamp(remaining, std::chrono::milliseconds { 0 }, wait);
            }
            const messaging::receive_status status { input_inbox.receive_wait(received, wait) };
            if (status == messaging::receive_status::closed)
                return;

            // 이벤트가 없는 턴(타임아웃)에도 최신 tree를 받는다.
            // set_tree가 마지막 포인터 위치로 hover를 재판정하므로,
            // 휠을 멈춘 뒤 도착한 tree의 반영이 다음 입력을 기다리지 않는다.
            if (const auto tree { tree_slot.take_newer(tree_version) }; tree.has_value())
            {
                tree_version = tree->version;
                controller.set_tree(tree->value);
            }
            if (auto surfaces { surface_tree_slot.take_newer(surface_version) }; surfaces.has_value())
            {
                surface_version = surfaces->version;
                controller.set_surface_trees(std::move(surfaces->value));
            }
            // 편집이 적용된 tree가 왔으면 여러 줄 칸의 caret 줄을 들인다. 바뀐 것이 없으면 아무것도 내지 않는다.
            dispatch(controller.follow_text_caret());
            // 게시자가 이미 검증했다. 그래도 잘못된 값이면 controller가 거절하고 직전 값이 남는다.
            if (touch_slot != nullptr)
                if (const auto touch { touch_slot->take_newer(touch_version) }; touch.has_value())
                {
                    touch_version = touch->version;
                    static_cast<void>(controller.set_touch_config(touch->value));
                }

            if (status == messaging::receive_status::received)
            {
                // drop_oldest 큐는 포화에서 앞을 버린다 — 접수 번호의 건너뜀이 그 흔적이다.
                // 유실분에 뗌·이탈 같은 상태 전이가 있었을 수 있으므로 몸짓부터 거둔다.
                if (received.sequence != next_sequence)
                    dispatch(controller.cancel_dropped_gestures());
                next_sequence = received.sequence + 1;

                // 큐가 밀렸으면 뒤에 더 이른 뗌·취소가 남아 있다. 벽시계가 아니라 입력 시각까지
                // 진행해야 짧은 탭이 길게 누르기로 바뀌지 않는다.
                //  - 판정을 이벤트보다 **먼저** 한다. 기한이 지난 뒤 온 이동이 먼저 처리되면 끌기나
                //    흘려보기로 바뀌어, 그 전에 열렸어야 할 메뉴가 사라진다.
                const auto time_of = [](const auto& value) -> std::optional<std::chrono::steady_clock::time_point> {
                    if constexpr (requires { value.time; })
                        return value.time;
                    else
                        return std::nullopt;
                };
                if (const auto time { std::visit(time_of, received.payload) }; time.has_value())
                    dispatch(controller.advance(*time));
                dispatch(controller.process(received.payload));
            }
            else
                dispatch(controller.advance(std::chrono::steady_clock::now()));

            if ((controller.snapshot() == published) == false)
            {
                published = controller.snapshot();
                static_cast<void>(interaction_slot.publish(published));
            }
        }
    }
} // namespace luil
