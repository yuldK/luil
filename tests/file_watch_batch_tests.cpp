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
