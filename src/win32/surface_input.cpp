#include "win32/surface_input.h"

#include <windowsx.h>

#include <utility>

namespace luil::win32 {
    std::optional<raw_input_event> translate_pointer_message(const pointer_message& message)
    {
        switch (message.message)
        {
        case WM_MOUSEMOVE: {
            pointer_moved_event moved {};
            moved.x = message.x;
            moved.y = message.y;
            moved.time = message.time;
            moved.surface = message.surface;
            return raw_input_event { std::move(moved) };
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: {
            pointer_pressed_event pressed {};
            pressed.x = message.x;
            pressed.y = message.y;
            pressed.button = pointer_button_of(message.message);
            pressed.time = message.time;
            // Shift+클릭은 텍스트 박스에서 선택을 그 자리까지 늘린다.
            pressed.shift = (message.word_parameter & MK_SHIFT) != 0;
            pressed.surface = message.surface;
            return raw_input_event { std::move(pressed) };
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            pointer_released_event released {};
            released.x = message.x;
            released.y = message.y;
            released.button = pointer_button_of(message.message);
            released.time = message.time;
            released.surface = message.surface;
            return raw_input_event { std::move(released) };
        }
        case WM_MOUSEWHEEL: {
            mouse_wheel_event wheel {};
            wheel.x = message.x;
            wheel.y = message.y;
            wheel.delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(message.word_parameter));
            wheel.time = message.time;
            wheel.surface = message.surface;
            return raw_input_event { std::move(wheel) };
        }
        default:
            return std::nullopt;
        }
    }

    namespace {
        [[nodiscard]] raw_input_event pressed_event(const pointer_sample& sample, const pointer_button button)
        {
            pointer_pressed_event pressed {};
            pressed.x = sample.x;
            pressed.y = sample.y;
            pressed.button = button;
            pressed.time = sample.time;
            pressed.surface = sample.surface;
            pressed.device = sample.device;
            pressed.pointer_id = sample.pointer_id;
            pressed.scale = sample.scale;
            return raw_input_event { std::move(pressed) };
        }

        [[nodiscard]] raw_input_event cancelled_event(const pointer_device device, const std::uint32_t pointer_id, const std::chrono::steady_clock::time_point time, const std::u8string& surface)
        {
            return raw_input_event { pointer_cancelled_event { device, pointer_id, surface, time } };
        }
    } // namespace

    std::vector<raw_input_event> pointer_sequence_tracker::accept(const pointer_sample& sample)
    {
        contact* const current { find(sample.pointer_id) };
        // 펜은 배럴 버튼이 우클릭이다. 터치에는 버튼이 하나뿐이다.
        const pointer_button button { sample.device == pointer_device::pen && sample.barrel ? pointer_button::right : pointer_button::left };
        std::vector<raw_input_event> events {};
        switch (sample.message)
        {
        case WM_POINTERDOWN:
            // 같은 접촉의 두 번째 DOWN은 없는 일이다. 있으면 앞의 것을 지킨다.
            if (current != nullptr || sample.in_contact == false)
                return {};
            contacts_.push_back(contact { sample.pointer_id, sample.device, button, sample.eraser });
            // 지우개 끝의 접촉은 삼킨다 — 삭제나 우클릭으로 임의로 바꾸지 않는다.
            if (sample.eraser == false)
                events.push_back(pressed_event(sample, button));
            return events;
        case WM_POINTERUPDATE: {
            if (current == nullptr)
            {
                // 비접촉 이동은 펜의 hover다. 터치에는 hover가 없다.
                //  - 우리 위에서 시작하지 않은 접촉의 이동도 여기로 온다. 삼킨다.
                if (sample.device != pointer_device::pen || sample.in_contact)
                    return {};
                pointer_moved_event moved {};
                moved.x = sample.x;
                moved.y = sample.y;
                moved.time = sample.time;
                moved.surface = sample.surface;
                moved.device = sample.device;
                moved.pointer_id = sample.pointer_id;
                events.push_back(raw_input_event { std::move(moved) });
                return events;
            }
            const contact held { *current };
            if (sample.canceled || sample.in_contact == false)
            {
                forget(sample.pointer_id);
                if (held.eraser == false)
                    events.push_back(cancelled_event(held.device, held.pointer_id, sample.time, sample.surface));
                return events;
            }
            if (held.eraser)
                return {};
            // 접촉 중 버튼이 바뀌었다. 이전 버튼의 누름을 취소한 뒤 새 버튼으로 누른다.
            if (button != held.button)
            {
                current->button = button;
                events.push_back(cancelled_event(held.device, held.pointer_id, sample.time, sample.surface));
                events.push_back(pressed_event(sample, button));
            }
            pointer_moved_event moved {};
            moved.x = sample.x;
            moved.y = sample.y;
            moved.time = sample.time;
            moved.surface = sample.surface;
            moved.device = sample.device;
            moved.pointer_id = sample.pointer_id;
            moved.in_contact = true;
            events.push_back(raw_input_event { std::move(moved) });
            return events;
        }
        case WM_POINTERUP: {
            if (current == nullptr)
                return {};
            const contact held { *current };
            forget(sample.pointer_id);
            if (held.eraser)
                return {};
            if (sample.canceled)
            {
                events.push_back(cancelled_event(held.device, held.pointer_id, sample.time, sample.surface));
                return events;
            }
            pointer_released_event released {};
            released.x = sample.x;
            released.y = sample.y;
            released.button = held.button;
            released.time = sample.time;
            released.surface = sample.surface;
            released.device = held.device;
            released.pointer_id = held.pointer_id;
            events.push_back(raw_input_event { std::move(released) });
            return events;
        }
        case WM_POINTERLEAVE:
            // 펜이 범위를 벗어났거나 창을 떠났다. hover를 거둔다.
            //  - 접촉 중이면 이탈이 아니다 — 캡처가 접촉을 지키고 뗌은 UP이 낸다.
            //    경계 통과를 접촉 해제로 오인하지 않는다. 터치도 창 밖으로 나갔다고
            //    뗌을 합성하지 않는다.
            if (current != nullptr || sample.device != pointer_device::pen)
                return {};
            events.push_back(raw_input_event { pointer_left_event { sample.surface, sample.device } });
            return events;
        default:
            return {};
        }
    }

    std::vector<raw_input_event> pointer_sequence_tracker::cancel(const std::uint32_t pointer_id, const std::chrono::steady_clock::time_point time, const std::u8string& surface)
    {
        const contact* const current { find(pointer_id) };
        if (current == nullptr)
            return {};
        const contact held { *current };
        forget(pointer_id);
        if (held.eraser)
            return {};
        return { cancelled_event(held.device, held.pointer_id, time, surface) };
    }

    bool pointer_sequence_tracker::in_contact(const std::uint32_t pointer_id) const noexcept
    {
        for (const contact& value : contacts_)
            if (value.pointer_id == pointer_id)
                return true;
        return false;
    }

    pointer_sequence_tracker::contact* pointer_sequence_tracker::find(const std::uint32_t pointer_id) noexcept
    {
        for (contact& value : contacts_)
            if (value.pointer_id == pointer_id)
                return &value;
        return nullptr;
    }

    void pointer_sequence_tracker::forget(const std::uint32_t pointer_id) noexcept
    {
        std::erase_if(contacts_, [pointer_id](const contact& value) { return value.pointer_id == pointer_id; });
    }

    std::chrono::steady_clock::time_point pointer_counter_time(
        const std::uint64_t message_count, const std::uint64_t now_count, const std::uint64_t frequency, const std::chrono::steady_clock::time_point now) noexcept
    {
        if (message_count == 0u || frequency == 0u || message_count > now_count)
            return now;
        // 곱하기 전에 초와 나머지로 나눠 64비트를 넘지 않게 한다.
        const std::uint64_t age { now_count - message_count };
        const std::uint64_t seconds { age / frequency };
        const std::uint64_t remainder { age % frequency };
        const std::chrono::nanoseconds elapsed { std::chrono::seconds { seconds } + std::chrono::nanoseconds { remainder * 1'000'000'000ull / frequency } };
        return now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(elapsed);
    }

    std::chrono::steady_clock::time_point pointer_tick_time(const std::uint32_t message_tick, const std::uint32_t now_tick, const std::chrono::steady_clock::time_point now) noexcept
    {
        // 부호 없는 뺄셈은 tick이 한 바퀴 돈 경계에서도 나이를 옳게 낸다.
        // 반 바퀴를 넘는 나이는 지금보다 뒤의 값(시계 어긋남)으로 본다.
        const std::uint32_t age { now_tick - message_tick };
        if (age > 0x7FFFFFFFu)
            return now;
        return now - std::chrono::milliseconds { age };
    }

    pointer_button pointer_button_of(const UINT message) noexcept
    {
        switch (message)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
            return pointer_button::left;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_RBUTTONUP:
            return pointer_button::right;
        default:
            return pointer_button::none;
        }
    }

    key_code named_key_from_virtual(const WPARAM virtual_key) noexcept
    {
        switch (virtual_key)
        {
        case VK_UP:
            return key_code::arrow_up;
        case VK_DOWN:
            return key_code::arrow_down;
        case VK_RETURN:
            return key_code::enter;
        case VK_ESCAPE:
            return key_code::escape;
        case VK_LEFT:
            return key_code::arrow_left;
        case VK_RIGHT:
            return key_code::arrow_right;
        case VK_HOME:
            return key_code::home;
        case VK_END:
            return key_code::end;
        case VK_DELETE:
            return key_code::delete_forward;
        case VK_TAB:
            return key_code::tab;
        case VK_PRIOR:
            return key_code::page_up;
        case VK_NEXT:
            return key_code::page_down;
        default:
            break;
        }
        if (virtual_key >= VK_F1 && virtual_key <= VK_F12)
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
            return function_keys[virtual_key - VK_F1];
        }
        return key_code::none;
    }

    key_code modified_key_from_virtual(const WPARAM virtual_key, const bool control, const bool alt, const bool character_key) noexcept
    {
        // Insert는 예전 클립보드 배치라 Ctrl이나 Shift와 함께일 때만 뜻이 있다.
        if (virtual_key == VK_INSERT)
            return key_code::insert;
        // Space는 글자이면서 **누름**이다. 초점을 가진 컨트롤을 실행하는 키라
        // 글자 키 관문 앞에서 꺼낸다 (keyboard-focus-design.md).
        //  - 텍스트 박스에서는 같은 Space가 키와 문자로 두 번 오고,
        //    controller가 키 쪽을 흘려보내 한 번만 먹는다.
        if (virtual_key == VK_SPACE)
            return key_code::space;
        if (character_key && control == false && alt == false)
            return key_code::none;
        // 수정자 키 자체는 이벤트가 아니다. 영문자·숫자는 공통 이름 키로,
        // 그 밖의 키는 플랫폼 대역으로 옮긴다.
        if (virtual_key == VK_SHIFT || virtual_key == VK_CONTROL || virtual_key == VK_MENU || virtual_key == VK_LWIN || virtual_key == VK_RWIN)
            return key_code::none;
        return platform_key_code(static_cast<std::uint32_t>(virtual_key));
    }
} // namespace luil::win32
