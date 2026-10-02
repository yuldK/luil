#include "luil/ui/ui_interaction.h"

#include "luil/messaging/envelope.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
#include <variant>

namespace luil {
    namespace {
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

    void interaction_controller::set_tree(std::shared_ptr<const ui_tree> tree) noexcept
    {
        tree_ = std::move(tree);

        // 사라진 element를 가리키는 초점·강조는 새 tree를 받는 즉시 거둔다.
        // snapshot을 곧바로 읽는 렌더러가 없어진 텍스트 박스의 caret을 기다리지 않게 한다.
        update_focus();

        // 포인터가 머문 자리에서 내용이 스크롤로 흐르면 커서 아래 element가 바뀐다.
        // 새 tree를 받을 때 마지막 위치로 hover를 다시 판정해
        // tooltip이 이전 대상을 따라 쓸려 다니지 않게 한다.
        // 끌기 중에는 hover가 잡은 대상에 남아야 하므로 건드리지 않는다.
        if (pointer_inside_ && pointer_drag_id_ == ui_element_id {} && snapshot_.drag.has_value() == false)
            update_hover(last_pointer_x_, last_pointer_y_, last_pointer_time_);
    }

    void interaction_controller::set_surface_trees(surface_tree_list surfaces) noexcept
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
            update_hover(last_pointer_x_, last_pointer_y_, last_pointer_time_);
    }

    void interaction_controller::clear_gone_surface_gestures() noexcept
    {
        // 누름은 시작한 표면에 매인다 (`pressed_surface_`).
        // 그 표면이 사라지면 잡은 대상을 다시 찾을 tree가 없어, 남은 끌기 대상이
        // 이후의 이동을 전부 조기 반환으로 삼킨다.
        //  - 주 창(빈 id)은 표면 목록의 임자가 아니라 여기서 보지 않는다.
        //  - 터치 접촉은 누름보다 오래 산다 (스크롤로 바뀐 뒤에도). 따로 거둔다.
        if (touch_.has_value() && touch_->surface.empty() == false && surface_tree(touch_->surface) == nullptr)
            cancel_touch();
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
        if (snapshot_.drag.has_value() && surface_tree(snapshot_.drag->surface) == nullptr)
            snapshot_.drag.reset();
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
                    if (policy_ == nullptr || tree == nullptr)
                        return {};
                    return policy_->on_wheel(*tree, value, delta);
                }
                else if constexpr (std::is_same_v<value_type, key_pressed_event>)
                    return process_key(value);
                else if constexpr (std::is_same_v<value_type, access_focus_event>)
                    return process_access_focus(value);
                else if constexpr (std::is_same_v<value_type, pointer_cancelled_event>)
                    return process_cancel(value);
                else if constexpr (std::is_same_v<value_type, character_typed_event>)
                {
                    // 문자 입력은 초점을 가진 텍스트 박스로만 간다.
                    // 받을 값인지는 앱 logic이 거른다.
                    const std::optional<text_input_target> target { text_target(snapshot_.focused_input.kind) };
                    // 텍스트 박스가 없으면 그 글자는 **묶음 안에서 항목을 찾는** 데 쓴다.
                    // 순서가 이 자리에 이미 서 있다 — 칸이 먼저이고, 글자 탐색은 그 다음이다.
                    if (target.has_value() == false)
                        return process_typeahead(value);
                    if (value.character == U'\b')
                        return { text_edit_action(*target, text::text_edit_command::backspace) };
                    // Ctrl+Backspace는 WM_CHAR가 U+007F로 준다.
                    if (value.character == 0x7Fu)
                        return { text_edit_action(*target, text::text_edit_command::delete_word_left) };
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

        pointer_inside_ = true;
        last_pointer_x_ = event.x;
        last_pointer_y_ = event.y;
        last_pointer_time_ = event.time;
        last_pointer_surface_ = event.surface;

        // 터치가 조작을 쥐고 있으면 마우스·펜의 이동은 hover만 바꾼다.
        // 잡은 전용 조작(스크롤 막대)을 다른 장치가 끌면 안 된다.
        if (touch_.has_value())
        {
            update_hover(event.x, event.y, event.time);
            return {};
        }

        if (pointer_contact_.has_value() && (pointer_contact_->device != event.device || pointer_contact_->id != event.pointer_id || pointer_contact_->surface != event.surface))
        {
            update_hover(event.x, event.y, event.time);
            return {};
        }

        // 잡은 대상은 누름이 시작된 표면에서 다시 찾는다.
        // 캡처 중에는 이벤트도 같은 표면에서 온다.
        const ui_tree* const pressed_tree { surface_tree(pressed_surface_) };

        // 텍스트 박스를 잡고 있으면 이동이 선택 범위를 늘린다.
        // 포인터가 칸을 벗어나도 이어진다.
        //  - 같은 id로 다시 찾는다.
        if (text_drag_id_ != ui_element_id {})
        {
            const ui_element* const target { pressed_tree != nullptr ? pressed_tree->find(text_drag_id_) : nullptr };
            if (target == nullptr)
                return {};
            return place_text_caret(*target, event.x, true, false);
        }

        // 스크롤 막대를 잡고 있으면 이동은 전부 그 element의 몫이다.
        // tree가 다시 빌드되어도 같은 id로 찾아 이어서 끈다.
        if (pointer_drag_id_ != ui_element_id {})
        {
            const ui_element* const target { pressed_tree != nullptr ? pressed_tree->find(pointer_drag_id_) : nullptr };
            const pointer_drag_target* const handler { target != nullptr ? target->pointer_drag() : nullptr };
            if (handler == nullptr || handler->on_move == nullptr)
                return {};

            const ui_action_context previous { pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, false };
            const ui_action_context current { pointer_drag_id_, event.x, event.y, false };
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            return handler->on_move(previous, current);
        }

        const ui_tree* const tree { surface_tree(event.surface) };
        if (tree == nullptr)
        {
            update_hover(event.x, event.y, event.time);
            return {};
        }

        // drag 중이면 ghost 위치와 수락 중인 drop 대상만 갱신한다.
        // drop 대상은 이벤트가 난 표면에서 찾는다.
        if (snapshot_.drag.has_value())
        {
            update_drag(*tree, event.x, event.y);
            update_hover(event.x, event.y, event.time);
            return {};
        }

        // 눌린 채 임계 거리를 넘으면 클릭 대신 drag가 시작된다.
        if (drag_candidate_ && pressed_button_ == pointer_button::left && distance_between(pressed_x_, pressed_y_, event.x, event.y) >= config_.drag_start_distance)
        {
            const ui_element* const source { pressed_tree != nullptr ? pressed_tree->find(pressed_id_) : nullptr };
            if (source != nullptr && source->enabled() && source->drag() != nullptr && source->drag()->make_payload)
            {
                const ui_action_context context { pressed_id_, pressed_x_, pressed_y_, false };
                // 끌기는 지금 이벤트가 난 표면에서 선다 — 캡처 중이라 누른 표면과 같다.
                // 그 표면이 사라지면 끌기도 죽고, 표시도 그 표면에만 선다
                // (multi-window-design.md).
                snapshot_.drag = drag_visual { source->drag()->make_payload(context), event.x, event.y, {}, event.surface };
                clear_press();
            }
            drag_candidate_ = false;
        }

        update_hover(event.x, event.y, event.time);
        return {};
    }

    void interaction_controller::update_drag(const ui_tree& tree, const float x, const float y)
    {
        snapshot_.drag->x = x;
        snapshot_.drag->y = y;
        snapshot_.drag->hovered_drop_target = {};
        if (const ui_element* const over { tree.find_drop_target(x, y, snapshot_.drag->payload) }; over != nullptr)
            snapshot_.drag->hovered_drop_target = over->id();
    }

    std::vector<input_action> interaction_controller::finish_drag(const ui_tree* const tree, const float x, const float y)
    {
        std::vector<input_action> actions {};
        const drag_visual drag { *snapshot_.drag };
        // **죽은 표면 위의 뗌은 drop이 아니다.** 놓을 자리를 찾지 않고
        // 거두기만 한다 — 그 표면에는 이제 아무것도 없다.
        const ui_element* const over { tree != nullptr ? tree->find_drop_target(x, y, drag.payload) : nullptr };
        if (over != nullptr && over->drop()->on_drop)
            actions = over->drop()->on_drop(drag.payload, ui_action_context { over->id(), x, y, false });
        snapshot_.drag.reset();
        clear_press();
        return actions;
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
            if ((source != nullptr && source->scroll != nullptr) || element.hit_opaque())
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

        [[nodiscard]] float positive_scale(const float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
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
        return pan_target { owner->id(), axis, positive_scale(source.scale), source.scroll };
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
        return pan_target { route->id, axis, positive_scale(route->scale), route->scroll };
    }

    bool valid_touch_gesture_config(const touch_gesture_config& config) noexcept
    {
        const auto distance = [](const float value) { return std::isfinite(value) && value > 0.0f; };
        return distance(config.pan_start_distance) && distance(config.press_move_tolerance) && config.pan_start_time.count() > 0 && config.long_press_time.count() > 0
            && config.pan_start_time < config.long_press_time;
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
            return;
        case text::text_edit_command::place_caret:
            text::text_edit_place_caret(state, request.offset, request.extend);
            return;
        }
    }

    text_input_view make_text_input_view(const text::text_edit_state& state, const std::optional<text_composition_event>& composition, const text_input_target target)
    {
        text_input_view view {};
        view.text = state.text;
        view.caret = state.caret;
        view.anchor = state.anchor;
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
        if (event.device == pointer_device::touch)
            return process_touch_press(event);
        // 실제 마우스·펜의 새 누름은 진행 중인 터치 조작을 취소하고 시작한다.
        // 취소된 접촉의 나머지 이벤트는 id가 맞는 접촉이 없어 삼켜진다.
        if (touch_.has_value())
            cancel_touch();
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
            return with_press(place_text_caret(*hit, event.x, word == false && event.shift, word));
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
        if (event.button == pointer_button::left && handler != nullptr)
        {
            pointer_drag_id_ = hit->id();
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            if (handler->on_press)
                return with_press(handler->on_press(ui_action_context { pointer_drag_id_, event.x, event.y, false }));
        }
        return actions;
    }

    void interaction_controller::apply_press_focus(const ui_tree& tree, const ui_element* const hit, const std::u8string& surface, const std::chrono::steady_clock::time_point time)
    {
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
            pointer_drag_id_ = {};
            clear_press();
            return {};
        }

        // drag를 끝낸다.
        // 수락하는 대상 위에서만 drop 액션이 실행된다.
        if (snapshot_.drag.has_value())
        {
            if (event.button == pointer_button::left)
                return finish_drag(tree, event.x, event.y);
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
        return run_trigger(*hit, trigger, event.x, event.y, false);
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
        // 흘릴 후보가 어느 축에도 없으면 시간 창 동안 끌기를 보류할 까닭이 없다.
        //  - 클릭 대상이 없는 여백·일반 글 위에서도 후보는 선다 (휠과 같은 탐색이다).
        contact.pan_open
            = contact.config.pan_enabled && (resolve_pan(*tree, event.x, event.y, scroll_axis::vertical).has_value() || resolve_pan(*tree, event.x, event.y, scroll_axis::horizontal).has_value());

        // 초점은 아직 옮기지 않는다. 스크롤이 될 접촉이 칸의 초점과 IME를 빼앗으면
        // 안 된다 — 탭이 확정되는 뗌에서 옮긴다.
        const ui_element* const hit { tree->hit_test(event.x, event.y) };
        pressed_surface_ = event.surface;
        if (hit == nullptr || hit->enabled() == false)
        {
            touch_ = std::move(contact);
            return {};
        }
        contact.target = hit->id();
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
        if (handler == nullptr)
        {
            touch_ = std::move(contact);
            return actions;
        }
        // 전용 손잡이는 누르는 즉시 조작이 시작된다. 스크롤 후보가 아니다.
        contact.phase = touch_phase::handle;
        contact.pan_open = false;
        touch_ = std::move(contact);
        pointer_drag_id_ = hit->id();
        pointer_drag_x_ = event.x;
        pointer_drag_y_ = event.y;
        // 조작이 이미 시작됐으므로 초점은 마우스처럼 누를 때 옮긴다.
        apply_press_focus(*tree, hit, event.surface, event.time);
        if (handler->on_press == nullptr)
            return actions;
        std::vector<input_action> moved { handler->on_press(ui_action_context { pointer_drag_id_, event.x, event.y, false }) };
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
        if (touch_.has_value() == false || touch_->id != event.pointer_id || touch_->surface != event.surface)
            return {};
        touch_contact& contact { *touch_ };
        const ui_tree* const tree { surface_tree(contact.surface) };
        // 거리는 누른 표면의 배율로 나눈 논리 픽셀이다.
        // 가장 먼 거리를 남기므로 이동 이벤트를 몇 번에 나눠 받았는지가 판정을 바꾸지 않는다.
        const float travel { distance_between(contact.start_x, contact.start_y, event.x, event.y) / contact.scale };
        contact.max_distance = std::max(contact.max_distance, travel);

        std::vector<input_action> actions {};
        switch (contact.phase)
        {
        case touch_phase::handle: {
            const ui_element* const target { tree != nullptr ? live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) : nullptr };
            const pointer_drag_target* const handler { target != nullptr ? target->pointer_drag() : nullptr };
            if (handler == nullptr)
            {
                cancel_touch();
                return {};
            }
            if (handler->on_move == nullptr)
                break;
            const ui_action_context previous { pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, false };
            const ui_action_context current { pointer_drag_id_, event.x, event.y, false };
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            actions = handler->on_move(previous, current);
            if (actions.empty() == false)
                contact.moved_action = true;
            break;
        }
        case touch_phase::dragging:
            if (tree == nullptr || live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) == nullptr)
            {
                cancel_touch();
                return {};
            }
            if (snapshot_.drag.has_value())
                update_drag(*tree, event.x, event.y);
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
                if (std::optional<pan_target> pan { resolve_pan(*tree, contact.start_x, contact.start_y, axis) }; pan.has_value())
                {
                    contact.phase = touch_phase::panning;
                    contact.pan = std::move(pan);
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
                if (source->drag() != nullptr && source->drag()->make_payload)
                {
                    const ui_action_context context { contact.target, contact.start_x, contact.start_y, false };
                    snapshot_.drag = drag_visual { source->drag()->make_payload(context), event.x, event.y, {}, contact.surface };
                    clear_press();
                    contact.phase = touch_phase::dragging;
                    update_drag(*tree, event.x, event.y);
                }
            }
            break;
        }
        }
        contact.last_x = event.x;
        contact.last_y = event.y;
        return actions;
    }

    std::vector<input_action> interaction_controller::process_touch_release(const pointer_released_event& event)
    {
        if (touch_.has_value() == false || touch_->id != event.pointer_id || touch_->surface != event.surface)
            return {};
        touch_contact contact { std::move(*touch_) };
        touch_.reset();
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
            return pan_step(*pan, contact.last_x, contact.last_y, event.x, event.y);
        }
        case touch_phase::dragging:
            if (tree == nullptr || live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) == nullptr)
            {
                snapshot_.drag.reset();
                clear_press();
                return {};
            }
            if (snapshot_.drag.has_value())
                return finish_drag(tree, event.x, event.y);
            clear_press();
            return {};
        case touch_phase::handle: {
            // 손잡이를 놓는 것은 클릭이 아니다 (마우스와 같다). 움직이지 않고
            // 오래 눌렀으면 길게 누르기만 남는다.
            clear_press();
            if (still == false || contact.moved_action || tree == nullptr)
                return {};
            const ui_element* const target { live_touch_target(*tree, contact.target, contact.start_x, contact.start_y) };
            if (target == nullptr || contact.config.long_press_enabled == false || event.time - contact.pressed_at < contact.config.long_press_time
                || target->action(ui_trigger::right_click) == nullptr)
                return {};
            if (policy_ != nullptr)
                policy_->on_click(*target);
            return run_trigger(*target, ui_trigger::right_click, event.x, event.y, false);
        }
        case touch_phase::pending:
            clear_press();
            // 허용치를 넘었지만 스크롤도 끌기도 되지 않은 접촉은 클릭으로 되돌리지 않는다.
            if (still == false)
                return {};
            return finish_touch_tap(contact, event);
        }
        return {};
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

        // 길게 누르기는 이벤트 시각으로만 잰다 — 누르고 있는 동안 메뉴를 여는
        // 타이머는 없다. 우클릭 액션이 없으면 그냥 탭이다.
        if (contact.config.long_press_enabled && event.time - contact.pressed_at >= contact.config.long_press_time && target->action(ui_trigger::right_click) != nullptr)
        {
            // 우클릭이 된 접촉은 연속 탭의 기록에서도 빠진다.
            last_click_id_ = {};
            last_click_surface_.clear();
            click_streak_ = 0;
            if (policy_ != nullptr)
                policy_->on_click(*target);
            return run_trigger(*target, ui_trigger::right_click, event.x, event.y, false);
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
            click_streak_ = repeat ? click_streak_ + 1 : 1;
            remember();
            if (click_streak_ >= 3)
                return { text_edit_action(*text, text::text_edit_command::select_all) };
            return place_text_caret(*target, event.x, false, click_streak_ == 2);
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
        return run_trigger(*target, trigger, event.x, event.y, false);
    }

    std::vector<input_action> interaction_controller::process_cancel(const pointer_cancelled_event& event)
    {
        if (event.device == pointer_device::touch)
        {
            if (touch_.has_value() && touch_->id == event.pointer_id && touch_->surface == event.surface)
                cancel_touch();
            return {};
        }
        // 장치만 같아도 다른 포인터·표면의 취소일 수 있다.
        if (touch_.has_value() || pointer_contact_.has_value() == false || pointer_contact_->device != event.device || pointer_contact_->id != event.pointer_id
            || pointer_contact_->surface != event.surface)
            return {};
        cancel_pointer_gesture();
        return {};
    }

    void interaction_controller::cancel_pointer_gesture() noexcept
    {
        pointer_contact_.reset();
        text_drag_id_ = {};
        if (snapshot_.drag.has_value() && snapshot_.drag->payload.files.empty())
            snapshot_.drag.reset();
        clear_press();
    }

    void interaction_controller::cancel_touch() noexcept
    {
        if (touch_.has_value() && touch_->phase == touch_phase::dragging)
            snapshot_.drag.reset();
        touch_.reset();
        clear_press();
    }

    bool interaction_controller::set_touch_config(const touch_gesture_config& config) noexcept
    {
        if (valid_touch_gesture_config(config) == false)
            return false;
        config_.touch = config;
        if (touch_.has_value())
        {
            const touch_phase phase { touch_->phase };
            const bool pan_off { touch_->config.pan_enabled && config.pan_enabled == false && (phase == touch_phase::pending || phase == touch_phase::panning) };
            const bool long_press_off { touch_->config.long_press_enabled && config.long_press_enabled == false && phase == touch_phase::pending };
            // 손잡이 조작은 계속하지만 그 접촉의 메뉴 후보는 즉시 끈다.
            // 다시 켜도 이미 내려가 있는 손가락에 메뉴 자격을 새로 주지 않는다.
            if (phase == touch_phase::handle && config.long_press_enabled == false)
                touch_->config.long_press_enabled = false;
            if (pan_off || long_press_off)
                cancel_touch();
        }
        return true;
    }

    std::vector<input_action> interaction_controller::place_text_caret(const ui_element& element, const float x, const bool extend, const bool select_word) const
    {
        const std::optional<text_input_target> target { text_target(element.id().kind) };
        if (target.has_value() == false || policy_ == nullptr)
            return {};
        const std::optional<std::size_t> offset { element.offset_at(x, config_.measure_text) };
        if (offset.has_value() == false)
            return {};

        text_edit_request request {};
        request.target = *target;
        request.command = select_word ? text::text_edit_command::select_word : text::text_edit_command::place_caret;
        request.offset = *offset;
        request.extend = extend;
        return { policy_->make_text_edit_action(request) };
    }

    std::vector<input_action> interaction_controller::copy_focused_selection(const text_input_target target, const bool erase) const
    {
        // 복사할 글은 초점 표면의 tree가 들고 있다.
        // 선택이 없으면 아무 일도 하지 않는다.
        const ui_tree* const focused_tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { focused_tree != nullptr ? focused_tree->find(snapshot_.focused_input) : nullptr };
        const std::optional<text_input_snapshot> value { element != nullptr ? element->text_input() : std::nullopt };
        if (value.has_value() == false || value->caret == value->anchor)
            return {};

        const std::size_t begin { value->caret < value->anchor ? value->caret : value->anchor };
        const std::size_t end { value->caret < value->anchor ? value->anchor : value->caret };
        std::vector<input_action> actions {};
        actions.push_back(input_action { clipboard_copy_request { std::u8string { value->text.substr(begin, end - begin) } } });
        if (erase)
            actions.push_back(text_edit_action(target, text::text_edit_command::cut));
        return actions;
    }

    std::optional<std::vector<input_action>> interaction_controller::process_text_input_key(const key_pressed_event& event)
    {
        const std::optional<text_input_target> focused { text_target(snapshot_.focused_input.kind) };
        if (focused.has_value() == false)
            return std::nullopt;
        const text_input_target target { *focused };
        const bool primary_shortcut { event.primary_shortcut_down() };
        const bool word_navigation { event.word_navigation_down() };

        switch (event.key)
        {
        case key_code::arrow_left:
            return std::vector<input_action> { text_edit_action(target, word_navigation ? text::text_edit_command::move_word_left : text::text_edit_command::move_left, event.shift) };
        case key_code::arrow_right:
            return std::vector<input_action> { text_edit_action(target, word_navigation ? text::text_edit_command::move_word_right : text::text_edit_command::move_right, event.shift) };
        case key_code::home:
            return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_start, event.shift) };
        case key_code::end:
            return std::vector<input_action> { text_edit_action(target, text::text_edit_command::move_line_end, event.shift) };
        case key_code::delete_forward:
            // Shift+Delete는 선택 영역을 잘라내고, Ctrl+Delete는 오른쪽 낱말을 지운다.
            if (event.shift && event.control == false)
                return copy_focused_selection(target, true);
            return std::vector<input_action> { text_edit_action(target, word_navigation ? text::text_edit_command::delete_word_right : text::text_edit_command::delete_forward) };
        case key_code::insert:
            // Ctrl+Insert는 복사, Shift+Insert는 붙여넣기다.
            if (event.control)
                return copy_focused_selection(target, false);
            if (event.shift)
                return std::vector<input_action> { input_action { clipboard_paste_request { target } } };
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
                return std::vector<input_action> { input_action { clipboard_paste_request { target } } };
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
            snapshot_.drag.reset();
            return {};
        }

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
        return run_trigger(*button, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, false);
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
        return (*dismiss)(ui_action_context { trap->id(), box.x + box.width / 2.0f, box.y + box.height / 2.0f, false });
    }

    std::optional<std::vector<input_action>> interaction_controller::process_menu_key(const key_pressed_event& event, const menu_kinds& kinds, const menu_location& host)
    {
        const ui_tree& tree { *host.tree };
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
            if (snapshot_.menu_highlight == ui_element_id {})
                return std::vector<input_action> {};
            const ui_element* const item { tree.find(snapshot_.menu_highlight) };
            if (item == nullptr || item->enabled() == false)
                return std::vector<input_action> {};
            const rect_f box { item->bounds() };
            return run_trigger(*item, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, false);
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
        if (tree_ != nullptr && holds_menu(*tree_))
            return { tree_.get(), {} };
        for (const auto& [id, tree] : surface_trees_)
            if (tree != nullptr && holds_menu(*tree))
                return { tree.get(), id };
        return {};
    }

    std::vector<input_action> interaction_controller::run_trigger(const ui_element& element, const ui_trigger trigger, const float x, const float y, const bool control)
    {
        const ui_action* const action { element.action(trigger) };
        if (action == nullptr)
            return {};
        return (*action)(ui_action_context { element.id(), x, y, control });
    }

    void interaction_controller::update_hover(const float x, const float y, const std::chrono::steady_clock::time_point time)
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
            return;
        snapshot_.hovered = hovered;
        if (none)
        {
            snapshot_.hovered_surface.clear();
            snapshot_.hover_started_at.reset();
        }
        else
        {
            snapshot_.hovered_surface = last_pointer_surface_;
            snapshot_.hover_started_at = time;
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
        return run_trigger(*element, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, false);
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
        // 수정자와 함께라면 앱 단축키다 (묶음과 같은 규칙).
        if (event.shortcut_modifier_down() || snapshot_.focused == ui_element_id {})
            return std::nullopt;
        const ui_tree* const tree { surface_tree(snapshot_.focused_surface) };
        const ui_element* const element { tree != nullptr ? tree->find(snapshot_.focused) : nullptr };
        if (element == nullptr || element->enabled() == false)
            return std::nullopt;
        const key_step_target* const target { element->key_step() };
        if (target == nullptr || target->on_step == nullptr)
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
        pressed_id_ = {};
        pressed_button_ = pointer_button::none;
        pressed_surface_.clear();
        drag_candidate_ = false;
        snapshot_.pressed = {};
        snapshot_.pressed_surface.clear();
    }

    void interaction_controller::cancel_dropped_gestures() noexcept
    {
        // 터치 접촉도 거둔다. 남은 손가락의 이벤트는 쥔 접촉이 없어 삼켜진다.
        touch_.reset();
        pointer_contact_.reset();
        clear_press();
        text_drag_id_ = {};
        snapshot_.drag.reset();
    }

    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot,
        messaging::latest_slot<surface_tree_list>& surface_tree_slot, messaging::channel<app_message>& app_inbox, messaging::latest_slot<interaction_snapshot>& interaction_slot,
        const std::function<void(ui_command)>& execute_ui_command, interaction_policy* const policy, interaction_config config, const std::function<void(app_ui_command)>& execute_app_ui_command,
        const std::function<void(clipboard_request)>& execute_clipboard, messaging::latest_slot<touch_gesture_config>* const touch_slot)
    {
        interaction_controller controller { policy, std::move(config) };
        std::uint64_t tree_version { 0 };
        std::uint64_t surface_version { 0 };
        std::uint64_t touch_version { 0 };
        interaction_snapshot published {};
        messaging::envelope<raw_input_event> received {};
        std::uint64_t next_sequence { 0 };
        while (true)
        {
            const messaging::receive_status status { input_inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
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
                    controller.cancel_dropped_gestures();
                next_sequence = received.sequence + 1;

                for (input_action& action : controller.process(received.payload))
                    if (const auto* const message { std::get_if<app_message>(&action) }; message != nullptr)
                    {
                        if (message->empty() == false)
                            post_with_retry(app_inbox, *message);
                    }
                    else if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr && execute_ui_command)
                        execute_ui_command(*command);
                    else if (auto* const app_command { std::get_if<app_ui_command>(&action) }; app_command != nullptr && execute_app_ui_command)
                        execute_app_ui_command(std::move(*app_command));
                    else if (auto* const copy { std::get_if<clipboard_copy_request>(&action) }; copy != nullptr && execute_clipboard)
                        execute_clipboard(clipboard_request { std::move(*copy) });
                    else if (const auto* const paste { std::get_if<clipboard_paste_request>(&action) }; paste != nullptr && execute_clipboard)
                        execute_clipboard(clipboard_request { *paste });
            }

            if ((controller.snapshot() == published) == false)
            {
                published = controller.snapshot();
                static_cast<void>(interaction_slot.publish(published));
            }
        }
    }
} // namespace luil
