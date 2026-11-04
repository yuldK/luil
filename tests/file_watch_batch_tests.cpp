#include "host/file_watch_batch.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

// 파일 감시의 OS를 모르는 몫을 시계 없이 본다 (file-watch-design.md의 "묶음 짓기"). 시각은 test가 준다.

namespace {
    using clock = luil::file_watch_batch::clock;
    using namespace std::chrono_literals;
    using luil::file_change;
    using luil::file_change_kind;

    const clock::time_point start { clock::time_point {} + 1h };
    constexpr luil::file_watch_ticket ticket { 7 };

    [[nodiscard]] luil::file_watch_batch make_batch(const std::size_t max_changes = 4096)
    {
        return luil::file_watch_batch { luil::file_watch_timing { 100ms, 1000ms, max_changes } };
    }

    // 낼 때가 된 묶음을 받는다. 받은 것으로 친다.
    [[nodiscard]] std::optional<luil::file_change_batch> take(luil::file_watch_batch& batch, const clock::time_point now)
    {
        std::optional<luil::file_change_batch> received {};
        const auto accept = [&received](luil::file_change_batch value) {
            received = std::move(value);
            return true;
        };
        luil::offer_file_change_batch(batch, ticket, accept, now);
        return received;
    }

    [[nodiscard]] std::vector<file_change> changes_after_settle(luil::file_watch_batch& batch)
    {
        const std::optional<luil::file_change_batch> received { take(batch, start + 10s) };
        REQUIRE(received.has_value());
        return received->changes;
    }

    [[nodiscard]] file_change created(std::u8string path)
    {
        return { file_change_kind::created, std::move(path), {} };
    }

    [[nodiscard]] file_change modified(std::u8string path)
    {
        return { file_change_kind::modified, std::move(path), {} };
    }

    [[nodiscard]] file_change removed(std::u8string path)
    {
        return { file_change_kind::removed, std::move(path), {} };
    }

    [[nodiscard]] file_change renamed(std::u8string from, std::u8string to)
    {
        return { file_change_kind::renamed, std::move(to), std::move(from) };
    }
}

TEST_CASE("File watch batches merge each pair of the coalescing table", "[file_watch]")
{
    SECTION("created then modified is created")
    {
        auto batch { make_batch() };
        batch.created(u8"a", start);
        batch.modified(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"a") });
    }
    SECTION("created then removed is nothing")
    {
        auto batch { make_batch() };
        batch.created(u8"a", start);
        batch.removed(u8"a", start);
        // 낼 것이 없으므로 묶음이 오지 않는다.
        REQUIRE(batch.due_at().has_value() == false);
        REQUIRE(take(batch, start + 10s).has_value() == false);
    }
    SECTION("modified then modified is modified")
    {
        auto batch { make_batch() };
        batch.modified(u8"a", start);
        batch.modified(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { modified(u8"a") });
    }
    SECTION("modified then removed is removed")
    {
        auto batch { make_batch() };
        batch.modified(u8"a", start);
        batch.removed(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { removed(u8"a") });
    }
    SECTION("removed then created is modified")
    {
        auto batch { make_batch() };
        batch.removed(u8"a", start);
        batch.created(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { modified(u8"a") });
    }
    SECTION("created then created is created")
    {
        auto batch { make_batch() };
        batch.created(u8"a", start);
        batch.created(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"a") });
    }
    SECTION("created A then renamed A to B is created B")
    {
        auto batch { make_batch() };
        batch.created(u8"a", start);
        batch.renamed(u8"a", u8"b", start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"b") });
    }
    SECTION("modified A then renamed A to B is renamed")
    {
        auto batch { make_batch() };
        batch.modified(u8"a", start);
        batch.renamed(u8"a", u8"b", start);
        REQUIRE(changes_after_settle(batch) == std::vector { renamed(u8"a", u8"b") });
    }
    SECTION("removed B then renamed A to B is renamed")
    {
        auto batch { make_batch() };
        batch.removed(u8"b", start);
        batch.renamed(u8"a", u8"b", start);
        REQUIRE(changes_after_settle(batch) == std::vector { renamed(u8"a", u8"b") });
    }
    SECTION("renamed A to B then modified B is renamed")
    {
        auto batch { make_batch() };
        batch.renamed(u8"a", u8"b", start);
        batch.modified(u8"b", start);
        REQUIRE(changes_after_settle(batch) == std::vector { renamed(u8"a", u8"b") });
    }
    SECTION("renamed A to B then renamed B to C is renamed A to C")
    {
        auto batch { make_batch() };
        batch.renamed(u8"a", u8"b", start);
        batch.renamed(u8"b", u8"c", start);
        REQUIRE(changes_after_settle(batch) == std::vector { renamed(u8"a", u8"c") });
    }
    SECTION("renamed A to B then removed B is removed A")
    {
        auto batch { make_batch() };
        batch.renamed(u8"a", u8"b", start);
        batch.removed(u8"b", start);
        REQUIRE(changes_after_settle(batch) == std::vector { removed(u8"a") });
    }
    SECTION("renamed A to B then created A is modified A and created B")
    {
        // 편집기의 백업 이름 저장이다 (a를 a~로 옮기고 새 a를 쓴다). 순 변화로 풀어 한 경로가 한 번만 나온다.
        auto batch { make_batch() };
        batch.renamed(u8"a", u8"a~", start);
        batch.created(u8"a", start);
        batch.modified(u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"a~"), modified(u8"a") });
    }
    SECTION("renaming back to the first name is modified")
    {
        auto batch { make_batch() };
        batch.renamed(u8"a", u8"b", start);
        batch.renamed(u8"b", u8"a", start);
        REQUIRE(changes_after_settle(batch) == std::vector { modified(u8"a") });
    }
}

TEST_CASE("File watch batches fold a temporary-file save into one change", "[file_watch]")
{
    SECTION("rename over an existing file reported as removed first")
    {
        // Windows의 `MoveFileExW(REPLACE_EXISTING)` 모양이다.
        auto batch { make_batch() };
        batch.created(u8"a.json.1.tmp", start);
        batch.modified(u8"a.json.1.tmp", start);
        batch.removed(u8"a.json", start);
        batch.moved_from(u8"a.json.1.tmp", 1, start);
        batch.moved_to(u8"a.json", 1, start);
        REQUIRE(changes_after_settle(batch) == std::vector { modified(u8"a.json") });
    }
    SECTION("rename over an existing file without a removal")
    {
        // inotify의 `rename(2)` 모양이다. 덮인 파일의 지움 알림이 없다.
        auto batch { make_batch() };
        batch.created(u8"a.json.1.tmp", start);
        batch.modified(u8"a.json.1.tmp", start);
        batch.moved_from(u8"a.json.1.tmp", 42, start);
        batch.moved_to(u8"a.json", 42, start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"a.json") });
    }
}

TEST_CASE("File watch batches pair rename halves and resolve the lonely ones", "[file_watch]")
{
    SECTION("an old name with no new name is removed")
    {
        auto batch { make_batch() };
        batch.moved_from(u8"out", 3, start);
        REQUIRE(changes_after_settle(batch) == std::vector { removed(u8"out") });
    }
    SECTION("a new name with no old name is created")
    {
        auto batch { make_batch() };
        batch.moved_to(u8"in", 3, start);
        REQUIRE(changes_after_settle(batch) == std::vector { created(u8"in") });
    }
    SECTION("a different cookie does not pair")
    {
        auto batch { make_batch() };
        batch.moved_from(u8"out", 3, start);
        batch.moved_to(u8"in", 4, start);
        REQUIRE(changes_after_settle(batch) == std::vector { removed(u8"out"), created(u8"in") });
    }
    SECTION("an event between the halves breaks the pair")
    {
        auto batch { make_batch() };
        batch.moved_from(u8"out", 3, start);
        batch.modified(u8"other", start);
        batch.moved_to(u8"in", 3, start);
        REQUIRE(changes_after_settle(batch) == std::vector { removed(u8"out"), modified(u8"other"), created(u8"in") });
    }
    SECTION("chained renames through halves")
    {
        auto batch { make_batch() };
        batch.moved_from(u8"a", 1, start);
        batch.moved_to(u8"b", 1, start);
        batch.moved_from(u8"b", 2, start);
        batch.moved_to(u8"c", 2, start);
        REQUIRE(changes_after_settle(batch) == std::vector { renamed(u8"a", u8"c") });
    }
}

TEST_CASE("File watch batches wait for settle and are capped by max delay", "[file_watch]")
{
    auto batch { make_batch() };
    REQUIRE(batch.due_at().has_value() == false);
    batch.modified(u8"a", start);
    REQUIRE(batch.due_at() == start + 100ms);
    // 조용해지기 전에는 내지 않는다.
    REQUIRE(take(batch, start + 99ms).has_value() == false);
    // 변경이 이어지면 settle이 밀린다.
    batch.modified(u8"b", start + 90ms);
    REQUIRE(batch.due_at() == start + 190ms);
    REQUIRE(take(batch, start + 189ms).has_value() == false);
    REQUIRE(take(batch, start + 190ms).has_value());
    REQUIRE(batch.due_at().has_value() == false);

    // 쉬지 않고 바뀌어도 첫 변경 뒤 max_delay에는 낸다.
    const clock::time_point second { start + 10s };
    for (int step { 0 }; step <= 20; ++step)
        batch.modified(u8"log", second + step * 50ms);
    REQUIRE(batch.due_at() == second + 1000ms);
    const std::optional<luil::file_change_batch> received { take(batch, second + 1000ms) };
    REQUIRE(received.has_value());
    REQUIRE(received->changes == std::vector { modified(u8"log") });
}

TEST_CASE("File watch batches that came and went make the next change wait for settle", "[file_watch]")
{
    auto batch { make_batch() };
    const clock::time_point next { start + 5s };

    // 비어 버린 묶음의 시각이 남으면 `max_delay`가 이미 지나 있어 다음 변경이 조용해지기를 기다리지 않고 나간다.
    const auto require_next_waits_for_settle = [&batch, next]() {
        REQUIRE(batch.due_at().has_value() == false);
        batch.modified(u8"b", next);
        REQUIRE(batch.due_at() == next + 100ms);
        REQUIRE(take(batch, next + 99ms).has_value() == false);
        const std::optional<luil::file_change_batch> received { take(batch, next + 100ms) };
        REQUIRE(received.has_value());
        REQUIRE(received->changes == std::vector { modified(u8"b") });
    };

    SECTION("Created then removed")
    {
        batch.created(u8"a", start);
        batch.removed(u8"a", start + 50ms);
        require_next_waits_for_settle();
    }
    SECTION("Created, renamed, then removed")
    {
        batch.created(u8"a", start);
        batch.renamed(u8"a", u8"c", start + 20ms);
        batch.removed(u8"c", start + 50ms);
        require_next_waits_for_settle();
    }
    SECTION("A rejected batch that came and went")
    {
        batch.created(u8"a", start);
        const auto reject = [](luil::file_change_batch) {
            return false;
        };
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.removed(u8"a", start + 150ms);
        require_next_waits_for_settle();
    }
    SECTION("A lonely old name resolved by the removal that empties the batch")
    {
        batch.created(u8"a", start);
        batch.created(u8"b", start + 10ms);
        batch.moved_from(u8"a", 1, start + 20ms);
        batch.removed(u8"b", start + 50ms);
        require_next_waits_for_settle();
    }
}

TEST_CASE("File watch batches keep their times while something still waits", "[file_watch]")
{
    auto batch { make_batch() };
    batch.created(u8"a", start);

    SECTION("Other entries are left")
    {
        batch.modified(u8"b", start + 10ms);
        batch.removed(u8"a", start + 50ms);
        REQUIRE(batch.due_at() == start + 150ms);
        // 변경이 이어지면 처음 시각이 `max_delay`를 잰다.
        batch.modified(u8"b", start + 950ms);
        REQUIRE(batch.due_at() == start + 1000ms);
    }
    SECTION("The removal is swallowed by a rescan")
    {
        batch.overflow(start + 10ms);
        batch.removed(u8"a", start + 50ms);
        REQUIRE(batch.due_at() == start + 150ms);
    }
    SECTION("A new old name waits after the last entry went")
    {
        // 앞의 옛 이름이 풀리며 항목이 사라져도 새 옛 이름이 기다리므로 묶음이 끝나지 않는다.
        batch.moved_from(u8"a", 1, start + 20ms);
        batch.moved_from(u8"b", 2, start + 50ms);
        REQUIRE(batch.due_at() == start + 150ms);
    }
    SECTION("The next half of a rename arrives after the last entry went")
    {
        batch.moved_from(u8"a", 1, start + 20ms);
        batch.moved_to(u8"b", 2, start + 50ms);
        REQUIRE(batch.due_at() == start + 150ms);
    }
}

TEST_CASE("File watch batches that only come and go keep no slots", "[file_watch]")
{
    auto batch { make_batch() };
    const auto come_and_go = [&batch]() {
        for (int index { 0 }; index < 1000; ++index)
        {
            batch.created(u8"a", start + 1ms * index);
            batch.removed(u8"a", start + 1ms * index);
        }
    };

    SECTION("Nothing else waits")
    {
        // 낼 것이 없어 `prepare`가 돌지 않고 `max_changes`도 지운 자리를 세지 않는다. 자리를 남기면 이런 변경만 이어지는 폴더에서 칸이 끝없이 는다.
        come_and_go();
        REQUIRE(batch.size() == 0u);
        REQUIRE(batch.slots() == 0u);
        REQUIRE(batch.due_at().has_value() == false);

        // 비운 뒤의 변경도 그대로 모인다.
        batch.modified(u8"b", start + 2s);
        REQUIRE(batch.slots() == 1u);
        REQUIRE(batch.due_at() == start + 2s + 100ms);
    }
    SECTION("Something else waits")
    {
        // 낼 것이 남아 있으면 비우지 않는다. 그 묶음을 받아들일 때 칸도 함께 빈다. 거절을 이어 가는 동안에는 칸이 남는다.
        batch.modified(u8"b", start);
        come_and_go();
        REQUIRE(batch.size() == 1u);
        const std::optional<luil::file_change_batch> received { take(batch, start + 10s) };
        REQUIRE(received.has_value());
        REQUIRE(received->changes == std::vector { modified(u8"b") });
        REQUIRE(batch.slots() == 0u);
    }
}

TEST_CASE("File watch batches over max changes become a rescan", "[file_watch]")
{
    auto batch { make_batch(3) };
    batch.created(u8"1", start);
    batch.created(u8"2", start);
    batch.created(u8"3", start);
    batch.created(u8"4", start);
    // 넘친 뒤의 변경은 담지 않는다. 다시 훑으면 보인다.
    batch.created(u8"5", start);
    const std::optional<luil::file_change_batch> received { take(batch, start + 10s) };
    REQUIRE(received.has_value());
    REQUIRE(received->rescan);
    REQUIRE(received->changes.empty());

    // 받은 뒤에는 새로 모은다.
    batch.created(u8"6", start + 20s);
    const std::optional<luil::file_change_batch> next { take(batch, start + 30s) };
    REQUIRE(next.has_value());
    REQUIRE(next->rescan == false);
    REQUIRE(next->changes == std::vector { created(u8"6") });
}

TEST_CASE("File watch batches mark an OS overflow as a rescan", "[file_watch]")
{
    auto batch { make_batch() };
    batch.created(u8"a", start);
    batch.overflow(start);
    const std::optional<luil::file_change_batch> received { take(batch, start + 10s) };
    REQUIRE(received.has_value());
    REQUIRE(received->rescan);
    REQUIRE(received->changes.empty());
    REQUIRE(received->ticket == ticket);
}

TEST_CASE("File watch batches keep a rejected batch and merge it into the next", "[file_watch]")
{
    auto batch { make_batch() };
    batch.created(u8"a", start);
    int calls { 0 };
    const auto reject = [&calls](luil::file_change_batch) {
        ++calls;
        return false;
    };
    luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
    REQUIRE(calls == 1);
    // 새 변경이 없어도 settle 뒤에 다시 낸다.
    REQUIRE(batch.due_at() == start + 200ms);

    // 그 사이의 변경과 합쳐진다.
    batch.modified(u8"a", start + 150ms);
    batch.created(u8"b", start + 150ms);
    const auto throwing = [&calls](luil::file_change_batch) -> bool {
        ++calls;
        throw 1;
    };
    // 던져도 거절로 본다.
    luil::offer_file_change_batch(batch, ticket, throwing, start + 250ms);
    REQUIRE(calls == 2);
    REQUIRE(batch.due_at() == start + 350ms);

    const std::optional<luil::file_change_batch> received { take(batch, start + 350ms) };
    REQUIRE(received.has_value());
    REQUIRE(received->changes == std::vector { created(u8"a"), created(u8"b") });
    REQUIRE(batch.due_at().has_value() == false);
}

TEST_CASE("File watch batches say when they offer a rejected batch again", "[file_watch]")
{
    auto batch { make_batch() };
    bool offered_again { true };
    const auto reject = [&offered_again](const luil::file_change_batch& offered) {
        offered_again = offered.retried;
        return false;
    };
    batch.set_name_filter([](const std::u8string_view name) { return name == u8"a"; });
    batch.created(u8"a", start);

    // 처음 내는 묶음은 거짓이다.
    luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
    REQUIRE(offered_again == false);

    SECTION("Until it is accepted")
    {
        luil::offer_file_change_batch(batch, ticket, reject, start + 200ms);
        REQUIRE(offered_again);
        // 새 변경과 합쳐도 그 묶음이다.
        batch.modified(u8"a", start + 250ms);
        const std::optional<luil::file_change_batch> received { take(batch, start + 350ms) };
        REQUIRE(received.has_value());
        REQUIRE(received->retried);
    }
    SECTION("Even when it became a rescan")
    {
        batch.overflow(start + 150ms);
        const std::optional<luil::file_change_batch> received { take(batch, start + 1s) };
        REQUIRE(received.has_value());
        REQUIRE(received->rescan);
        REQUIRE(received->retried);
    }
    SECTION("Not after it came and went")
    {
        batch.removed(u8"a", start + 150ms);
    }
    SECTION("Not after the filter emptied it")
    {
        // 다른 이름만 남으면 낼 것이 없어 거절한 묶음은 사라진다.
        batch.modified(u8"other", start + 120ms);
        batch.removed(u8"a", start + 150ms);
        REQUIRE(take(batch, start + 1s).has_value() == false);
    }

    // 어느 길이든 그 뒤의 묶음은 새것이다.
    batch.modified(u8"a", start + 5s);
    const std::optional<luil::file_change_batch> next { take(batch, start + 6s) };
    REQUIRE(next.has_value());
    REQUIRE(next->retried == false);
}

TEST_CASE("File watch batches with a name filter judge a rejected batch by that name", "[file_watch]")
{
    auto batch { make_batch() };
    batch.set_name_filter([](const std::u8string_view name) { return name == u8"a"; });
    const auto reject = [](luil::file_change_batch) {
        return false;
    };
    const auto next_batch = [&batch]() {
        std::optional<luil::file_change_batch> received { take(batch, start + 1s) };
        REQUIRE(received.has_value());
        return *std::move(received);
    };

    SECTION("The name came and went while another name was held with it")
    {
        // 쥔 것이 사라졌다. 앱에 나간 적 없는 옆 이름이 남아 묶음이 비지 않아도 다시 생긴 그 이름은 새것이다.
        batch.created(u8"a", start);
        batch.created(u8"other", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.removed(u8"a", start + 120ms);
        // 항목과 시각은 그대로다. 표식만 풀린다.
        REQUIRE(batch.size() == 1u);
        REQUIRE(batch.due_at() == start + 220ms);
        batch.created(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { created(u8"a") });
        REQUIRE(received.retried == false);
    }
    SECTION("The name came and went while another name arrived after the rejection")
    {
        batch.created(u8"a", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.modified(u8"other", start + 120ms);
        batch.removed(u8"a", start + 150ms);
        batch.created(u8"a", start + 180ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { created(u8"a") });
        REQUIRE(received.retried == false);
    }
    SECTION("A created name moved away and a new one took its place")
    {
        // 생긴 파일이 다른 이름으로 갔다. 그 이름의 순 변화는 없다.
        batch.created(u8"a", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.renamed(u8"a", u8"a.bak", start + 120ms);
        batch.created(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { created(u8"a") });
        REQUIRE(received.retried == false);
    }
    SECTION("The same move reported in two halves")
    {
        batch.created(u8"a", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.moved_from(u8"a", 1, start + 120ms);
        batch.moved_to(u8"a.bak", 1, start + 120ms);
        batch.created(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { created(u8"a") });
        REQUIRE(received.retried == false);
    }
    SECTION("A lonely old half resolved by the next event")
    {
        batch.created(u8"a", start);
        batch.created(u8"other", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.moved_from(u8"a", 1, start + 120ms);
        batch.created(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { created(u8"a") });
        REQUIRE(received.retried == false);
    }
    SECTION("A changed name that moved away stays as the old name")
    {
        // 있던 파일이 떠난 것은 순 변화다. 옛 이름으로 남아 쥔 것이 이어진다.
        batch.modified(u8"a", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.renamed(u8"a", u8"a.bak", start + 120ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { renamed(u8"a", u8"a.bak") });
        REQUIRE(received.retried);
    }
    SECTION("A changed name that was removed and written again is still held")
    {
        batch.modified(u8"a", start);
        batch.modified(u8"other", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.removed(u8"a", start + 120ms);
        batch.created(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.changes == std::vector { modified(u8"a") });
        REQUIRE(received.retried);
    }
    SECTION("A rescan keeps it because it holds no entries to judge")
    {
        batch.created(u8"a", start);
        batch.created(u8"other", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.overflow(start + 120ms);
        batch.removed(u8"a", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.rescan);
        REQUIRE(received.retried);
    }
    SECTION("An end batch keeps it")
    {
        // 끝 묶음을 지을 때 풀리는 반쪽이 그 이름을 지워도 끝 묶음은 낸다.
        batch.created(u8"a", start);
        batch.created(u8"other", start);
        luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);
        batch.moved_from(u8"a", 1, start + 120ms);
        batch.finish(luil::file_watch_end::lost, u8"gone", start + 150ms);
        const luil::file_change_batch received { next_batch() };
        REQUIRE(received.end == luil::file_watch_end::lost);
        REQUIRE(received.changes.empty());
        REQUIRE(received.retried);
    }
}

TEST_CASE("File watch batches without a name filter judge a rejected batch as a whole", "[file_watch]")
{
    auto batch { make_batch() };
    const auto reject = [](luil::file_change_batch) {
        return false;
    };
    batch.created(u8"a", start);
    luil::offer_file_change_batch(batch, ticket, reject, start + 100ms);

    SECTION("Another entry is left")
    {
        // 모두 앱에 나갈 항목이다. 항목마다 세지 않으므로 하나가 사라져도 묶음은 이어진다.
        batch.created(u8"b", start + 120ms);
        batch.removed(u8"a", start + 150ms);
        batch.created(u8"a", start + 180ms);
        const std::optional<luil::file_change_batch> received { take(batch, start + 1s) };
        REQUIRE(received.has_value());
        REQUIRE(received->changes == std::vector { created(u8"b"), created(u8"a") });
        REQUIRE(received->retried);
    }
    SECTION("A created name moved away")
    {
        batch.renamed(u8"a", u8"b", start + 120ms);
        const std::optional<luil::file_change_batch> received { take(batch, start + 1s) };
        REQUIRE(received.has_value());
        REQUIRE(received->changes == std::vector { created(u8"b") });
        REQUIRE(received->retried);
    }
    SECTION("Nothing is left")
    {
        batch.removed(u8"a", start + 120ms);
        batch.created(u8"a", start + 150ms);
        const std::optional<luil::file_change_batch> received { take(batch, start + 1s) };
        REQUIRE(received.has_value());
        REQUIRE(received->changes == std::vector { created(u8"a") });
        REQUIRE(received->retried == false);
    }
}

TEST_CASE("File watch batches over the limit after a rejection become a rescan", "[file_watch]")
{
    auto batch { make_batch(2) };
    batch.created(u8"a", start);
    batch.created(u8"b", start);
    luil::offer_file_change_batch(batch, ticket, [](luil::file_change_batch) { return false; }, start + 100ms);
    batch.created(u8"c", start + 120ms);
    const std::optional<luil::file_change_batch> received { take(batch, start + 10s) };
    REQUIRE(received.has_value());
    REQUIRE(received->rescan);
    REQUIRE(received->changes.empty());
}

TEST_CASE("File watch batches send exactly one end batch", "[file_watch]")
{
    auto batch { make_batch() };
    batch.modified(u8"a", start);
    batch.finish(luil::file_watch_end::removed, {}, start + 10ms);
    // 끝 묶음은 settle을 기다리지 않는다.
    REQUIRE(batch.due_at() == start + 10ms);
    // 두 번째 끝과 끝 뒤의 변경은 받지 않는다.
    batch.finish(luil::file_watch_end::lost, u8"late", start + 11ms);
    batch.created(u8"late", start + 12ms);

    luil::offer_file_change_batch(batch, ticket, [](luil::file_change_batch) { return false; }, start + 20ms);
    REQUIRE(batch.closed() == false);
    // 거절된 끝 묶음도 settle 뒤에 다시 낸다.
    REQUIRE(batch.due_at() == start + 120ms);
    const std::optional<luil::file_change_batch> received { take(batch, start + 120ms) };
    REQUIRE(received.has_value());
    REQUIRE(received->end == luil::file_watch_end::removed);
    REQUIRE(received->error.empty());
    REQUIRE(received->changes == std::vector { modified(u8"a") });
    REQUIRE(batch.closed());
    REQUIRE(batch.due_at().has_value() == false);
    REQUIRE(take(batch, start + 10s).has_value() == false);
}

TEST_CASE("File watch batches filter one file after merging", "[file_watch]")
{
    auto batch { make_batch() };
    batch.set_name_filter([](const std::u8string_view name) { return name == u8"a.json"; });
    batch.created(u8"a.json.tmp", start);
    batch.modified(u8"a.json.tmp", start);
    batch.modified(u8"other.txt", start);
    batch.moved_from(u8"a.json.tmp", 5, start);
    batch.moved_to(u8"a.json", 5, start);
    REQUIRE(changes_after_settle(batch) == std::vector { created(u8"a.json") });

    // 다른 이름만 바뀐 묶음은 오지 않는다.
    batch.modified(u8"other.txt", start + 20s);
    REQUIRE(take(batch, start + 30s).has_value() == false);
    REQUIRE(batch.due_at().has_value() == false);

    // 그 이름이 옛 이름인 이름 바꿈은 남는다 ("지금은 없다").
    batch.renamed(u8"a.json", u8"a.json.bak", start + 40s);
    const std::optional<luil::file_change_batch> received { take(batch, start + 50s) };
    REQUIRE(received.has_value());
    REQUIRE(received->changes == std::vector { renamed(u8"a.json", u8"a.json.bak") });
}

TEST_CASE("File watch paths use forward slashes", "[file_watch]")
{
    REQUIRE(luil::to_watch_path(u8"sub\\folder\\a.txt") == u8"sub/folder/a.txt");
    REQUIRE(luil::to_watch_path(u8"sub/a.txt") == u8"sub/a.txt");
    REQUIRE(luil::to_watch_path(u8"한글\\이름.txt") == u8"한글/이름.txt");
}
