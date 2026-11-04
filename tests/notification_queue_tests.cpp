#include "host/notification_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// OS 알림의 플랫폼을 가리지 않는 몫을 OS 없이 본다 (os-notification-design.md 결정 8과 "올리기").

namespace {
    [[nodiscard]] luil::notification_event pressed(const std::u8string& id)
    {
        return { luil::notification_event_kind::activated, id };
    }

    [[nodiscard]] std::u8string numbered(const int number)
    {
        const std::string text { "n" + std::to_string(number) };
        return std::u8string { text.begin(), text.end() };
    }

    // 코드 포인트 수다. test 입력은 올바른 UTF-8이다.
    [[nodiscard]] std::size_t code_points(const std::u8string& text)
    {
        std::size_t count { 0 };
        for (const char8_t byte : text)
        {
            if ((byte & 0xC0) != 0x80)
                ++count;
        }
        return count;
    }
}

TEST_CASE("The notification queue keeps the order and puts a refused event back in front", "[notification][queue]")
{
    luil::notification_queue queue {};
    REQUIRE(queue.empty());
    queue.push(pressed(u8"a"));
    queue.push(pressed(u8"b"));
    queue.push(pressed(u8"c"));
    REQUIRE(queue.size() == 3);

    std::optional<luil::notification_event> first { queue.take() };
    REQUIRE(first.has_value());
    REQUIRE(first->id == u8"a");
    // inbox가 차서 내지 못했다. 되돌린 것이 다음에 가장 먼저 나간다.
    queue.put_back(std::move(*first));
    REQUIRE(queue.take()->id == u8"a");
    REQUIRE(queue.take()->id == u8"b");
    REQUIRE(queue.take()->id == u8"c");
    REQUIRE(queue.take().has_value() == false);
}

TEST_CASE("Past 64 events the oldest are dropped and counted on the next one", "[notification][queue]")
{
    luil::notification_queue queue {};
    for (int number = 0; number < 70; ++number)
        queue.push(pressed(numbered(number)));
    REQUIRE(queue.size() == luil::notification_queue::capacity);

    // 0~5를 버렸다. 남은 가장 오래된 사건이 그 수를 든다.
    const std::optional<luil::notification_event> oldest { queue.take() };
    REQUIRE(oldest->id == u8"n6");
    REQUIRE(oldest->lost_before == 6);
    // 그 뒤의 사건은 버린 것이 없다.
    const std::optional<luil::notification_event> next { queue.take() };
    REQUIRE(next->id == u8"n7");
    REQUIRE(next->lost_before == 0);

    // 센 사건을 되돌려도 수가 그대로다. 내고 나면 다음 사건부터 0이다.
    queue.put_back(*oldest);
    REQUIRE(queue.take()->lost_before == 6);
    REQUIRE(queue.take()->lost_before == 0);
}

TEST_CASE("Delivery stops at a refused post and resumes with the same event", "[notification][queue]")
{
    luil::notification_queue queue {};
    queue.push(pressed(u8"a"));
    queue.push({ luil::notification_event_kind::dismissed, u8"b" });
    queue.push(pressed(u8"c"));

    std::vector<std::u8string> made {};
    const auto make = [&made](const luil::notification_event& event) {
        made.push_back(event.id);
        return luil::app_message { event };
    };
    // 둘째 게시에서 inbox가 찼다.
    int posts { 0 };
    const luil::notification_delivery stalled { luil::deliver_notification_events(queue, make, [&posts](luil::app_message) { return ++posts == 2 ? std::uint64_t { 0 } : std::uint64_t { 7 }; }) };
    REQUIRE(stalled.delivered == 1);
    REQUIRE(stalled.activated);
    REQUIRE(stalled.stalled);
    REQUIRE(queue.size() == 2);

    // 다음 frame에 같은 사건부터 다시 낸다. 메시지는 다시 짓는다.
    std::vector<luil::notification_event> posted {};
    const auto record = [&posted](luil::app_message message) {
        posted.push_back(*message.get<luil::notification_event>());
        return std::uint64_t { 1 };
    };
    const luil::notification_delivery resumed { luil::deliver_notification_events(queue, make, record) };
    REQUIRE(resumed.delivered == 2);
    REQUIRE(resumed.stalled == false);
    REQUIRE(posted.size() == 2);
    REQUIRE(posted[0].id == u8"b");
    REQUIRE(posted[1].id == u8"c");
    REQUIRE(made == std::vector<std::u8string> { u8"a", u8"b", u8"b", u8"c" });
    REQUIRE(queue.empty());
}

TEST_CASE("An empty message uses the event up and a throwing hook drops only its event", "[notification][queue]")
{
    luil::notification_queue queue {};
    queue.push(pressed(u8"quiet"));
    queue.push(pressed(u8"throws"));
    queue.push({ luil::notification_event_kind::dismissed, u8"after" });
    int posts { 0 };
    const auto make = [](const luil::notification_event& event) -> luil::app_message {
        if (event.id == u8"throws")
            throw std::runtime_error { "The hook fails on purpose." };
        if (event.id == u8"quiet")
            return {};
        return luil::app_message { event };
    };
    const auto count = [&posts](luil::app_message) {
        ++posts;
        return std::uint64_t { 1 };
    };
    const luil::notification_delivery delivery { luil::deliver_notification_events(queue, make, count) };
    REQUIRE(delivery.delivered == 2);
    REQUIRE(delivery.failed_calls == 1);
    REQUIRE(delivery.activated);
    REQUIRE(posts == 1);
    REQUIRE(queue.empty());
}

TEST_CASE("Notification ids are ASCII keys of one to sixty-four characters", "[notification][rules]")
{
    REQUIRE(luil::notification_id_valid(u8"demo-1"));
    REQUIRE(luil::notification_id_valid(u8"a.B_9-z"));
    REQUIRE(luil::notification_id_valid(std::u8string(64, u8'x')));
    REQUIRE(luil::notification_id_valid(std::u8string(65, u8'x')) == false);
    REQUIRE(luil::notification_id_valid(u8"") == false);
    REQUIRE(luil::notification_id_valid(u8"two words") == false);
    REQUIRE(luil::notification_id_valid(u8"알림") == false);
    REQUIRE(luil::notification_id_valid(u8"a:b") == false);
    REQUIRE(luil::notification_id_valid(u8"a/b") == false);
}

TEST_CASE("Titles and bodies are cut on code point boundaries and end with an ellipsis", "[notification][rules]")
{
    // 상한 안이면 그대로다.
    REQUIRE(luil::truncate_code_points(u8"abc", 3) == u8"abc");
    // 넘으면 앞의 limit - 1개에 말줄임표를 단다. 결과가 상한을 넘지 않는다.
    REQUIRE(luil::truncate_code_points(u8"abcd", 3) == u8"ab…");
    // 여러 바이트 글자를 가르지 않는다.
    REQUIRE(luil::truncate_code_points(u8"가나다라", 3) == u8"가나…");
    REQUIRE(luil::truncate_code_points(u8"\U0001F600\U0001F600\U0001F600", 2) == u8"\U0001F600…");
    REQUIRE(luil::truncate_code_points(u8"ab", 1) == u8"…");
    REQUIRE(luil::truncate_code_points(u8"ab", 0).empty());

    luil::notification value {};
    value.id = u8"long";
    value.title = std::u8string(300, u8't');
    value.body = std::u8string(1024, u8'b');
    std::u8string error {};
    const std::optional<luil::notification_text> text { luil::prepare_notification_text(value, error) };
    REQUIRE(text.has_value());
    REQUIRE(code_points(text->title) == luil::notification_title_limit);
    REQUIRE(text->title.ends_with(u8"…"));
    // 본문은 딱 상한이라 자르지 않는다.
    REQUIRE(text->body == value.body);
}

TEST_CASE("A notification with a bad id or broken UTF-8 is refused before the OS", "[notification][rules]")
{
    std::u8string error {};
    luil::notification value {};
    value.id = u8"bad id";
    value.title = u8"title";
    REQUIRE(luil::prepare_notification_text(value, error).has_value() == false);
    REQUIRE(error.empty() == false);

    error.clear();
    value.id = u8"good";
    value.body = std::u8string { u8"broken " } + static_cast<char8_t>(0xC3);
    REQUIRE(luil::prepare_notification_text(value, error).has_value() == false);
    REQUIRE(error.empty() == false);
}

TEST_CASE("The pending intent request code is stable per id and differs between ids", "[notification][rules]")
{
    REQUIRE(luil::notification_request_code(u8"demo-1") == luil::notification_request_code(u8"demo-1"));
    REQUIRE(luil::notification_request_code(u8"demo-1") != luil::notification_request_code(u8"demo-2"));
}
