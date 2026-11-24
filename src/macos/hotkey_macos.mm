#include "macos/hotkey_macos.h"

#include "macos/macos_input.h"

#include <Carbon/Carbon.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luil::macos {
    namespace {
        // 객체마다 다른 `EventHotKeyID::signature`의 앞 두 글자다 ('lu'). 뒤 16비트가 객체 차례다.
        constexpr std::uint32_t signature_base { (std::uint32_t { 'l' } << 24) | (std::uint32_t { 'u' } << 16) };

        [[nodiscard]] win32::hotkey_status status_of(const hotkey_state state) noexcept
        {
            switch (state)
            {
            case hotkey_state::conflict:
                return win32::hotkey_status::conflict;
            case hotkey_state::reserved:
                return win32::hotkey_status::reserved;
            case hotkey_state::needs_modifier:
                return win32::hotkey_status::needs_modifier;
            case hotkey_state::invalid_key:
                return win32::hotkey_status::invalid_key;
            case hotkey_state::invalid_id:
                return win32::hotkey_status::invalid_id;
            case hotkey_state::failed:
                return win32::hotkey_status::failed;
            case hotkey_state::registered:
                break;
            }
            return win32::hotkey_status::registered;
        }

        // 영문 진단이다. 끝에 OSStatus를 단다.
        [[nodiscard]] std::u8string os_error(const std::string_view what, const OSStatus status)
        {
            const std::string text { std::string { what } + " (OSStatus " + std::to_string(status) + ")." };
            return std::u8string { text.begin(), text.end() };
        }

        // Carbon 가상 키가 없는 키다. 키 없음(`none`)은 core가 이미 `invalid_key`로 거른다.
        [[nodiscard]] bool unmapped(const global_hotkey& hotkey) noexcept
        {
            return hotkey.chord.key != key_code::none && carbon_key_of(hotkey.chord.key).has_value() == false;
        }

        OSStatus hotkey_pressed(EventHandlerCallRef, EventRef event, void* const user_data)
        {
            EventHotKeyID hotkey {};
            const OSStatus status { GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotkey), nullptr, &hotkey) };
            if (status != noErr || user_data == nullptr)
                return eventNotHandledErr;
            if (static_cast<hotkey_host*>(user_data)->handle_pressed(hotkey.signature, hotkey.id) == false)
                return eventNotHandledErr;
            return noErr;
        }
    }

    hotkey_host::hotkey_host()
    {
        // 메인 thread만 만든다.
        static std::uint32_t next { 0 };
        signature_ = signature_base | (next++ & 0xFFFF);
    }

    hotkey_host::~hotkey_host()
    {
        release_all();
        // 장부가 모르는 등록은 없어야 하지만 남은 것이 있으면 푼다. 눌림이 사라진 객체로 오지 않게 한다.
        for (const auto& [number, reference] : registered_)
            static_cast<void>(UnregisterEventHotKey(reference));
        registered_.clear();
        if (handler_ != nullptr)
            static_cast<void>(RemoveEventHandler(handler_));
    }

    void hotkey_host::set_pressed(std::function<void(const std::u8string& id)> pressed)
    {
        pressed_ = std::move(pressed);
    }

    void hotkey_host::set_report(std::function<void(const win32::hotkey_report& report)> report)
    {
        report_ = std::move(report);
    }

    void hotkey_host::update(const std::span<const global_hotkey> wanted, const bool key_capture)
    {
        // Carbon 자리가 없는 키는 키를 지워 core가 `invalid_key`로 거르게 한다. 그런 키가 없으면 목록을 그대로 넘긴다.
        //  - 지운 사본도 frame마다 같은 값이라 core의 대조가 같음을 알아본다. `pressed`는 싣지 않는다 (core가 보지 않는다).
        std::vector<global_hotkey> mapped {};
        std::span<const global_hotkey> list { wanted };
        if (std::any_of(wanted.begin(), wanted.end(), unmapped))
        {
            mapped.reserve(wanted.size());
            for (const global_hotkey& hotkey : wanted)
            {
                global_hotkey copy { .id = hotkey.id, .chord = hotkey.chord };
                if (unmapped(hotkey))
                    copy.chord.key = key_code::none;
                mapped.push_back(std::move(copy));
            }
            list = mapped;
        }
        const hotkey_plan plan { sync_.update(list, key_capture) };
        if (report_ != nullptr)
        {
            for (const hotkey_decision& decision : plan.decisions)
                report_(win32::hotkey_report { .id = decision.id, .status = status_of(decision.state), .error = decision.error });
        }
        for (const hotkey_call& call : plan.calls)
            execute(call);
    }

    void hotkey_host::release_all()
    {
        for (const hotkey_call& call : sync_.release_all())
            execute(call);
    }

    bool hotkey_host::handle_pressed(const std::uint32_t signature, const std::uint32_t number)
    {
        if (signature != signature_)
            return false;
        // 장부에 없는 번호는 풀기 직전에 들어온 눌림이다. 이 객체의 것이니 받고 버린다.
        const std::optional<std::u8string> id { sync_.id_of(number) };
        if (id.has_value() == false || pressed_ == nullptr)
            return true;
        try
        {
            pressed_(*id);
        }
        catch (...)
        {
            // 예외가 Carbon의 C 틀을 건너지 않게 막는다.
        }
        return true;
    }

    void hotkey_host::execute(const hotkey_call& call)
    {
        if (call.kind == hotkey_call_kind::remove)
        {
            // 풀기가 실패해도 할 일이 없다. 프로세스가 끝나면 OS도 거둔다.
            if (const auto found { registered_.find(call.number) }; found != registered_.end())
            {
                static_cast<void>(UnregisterEventHotKey(found->second));
                registered_.erase(found);
            }
            return;
        }

        hotkey_call_outcome outcome { hotkey_call_outcome::succeeded };
        std::u8string error {};
        // 처리기는 처음 걸 때 단다. 걸지 않는 앱은 Carbon을 부르지 않는다.
        if (handler_ == nullptr)
        {
            const EventTypeSpec type { kEventClassKeyboard, kEventHotKeyPressed };
            if (const OSStatus status { InstallEventHandler(GetApplicationEventTarget(), &hotkey_pressed, 1, &type, this, &handler_) }; status != noErr)
            {
                handler_ = nullptr;
                outcome = hotkey_call_outcome::failed;
                error = os_error("Failed to install the global hotkey event handler", status);
            }
        }
        if (outcome == hotkey_call_outcome::succeeded)
        {
            // `update`가 자리 없는 키를 걸렀으므로 늘 있다.
            const std::uint16_t key { carbon_key_of(call.chord.key).value_or(0) };
            const EventHotKeyID identity { .signature = signature_, .id = static_cast<UInt32>(call.number) };
            EventHotKeyRef reference { nullptr };
            // 배타로 건다. 다른 프로세스가 배타로 쥔 조합은 `eventHotKeyExistsErr`로 돌아와 충돌을 알린다.
            const OSStatus status { RegisterEventHotKey(key, carbon_hotkey_modifiers_of(call.chord), identity, GetApplicationEventTarget(), kEventHotKeyExclusive, &reference) };
            if (status == noErr && reference != nullptr)
                registered_[call.number] = reference;
            else if (status == eventHotKeyExistsErr)
                outcome = hotkey_call_outcome::conflict;
            else
            {
                outcome = hotkey_call_outcome::failed;
                error = os_error("Failed to register the global hotkey", status);
            }
        }
        if (std::optional<hotkey_decision> decision { sync_.completed(call, outcome, std::move(error)) }; decision.has_value() && report_ != nullptr)
            report_(win32::hotkey_report { .id = decision->id, .status = status_of(decision->state), .error = decision->error });
    }
}
