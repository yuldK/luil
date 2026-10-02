#include "win32/popup_dismiss.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace {
    using luil::popup_dismiss_reason;
    using luil::win32::take_popup_dismiss_action;

    // 앱이 logic inbox로 나르는 메시지의 대역이다.
    struct fake_intent
    {
        std::u8string name {};
    };

    [[nodiscard]] const fake_intent* intent_of(const std::optional<luil::input_action>& action)
    {
        if (action.has_value() == false)
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&*action) };
        if (message == nullptr)
            return nullptr;
        return message->get<fake_intent>();
    }
} // namespace

TEST_CASE("A dismiss trigger reaches the app with its reason", "[win32][popup]")
{
    // 계기를 구분하는 것이 이 factory를 두는 이유다.
    std::vector<popup_dismiss_reason> seen {};
    const auto dismiss = [&seen](const popup_dismiss_reason reason) {
        seen.push_back(reason);
        return luil::make_app_action(fake_intent { u8"close" });
    };

    bool requested { false };
    const auto action { take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::escape_key) };
    REQUIRE(seen == std::vector<popup_dismiss_reason> { popup_dismiss_reason::escape_key });
    const fake_intent* const intent { intent_of(action) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->name == u8"close");
    REQUIRE(requested);
}

TEST_CASE("A popup without a dismiss factory answers no trigger", "[win32][popup]")
{
    const std::function<luil::input_action(popup_dismiss_reason)> none {};
    bool requested { false };
    REQUIRE(take_popup_dismiss_action(none, requested, popup_dismiss_reason::pointer_press_outside).has_value() == false);
    // 낸 것이 없으므로 표식도 서지 않는다.
    REQUIRE(requested == false);
}

TEST_CASE("An empty action keeps the popup open for that reason", "[win32][popup]")
{
    // 계기마다 갈리는 popup이다: 창이 움직여도 남고, 밖을 누르면 닫는다.
    std::vector<popup_dismiss_reason> asked {};
    const auto dismiss = [&asked](const popup_dismiss_reason reason) {
        asked.push_back(reason);
        if (reason == popup_dismiss_reason::surface_moved)
            return luil::input_action {};
        return luil::make_app_action(fake_intent { u8"close" });
    };

    bool requested { false };
    REQUIRE(take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::surface_moved).has_value() == false);
    // 낸 것이 없으므로 표식이 서지 않는다 — 같은 frame의 다음 계기를 막지 않는다.
    REQUIRE(requested == false);
    REQUIRE(take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::pointer_press_outside).has_value());
    REQUIRE(requested);
    REQUIRE(asked == std::vector<popup_dismiss_reason> { popup_dismiss_reason::surface_moved, popup_dismiss_reason::pointer_press_outside });
}

TEST_CASE("A popup asks to close once until the next frame keeps it", "[win32][popup]")
{
    int calls { 0 };
    const auto dismiss = [&calls](const popup_dismiss_reason) {
        ++calls;
        return luil::make_app_action(fake_intent { u8"close" });
    };

    bool requested { false };
    REQUIRE(take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::pointer_press_outside).has_value());
    // 같은 frame의 다음 계기는 이미 낸 것을 되풀이하지 않는다.
    REQUIRE(take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::escape_key).has_value() == false);
    REQUIRE(calls == 1);

    // 새 frame이 popup을 계속 실었다는 것은 앱이 유지하기로 했다는 뜻이라
    // 소유자가 표식을 푼다 (`popup_surface::adopt`).
    requested = false;
    REQUIRE(take_popup_dismiss_action(dismiss, requested, popup_dismiss_reason::escape_key).has_value());
    REQUIRE(calls == 2);
}
