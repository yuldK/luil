#include "host/pointer_sequence.h"

#include <utility>

namespace luil {
    namespace {
        [[nodiscard]] raw_input_event pressed_event(const pointer_sample& sample, const pointer_button button)
        {
            pointer_pressed_event pressed {};
            pressed.x = sample.x;
            pressed.y = sample.y;
            pressed.button = button;
            pressed.time = sample.time;
            pressed.shift = sample.shift;
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
        switch (sample.phase)
        {
        case pointer_phase::down:
            // 같은 접촉의 두 번째 DOWN은 없는 일이다. 있으면 앞의 것을 지킨다.
            if (current != nullptr || sample.in_contact == false)
                return {};
            contacts_.push_back(contact { sample.pointer_id, sample.device, button, sample.eraser });
            // 지우개 끝의 접촉은 삼킨다 — 삭제나 우클릭으로 임의로 바꾸지 않는다.
            if (sample.eraser == false)
                events.push_back(pressed_event(sample, button));
            return events;
        case pointer_phase::update: {
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
        case pointer_phase::up: {
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
        case pointer_phase::leave:
            // 펜이 범위를 벗어났거나 창을 떠났다. hover를 거둔다.
            //  - 접촉 중이면 이탈이 아니다 — 캡처가 접촉을 지키고 뗌은 UP이 낸다.
            //    경계 통과를 접촉 해제로 오인하지 않는다. 터치도 창 밖으로 나갔다고
            //    뗌을 합성하지 않는다.
            if (current != nullptr || sample.device != pointer_device::pen)
                return {};
            events.push_back(raw_input_event { pointer_left_event { sample.surface, sample.device } });
            return events;
        }
        return {};
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
} // namespace luil
