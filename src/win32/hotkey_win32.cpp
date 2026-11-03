#include "win32/hotkey_win32.h"

#include "win32/win32_error.h"

#include <utility>

namespace luil::win32 {
    namespace {
        // `scoped_hotkey_api`가 세운 OS 호출이다. UI thread의 창 생성과 test만 만진다.
        [[nodiscard]] hotkey_api& hotkey_api_override()
        {
            static hotkey_api api {};
            return api;
        }

        [[nodiscard]] hotkey_report report_of(const hotkey_decision& decision)
        {
            return hotkey_report { .id = decision.id, .status = hotkey_status_of(decision.state), .error = decision.error };
        }
    }

    hotkey_api real_hotkey_api()
    {
        return hotkey_api {
            .add = [](const HWND window, const int number, const UINT modifiers, const UINT virtual_key) {
                SetLastError(ERROR_SUCCESS);
                const BOOL ok { RegisterHotKey(window, number, modifiers, virtual_key) };
                return hotkey_os_result { .ok = ok != FALSE, .error = ok != FALSE ? 0 : GetLastError() };
            }
            , .remove = [](const HWND window, const int number) { return UnregisterHotKey(window, number) != FALSE; }
        };
    }

    hotkey_api hotkey_api_for_new_windows()
    {
        if (const hotkey_api& api { hotkey_api_override() }; api.add != nullptr && api.remove != nullptr)
            return api;
        return real_hotkey_api();
    }

    scoped_hotkey_api::scoped_hotkey_api(hotkey_api api)
        : previous_ { std::exchange(hotkey_api_override(), std::move(api)) }
    {
    }

    scoped_hotkey_api::~scoped_hotkey_api()
    {
        hotkey_api_override() = std::move(previous_);
    }

    UINT hotkey_modifiers_of(const key_chord& chord) noexcept
    {
        UINT modifiers { MOD_NOREPEAT };
        if (chord.control)
            modifiers |= MOD_CONTROL;
        if (chord.shift)
            modifiers |= MOD_SHIFT;
        if (chord.alt)
            modifiers |= MOD_ALT;
        return modifiers;
    }

    hotkey_status hotkey_status_of(const hotkey_state state) noexcept
    {
        switch (state)
        {
        case hotkey_state::conflict:
            return hotkey_status::conflict;
        case hotkey_state::reserved:
            return hotkey_status::reserved;
        case hotkey_state::needs_modifier:
            return hotkey_status::needs_modifier;
        case hotkey_state::invalid_key:
            return hotkey_status::invalid_key;
        case hotkey_state::invalid_id:
            return hotkey_status::invalid_id;
        case hotkey_state::failed:
            return hotkey_status::failed;
        case hotkey_state::registered:
            break;
        }
        return hotkey_status::registered;
    }

    void hotkey_host::configure(const HWND window, hotkey_api api)
    {
        window_ = window;
        api_ = std::move(api);
    }

    std::vector<hotkey_report> hotkey_host::apply(const std::span<const global_hotkey> wanted, const bool key_capture)
    {
        std::vector<hotkey_report> reports {};
        if (window_ == nullptr || api_.add == nullptr || api_.remove == nullptr)
            return reports;
        const hotkey_plan plan { sync_.update(wanted, key_capture) };
        for (const hotkey_decision& decision : plan.decisions)
            reports.push_back(report_of(decision));
        for (const hotkey_call& call : plan.calls)
            execute(call, reports);
        return reports;
    }

    void hotkey_host::release_all()
    {
        if (window_ == nullptr || api_.remove == nullptr)
            return;
        std::vector<hotkey_report> unused {};
        for (const hotkey_call& call : sync_.release_all())
            execute(call, unused);
    }

    void hotkey_host::execute(const hotkey_call& call, std::vector<hotkey_report>& reports)
    {
        if (call.kind == hotkey_call_kind::remove)
        {
            // 풀기가 실패해도 할 일이 없다. 창이 사라지면 OS도 거둔다.
            static_cast<void>(api_.remove(window_, call.number));
            return;
        }
        const hotkey_os_result result { api_.add(window_, call.number, hotkey_modifiers_of(call.chord), call.virtual_key) };
        hotkey_call_outcome outcome { hotkey_call_outcome::succeeded };
        std::u8string error {};
        if (result.ok == false)
        {
            if (result.error == ERROR_HOTKEY_ALREADY_REGISTERED)
                outcome = hotkey_call_outcome::conflict;
            else
            {
                outcome = hotkey_call_outcome::failed;
                error = make_hresult_error(u8"Failed to register the global hotkey.", HRESULT_FROM_WIN32(result.error));
            }
        }
        if (std::optional<hotkey_decision> decision { sync_.completed(call, outcome, std::move(error)) }; decision.has_value())
            reports.push_back(report_of(*decision));
    }
}
