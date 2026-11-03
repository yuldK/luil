#include "host/hotkey_sync.h"
#include "host/key_mapping.h"

#include "luil/app/global_hotkey.h"
#include "luil/ui/commands.h"
#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {
    using luil::global_hotkey;
    using luil::hotkey_call;
    using luil::hotkey_call_kind;
    using luil::hotkey_call_outcome;
    using luil::hotkey_decision;
    using luil::hotkey_state;
    using luil::hotkey_sync;
    using luil::key_chord;
    using luil::key_code;

    // 플랫폼 대역의 키다. 이름이 없는 F13~F24, Pause 같은 키를 이 값으로 적는다.
    [[nodiscard]] key_code band(const std::uint32_t virtual_key)
    {
        return luil::platform_key_code(virtual_key);
    }

    // 드문 조합이다. 앱이 걸기 좋은 모양(F13 + 세 수정자)이다.
    [[nodiscard]] key_chord rare(const std::uint32_t virtual_key = 0x7C)
    {
        return key_chord { band(virtual_key), true, true, true };
    }

    [[nodiscard]] key_chord with(const key_code key, const bool control, const bool shift, const bool alt)
    {
        return key_chord { key, control, shift, alt };
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

    // 호출을 "-번호 +번호:id" 글로 줄인다. 없으면 "none"이다.
    [[nodiscard]] std::string describe(const std::vector<hotkey_call>& calls)
    {
        if (calls.empty())
            return "none";
        std::string text {};
        for (const hotkey_call& call : calls)
        {
            if (text.empty() == false)
                text += ' ';
            text += call.kind == hotkey_call_kind::add ? "+" : "-";
            text += std::to_string(call.number);
            if (call.kind == hotkey_call_kind::add)
                text += ":" + narrow(call.id);
        }
        return text;
    }

    [[nodiscard]] std::string state_name(const hotkey_state state)
    {
        switch (state)
        {
        case hotkey_state::registered:
            return "registered";
        case hotkey_state::conflict:
            return "conflict";
        case hotkey_state::reserved:
            return "reserved";
        case hotkey_state::needs_modifier:
            return "needs_modifier";
        case hotkey_state::invalid_key:
            return "invalid_key";
        case hotkey_state::invalid_id:
            return "invalid_id";
        case hotkey_state::failed:
            return "failed";
        }
        return "?";
    }

    // 알릴 결과를 "id=상태" 글로 줄여 글자 차례로 늘어놓는다. 없으면 "none"이다.
    [[nodiscard]] std::string describe(const std::vector<hotkey_decision>& decisions)
    {
        if (decisions.empty())
            return "none";
        std::vector<std::string> items {};
        for (const hotkey_decision& decision : decisions)
            items.push_back(narrow(decision.id) + "=" + state_name(decision.state));
        std::sort(items.begin(), items.end());
        std::string text {};
        for (const std::string& item : items)
        {
            if (text.empty() == false)
                text += ' ';
            text += item;
        }
        return text;
    }

    // 한 번의 대조와 그 호출 결과다. 더하기마다 OS가 `outcome`으로 답한 것으로 되돌려 알릴 결과를 모두 모은다.
    struct applied
    {
        std::vector<hotkey_call> calls {};
        std::vector<hotkey_decision> decisions {};
    };

    [[nodiscard]] applied run_update(hotkey_sync& sync, const std::vector<global_hotkey>& wanted, const bool key_capture = false, const hotkey_call_outcome outcome = hotkey_call_outcome::succeeded)
    {
        const luil::hotkey_plan plan { sync.update(wanted, key_capture) };
        applied result { .calls = plan.calls, .decisions = plan.decisions };
        for (const hotkey_call& call : plan.calls)
        {
            if (call.kind != hotkey_call_kind::add)
                continue;
            if (std::optional<hotkey_decision> decision { sync.completed(call, outcome, u8"boom") }; decision.has_value())
                result.decisions.push_back(*decision);
        }
        return result;
    }
}

TEST_CASE("An empty hotkey list asks the OS for nothing", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const applied result { run_update(sync, {}) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
    REQUIRE(sync.id_of(1).has_value() == false);
}

TEST_CASE("The first list adds each hotkey and the same list asks for nothing again", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)) };

    applied result { run_update(sync, wanted) };
    REQUIRE(describe(result.calls) == "+1:a +2:b");
    REQUIRE(describe(result.decisions) == "a=registered b=registered");
    REQUIRE(result.calls[0].chord == rare(0x7C));
    REQUIRE(result.calls[0].virtual_key == 0x7C);

    // 같은 목록은 호출도 보고도 없다. 눌림 함수는 견주지 않는다.
    std::vector<global_hotkey> again { wanted };
    again[0].pressed = [] { return luil::input_action {}; };
    result = run_update(sync, again);
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
}

TEST_CASE("A changed chord releases the old number and adds a new one", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare(0x7C)) }).calls) == "+1:a");

    const applied result { run_update(sync, { make_hotkey(u8"a", rare(0x7D)) }) };
    // 풀기 다음 걸기다. 번호는 다시 쓰지 않는다.
    REQUIRE(describe(result.calls) == "-1 +2:a");
    // 조합을 바꾼 시도는 결과가 같아도 알린다.
    REQUIRE(describe(result.decisions) == "a=registered");
    REQUIRE(sync.id_of(1).has_value() == false);
    REQUIRE(sync.id_of(2) == std::u8string { u8"a" });
}

TEST_CASE("A hotkey taken out of the list is released and its number is never reused", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare()) }).calls) == "+1:a");

    applied result { run_update(sync, {}) };
    REQUIRE(describe(result.calls) == "-1");
    // 뺀 항목은 알리지 않는다.
    REQUIRE(describe(result.decisions) == "none");

    result = run_update(sync, { make_hotkey(u8"a", rare()) });
    REQUIRE(describe(result.calls) == "+2:a");
}

TEST_CASE("Two ids that swap their chords in one list release before they add", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)) }).calls) == "+1:a +2:b");

    const applied result { run_update(sync, { make_hotkey(u8"a", rare(0x7D)), make_hotkey(u8"b", rare(0x7C)) }) };
    REQUIRE(describe(result.calls) == "-1 -2 +3:a +4:b");
    REQUIRE(describe(result.decisions) == "a=registered b=registered");
}

TEST_CASE("A chord change whose new chord fails leaves the old one released", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare(0x7C)) }).calls) == "+1:a");

    const applied result { run_update(sync, { make_hotkey(u8"a", rare(0x7D)) }, false, hotkey_call_outcome::conflict) };
    REQUIRE(describe(result.calls) == "-1 +2:a");
    REQUIRE(describe(result.decisions) == "a=conflict");
    // 옛 조합이 몰래 살아 있지 않다.
    REQUIRE(sync.id_of(1).has_value() == false);
    REQUIRE(sync.id_of(2).has_value() == false);
}

TEST_CASE("The same chord twice in a list is added once and the later one conflicts without asking the OS", "[hotkey][sync]")
{
    hotkey_sync sync {};
    applied result { run_update(sync, { make_hotkey(u8"a", rare()), make_hotkey(u8"b", rare()) }) };
    REQUIRE(describe(result.calls) == "+1:a");
    REQUIRE(describe(result.decisions) == "a=registered b=conflict");

    // 같은 목록은 다시 알리지 않는다.
    result = run_update(sync, { make_hotkey(u8"a", rare()), make_hotkey(u8"b", rare()) });
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
}

TEST_CASE("Two spellings of the same virtual key are the same chord", "[hotkey][sync]")
{
    hotkey_sync sync {};
    // 플랫폼 대역의 VK_RETURN과 이름 키 enter는 같은 키다.
    const key_chord by_band { band(0x0D), true, false, false };
    const key_chord by_name { key_code::enter, true, false, false };
    const applied result { run_update(sync, { make_hotkey(u8"a", by_band), make_hotkey(u8"b", by_name) }) };
    REQUIRE(describe(result.calls) == "+1:a");
    REQUIRE(describe(result.decisions) == "a=registered b=conflict");
}

TEST_CASE("A later duplicate takes the chord when the earlier one leaves and gives it back when an earlier one returns", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare()), make_hotkey(u8"b", rare()) }).calls) == "+1:a");

    // 앞 항목이 빠지면 뒤 항목이 자리를 잡는다.
    applied result { run_update(sync, { make_hotkey(u8"b", rare()) }) };
    REQUIRE(describe(result.calls) == "-1 +2:b");
    REQUIRE(describe(result.decisions) == "b=registered");
    REQUIRE(sync.id_of(2) == std::u8string { u8"b" });

    // 앞에 새 항목이 서면 그 항목이 이기고 건 뒤 항목은 풀린다.
    result = run_update(sync, { make_hotkey(u8"c", rare()), make_hotkey(u8"b", rare()) });
    REQUIRE(describe(result.calls) == "-2 +3:c");
    REQUIRE(describe(result.decisions) == "b=conflict c=registered");
}

TEST_CASE("An empty or repeated id is invalid and never reaches the OS", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted {
        make_hotkey(u8"", rare(0x7C))
        , make_hotkey(u8"a", rare(0x7D))
        , make_hotkey(u8"a", rare(0x7E))
    };
    applied result { run_update(sync, wanted) };
    REQUIRE(describe(result.calls) == "+1:a");
    REQUIRE(describe(result.decisions) == "=invalid_id a=invalid_id a=registered");

    result = run_update(sync, wanted);
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
}

TEST_CASE("Keys without a Win32 virtual key are invalid", "[hotkey][sync]")
{
    const std::vector<key_code> keys {
        key_code::none
        , key_code::zoom_in
        , key_code::zoom_out
        , key_code::volume_up
        , key_code::volume_down
        // 수정자 가상 키다.
        , band(0x10)
        , band(0x11)
        , band(0x12)
        , band(0x5B)
        , band(0x5C)
        , band(0xA0)
        , band(0xA1)
        , band(0xA2)
        , band(0xA3)
        , band(0xA4)
        , band(0xA5)
        // 1~0xFE 밖의 대역 값이다.
        , key_code::first_platform_key
        , band(0xFF)
        , band(0x100)
        , band(0x1000)
    };

    for (const key_code key : keys)
    {
        INFO("key " << static_cast<std::uint32_t>(key));
        REQUIRE(luil::classify_hotkey_chord(with(key, true, true, true)) == hotkey_state::invalid_key);
    }

    hotkey_sync sync {};
    const applied result { run_update(sync, { make_hotkey(u8"a", with(key_code::none, true, false, false)) }) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "a=invalid_key");
}

TEST_CASE("Chords the OS reserves are reserved whatever spelling they use", "[hotkey][sync]")
{
    const std::vector<key_chord> chords {
        with(key_code::f4, false, false, true)
        , with(key_code::tab, false, false, true)
        , with(key_code::escape, false, false, true)
        , with(key_code::escape, true, false, false)
        , with(key_code::space, false, false, true)
        , with(key_code::delete_forward, true, false, true)
        // 플랫폼 대역으로 적은 같은 조합이다.
        , with(band(0x73), false, false, true)
        , with(band(0x1B), true, false, false)
    };

    for (const key_chord& chord : chords)
    {
        INFO("key " << static_cast<std::uint32_t>(chord.key));
        REQUIRE(luil::classify_hotkey_chord(chord) == hotkey_state::reserved);
    }

    // F12는 수정자와 상관없이 막는다.
    for (int mask = 0; mask < 8; ++mask)
    {
        const key_chord named { key_code::f12, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
        const key_chord banded { band(0x7B), (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
        INFO("mask " << mask);
        REQUIRE(luil::classify_hotkey_chord(named) == hotkey_state::reserved);
        REQUIRE(luil::classify_hotkey_chord(banded) == hotkey_state::reserved);
    }

    hotkey_sync sync {};
    const applied result { run_update(sync, { make_hotkey(u8"close", with(key_code::f4, false, false, true)) }) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "close=reserved");
}

TEST_CASE("Every chord the shortcut capture shows as reserved is reserved here", "[hotkey][sync]")
{
    // 키 잡기 칸과 같은 목록을 쓴다. 이름 키 어디에서든 어긋나지 않는다.
    for (std::uint32_t value = 1; value < static_cast<std::uint32_t>(key_code::first_platform_key); ++value)
    {
        const auto key { static_cast<key_code>(value) };
        if (luil::win32_virtual_from_key(key) == 0)
            continue;
        for (int mask = 0; mask < 8; ++mask)
        {
            const key_chord chord { key, (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
            if (luil::system_reserved_chord(chord) == false)
                continue;
            INFO("key " << value << " mask " << mask);
            REQUIRE(luil::classify_hotkey_chord(chord) == hotkey_state::reserved);
        }
    }
}

TEST_CASE("Keys that make text or move the caret need Ctrl or Alt", "[hotkey][sync]")
{
    const std::vector<key_code> text_keys {
        key_code::key_a
        , key_code::key_z
        , key_code::key_0
        , key_code::key_9
        , key_code::minus
        , key_code::grave
        , key_code::numpad_0
        , key_code::numpad_add
        , key_code::arrow_up
        , key_code::arrow_left
        , key_code::home
        , key_code::end
        , key_code::page_up
        , key_code::page_down
        , key_code::insert
        , key_code::delete_forward
        , key_code::backspace
        , key_code::enter
        , key_code::tab
        , key_code::space
        , key_code::escape
        // 이름 없는 글자 키(OEM_102, CapsLock, NumLock, 메뉴 키)도 같다.
        , band(0xE2)
        , band(0x14)
        , band(0x90)
        , band(0x5D)
        // 플랫폼 대역으로 적은 Enter도 같은 키다.
        , band(0x0D)
    };

    for (const key_code key : text_keys)
    {
        INFO("key " << static_cast<std::uint32_t>(key));
        REQUIRE(luil::classify_hotkey_chord(with(key, false, false, false)) == hotkey_state::needs_modifier);
        REQUIRE(luil::classify_hotkey_chord(with(key, false, true, false)) == hotkey_state::needs_modifier);
        // Ctrl 다음 Shift나 Alt가 있으면 된다. 예약 조합(Alt+Tab 따위)과 겹치지 않게 Ctrl+Shift로 본다.
        REQUIRE(luil::classify_hotkey_chord(with(key, true, true, false)).has_value() == false);
        REQUIRE(luil::classify_hotkey_chord(with(key, true, false, false)) != hotkey_state::needs_modifier);
        REQUIRE(luil::classify_hotkey_chord(with(key, false, false, true)) != hotkey_state::needs_modifier);
    }
}

TEST_CASE("Function and media keys may stand alone", "[hotkey][sync]")
{
    const std::vector<key_code> alone {
        key_code::f1
        , key_code::f5
        , key_code::f11
        // F13~F24다.
        , band(0x7C)
        , band(0x87)
        // Pause, PrintScreen, ScrollLock다.
        , band(0x13)
        , band(0x2C)
        , band(0x91)
        // 브라우저·미디어·실행 키다.
        , band(0xA6)
        , band(0xAD)
        , band(0xB3)
        , band(0xB7)
    };

    for (const key_code key : alone)
    {
        INFO("key " << static_cast<std::uint32_t>(key));
        REQUIRE(luil::classify_hotkey_chord(with(key, false, false, false)).has_value() == false);
        REQUIRE(luil::classify_hotkey_chord(with(key, false, true, false)).has_value() == false);
    }
    // 목록 가장자리의 바깥이다. F12는 예약이고 F25와 그 밖의 대역은 수정자가 필요하다.
    REQUIRE(luil::classify_hotkey_chord(with(band(0x7B), false, false, false)) == hotkey_state::reserved);
    REQUIRE(luil::classify_hotkey_chord(with(band(0x88), false, false, false)) == hotkey_state::needs_modifier);
    REQUIRE(luil::classify_hotkey_chord(with(band(0xA5), false, false, false)) == hotkey_state::invalid_key);
    REQUIRE(luil::classify_hotkey_chord(with(band(0xB8), false, false, false)) == hotkey_state::needs_modifier);

    hotkey_sync sync {};
    const applied result { run_update(sync, { make_hotkey(u8"plain", with(key_code::key_a, false, false, false)) }) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "plain=needs_modifier");
}

TEST_CASE("A failure is reported once and retried only after the window is activated", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };

    applied result { run_update(sync, wanted, false, hotkey_call_outcome::conflict) };
    REQUIRE(describe(result.calls) == "+1:a");
    REQUIRE(describe(result.decisions) == "a=conflict");
    REQUIRE(sync.id_of(1).has_value() == false);

    // 같은 요청으로는 다시 걸지 않는다.
    for (int frame = 0; frame < 3; ++frame)
    {
        result = run_update(sync, wanted, false, hotkey_call_outcome::conflict);
        REQUIRE(describe(result.calls) == "none");
        REQUIRE(describe(result.decisions) == "none");
    }

    // 활성화 뒤의 대조가 한 번 다시 건다. 결과가 같으면 알리지 않는다.
    sync.reactivated();
    result = run_update(sync, wanted, false, hotkey_call_outcome::conflict);
    REQUIRE(describe(result.calls) == "+2:a");
    REQUIRE(describe(result.decisions) == "none");
    result = run_update(sync, wanted, false, hotkey_call_outcome::conflict);
    REQUIRE(describe(result.calls) == "none");

    // 이번에는 걸린다. 결과가 달라졌으므로 알린다.
    sync.reactivated();
    result = run_update(sync, wanted, false, hotkey_call_outcome::succeeded);
    REQUIRE(describe(result.calls) == "+3:a");
    REQUIRE(describe(result.decisions) == "a=registered");
    REQUIRE(sync.id_of(3) == std::u8string { u8"a" });
}

TEST_CASE("An OS failure carries its diagnostic and retries like a conflict", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    const luil::hotkey_plan plan { sync.update(wanted, false) };
    REQUIRE(plan.calls.size() == 1);
    const std::optional<hotkey_decision> decision { sync.completed(plan.calls[0], hotkey_call_outcome::failed, u8"diagnostic text") };
    REQUIRE(decision.has_value());
    REQUIRE(decision->state == hotkey_state::failed);
    REQUIRE(decision->error == u8"diagnostic text");

    REQUIRE(describe(run_update(sync, wanted).calls) == "none");
    sync.reactivated();
    REQUIRE(describe(run_update(sync, wanted).calls) == "+2:a");
}

TEST_CASE("Activating the window leaves a registered hotkey alone", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    REQUIRE(describe(run_update(sync, wanted).calls) == "+1:a");
    sync.reactivated();
    const applied result { run_update(sync, wanted) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
}

TEST_CASE("A rejected hotkey is not retried by activation", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", with(key_code::key_a, false, false, false)) };
    REQUIRE(describe(run_update(sync, wanted).decisions) == "a=needs_modifier");
    sync.reactivated();
    const applied result { run_update(sync, wanted) };
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");
}

TEST_CASE("Key capture releases everything silently and restores it afterwards", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)) };
    REQUIRE(describe(run_update(sync, wanted).calls) == "+1:a +2:b");

    applied result { run_update(sync, wanted, true) };
    REQUIRE(describe(result.calls) == "-1 -2");
    REQUIRE(describe(result.decisions) == "none");
    REQUIRE(sync.id_of(1).has_value() == false);

    // 잡는 동안 같은 목록은 아무 일도 없다. 새 항목은 미룬다.
    result = run_update(sync, wanted, true);
    REQUIRE(describe(result.calls) == "none");
    const std::vector<global_hotkey> grown { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)), make_hotkey(u8"c", rare(0x7E)) };
    result = run_update(sync, grown, true);
    REQUIRE(describe(result.calls) == "none");
    REQUIRE(describe(result.decisions) == "none");

    // 끝나면 다시 건다. 결과가 그대로인 항목은 알리지 않고, 처음 거는 항목만 알린다.
    result = run_update(sync, grown, false);
    REQUIRE(describe(result.calls) == "+3:a +4:b +5:c");
    REQUIRE(describe(result.decisions) == "c=registered");
    REQUIRE(sync.id_of(3) == std::u8string { u8"a" });
}

TEST_CASE("A conflict that appears while the key capture holds the hotkeys is reported when they come back", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)) };
    REQUIRE(describe(run_update(sync, wanted).calls) == "+1:a +2:b");
    REQUIRE(describe(run_update(sync, wanted, true).calls) == "-1 -2");

    // 풀린 사이 다른 프로그램이 a의 조합을 잡았다.
    const luil::hotkey_plan plan { sync.update(wanted, false) };
    REQUIRE(describe(plan.calls) == "+3:a +4:b");
    const std::optional<hotkey_decision> first { sync.completed(plan.calls[0], hotkey_call_outcome::conflict) };
    const std::optional<hotkey_decision> second { sync.completed(plan.calls[1], hotkey_call_outcome::succeeded) };
    REQUIRE(first.has_value());
    REQUIRE(first->state == hotkey_state::conflict);
    REQUIRE(second.has_value() == false);
}

TEST_CASE("A hotkey that was a conflict stays one through a key capture until activation", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    REQUIRE(describe(run_update(sync, wanted, false, hotkey_call_outcome::conflict).decisions) == "a=conflict");
    REQUIRE(describe(run_update(sync, wanted, true).calls) == "none");
    // 잡기가 끝나도 걸지 못했던 항목을 되걸지 않는다.
    REQUIRE(describe(run_update(sync, wanted, false).calls) == "none");
}

TEST_CASE("A reactivation during a key capture is kept until the capture ends", "[hotkey][sync]")
{
    hotkey_sync sync {};
    const std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare()) };
    REQUIRE(describe(run_update(sync, wanted, false, hotkey_call_outcome::conflict).decisions) == "a=conflict");
    sync.reactivated();
    REQUIRE(describe(run_update(sync, wanted, true).calls) == "none");
    REQUIRE(describe(run_update(sync, wanted, false).calls) == "+2:a");
}

TEST_CASE("Numbers wrap after the last one and skip the ones still in use", "[hotkey][sync]")
{
    static_assert(hotkey_sync::max_number == 0xBFFF);

    hotkey_sync sync { 3 };
    std::vector<global_hotkey> wanted { make_hotkey(u8"a", rare(0x7C)) };
    REQUIRE(describe(run_update(sync, wanted).calls) == "+1:a");
    wanted.push_back(make_hotkey(u8"b", rare(0x7D)));
    REQUIRE(describe(run_update(sync, wanted).calls) == "+2:b");
    wanted.push_back(make_hotkey(u8"c", rare(0x7E)));
    REQUIRE(describe(run_update(sync, wanted).calls) == "+3:c");

    // b를 풀고 새 항목을 더하면 끝 다음이라 1로 돌아간다. 1은 a가 쓰므로 건너뛰어 2다.
    wanted.erase(wanted.begin() + 1);
    REQUIRE(describe(run_update(sync, wanted).calls) == "-2");
    wanted.push_back(make_hotkey(u8"d", rare(0x7F)));
    REQUIRE(describe(run_update(sync, wanted).calls) == "+2:d");
    REQUIRE(sync.id_of(2) == std::u8string { u8"d" });
    REQUIRE(sync.id_of(1) == std::u8string { u8"a" });
    REQUIRE(sync.id_of(3) == std::u8string { u8"c" });

    // 번호가 다 쓰였으면 걸지 않고 실패로 알린다.
    wanted.push_back(make_hotkey(u8"e", rare(0x80)));
    const applied exhausted { run_update(sync, wanted) };
    REQUIRE(describe(exhausted.calls) == "none");
    REQUIRE(describe(exhausted.decisions) == "e=failed");
    REQUIRE(exhausted.decisions[0].error.empty() == false);
}

TEST_CASE("A number that is not in the ledger names no id", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare()) }).calls) == "+1:a");
    REQUIRE(sync.id_of(1) == std::u8string { u8"a" });
    REQUIRE(sync.id_of(0).has_value() == false);
    REQUIRE(sync.id_of(2).has_value() == false);
    // 시스템의 IDHOT_* 번호는 음수라 WPARAM으로 큰 수가 된다.
    REQUIRE(sync.id_of(static_cast<std::uintptr_t>(-1)).has_value() == false);
    REQUIRE(sync.id_of(static_cast<std::uintptr_t>(hotkey_sync::max_number) + 1).has_value() == false);

    REQUIRE(describe(run_update(sync, {}).calls) == "-1");
    REQUIRE(sync.id_of(1).has_value() == false);
}

TEST_CASE("Releasing everything frees every number and empties the ledger", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare(0x7C)), make_hotkey(u8"b", rare(0x7D)), make_hotkey(u8"c", with(key_code::key_a, false, false, false)) }).calls) == "+1:a +2:b");
    REQUIRE(describe(sync.release_all()) == "-1 -2");
    REQUIRE(sync.id_of(1).has_value() == false);
    REQUIRE(describe(sync.release_all()) == "none");
}

TEST_CASE("Releasing during a key capture frees nothing twice", "[hotkey][sync]")
{
    hotkey_sync sync {};
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare()) }).calls) == "+1:a");
    REQUIRE(describe(run_update(sync, { make_hotkey(u8"a", rare()) }, true).calls) == "-1");
    REQUIRE(describe(sync.release_all()) == "none");
}

TEST_CASE("A completion that names a number the ledger does not hold is ignored", "[hotkey][sync]")
{
    hotkey_sync sync {};
    hotkey_call stray {};
    stray.number = 7;
    REQUIRE(sync.completed(stray, hotkey_call_outcome::conflict).has_value() == false);
    stray.kind = hotkey_call_kind::remove;
    REQUIRE(sync.completed(stray, hotkey_call_outcome::succeeded).has_value() == false);
}

TEST_CASE("The inverse key table agrees with the Win32 key tables", "[hotkey][key-mapping]")
{
    // 구두점·키패드: named_key_from_win32_virtual의 역이다.
    for (std::uint32_t virtual_key = 0; virtual_key <= 0xFF; ++virtual_key)
    {
        const key_code key { luil::named_key_from_win32_virtual(virtual_key) };
        if (key == key_code::none)
            continue;
        INFO("virtual key " << virtual_key);
        REQUIRE(luil::win32_virtual_from_key(key) == virtual_key);
    }

    // 영문자·숫자: platform_key_of의 역이다.
    for (std::uint32_t virtual_key = 0; virtual_key <= 0xFF; ++virtual_key)
    {
        const key_code key { luil::alphanumeric_key_code(virtual_key) };
        if (key == key_code::none)
            continue;
        INFO("virtual key " << virtual_key);
        REQUIRE(luil::platform_key_of(key) == virtual_key);
        REQUIRE(luil::win32_virtual_from_key(key) == virtual_key);
    }

    // 플랫폼 대역: 값에서 대역의 시작을 뺀 것이다. 수정자 가상 키는 없다.
    for (std::uint32_t virtual_key = 1; virtual_key <= 0xFE; ++virtual_key)
    {
        const bool modifier { (virtual_key >= 0x10 && virtual_key <= 0x12) || virtual_key == 0x5B || virtual_key == 0x5C || (virtual_key >= 0xA0 && virtual_key <= 0xA5) };
        INFO("virtual key " << virtual_key);
        REQUIRE(luil::win32_virtual_from_key(static_cast<key_code>(static_cast<std::uint32_t>(key_code::first_platform_key) + virtual_key)) == (modifier ? 0u : virtual_key));
    }
    REQUIRE(luil::win32_virtual_from_key(key_code::first_platform_key) == 0);
    REQUIRE(luil::win32_virtual_from_key(band(0xFF)) == 0);
    REQUIRE(luil::win32_virtual_from_key(band(0x100)) == 0);
}

TEST_CASE("The function keys map one by one around F5", "[hotkey][key-mapping]")
{
    // F5는 이름 키 사이에 따로 있어 순서로 셀 수 없다.
    const std::vector<key_code> keys {
        key_code::f1, key_code::f2, key_code::f3, key_code::f4, key_code::f5, key_code::f6
        , key_code::f7, key_code::f8, key_code::f9, key_code::f10, key_code::f11, key_code::f12
    };

    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        INFO("function key " << index + 1);
        REQUIRE(luil::win32_virtual_from_key(keys[index]) == 0x70 + index);
    }
}

TEST_CASE("Keys that are not keys have no virtual key", "[hotkey][key-mapping]")
{
    REQUIRE(luil::win32_virtual_from_key(key_code::none) == 0);
    REQUIRE(luil::win32_virtual_from_key(key_code::zoom_in) == 0);
    REQUIRE(luil::win32_virtual_from_key(key_code::zoom_out) == 0);
    REQUIRE(luil::win32_virtual_from_key(key_code::volume_up) == 0);
    REQUIRE(luil::win32_virtual_from_key(key_code::volume_down) == 0);
}
