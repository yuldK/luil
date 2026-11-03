#include "luil/app/app_host.h"
#include "luil/app/global_hotkey.h"
#include "luil/ui/commands.h"
#include "luil/ui/ui_events.h"
#include "luil/win32/win32_window.h"
#include "host/key_mapping.h"
#include "win32/hotkey_win32.h"
#include "win32/surface_input.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using luil::global_hotkey;
    using luil::key_chord;
    using luil::key_code;
    using luil::win32::hotkey_host;
    using luil::win32::hotkey_report;
    using luil::win32::hotkey_status;

    // 드문 조합이다. Ctrl+Alt+Shift+F13~F24는 키보드에 없는 키라 사용자가 쓰지 않는다.
    [[nodiscard]] key_chord rare(const std::uint32_t virtual_key = 0x7C)
    {
        return key_chord { luil::platform_key_code(virtual_key), true, true, true };
    }

    [[nodiscard]] global_hotkey make_hotkey(std::u8string id, const key_chord chord)
    {
        global_hotkey hotkey {};
        hotkey.id = std::move(id);
        hotkey.chord = chord;
        return hotkey;
    }

    [[nodiscard]] std::string narrow(const std::u8string_view text)
    {
        return std::string(text.begin(), text.end());
    }

    // 메시지를 받지 않는 보이지 않는 창이다. OS 대역은 창을 보지 않고 값만 적는다. 실제 OS를 부르는 live test도 쓴다.
    class plain_window
    {
    public:
        plain_window()
            : window_ { CreateWindowExW(0, L"STATIC", L"hotkey test", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) }
        {
        }
        plain_window(const plain_window&) = delete;
        plain_window& operator=(const plain_window&) = delete;
        ~plain_window()
        {
            if (window_ != nullptr)
                DestroyWindow(window_);
        }

        [[nodiscard]] HWND get() const noexcept
        {
            return window_;
        }

    private:
        HWND window_ { nullptr };
    };

    // OS의 대역이다. 받은 호출을 적고, 시스템 전체에서 (수정자, 가상 키)가 하나씩만 걸리는 것을 흉내 낸다.
    // 실제 `RegisterHotKey`는 개발자 PC의 키를 쥐므로 ctest는 이것만 쓴다 (실제 호출은 `[.hotkey-live]`가 따로 본다).
    struct fake_os
    {
        struct entry
        {
            HWND window { nullptr };
            int number { 0 };
            UINT modifiers { 0 };
            UINT virtual_key { 0 };
        };

        std::mutex guard {};
        std::vector<entry> held {};
        std::vector<std::string> log {};
        std::atomic<int> adds { 0 };
        std::atomic<int> removes { 0 };
        // 0이 아니면 더하기가 이 오류로 실패한다.
        DWORD forced_error { 0 };

        [[nodiscard]] static std::string text_of(const char* const verb, const int number, const UINT modifiers, const UINT virtual_key)
        {
            std::ostringstream text {};
            text << verb << "#" << number << " mods=0x" << std::hex << modifiers << " vk=0x" << virtual_key;
            return text.str();
        }

        [[nodiscard]] luil::win32::hotkey_api api()
        {
            return luil::win32::hotkey_api {
                .add = [this](const HWND window, const int number, const UINT modifiers, const UINT virtual_key) {
                    const std::lock_guard lock { guard };
                    ++adds;
                    log.push_back(text_of("add", number, modifiers, virtual_key));
                    if (forced_error != 0)
                        return luil::win32::hotkey_os_result { .ok = false, .error = forced_error };
                    const UINT identity { modifiers & ~static_cast<UINT>(MOD_NOREPEAT) };
                    for (const entry& other : held)
                    {
                        if ((other.modifiers & ~static_cast<UINT>(MOD_NOREPEAT)) == identity && other.virtual_key == virtual_key)
                            return luil::win32::hotkey_os_result { .ok = false, .error = ERROR_HOTKEY_ALREADY_REGISTERED };
                    }
                    held.push_back(entry { window, number, modifiers, virtual_key });
                    return luil::win32::hotkey_os_result { .ok = true };
                }
                , .remove = [this](const HWND window, const int number) {
                    const std::lock_guard lock { guard };
                    ++removes;
                    log.push_back("remove#" + std::to_string(number));
                    const auto found { std::find_if(held.begin(), held.end(), [&](const entry& item) { return item.window == window && item.number == number; }) };
                    if (found == held.end())
                        return false;
                    held.erase(found);
                    return true;
                }
            };
        }

        // 다른 프로그램이 이 조합을 쥐고 있다.
        void hold_elsewhere(const UINT modifiers, const UINT virtual_key)
        {
            const std::lock_guard lock { guard };
            held.push_back(entry { nullptr, 0, modifiers, virtual_key });
        }

        void release_elsewhere()
        {
            const std::lock_guard lock { guard };
            held.erase(std::remove_if(held.begin(), held.end(), [](const entry& item) { return item.window == nullptr; }), held.end());
        }

        [[nodiscard]] std::size_t held_count()
        {
            const std::lock_guard lock { guard };
            return held.size();
        }
    };

    constexpr UINT rare_modifiers { MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_NOREPEAT };
}

TEST_CASE("Every Win32 virtual key the app path turns into a key maps back to itself", "[win32][hotkey][key-mapping]")
{
    for (std::uint32_t virtual_key = 1; virtual_key <= 0xFE; ++virtual_key)
    {
        // 앱 안 경로다. 이름 키가 먼저고, 없으면 수정자와 함께 온 키로 본다 (글자 키 관문은 연다).
        key_code key { luil::win32::named_key_from_virtual(virtual_key) };
        if (key == key_code::none)
            key = luil::win32::modified_key_from_virtual(virtual_key, true, false, false);
        // 수정자 키 자체(Shift·Ctrl·Alt·Windows)만 앱 안에서도 키가 아니다. 다른 키가 빠지면 두 표가 어긋난 것이다.
        if (key == key_code::none)
        {
            const bool modifier_key { (virtual_key >= 0x10 && virtual_key <= 0x12) || virtual_key == 0x5B || virtual_key == 0x5C };
            INFO("virtual key " << virtual_key);
            REQUIRE(modifier_key);
            continue;
        }
        const bool modifier { (virtual_key >= 0xA0 && virtual_key <= 0xA5) };
        INFO("virtual key " << virtual_key << " became key " << static_cast<std::uint32_t>(key));
        REQUIRE(luil::win32_virtual_from_key(key) == (modifier ? 0u : virtual_key));
    }
}

TEST_CASE("Every named key with a label maps to a virtual key the app path reads back as the same key", "[win32][hotkey][key-mapping]")
{
    // VK가 없는 이름 키(메뉴 막대의 `alt_tap` 따위)는 글이 비어 여기서 빠진다.
    std::size_t checked { 0 };
    for (std::uint32_t value = 1; value < static_cast<std::uint32_t>(key_code::first_platform_key); ++value)
    {
        const auto key { static_cast<key_code>(value) };
        if (luil::shortcut_label(key_chord { key }).empty())
            continue;
        const std::uint32_t virtual_key { luil::win32_virtual_from_key(key) };
        INFO("key " << value << " virtual key " << virtual_key);
        REQUIRE(virtual_key != 0);
        key_code back { luil::win32::named_key_from_virtual(virtual_key) };
        if (back == key_code::none)
            back = luil::win32::modified_key_from_virtual(virtual_key, true, false, false);
        REQUIRE(back == key);
        ++checked;
    }
    // 영문자 26 + 숫자 10 + F키 12 + 이동·편집 키 15 + 구두점 11 + 키패드 15다. 줄어들면 표가 새고 있다.
    REQUIRE(checked >= 89);
}

TEST_CASE("A chord becomes the Win32 modifiers with no auto repeat", "[win32][hotkey]")
{
    using luil::win32::hotkey_modifiers_of;
    REQUIRE(hotkey_modifiers_of(key_chord { key_code::key_a }) == MOD_NOREPEAT);
    REQUIRE(hotkey_modifiers_of(key_chord { key_code::key_a, true }) == (MOD_NOREPEAT | MOD_CONTROL));
    REQUIRE(hotkey_modifiers_of(key_chord { key_code::key_a, false, true }) == (MOD_NOREPEAT | MOD_SHIFT));
    REQUIRE(hotkey_modifiers_of(key_chord { key_code::key_a, false, false, true }) == (MOD_NOREPEAT | MOD_ALT));
    REQUIRE(hotkey_modifiers_of(rare()) == rare_modifiers);
}

TEST_CASE("The public hotkey statuses follow the core states by name and order", "[win32][hotkey]")
{
    constexpr std::array states {
        luil::hotkey_state::registered
        , luil::hotkey_state::conflict
        , luil::hotkey_state::reserved
        , luil::hotkey_state::needs_modifier
        , luil::hotkey_state::invalid_key
        , luil::hotkey_state::invalid_id
        , luil::hotkey_state::failed
    };
    constexpr std::array statuses {
        hotkey_status::registered
        , hotkey_status::conflict
        , hotkey_status::reserved
        , hotkey_status::needs_modifier
        , hotkey_status::invalid_key
        , hotkey_status::invalid_id
        , hotkey_status::failed
    };

    for (std::size_t index = 0; index < states.size(); ++index)
        REQUIRE(luil::win32::hotkey_status_of(states[index]) == statuses[index]);
}

TEST_CASE("A hotkey host registers the chord with the Win32 modifiers and virtual key", "[win32][hotkey]")
{
    fake_os os {};
    const plain_window window {};
    REQUIRE(window.get() != nullptr);
    hotkey_host host {};
    host.configure(window.get(), os.api());

    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)) };
    const std::vector<hotkey_report> reports { host.apply(wanted, false) };
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0] == hotkey_report { .id = u8"a", .status = hotkey_status::registered });
    REQUIRE(os.log == std::vector<std::string> { "add#1 mods=0x4007 vk=0x7c" });
    REQUIRE(host.id_of(1) == std::u8string { u8"a" });
    REQUIRE(host.id_of(2).has_value() == false);

    // 같은 목록은 호출도 보고도 없다.
    REQUIRE(host.apply(wanted, false).empty());
    REQUIRE(os.log.size() == 1);
}

TEST_CASE("A hotkey host maps the OS errors to conflict and failed", "[win32][hotkey]")
{
    fake_os os {};
    const plain_window window {};
    hotkey_host host {};
    host.configure(window.get(), os.api());
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };

    // 다른 프로그램이 쓰는 조합이다.
    os.hold_elsewhere(rare_modifiers, 0x7C);
    std::vector<hotkey_report> reports { host.apply(wanted, false) };
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].status == hotkey_status::conflict);
    REQUIRE(reports[0].error.empty());
    REQUIRE(host.id_of(1).has_value() == false);
    // 같은 요청으로는 다시 걸지 않는다.
    REQUIRE(host.apply(wanted, false).empty());
    REQUIRE(os.adds.load() == 1);

    // 활성화 뒤에 한 번 다시 건다. 그 프로그램이 놓았으면 걸린다.
    os.release_elsewhere();
    host.reactivated();
    reports = host.apply(wanted, false);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].status == hotkey_status::registered);
    REQUIRE(host.id_of(2) == std::u8string { u8"a" });

    // 그 밖의 오류는 번호를 에러 글에 싣는다.
    host.release_all();
    fake_os other {};
    hotkey_host failing {};
    failing.configure(window.get(), other.api());
    other.forced_error = ERROR_ACCESS_DENIED;
    reports = failing.apply(wanted, false);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].status == hotkey_status::failed);
    REQUIRE(narrow(reports[0].error).find("0x80070005") != std::string::npos);
}

TEST_CASE("A hotkey host reports the rejected chords without calling the OS", "[win32][hotkey]")
{
    fake_os os {};
    const plain_window window {};
    hotkey_host host {};
    host.configure(window.get(), os.api());

    const std::vector<global_hotkey> wanted {
        make_hotkey(u8"letter", key_chord { key_code::key_a })
        , make_hotkey(u8"close", key_chord { key_code::f4, false, false, true })
        , make_hotkey(u8"none", key_chord { key_code::none, true })
        , make_hotkey(u8"", rare())
    };
    const std::vector<hotkey_report> reports { host.apply(wanted, false) };
    REQUIRE(reports.size() == 4);
    REQUIRE(reports[0] == hotkey_report { .id = u8"letter", .status = hotkey_status::needs_modifier });
    REQUIRE(reports[1] == hotkey_report { .id = u8"close", .status = hotkey_status::reserved });
    REQUIRE(reports[2] == hotkey_report { .id = u8"none", .status = hotkey_status::invalid_key });
    REQUIRE(reports[3] == hotkey_report { .id = u8"", .status = hotkey_status::invalid_id });
    REQUIRE(os.adds.load() == 0);
}

TEST_CASE("A hotkey host releases everything at the end and forgets the numbers", "[win32][hotkey]")
{
    fake_os os {};
    const plain_window window {};
    hotkey_host host {};
    host.configure(window.get(), os.api());
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)) };
    REQUIRE(host.apply(wanted, false).size() == 2);
    REQUIRE(os.held_count() == 2);

    host.release_all();
    REQUIRE(os.held_count() == 0);
    REQUIRE(host.id_of(1).has_value() == false);
    REQUIRE(host.id_of(2).has_value() == false);
    // 푼 뒤 같은 조합을 다른 쪽이 걸 수 있다.
    os.hold_elsewhere(rare_modifiers, 0x7C);
    REQUIRE(os.held_count() == 1);
}

TEST_CASE("A hotkey host without a window or an OS does nothing", "[win32][hotkey]")
{
    hotkey_host host {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    REQUIRE(host.apply(wanted, false).empty());
    host.release_all();
    REQUIRE(host.id_of(1).has_value() == false);
}

TEST_CASE("The key capture releases the OS hotkeys and brings them back without a report", "[win32][hotkey]")
{
    fake_os os {};
    const plain_window window {};
    hotkey_host host {};
    host.configure(window.get(), os.api());
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    REQUIRE(host.apply(wanted, false).size() == 1);
    REQUIRE(os.held_count() == 1);

    REQUIRE(host.apply(wanted, true).empty());
    REQUIRE(os.held_count() == 0);
    REQUIRE(host.apply(wanted, false).empty());
    REQUIRE(os.held_count() == 1);
    REQUIRE(host.id_of(2) == std::u8string { u8"a" });
}

namespace {
    constexpr const wchar_t* hotkey_window_class_name { L"Luil.Test.Hotkey" };

    BOOL CALLBACK find_hotkey_window(const HWND window, const LPARAM parameter)
    {
        std::array<wchar_t, 256> name {};
        if (GetClassNameW(window, name.data(), static_cast<int>(name.size())) > 0 && std::wcscmp(name.data(), hotkey_window_class_name) == 0)
        {
            *reinterpret_cast<HWND*>(parameter) = window;
            return FALSE;
        }
        return TRUE;
    }

    // 눌림이 앱 메시지로 닿았다는 증거다.
    struct hotkey_pressed_intent
    {
        std::u8string id {};
    };

    // 한 번의 실행에서 본 것이다. 쓰는 thread는 정해져 있고, 실행이 끝나고 thread를 거둔 뒤에 읽는다.
    struct hotkey_probe
    {
        // 창 thread: 보고를 받은 순서.
        std::vector<hotkey_report> reports {};
        // logic thread: 앱 메시지로 닿은 눌림.
        std::vector<std::u8string> presses {};
        std::atomic<int> report_count { 0 };
        std::atomic<int> handled { 0 };
        std::atomic<int> throws { 0 };
        // 이 횟수만큼 눌림이 닿으면 frame에서 단축키를 모두 뺀다. 0이면 빼지 않는다.
        int drop_after { 0 };
    };

    class hotkey_driver final : public luil::logic_driver
    {
    public:
        explicit hotkey_driver(hotkey_probe& probe, const bool several)
            : probe_ { probe }
            , several_ { several }
        {
        }

        void handle(luil::app_message message) override
        {
            if (const auto* const pressed { message.get<hotkey_pressed_intent>() }; pressed != nullptr)
            {
                probe_.presses.push_back(pressed->id);
                if (++probe_.handled >= probe_.drop_after && probe_.drop_after != 0)
                    enabled_ = false;
            }
            if (message.get<bool>() != nullptr)
                closed_.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::ui_frame> make_frame() override
        {
            auto frame { std::make_shared<luil::ui_frame>() };
            // 숨긴 주 창이다. test가 앞 창을 가져가지 않는다. 숨어도 눌림은 온다.
            frame->main_hidden = true;
            if (enabled_ == false)
                return frame;
            global_hotkey counting { make_hotkey(u8"hk-a", rare(0x7C)) };
            counting.pressed = [] { return luil::make_app_action(hotkey_pressed_intent { u8"hk-a" }); };
            frame->hotkeys.push_back(std::move(counting));
            if (several_)
            {
                global_hotkey throwing { make_hotkey(u8"hk-throw", rare(0x7D)) };
                throwing.pressed = [this]() -> luil::input_action {
                    ++probe_.throws;
                    throw std::runtime_error { "pressed threw" };
                };
                frame->hotkeys.push_back(std::move(throwing));
                // 눌림 함수가 없는 항목은 걸리지만 눌림을 버린다.
                frame->hotkeys.push_back(make_hotkey(u8"hk-null", rare(0x7E)));
            }
            return frame;
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { true };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed_.load();
        }

    private:
        hotkey_probe& probe_;
        bool several_ { false };
        bool enabled_ { true };
        std::atomic<bool> closed_ { false };
    };

    template<typename condition_type>
    void wait_for(condition_type condition)
    {
        const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
        while (condition() == false && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
    }

    // 창이 서면 다른 thread에서 시스템인 양 `WM_HOTKEY`를 보내고, 끝나면 창을 닫는다. 각 test가 `script`를 정한다.
    class hotkey_delegate final : public luil::win32::window_delegate
    {
    public:
        using script_type = std::function<void(HWND window)>;

        hotkey_delegate(hotkey_probe& probe, script_type script)
            : probe_ { probe }
            , script_ { std::move(script) }
        {
        }
        hotkey_delegate(const hotkey_delegate&) = delete;
        hotkey_delegate& operator=(const hotkey_delegate&) = delete;
        ~hotkey_delegate()
        {
            join();
        }

        void join()
        {
            if (poster_.joinable())
                poster_.join();
        }

        void on_started(luil::app_host&) override
        {
            HWND window { nullptr };
            EnumThreadWindows(GetCurrentThreadId(), &find_hotkey_window, reinterpret_cast<LPARAM>(&window));
            if (window == nullptr)
                return;
            const auto run = [this, window] {
                script_(window);
                PostMessageW(window, WM_CLOSE, 0, 0);
            };
            poster_ = std::thread { run };
        }

        [[nodiscard]] luil::app_message make_hotkey_message(const hotkey_report& report) override
        {
            probe_.reports.push_back(report);
            // 첫 보고에서 던져도 같은 대조의 나머지 보고는 간다.
            if (++probe_.report_count == 1 && throw_on_first_report)
                throw std::runtime_error { "make_hotkey_message threw" };
            return {};
        }

        // 켜면 첫 보고에서 던진다.
        bool throw_on_first_report { false };

    private:
        hotkey_probe& probe_;
        script_type script_;
        std::thread poster_ {};
    };

    [[nodiscard]] int run_hotkey_window(hotkey_driver& driver, hotkey_delegate& delegate)
    {
        luil::win32::enable_per_monitor_dpi_awareness();
        const luil::win32::com_sta_scope com {};
        luil::win32::window_config config {};
        config.class_name = hotkey_window_class_name;
        config.caption.title = u8"hotkey window";
        config.renderer = luil::renderer_mode::cpu;
        const int result { luil::win32::run_application_window(config, { &driver, nullptr, &delegate }) };
        delegate.join();
        // 창 파괴가 남긴 WM_QUIT을 걷어낸다. 뒤 test가 오염된 큐를 보지 않게 한다.
        MSG message {};
        static_cast<void>(PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
        return result;
    }
}

TEST_CASE("WM_HOTKEY on the main window reaches the pressed function of the frame's hotkey", "[win32][window][hotkey]")
{
    fake_os os {};
    // 개발자 PC의 키를 쥐지 않는다. 새 주 창이 이 대역을 쓴다.
    const luil::win32::scoped_hotkey_api scope { os.api() };
    hotkey_probe probe {};
    probe.drop_after = 2;
    hotkey_driver driver { probe, true };
    const auto script = [&](const HWND window) {
        wait_for([&] { return os.adds.load() >= 3; });
        // 장부에 없는 번호와 시스템의 IDHOT_*(음수)는 무시한다.
        PostMessageW(window, WM_HOTKEY, 99, 0);
        PostMessageW(window, WM_HOTKEY, static_cast<WPARAM>(-1), 0);
        PostMessageW(window, WM_HOTKEY, 1, MAKELPARAM(MOD_CONTROL | MOD_SHIFT | MOD_ALT, 0x7C));
        wait_for([&] { return probe.handled.load() >= 1; });
        // 눌림 함수가 던져도 창은 계속 돈다. 눌림 함수가 없는 항목은 버린다.
        PostMessageW(window, WM_HOTKEY, 2, 0);
        wait_for([&] { return probe.throws.load() >= 1; });
        PostMessageW(window, WM_HOTKEY, 3, 0);
        // 두 번째 눌림에서 앱이 단축키를 모두 뺀다.
        PostMessageW(window, WM_HOTKEY, 1, 0);
        wait_for([&] { return os.removes.load() >= 3; });
        // 푼 번호의 늦은 눌림은 다른 id로 읽히지 않는다.
        PostMessageW(window, WM_HOTKEY, 1, 0);
        PostMessageW(window, WM_HOTKEY, 2, 0);
    };
    hotkey_delegate delegate { probe, script };
    delegate.throw_on_first_report = true;
    REQUIRE(run_hotkey_window(driver, delegate) == 0);

    REQUIRE(probe.presses == std::vector<std::u8string> { u8"hk-a", u8"hk-a" });
    REQUIRE(probe.throws.load() == 1);
    REQUIRE(probe.reports.size() == 3);
    for (const hotkey_report& report : probe.reports)
        REQUIRE(report.status == hotkey_status::registered);
    REQUIRE(probe.reports[0].id == u8"hk-a");
    REQUIRE(probe.reports[1].id == u8"hk-throw");
    REQUIRE(probe.reports[2].id == u8"hk-null");
    REQUIRE(os.log.size() == 6);
    REQUIRE(os.log[0] == "add#1 mods=0x4007 vk=0x7c");
    REQUIRE(os.log[1] == "add#2 mods=0x4007 vk=0x7d");
    REQUIRE(os.log[2] == "add#3 mods=0x4007 vk=0x7e");
    REQUIRE(os.log[3] == "remove#1");
    REQUIRE(os.log[4] == "remove#2");
    REQUIRE(os.log[5] == "remove#3");
    REQUIRE(os.held_count() == 0);
    REQUIRE(luil::current_ui_platform().capabilities.global_hotkeys);
}

TEST_CASE("A conflicting hotkey is retried once when the main window is activated and closing releases it", "[win32][window][hotkey]")
{
    fake_os os {};
    // 다른 프로그램이 먼저 쥐고 있다.
    os.hold_elsewhere(rare_modifiers, 0x7C);
    const luil::win32::scoped_hotkey_api scope { os.api() };
    hotkey_probe probe {};
    hotkey_driver driver { probe, false };
    const auto script = [&](const HWND window) {
        wait_for([&] { return probe.report_count.load() >= 1; });
        // frame이 몇 번 더 와도 같은 요청으로 다시 걸지 않는다. 활성화가 와야 다시 건다.
        std::this_thread::sleep_for(std::chrono::milliseconds { 100 });
        os.release_elsewhere();
        PostMessageW(window, WM_ACTIVATE, WA_ACTIVE, 0);
        wait_for([&] { return probe.report_count.load() >= 2; });
    };
    hotkey_delegate delegate { probe, script };
    REQUIRE(run_hotkey_window(driver, delegate) == 0);

    REQUIRE(probe.reports.size() == 2);
    REQUIRE(probe.reports[0] == hotkey_report { .id = u8"hk-a", .status = hotkey_status::conflict });
    REQUIRE(probe.reports[1] == hotkey_report { .id = u8"hk-a", .status = hotkey_status::registered });
    // 창이 처음 앞으로 설 때의 활성화가 끼어들면 다시 거는 횟수가 늘 수 있다. 보고는 결과가 달라질 때 한 번뿐이다.
    REQUIRE(os.adds.load() >= 2);
    // 창이 닫히면 풀린다. 다른 프로그램의 것은 이미 놓았다.
    REQUIRE(os.held_count() == 0);
    REQUIRE(os.log.back().rfind("remove#", 0) == 0);
}

// 실제 OS에 드문 조합을 걸고 곧바로 푼다 (docs/global-hotkey-design.md의 X-9 실측). ctest는 돌리지 않는다.
//  - `luil_tests "[.hotkey-live]"`로 올린다. Ctrl+Alt+Shift+F13~F14는 키보드에 없는 키다. 결과와 상관없이 푼다.
TEST_CASE("Live hotkeys are registered, collide between windows and are released", "[.hotkey-live]")
{
    const plain_window first {};
    const plain_window second {};
    const plain_window third {};
    REQUIRE(first.get() != nullptr);
    REQUIRE(second.get() != nullptr);
    REQUIRE(third.get() != nullptr);
    hotkey_host one {};
    hotkey_host two {};
    hotkey_host three {};
    one.configure(first.get(), luil::win32::real_hotkey_api());
    two.configure(second.get(), luil::win32::real_hotkey_api());
    three.configure(third.get(), luil::win32::real_hotkey_api());
    const std::vector<global_hotkey> wanted { make_hotkey(u8"live-a", rare(0x7C)) };

    const std::vector<hotkey_report> first_reports { one.apply(wanted, false) };
    // 같은 조합을 둘째 창이 걸면 OS가 막는다.
    const std::vector<hotkey_report> second_reports { two.apply(wanted, false) };
    // 첫째가 놓고 둘째가 활성화 뒤에 다시 걸면 걸린다.
    const std::vector<global_hotkey> none {};
    const std::vector<hotkey_report> released { one.apply(none, false) };
    two.reactivated();
    const std::vector<hotkey_report> retried { two.apply(wanted, false) };
    // 둘째가 끝에 모두 풀면 셋째가 같은 조합을 걸 수 있다. 남은 등록이 없다는 뜻이다.
    two.release_all();
    const std::vector<hotkey_report> third_reports { three.apply(wanted, false) };
    three.release_all();
    one.release_all();

    // 같은 창에서 번호만 다른 둘째 등록과, 예약이라고 알려진 Ctrl+Alt+Shift+F12를 라이브러리를 거치지 않고 재 본다.
    const luil::win32::hotkey_api real { luil::win32::real_hotkey_api() };
    const luil::win32::hotkey_os_result base { real.add(first.get(), 1, rare_modifiers, 0x7D) };
    const luil::win32::hotkey_os_result same_window_second { real.add(first.get(), 2, rare_modifiers, 0x7D) };
    static_cast<void>(real.remove(first.get(), 1));
    static_cast<void>(real.remove(first.get(), 2));
    const luil::win32::hotkey_os_result f12 { real.add(first.get(), 3, rare_modifiers, 0x7B) };
    static_cast<void>(real.remove(first.get(), 3));
    // 모두 풀린 뒤 다시 걸린다.
    const luil::win32::hotkey_os_result after { real.add(first.get(), 4, rare_modifiers, 0x7D) };
    static_cast<void>(real.remove(first.get(), 4));

    const auto describe_reports = [](const std::vector<hotkey_report>& reports) {
        std::string text {};
        for (const hotkey_report& report : reports)
            text += narrow(report.id) + "=" + std::to_string(static_cast<int>(report.status)) + (report.error.empty() ? "" : " " + narrow(report.error)) + ";";
        return text.empty() ? std::string { "none" } : text;
    };
    WARN("first window " << describe_reports(first_reports));
    WARN("second window while the first holds it " << describe_reports(second_reports));
    WARN("first releases " << describe_reports(released));
    WARN("second window after activation " << describe_reports(retried));
    WARN("third window after the second released " << describe_reports(third_reports));
    WARN("raw same window, id 1 ok " << base.ok << " error " << base.error << "; id 2 same chord ok " << same_window_second.ok << " error " << same_window_second.error);
    WARN("raw Ctrl+Alt+Shift+F12 ok " << f12.ok << " error " << f12.error);
    WARN("raw again after release ok " << after.ok << " error " << after.error);

    REQUIRE(first_reports.size() == 1);
    REQUIRE(first_reports[0].status == hotkey_status::registered);
    REQUIRE(second_reports.size() == 1);
    REQUIRE(second_reports[0].status == hotkey_status::conflict);
    REQUIRE(released.empty());
    REQUIRE(retried.size() == 1);
    REQUIRE(retried[0].status == hotkey_status::registered);
    REQUIRE(third_reports.size() == 1);
    REQUIRE(third_reports[0].status == hotkey_status::registered);
    REQUIRE(base.ok);
    REQUIRE(after.ok);
}

TEST_CASE("A hotkey host rejects the Alt tap of the menu bar without calling the OS", "[win32][hotkey][menu-bar]")
{
    // `alt_tap`은 VK가 없는 이름 키다. 라벨이 비어 위의 왕복 test에서 빠지고, 걸려 하면 OS를 부르지 않고 거절한다.
    REQUIRE(luil::shortcut_label(key_chord { key_code::alt_tap }).empty());
    REQUIRE(luil::win32_virtual_from_key(key_code::alt_tap) == 0);
    fake_os os {};
    const plain_window window {};
    hotkey_host host {};
    host.configure(window.get(), os.api());
    const std::vector<global_hotkey> wanted { make_hotkey(u8"menu", key_chord { key_code::alt_tap, true, false, true }) };
    const std::vector<hotkey_report> reports { host.apply(wanted, false) };
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0] == hotkey_report { .id = u8"menu", .status = hotkey_status::invalid_key });
    REQUIRE(os.adds.load() == 0);
}
