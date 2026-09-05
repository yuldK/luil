#include "win32/popup_reconcile.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
    using luil::win32::popup_placement;
} // namespace

TEST_CASE("Popup reconciliation creates, moves, and destroys by id", "[win32][popup]")
{
    const std::vector<popup_placement> alive {
        { u8"menu", 10, 10, 100, 200 },
        { u8"tip", 50, 50, 80, 30 },
    };
    const std::vector<popup_placement> wanted {
        { u8"menu", 10, 40, 100, 200 },
        { u8"select", 0, 0, 120, 90 },
    };

    const auto plan { luil::win32::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 1 });
    REQUIRE(plan.move == std::vector<std::size_t> { 0 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"tip" });
}

TEST_CASE("Popup reconciliation leaves an unchanged popup alone", "[win32][popup]")
{
    const std::vector<popup_placement> same {
        { u8"menu", 10, 10, 100, 200 },
    };
    const auto plan { luil::win32::reconcile_popups(same, same) };
    REQUIRE(plan.create.empty());
    REQUIRE(plan.move.empty());
    REQUIRE(plan.destroy.empty());
}

TEST_CASE("Popup reconciliation treats a sizeless popup as absent", "[win32][popup]")
{
    // 크기 없는 popup은 만들지 않는다.
    const std::vector<popup_placement> alive {};
    const std::vector<popup_placement> wanted {
        { u8"empty", 10, 10, 0, 40 },
    };
    REQUIRE(luil::win32::reconcile_popups(alive, wanted).create.empty());

    // 살아 있던 popup이 크기를 잃으면 없앤다.
    const std::vector<popup_placement> shrunk {
        { u8"empty", 10, 10, 0, 40 },
    };
    const std::vector<popup_placement> existing {
        { u8"empty", 10, 10, 100, 40 },
    };
    const auto plan { luil::win32::reconcile_popups(existing, shrunk) };
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"empty" });
}

TEST_CASE("A popup that changes anchor is rebuilt rather than moved", "[win32][popup]")
{
    // Win32의 창 소유자는 만든 뒤 바꾸는 것이 정식 경로가 아니라
    // 없앴다가 다시 만든다 (popup-anchor-design.md).
    const std::vector<popup_placement> alive {
        { u8"menu", 10, 10, 100, 200, {} },
    };
    const std::vector<popup_placement> wanted {
        { u8"menu", 10, 10, 100, 200, u8"tool" },
    };

    const auto plan { luil::win32::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 0 });
    REQUIRE(plan.move.empty());
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("An anchored popup still moves inside its own surface", "[win32][popup]")
{
    const std::vector<popup_placement> alive {
        { u8"menu", 10, 10, 100, 200, u8"tool" },
    };
    const std::vector<popup_placement> wanted {
        { u8"menu", 10, 40, 100, 200, u8"tool" },
    };

    const auto plan { luil::win32::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create.empty());
    REQUIRE(plan.move == std::vector<std::size_t> { 0 });
    REQUIRE(plan.destroy.empty());
}

TEST_CASE("Window reconciliation creates and destroys by id without moving", "[win32][window]")
{
    const std::vector<std::u8string> alive { u8"tools", u8"log" };
    const std::vector<std::u8string> wanted { u8"tools", u8"inspector" };

    const auto plan { luil::win32::reconcile_windows(alive, wanted) };
    // 없던 id는 만들고 사라진 id는 없앤다.
    REQUIRE(plan.create == std::vector<std::size_t> { 1 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"log" });

    // 같은 목록이면 아무 일도 내지 않는다.
    // 배치는 만들 때 한 번이라 옮기라는 지시 자체가 없다.
    const auto unchanged { luil::win32::reconcile_windows(alive, alive) };
    REQUIRE(unchanged.create.empty());
    REQUIRE(unchanged.destroy.empty());
}

TEST_CASE("Popup clamping pushes the popup inside the work area", "[win32][popup]")
{
    const luil::win32::screen_area work { 0, 0, 1920, 1080 };

    // 오른쪽 아래로 넘치면 안쪽으로 민다.
    const auto [right_x, bottom_y] { luil::win32::clamp_popup_position(1900, 1060, 100, 100, work) };
    REQUIRE(right_x == 1820);
    REQUIRE(bottom_y == 980);

    // 왼쪽 위로 넘치면 경계에 맞춘다.
    const auto [left_x, top_y] { luil::win32::clamp_popup_position(-30, -5, 100, 100, work) };
    REQUIRE(left_x == 0);
    REQUIRE(top_y == 0);

    // 영역보다 크면 왼쪽 위를 맞춰 시작 부분이 보인다.
    const auto [wide_x, wide_y] { luil::win32::clamp_popup_position(500, 20, 3000, 100, work) };
    REQUIRE(wide_x == 0);
    REQUIRE(wide_y == 20);

    // 이미 안에 있으면 그대로다.
    const auto [kept_x, kept_y] { luil::win32::clamp_popup_position(400, 300, 100, 100, work) };
    REQUIRE(kept_x == 400);
    REQUIRE(kept_y == 300);
}
