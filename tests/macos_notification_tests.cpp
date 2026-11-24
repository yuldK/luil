#include "macos/notification_rules.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// macOS OS 알림의 AppKit을 모르는 몫이다 (src/macos/notification_rules.h). 알림 센터를 부르지 않고 권한도 묻지 않는다.
namespace {
    using luil::macos::notification_authorization;
    using luil::macos::notification_response_action;

    [[nodiscard]] luil::macos::notification_settings settings(const notification_authorization authorization, const bool alerts = true, const bool center = true)
    {
        return { authorization, alerts, center };
    }

    [[nodiscard]] luil::notification_event pressed(const std::u8string& id)
    {
        return { luil::notification_event_kind::activated, id };
    }
}

TEST_CASE("The macOS notification settings map to the public availability", "[notification][macos]")
{
    using luil::notification_availability;
    using luil::macos::notification_availability_of;
    CHECK(notification_availability_of(settings(notification_authorization::not_determined)) == notification_availability::permission_needed);
    CHECK(notification_availability_of(settings(notification_authorization::denied)) == notification_availability::blocked);
    CHECK(notification_availability_of(settings(notification_authorization::authorized)) == notification_availability::available);
    CHECK(notification_availability_of(settings(notification_authorization::provisional)) == notification_availability::available);
    CHECK(notification_availability_of(settings(notification_authorization::other)) == notification_availability::available);
    // 배너나 알림 센터 하나만 남아도 보일 자리가 있다. 둘 다 끄면 막힘이다.
    CHECK(notification_availability_of(settings(notification_authorization::authorized, false, true)) == notification_availability::available);
    CHECK(notification_availability_of(settings(notification_authorization::authorized, true, false)) == notification_availability::available);
    CHECK(notification_availability_of(settings(notification_authorization::authorized, false, false)) == notification_availability::blocked);
    CHECK(notification_availability_of(settings(notification_authorization::provisional, false, false)) == notification_availability::blocked);
    // 묻지 않은 앱은 설정 칸과 상관없이 권한이 먼저다.
    CHECK(notification_availability_of(settings(notification_authorization::not_determined, false, false)) == notification_availability::permission_needed);
}

TEST_CASE("The macOS notification authorization maps to the permission state", "[notification][macos]")
{
    using luil::permission_state;
    using luil::macos::notification_permission_of;
    CHECK(notification_permission_of(notification_authorization::not_determined) == permission_state::denied);
    CHECK(notification_permission_of(notification_authorization::denied) == permission_state::denied);
    CHECK(notification_permission_of(notification_authorization::authorized) == permission_state::granted);
    CHECK(notification_permission_of(notification_authorization::provisional) == permission_state::granted);
    CHECK(notification_permission_of(notification_authorization::other) == permission_state::granted);
}

TEST_CASE("A macOS notification request carries the id, the cut text, the sound and the category", "[notification][macos]")
{
    luil::notification value {};
    value.id = u8"demo-1";
    value.title = u8"제목";
    value.body = u8"본문";
    std::u8string error {};
    std::optional<luil::macos::notification_request_content> content { luil::macos::make_notification_request(value, error) };
    REQUIRE(content.has_value());
    CHECK(error.empty());
    CHECK(content->identifier == u8"demo-1");
    CHECK(content->title == u8"제목");
    CHECK(content->body == u8"본문");
    CHECK(content->sound);
    CHECK(content->category == luil::macos::notification_category);

    value.silent = true;
    value.body.clear();
    value.title = std::u8string(300, u8'a');
    content = luil::macos::make_notification_request(value, error);
    REQUIRE(content.has_value());
    CHECK(content->sound == false);
    CHECK(content->body.empty());
    // 제목은 255자에 `…`(3바이트)를 단다.
    CHECK(content->title.size() == 255 + 3);
    CHECK(content->title.ends_with(u8"…"));
}

TEST_CASE("A macOS notification request refuses a bad id or bad UTF-8 without a request", "[notification][macos]")
{
    for (const std::u8string& id : std::vector<std::u8string> { u8"", u8"has space", u8"한글", std::u8string(65, u8'a'), u8"slash/id" })
    {
        luil::notification value {};
        value.id = id;
        value.title = u8"title";
        std::u8string error {};
        CHECK(luil::macos::make_notification_request(value, error).has_value() == false);
        CHECK(error.empty() == false);
    }
    luil::notification value {};
    value.id = u8"ok.id_1-2";
    value.title = std::u8string { u8"bad " } + static_cast<char8_t>(0xFF);
    std::u8string error {};
    CHECK(luil::macos::make_notification_request(value, error).has_value() == false);
    CHECK(error.empty() == false);
    value.title = u8"good";
    value.id = std::u8string(64, u8'z');
    CHECK(luil::macos::make_notification_request(value, error).has_value());
}

TEST_CASE("A macOS notification response becomes an event only for a library id", "[notification][macos]")
{
    using luil::notification_event_kind;
    using luil::macos::notification_event_from_response;
    const std::optional<luil::notification_event> opened { notification_event_from_response(u8"demo-1", notification_response_action::open, true) };
    REQUIRE(opened.has_value());
    CHECK(opened->kind == notification_event_kind::activated);
    CHECK(opened->id == u8"demo-1");
    CHECK(opened->launched);

    // 라이브러리는 단추를 달지 않으므로 다른 동작도 누름이다.
    const std::optional<luil::notification_event> other { notification_event_from_response(u8"demo-1", notification_response_action::other, false) };
    REQUIRE(other.has_value());
    CHECK(other->kind == notification_event_kind::activated);
    CHECK(other->launched == false);

    // 닫음에는 띄움을 싣지 않는다.
    const std::optional<luil::notification_event> dismissed { notification_event_from_response(u8"demo-1", notification_response_action::dismiss, true) };
    REQUIRE(dismissed.has_value());
    CHECK(dismissed->kind == notification_event_kind::dismissed);
    CHECK(dismissed->launched == false);

    // 이 라이브러리의 id 규칙 밖이면(다른 길로 올린 알림) 사건이 없다.
    CHECK(notification_event_from_response(u8"", notification_response_action::open, false).has_value() == false);
    CHECK(notification_event_from_response(u8"5F0C-UUID with space", notification_response_action::open, false).has_value() == false);
}

TEST_CASE("The macOS launch tracker marks only the click that launched the app", "[notification][macos]")
{
    SECTION("a click before launching finished launched the app")
    {
        luil::macos::notification_launch_tracker tracker {};
        CHECK(tracker.finished() == false);
        CHECK(tracker.take());
        tracker.finish_launching(false);
        CHECK(tracker.finished());
        CHECK(tracker.take() == false);
    }
    SECTION("AppKit said the app launched for a notification: the first click after it")
    {
        luil::macos::notification_launch_tracker tracker {};
        tracker.finish_launching(true);
        CHECK(tracker.take());
        CHECK(tracker.take() == false);
    }
    SECTION("an ordinary launch has no launching click and a second report changes nothing")
    {
        luil::macos::notification_launch_tracker tracker {};
        tracker.finish_launching(false);
        tracker.finish_launching(true);
        CHECK(tracker.take() == false);
    }
}

TEST_CASE("The macOS notification relay holds events until a receiver is set", "[notification][macos]")
{
    luil::macos::notification_relay relay {};
    relay.deliver(pressed(u8"a"));
    relay.deliver(pressed(u8"b"));
    CHECK(relay.pending() == 2);

    std::vector<luil::notification_event> received {};
    relay.set_receiver([&received](luil::notification_event event) { received.push_back(std::move(event)); });
    CHECK(relay.pending() == 0);
    REQUIRE(received.size() == 2);
    CHECK(received[0].id == u8"a");
    CHECK(received[1].id == u8"b");

    relay.deliver(pressed(u8"c"));
    REQUIRE(received.size() == 3);
    CHECK(received[2].id == u8"c");

    // 풀면 다시 쥔다.
    relay.set_receiver(nullptr);
    relay.deliver(pressed(u8"d"));
    CHECK(relay.pending() == 1);
    CHECK(received.size() == 3);
}

TEST_CASE("The macOS notification relay counts the events it dropped before a receiver", "[notification][macos]")
{
    luil::macos::notification_relay relay {};
    for (int index = 0; index < 70; ++index)
        relay.deliver(pressed(u8"n" + std::u8string(static_cast<std::size_t>(index % 10) + 1, u8'x')));
    CHECK(relay.pending() == luil::notification_queue::capacity);
    std::vector<luil::notification_event> received {};
    relay.set_receiver([&received](luil::notification_event event) { received.push_back(std::move(event)); });
    REQUIRE(received.size() == luil::notification_queue::capacity);
    CHECK(received.front().lost_before == 6);
    CHECK(received[1].lost_before == 0);
}

TEST_CASE("A throwing macOS notification receiver loses only its event", "[notification][macos]")
{
    luil::macos::notification_relay relay {};
    relay.deliver(pressed(u8"boom"));
    relay.deliver(pressed(u8"next"));
    std::vector<std::u8string> received {};
    relay.set_receiver([&received](luil::notification_event event) {
        if (event.id == u8"boom")
            throw std::runtime_error { "receiver failed" };
        received.push_back(event.id);
    });
    CHECK(relay.pending() == 0);
    REQUIRE(received.size() == 1);
    CHECK(received[0] == u8"next");
}
