#include "win32/layered_placement.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>

namespace {
    using luil::win32::deferred_placements;
    using luil::win32::screen_bounds;

    constexpr std::chrono::milliseconds timeout { 250 };

    [[nodiscard]] deferred_placements::clock::time_point at(const int milliseconds)
    {
        return deferred_placements::clock::time_point { std::chrono::milliseconds { milliseconds } };
    }
} // namespace

TEST_CASE("A deferred placement waits for the tree that handled its size", "[win32][layered]")
{
    deferred_placements placements {};
    const screen_bounds grown { 100, 50, 800, 650 };
    placements.push(grown, 7, at(0));

    // 표 7을 처리하기 전의 tree는 옛 크기로 배치돼 있다. 그 tree로 새 크기를 내면 아래 캡션이 어긋난다.
    REQUIRE(placements.take_ready(6, at(10), timeout) == std::nullopt);
    REQUIRE(placements.empty() == false);
    REQUIRE(placements.take_ready(7, at(20), timeout) == grown);
    REQUIRE(placements.empty());
}

TEST_CASE("A tree that handled several sizes presents the newest of them and drops the older", "[win32][layered]")
{
    deferred_placements placements {};
    placements.push({ 0, 90, 800, 610 }, 3, at(0));
    placements.push({ 0, 80, 800, 620 }, 4, at(5));
    placements.push({ 0, 70, 800, 630 }, 5, at(10));

    // 끄는 동안 logic이 3·4를 한 batch로 처리했다. 마지막(5)을 기다리지 않고 4를 낸다 — 기다리면 끄는 동안 창이 멈춘다.
    REQUIRE(placements.take_ready(4, at(12), timeout) == screen_bounds { 0, 80, 800, 620 });
    REQUIRE(placements.latest() == screen_bounds { 0, 70, 800, 630 });
    REQUIRE(placements.take_ready(4, at(13), timeout) == std::nullopt);
    REQUIRE(placements.take_ready(5, at(14), timeout) == screen_bounds { 0, 70, 800, 630 });
    REQUIRE(placements.empty());
}

TEST_CASE("A deferred move keeps its place behind the pending resize", "[win32][layered]")
{
    deferred_placements placements {};
    placements.push({ 0, 0, 800, 600 }, 2, at(0));
    placements.push_move({ 40, 0, 800, 600 }, at(1));

    // 자리만 옮긴 배치는 앞 크기의 tree를 함께 기다린다. 먼저 나가면 옛 크기의 그림이 새 자리에 선다.
    REQUIRE(placements.take_ready(1, at(2), timeout) == std::nullopt);
    REQUIRE(placements.take_ready(2, at(3), timeout) == screen_bounds { 40, 0, 800, 600 });
}

TEST_CASE("A placement without a ticket presents with the next frame", "[win32][layered]")
{
    deferred_placements placements {};
    // 앱이 크기 메시지를 받지 않으면 기다릴 tree가 없다.
    placements.push({ 0, 0, 640, 480 }, 0, at(0));
    REQUIRE(placements.take_ready(0, at(0), timeout) == screen_bounds { 0, 0, 640, 480 });
}

TEST_CASE("A placement whose tree never comes presents after the timeout", "[win32][layered]")
{
    deferred_placements placements {};
    placements.push({ 0, 0, 640, 480 }, 9, at(0));
    placements.push({ 0, 0, 660, 480 }, 10, at(100));

    REQUIRE(placements.deadline(timeout) == at(250));
    REQUIRE(placements.take_ready(3, at(249), timeout) == std::nullopt);
    // 가장 오래 기다린 배치가 기한을 넘으면 가장 새 배치를 가진 tree로 낸다.
    REQUIRE(placements.take_ready(3, at(250), timeout) == screen_bounds { 0, 0, 660, 480 });
    REQUIRE(placements.empty());
    REQUIRE(placements.deadline(timeout) == std::nullopt);
}

TEST_CASE("The latest deferred placement takes the ticket of its metrics message", "[win32][layered]")
{
    deferred_placements placements {};
    placements.push({ 0, 0, 640, 480 }, 0, at(0));
    placements.set_latest_ticket(12);
    REQUIRE(placements.take_ready(11, at(1), timeout) == std::nullopt);
    REQUIRE(placements.take_ready(12, at(2), timeout) == screen_bounds { 0, 0, 640, 480 });
}
