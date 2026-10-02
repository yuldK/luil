#include "android/android_input.h"

#include <android/input.h>
#include <android/keycodes.h>

#include <algorithm>
#include <utility>

namespace luil::android {
    namespace {
        // Win32의 WHEEL_DELTA다. 휠 한 눈금이 이만큼이다.
        constexpr float wheel_detent { 120.0f };

        [[nodiscard]] bool has_meta(const std::int32_t meta_state, const std::int32_t flag) noexcept
        {
            return (meta_state & flag) != 0;
        }

        [[nodiscard]] pointer_device device_of(const std::int32_t tool_type) noexcept
        {
            switch (tool_type)
            {
            case AMOTION_EVENT_TOOL_TYPE_STYLUS:
            case AMOTION_EVENT_TOOL_TYPE_ERASER:
                return pointer_device::pen;
            case AMOTION_EVENT_TOOL_TYPE_MOUSE:
                return pointer_device::mouse;
            default:
                // 손가락과 모르는 도구(터치스크린)는 터치다.
                return pointer_device::touch;
            }
        }

        // 이름이 있는 키다. 수정자와 상관없이 키 이벤트다 (Win32 `named_key_from_virtual`).
        [[nodiscard]] key_code named_key(const std::int32_t key) noexcept
        {
            switch (key)
            {
            case AKEYCODE_DPAD_UP:
                return key_code::arrow_up;
            case AKEYCODE_DPAD_DOWN:
                return key_code::arrow_down;
            case AKEYCODE_DPAD_LEFT:
                return key_code::arrow_left;
            case AKEYCODE_DPAD_RIGHT:
                return key_code::arrow_right;
            case AKEYCODE_ENTER:
            case AKEYCODE_NUMPAD_ENTER:
            case AKEYCODE_DPAD_CENTER:
                return key_code::enter;
            case AKEYCODE_ESCAPE:
                return key_code::escape;
            case AKEYCODE_MOVE_HOME:
                return key_code::home;
            case AKEYCODE_MOVE_END:
                return key_code::end;
            case AKEYCODE_FORWARD_DEL:
                return key_code::delete_forward;
            case AKEYCODE_TAB:
                return key_code::tab;
            case AKEYCODE_PAGE_UP:
                return key_code::page_up;
            case AKEYCODE_PAGE_DOWN:
                return key_code::page_down;
            default:
                break;
            }
            if (key >= AKEYCODE_F1 && key <= AKEYCODE_F12)
            {
                constexpr key_code function_keys[] {
                    key_code::f1,
                    key_code::f2,
                    key_code::f3,
                    key_code::f4,
                    key_code::f5,
                    key_code::f6,
                    key_code::f7,
                    key_code::f8,
                    key_code::f9,
                    key_code::f10,
                    key_code::f11,
                    key_code::f12,
                };
                return function_keys[key - AKEYCODE_F1];
            }
            return key_code::none;
        }

        [[nodiscard]] bool modifier_key(const std::int32_t key) noexcept
        {
            switch (key)
            {
            case AKEYCODE_SHIFT_LEFT:
            case AKEYCODE_SHIFT_RIGHT:
            case AKEYCODE_CTRL_LEFT:
            case AKEYCODE_CTRL_RIGHT:
            case AKEYCODE_ALT_LEFT:
            case AKEYCODE_ALT_RIGHT:
            case AKEYCODE_META_LEFT:
            case AKEYCODE_META_RIGHT:
            case AKEYCODE_CAPS_LOCK:
            case AKEYCODE_NUM_LOCK:
            case AKEYCODE_SCROLL_LOCK:
            case AKEYCODE_FUNCTION:
                return true;
            default:
                return false;
            }
        }

        // 이름 없는 키다. 수정자가 있을 때의 단축키와 플랫폼 대역 (Win32 `modified_key_from_virtual`).
        [[nodiscard]] key_code modified_key(const std::int32_t key, const bool shortcut, const bool character_key) noexcept
        {
            // Insert는 예전 클립보드 배치라 Ctrl이나 Shift와 함께일 때만 뜻이 있다.
            if (key == AKEYCODE_INSERT)
                return key_code::insert;
            // Space는 글자이면서 누름이다. 텍스트 칸에서는 controller가 키 쪽을 흘려보낸다.
            if (key == AKEYCODE_SPACE)
                return key_code::space;
            if (modifier_key(key))
                return key_code::none;
            if (key >= AKEYCODE_A && key <= AKEYCODE_Z)
                return shortcut ? alphanumeric_key_code(static_cast<std::uint32_t>('A' + (key - AKEYCODE_A))) : key_code::none;
            if (key >= AKEYCODE_0 && key <= AKEYCODE_9)
                return shortcut ? alphanumeric_key_code(static_cast<std::uint32_t>('0' + (key - AKEYCODE_0))) : key_code::none;
            // 글자를 만드는 키는 수정자가 있을 때만 키다. 그냥 치면 글자의 것이다.
            if (character_key && shortcut == false)
                return key_code::none;
            return static_cast<key_code>(static_cast<std::uint32_t>(key_code::first_platform_key) + static_cast<std::uint32_t>(key));
        }
    } // namespace

    std::chrono::steady_clock::time_point event_time(const std::int64_t nanoseconds) noexcept
    {
        return std::chrono::steady_clock::time_point { std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds { nanoseconds }) };
    }

    std::vector<raw_input_event> input_translator::translate(const motion_input& input, const surface_mapping& mapping)
    {
        std::vector<raw_input_event> events {};
        if (input.current.pointers.empty())
            return events;
        // 한 이벤트의 포인터는 한 도구다. 마우스는 접촉이 아니라 버튼으로 다룬다.
        if (input.current.pointers.front().tool_type == AMOTION_EVENT_TOOL_TYPE_MOUSE)
        {
            translate_mouse(input, mapping, events);
            return events;
        }

        const std::int32_t action { input.action & AMOTION_EVENT_ACTION_MASK };
        const std::size_t action_index { static_cast<std::size_t>((input.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT) };
        // S Pen의 옆 버튼은 STYLUS_PRIMARY로 온다. 일부 펜은 SECONDARY로 보낸다.
        const bool barrel { (input.button_state & (AMOTION_EVENT_BUTTON_STYLUS_PRIMARY | AMOTION_EVENT_BUTTON_SECONDARY)) != 0 };
        const bool shift { has_meta(input.meta_state, AMETA_SHIFT_ON) };
        const auto sample_of = [&](const motion_frame& frame, const motion_pointer& pointer, const pointer_phase phase, const bool in_contact) {
            pointer_sample sample {};
            sample.phase = phase;
            sample.device = device_of(pointer.tool_type);
            sample.pointer_id = static_cast<std::uint32_t>(pointer.id);
            sample.in_contact = in_contact;
            sample.barrel = barrel;
            sample.eraser = pointer.tool_type == AMOTION_EVENT_TOOL_TYPE_ERASER;
            sample.shift = shift;
            sample.x = pointer.x - mapping.origin_x;
            sample.y = pointer.y - mapping.origin_y;
            sample.scale = mapping.scale;
            sample.time = event_time(frame.time_ns);
            return sample;
        };

        // 묶여 온 과거 표본이 먼저다. 과거 표본은 이동에만 있다.
        const bool hovering { action == AMOTION_EVENT_ACTION_HOVER_MOVE };
        if (action == AMOTION_EVENT_ACTION_MOVE || hovering)
            for (const motion_frame& frame : input.history)
                for (const motion_pointer& pointer : frame.pointers)
                    accept(sample_of(frame, pointer, pointer_phase::update, hovering == false), events);

        const motion_frame& frame { input.current };
        for (std::size_t index { 0 }; index < frame.pointers.size(); ++index)
        {
            const motion_pointer& pointer { frame.pointers[index] };
            const bool indexed { index == action_index };
            switch (action)
            {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                // 새로 닿은 것은 번호가 가리키는 하나뿐이다. 나머지는 그대로 닿아 있다.
                accept(sample_of(frame, pointer, indexed ? pointer_phase::down : pointer_phase::update, true), events);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                accept(sample_of(frame, pointer, pointer_phase::update, true), events);
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
                accept(sample_of(frame, pointer, indexed ? pointer_phase::up : pointer_phase::update, indexed == false), events);
                break;
            case AMOTION_EVENT_ACTION_CANCEL: {
                // 시스템이 몸짓을 가져갔다 (뒤로 가기 제스처 등). 모든 접촉이 정상적인 뗌 없이 끝난다.
                pointer_sample sample { sample_of(frame, pointer, pointer_phase::up, false) };
                sample.canceled = true;
                accept(sample, events);
                break;
            }
            case AMOTION_EVENT_ACTION_HOVER_ENTER:
            case AMOTION_EVENT_ACTION_HOVER_MOVE:
                accept(sample_of(frame, pointer, pointer_phase::update, false), events);
                break;
            case AMOTION_EVENT_ACTION_HOVER_EXIT:
                accept(sample_of(frame, pointer, pointer_phase::leave, false), events);
                break;
            default:
                // 펜의 버튼 동작은 이어지는 이동의 버튼 상태로 다룬다 (추적기가 접촉 중 전환을 안다).
                break;
            }
        }
        return events;
    }

    void input_translator::accept(const pointer_sample& sample, std::vector<raw_input_event>& events)
    {
        const bool was_in_contact { tracker_.in_contact(sample.pointer_id) };
        for (raw_input_event& event : tracker_.accept(sample))
            events.push_back(std::move(event));
        const bool now_in_contact { tracker_.in_contact(sample.pointer_id) };
        if (was_in_contact == false && now_in_contact)
            contacts_.push_back(sample.pointer_id);
        else if (was_in_contact && now_in_contact == false)
            std::erase(contacts_, sample.pointer_id);
    }

    void input_translator::translate_mouse(const motion_input& input, const surface_mapping& mapping, std::vector<raw_input_event>& events)
    {
        const motion_pointer& pointer { input.current.pointers.front() };
        const float x { pointer.x - mapping.origin_x };
        const float y { pointer.y - mapping.origin_y };
        const std::chrono::steady_clock::time_point time { event_time(input.current.time_ns) };
        const bool shift { has_meta(input.meta_state, AMETA_SHIFT_ON) };
        const bool control { has_meta(input.meta_state, AMETA_CTRL_ON) };
        const auto moved = [&] {
            pointer_moved_event event {};
            event.x = x;
            event.y = y;
            event.time = time;
            event.device = pointer_device::mouse;
            return raw_input_event { event };
        };

        switch (input.action & AMOTION_EVENT_ACTION_MASK)
        {
        case AMOTION_EVENT_ACTION_HOVER_ENTER:
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
        case AMOTION_EVENT_ACTION_MOVE:
            mouse_inside_ = true;
            events.push_back(moved());
            return;
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
            // 버튼을 누를 때도 hover가 끝났다고 온다. 창을 떠난 것만 이탈이다.
            if (input.button_state != 0)
                return;
            mouse_inside_ = false;
            events.push_back(raw_input_event { pointer_left_event { {}, pointer_device::mouse } });
            return;
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_BUTTON_PRESS:
        case AMOTION_EVENT_ACTION_BUTTON_RELEASE: {
            // 실제 마우스는 DOWN 뒤에 버튼 동작(BUTTON_PRESS)을 함께 보내지만, 주입된 마우스
            // (`input mouse tap`)는 DOWN·UP만 보낸다. 먼저 온 쪽이 누름을 만들고 겹치는 것은 버린다.
            const std::int32_t action { input.action & AMOTION_EVENT_ACTION_MASK };
            const bool press { action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_BUTTON_PRESS };
            std::int32_t changed { input.action_button };
            if (action == AMOTION_EVENT_ACTION_DOWN)
                // 버튼 상태가 비어 있으면(주입) 왼쪽이다.
                changed = (input.button_state & AMOTION_EVENT_BUTTON_SECONDARY) != 0 && (input.button_state & AMOTION_EVENT_BUTTON_PRIMARY) == 0 ? AMOTION_EVENT_BUTTON_SECONDARY
                                                                                                                                                 : AMOTION_EVENT_BUTTON_PRIMARY;
            else if (action == AMOTION_EVENT_ACTION_UP)
                // 마지막 버튼을 뗐다. 아직 눌린 것으로 아는 버튼이 그것이다.
                changed = mouse_left_ ? AMOTION_EVENT_BUTTON_PRIMARY : AMOTION_EVENT_BUTTON_SECONDARY;
            pointer_button button { pointer_button::none };
            bool* held { nullptr };
            if (changed == AMOTION_EVENT_BUTTON_PRIMARY)
            {
                button = pointer_button::left;
                held = &mouse_left_;
            }
            else if (changed == AMOTION_EVENT_BUTTON_SECONDARY)
            {
                button = pointer_button::right;
                held = &mouse_right_;
            }
            // 가운데·옆 버튼은 luil에 뜻이 없다. 뗌만 오거나 누름이 겹치는 것은 버린다.
            if (held == nullptr || *held == press)
                return;
            *held = press;
            if (press)
            {
                pointer_pressed_event event {};
                event.x = x;
                event.y = y;
                event.button = button;
                event.time = time;
                event.shift = shift;
                event.device = pointer_device::mouse;
                event.scale = mapping.scale;
                events.push_back(raw_input_event { std::move(event) });
                return;
            }
            pointer_released_event event {};
            event.x = x;
            event.y = y;
            event.button = button;
            event.time = time;
            event.device = pointer_device::mouse;
            events.push_back(raw_input_event { std::move(event) });
            return;
        }
        case AMOTION_EVENT_ACTION_SCROLL: {
            // 휠 값은 한 눈금이 1이고 위·오른쪽이 양수다 — Win32 휠과 같은 방향이다.
            const auto wheel = [&](const float value, const bool horizontal) {
                mouse_wheel_event event {};
                event.x = x;
                event.y = y;
                event.delta = value * wheel_detent;
                event.time = time;
                event.control = control;
                event.shift = shift;
                event.horizontal = horizontal;
                return raw_input_event { std::move(event) };
            };
            if (input.vertical_scroll != 0.0f)
                events.push_back(wheel(input.vertical_scroll, false));
            if (input.horizontal_scroll != 0.0f)
                events.push_back(wheel(input.horizontal_scroll, true));
            return;
        }
        default:
            return;
        }
    }

    std::vector<raw_input_event> input_translator::translate(const key_input& input) const
    {
        std::vector<raw_input_event> events {};
        // 누름만 본다. 자동 반복은 반복 횟수로 온다.
        if (input.action != AKEY_EVENT_ACTION_DOWN)
            return events;
        const bool control { has_meta(input.meta_state, AMETA_CTRL_ON) };
        const bool shift { has_meta(input.meta_state, AMETA_SHIFT_ON) };
        const bool alt { has_meta(input.meta_state, AMETA_ALT_ON) };
        const bool meta { has_meta(input.meta_state, AMETA_META_ON) };
        const bool shortcut { control || alt || meta };
        const std::chrono::steady_clock::time_point time { event_time(input.time_ns) };
        // Backspace는 글자다 (Win32의 WM_CHAR U+0008). 수정자가 있을 때만 키로도 간다.
        const bool backspace { input.key_code == AKEYCODE_DEL };
        const bool character_key { backspace || input.unicode_char != 0 };

        // 편집 전용 키(복사·붙여넣기·잘라내기)는 Ctrl 단축키로 옮긴다. 이런 키가 있는 키보드에서
        // 텍스트 칸이 같은 명령을 받는다.
        const auto edit_key = [](const std::int32_t code) {
            switch (code)
            {
            case AKEYCODE_COPY:
                return key_code::key_c;
            case AKEYCODE_PASTE:
                return key_code::key_v;
            case AKEYCODE_CUT:
                return key_code::key_x;
            default:
                return key_code::none;
            }
        };
        if (const key_code edit { edit_key(input.key_code) }; edit != key_code::none)
        {
            key_pressed_event event { edit, true, shift, false, input.repeat_count > 0, time };
            event.primary_shortcut = true;
            event.word_navigation = control;
            events.push_back(raw_input_event { std::move(event) });
            return events;
        }

        key_code key { named_key(input.key_code) };
        if (key == key_code::none)
            key = modified_key(input.key_code, shortcut, character_key);
        if (key != key_code::none)
        {
            key_pressed_event event { key, control, shift, alt, input.repeat_count > 0, time };
            // 편집 역할은 Windows와 같이 Ctrl이 맡는다. Meta(검색·Windows 키)는 따로 둔다.
            event.primary_shortcut = control;
            event.word_navigation = control;
            event.meta = meta;
            events.push_back(raw_input_event { std::move(event) });
        }

        // 글자는 Alt·Meta 없이 친 것만이다. Ctrl+Backspace는 Win32처럼 U+007F다.
        if (alt || meta)
            return events;
        if (backspace)
            events.push_back(raw_input_event { character_typed_event { control ? char32_t { 0x7F } : U'\b', time } });
        else if (input.unicode_char > 0 && control == false)
            events.push_back(raw_input_event { character_typed_event { static_cast<char32_t>(input.unicode_char), time } });
        return events;
    }

    std::vector<raw_input_event> input_translator::cancel_all(const std::chrono::steady_clock::time_point time)
    {
        std::vector<raw_input_event> events {};
        for (const std::uint32_t id : contacts_)
            for (raw_input_event& event : tracker_.cancel(id, time, {}))
                events.push_back(std::move(event));
        contacts_.clear();
        // 눌려 있던 마우스 버튼은 화면 밖 합성 뗌으로 거둔다 (마우스의 기존 경로다).
        for (auto [held, button] : { std::pair { &mouse_left_, pointer_button::left }, std::pair { &mouse_right_, pointer_button::right } })
            if (*held)
            {
                *held = false;
                pointer_released_event event {};
                event.x = -10000.0f;
                event.y = -10000.0f;
                event.button = button;
                event.time = time;
                event.device = pointer_device::mouse;
                events.push_back(raw_input_event { std::move(event) });
            }
        if (mouse_inside_)
        {
            mouse_inside_ = false;
            events.push_back(raw_input_event { pointer_left_event { {}, pointer_device::mouse } });
        }
        return events;
    }
} // namespace luil::android
