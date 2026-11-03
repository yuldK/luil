#include "host/hotkey_sync.h"

#include "host/key_mapping.h"

#include <algorithm>
#include <tuple>

namespace luil {
    namespace {
        // OS 앞에서 같은 조합을 가르는 값이다. 키 이름이 달라도 가상 키가 같으면 같은 조합이다.
        using hotkey_slot = std::tuple<bool, bool, bool, std::uint32_t>;

        // `system_reserved_chord`와 같은 표를 가상 키로 적은 것이다. 플랫폼 대역의 값으로 쓴 같은 키를 놓치지 않는다.
        //  - F12는 디버거 몫이라 수정자와 상관없이 막는다 (`RegisterHotKey` 문서).
        [[nodiscard]] bool reserved_virtual_key(const key_chord& chord, const std::uint32_t virtual_key) noexcept
        {
            if (virtual_key == 0x7B) // VK_F12
                return true;
            if (chord.alt && chord.control == false && chord.shift == false)
                return virtual_key == 0x73 || virtual_key == 0x09 || virtual_key == 0x1B || virtual_key == 0x20; // F4, Tab, Esc, Space
            if (chord.alt && chord.control)
                return virtual_key == 0x2E; // Delete
            return chord.control && chord.alt == false && chord.shift == false && virtual_key == 0x1B;
        }

        // 수정자 없이 걸어도 시스템 입력을 해치지 않는 키다. 글자를 만들거나 이동·편집에 쓰는 키는 여기 없다.
        //  - 목록 밖의 플랫폼 대역 키는 `OEM_102`처럼 글자를 만드는 것이 있어 Ctrl이나 Alt가 필요하다.
        [[nodiscard]] bool needs_no_modifier(const std::uint32_t virtual_key) noexcept
        {
            return virtual_key == 0x13 // VK_PAUSE
                || virtual_key == 0x2C // VK_SNAPSHOT
                || (virtual_key >= 0x70 && virtual_key <= 0x7A) // F1~F11
                || (virtual_key >= 0x7C && virtual_key <= 0x87) // F13~F24
                || virtual_key == 0x91 // VK_SCROLL
                || (virtual_key >= 0xA6 && virtual_key <= 0xB7) // 브라우저·미디어·실행 키
            ;
        }

        // 한 번의 대조가 보는 목록 항목이다.
        struct candidate
        {
            std::u8string id {};
            std::size_t occurrence { 0 };
            key_chord chord {};
            std::uint32_t virtual_key { 0 };
            // OS를 부르지 않고 정해지는 결과다. 비면 걸어 볼 항목이다.
            std::optional<hotkey_state> rejection {};
        };
    }

    std::optional<hotkey_state> classify_hotkey_chord(const key_chord& chord) noexcept
    {
        const std::uint32_t virtual_key { win32_virtual_from_key(chord.key) };
        if (virtual_key == 0)
            return hotkey_state::invalid_key;
        if (system_reserved_chord(chord) || reserved_virtual_key(chord, virtual_key))
            return hotkey_state::reserved;
        if (chord.control == false && chord.alt == false && needs_no_modifier(virtual_key) == false)
            return hotkey_state::needs_modifier;
        return std::nullopt;
    }

    hotkey_sync::hotkey_sync(const int last_number) noexcept
        : last_number_ { std::clamp(last_number, 1, max_number) }
    {
    }

    bool hotkey_sync::unchanged(const std::span<const global_hotkey> wanted, const bool key_capture) const noexcept
    {
        if (retry_ || key_capture != last_capture_ || wanted.size() != last_wanted_.size())
            return false;
        for (std::size_t index = 0; index < wanted.size(); ++index)
        {
            if (wanted[index].id != last_wanted_[index].first || wanted[index].chord != last_wanted_[index].second)
                return false;
        }
        return true;
    }

    bool hotkey_sync::wants_attempt(const record& item) const noexcept
    {
        // 이 조합으로 걸어 본 적이 없다 (처음이거나 키 잡기 동안 미뤘다).
        if (item.reported == false)
            return true;
        // 앞서 라이브러리가 걸러 정한 결과였다 (같은 목록의 앞 항목이 쥐었다가 놓았다).
        if (item.from_os == false)
            return true;
        // 키 잡기로 풀린 것을 되건다.
        if (item.state == hotkey_state::registered)
            return true;
        // 걸지 못한 항목은 활성화 뒤에만 다시 건다.
        return retry_ && (item.state == hotkey_state::conflict || item.state == hotkey_state::failed);
    }

    bool hotkey_sync::number_in_use(const int number, const std::vector<record>& pending) const noexcept
    {
        const auto holds = [number](const record& item) { return item.number == number; };
        return std::any_of(records_.begin(), records_.end(), holds) || std::any_of(pending.begin(), pending.end(), holds);
    }

    int hotkey_sync::allocate(const std::vector<record>& pending)
    {
        for (int tried = 0; tried < last_number_; ++tried)
        {
            const int number { next_number_ };
            next_number_ = number >= last_number_ ? 1 : number + 1;
            if (number_in_use(number, pending) == false)
                return number;
        }
        return 0;
    }

    hotkey_plan hotkey_sync::update(const std::span<const global_hotkey> wanted, const bool key_capture)
    {
        hotkey_plan plan {};
        if (unchanged(wanted, key_capture))
            return plan;
        last_wanted_.clear();
        for (const global_hotkey& hotkey : wanted)
            last_wanted_.emplace_back(hotkey.id, hotkey.chord);
        last_capture_ = key_capture;

        // 목록을 걸어 볼 항목과 걸러낼 항목으로 가른다. 같은 조합은 앞 항목만 걸어 본다.
        std::vector<candidate> candidates {};
        candidates.reserve(wanted.size());
        std::vector<hotkey_slot> held {};
        for (const global_hotkey& hotkey : wanted)
        {
            candidate item { .id = hotkey.id, .chord = hotkey.chord };
            for (const candidate& earlier : candidates)
            {
                if (earlier.id == item.id)
                    ++item.occurrence;
            }
            if (item.id.empty() || item.occurrence > 0)
                item.rejection = hotkey_state::invalid_id;
            else if (const std::optional<hotkey_state> rejected { classify_hotkey_chord(item.chord) }; rejected.has_value())
                item.rejection = rejected;
            else
            {
                item.virtual_key = win32_virtual_from_key(item.chord.key);
                const hotkey_slot slot { item.chord.control, item.chord.shift, item.chord.alt, item.virtual_key };
                if (std::find(held.begin(), held.end(), slot) != held.end())
                    item.rejection = hotkey_state::conflict;
                else
                    held.push_back(slot);
            }
            candidates.push_back(std::move(item));
        }

        const auto find_candidate = [&candidates](const record& item) -> const candidate* {
            for (const candidate& entry : candidates)
            {
                if (entry.id == item.id && entry.occurrence == item.occurrence)
                    return &entry;
            }
            return nullptr;
        };

        // 풀기가 먼저다. 빠졌거나 조합이 바뀌었거나 걸지 않게 됐거나 키 잡기 중이면 푼다.
        for (record& item : records_)
        {
            if (item.number == 0)
                continue;
            const candidate* const match { find_candidate(item) };
            const bool keep { key_capture == false && match != nullptr && match->rejection.has_value() == false && match->chord == item.chord };
            if (keep)
                continue;
            plan.calls.push_back(hotkey_call { .kind = hotkey_call_kind::remove, .number = item.number, .id = item.id });
            item.number = 0;
        }

        // 그 다음 목록 차례로 새 장부를 짓고 걸 것을 정한다.
        std::vector<record> next {};
        next.reserve(candidates.size());
        for (const candidate& entry : candidates)
        {
            record current { .id = entry.id, .occurrence = entry.occurrence, .chord = entry.chord };
            for (const record& previous : records_)
            {
                // 조합이 바뀐 항목은 새 항목이다. 처음 시도로 알린다.
                if (previous.id == entry.id && previous.occurrence == entry.occurrence && previous.chord == entry.chord)
                {
                    current = previous;
                    break;
                }
            }

            if (entry.rejection.has_value())
            {
                current.from_os = false;
                if (current.reported == false || current.state != *entry.rejection)
                    plan.decisions.push_back(hotkey_decision { .id = entry.id, .state = *entry.rejection });
                current.state = *entry.rejection;
                current.reported = true;
            }
            else if (key_capture == false && current.number == 0 && wants_attempt(current))
            {
                current.number = allocate(next);
                if (current.number == 0)
                {
                    // 번호가 다 쓰였다. 살아 있는 항목이 49151개라야 하는 일이라 사실상 오지 않는다.
                    current.from_os = true;
                    if (current.reported == false || current.state != hotkey_state::failed)
                        plan.decisions.push_back(hotkey_decision { .id = entry.id, .state = hotkey_state::failed, .error = u8"No free hotkey number is left." });
                    current.state = hotkey_state::failed;
                    current.reported = true;
                }
                else
                {
                    plan.calls.push_back(hotkey_call { .kind = hotkey_call_kind::add, .number = current.number, .id = entry.id, .chord = entry.chord, .virtual_key = entry.virtual_key });
                }
            }
            next.push_back(std::move(current));
        }
        records_ = std::move(next);
        // 다시 걸기는 키 잡기가 끝난 대조에서 한 번만 한다.
        if (key_capture == false)
            retry_ = false;
        return plan;
    }

    std::optional<hotkey_decision> hotkey_sync::completed(const hotkey_call& call, const hotkey_call_outcome outcome, std::u8string error)
    {
        if (call.kind != hotkey_call_kind::add || call.number == 0)
            return std::nullopt;
        const auto found { std::find_if(records_.begin(), records_.end(), [&call](const record& item) { return item.number == call.number; }) };
        if (found == records_.end())
            return std::nullopt;
        hotkey_state state { hotkey_state::registered };
        if (outcome == hotkey_call_outcome::conflict)
            state = hotkey_state::conflict;
        else if (outcome == hotkey_call_outcome::failed)
            state = hotkey_state::failed;
        if (outcome != hotkey_call_outcome::succeeded)
            found->number = 0;
        const bool changed { found->reported == false || found->state != state };
        found->state = state;
        found->reported = true;
        found->from_os = true;
        if (changed == false)
            return std::nullopt;
        return hotkey_decision {
            .id = found->id
            , .state = state
            , .error = state == hotkey_state::failed ? std::move(error) : std::u8string {}
        };
    }

    void hotkey_sync::reactivated() noexcept
    {
        retry_ = true;
    }

    std::vector<hotkey_call> hotkey_sync::release_all()
    {
        std::vector<hotkey_call> calls {};
        for (const record& item : records_)
        {
            if (item.number != 0)
                calls.push_back(hotkey_call { .kind = hotkey_call_kind::remove, .number = item.number, .id = item.id });
        }
        records_.clear();
        last_wanted_.clear();
        last_capture_ = false;
        retry_ = false;
        return calls;
    }

    std::optional<std::u8string> hotkey_sync::id_of(const std::uintptr_t number) const
    {
        if (number == 0 || number > static_cast<std::uintptr_t>(max_number))
            return std::nullopt;
        for (const record& item : records_)
        {
            if (item.number == static_cast<int>(number))
                return item.id;
        }
        return std::nullopt;
    }
}
