#include "macos/dock_plan.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

// Dock 타일과 Dock 메뉴의 계산이다 (src/macos/dock_plan.h). AppKit을 모르는 순수 층이라 core test에서 돈다.
namespace {
    using luil::macos::dock_bar;
    using luil::macos::dock_bar_tint;
    using luil::macos::dock_rect;

    [[nodiscard]] luil::taskbar_jump_task task(std::u8string title)
    {
        return luil::taskbar_jump_task { .title = std::move(title) };
    }
}

TEST_CASE("dock bar follows the progress state", "[macos][dock]")
{
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::none, 500) == dock_bar {});
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::normal, 250) == dock_bar { .visible = true, .tint = dock_bar_tint::accent, .fraction = 0.25f });
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::error, 1000) == dock_bar { .visible = true, .tint = dock_bar_tint::error, .fraction = 1.0f });
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::paused, 0) == dock_bar { .visible = true, .tint = dock_bar_tint::paused, .fraction = 0.0f });
    // 불확정은 단계를 보지 않고 빗금 친 꽉 찬 막대다.
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::indeterminate, 300) == dock_bar { .visible = true, .striped = true, .fraction = 1.0f });
    // 단계 수를 넘는 값은 꽉 찬 막대로 자른다.
    CHECK(luil::macos::make_dock_bar(luil::taskbar_progress_state::normal, 5000).fraction == 1.0f);
}

TEST_CASE("dock tile layout keeps the overlay above the bar", "[macos][dock]")
{
    const luil::macos::dock_tile_layout plain { luil::macos::layout_dock_tile(128.0f, 128.0f, false) };
    CHECK(plain.track == dock_rect {});
    CHECK(plain.overlay.x + plain.overlay.width == Catch::Approx(128.0f));
    CHECK(plain.overlay.y == 0.0f);
    CHECK(plain.overlay.width == plain.overlay.height);

    const luil::macos::dock_tile_layout barred { luil::macos::layout_dock_tile(128.0f, 128.0f, true) };
    CHECK(barred.track.width > 0.0f);
    CHECK(barred.track.height > 0.0f);
    CHECK(barred.track.x + barred.track.width <= 128.0f);
    CHECK(barred.overlay.y >= barred.track.y + barred.track.height);
    CHECK(barred.overlay.y + barred.overlay.height <= 128.0f);

    CHECK(luil::macos::layout_dock_tile(0.0f, 128.0f, true).overlay == dock_rect {});
}

TEST_CASE("dock overlay fits inside its square", "[macos][dock]")
{
    const dock_rect box { .x = 10.0f, .y = 20.0f, .width = 40.0f, .height = 40.0f };
    const dock_rect wide { luil::macos::fit_dock_rect(box, 200.0f, 100.0f) };
    CHECK(wide.x == Catch::Approx(10.0f));
    CHECK(wide.width == Catch::Approx(40.0f));
    CHECK(wide.height == Catch::Approx(20.0f));
    CHECK(wide.y == Catch::Approx(30.0f));
    CHECK(luil::macos::fit_dock_rect(box, 0.0f, 10.0f) == dock_rect {});
}

TEST_CASE("dock menu rejects tasks it cannot carry", "[macos][dock]")
{
    CHECK(luil::macos::dock_menu_problem(luil::taskbar_jump_list {}) == std::nullopt);

    luil::taskbar_jump_list list { .tasks = { task(u8"New window"), task(u8"Open recent") } };
    list.tasks[1].arguments = { u8"--open", u8"a file.txt" };
    list.tasks[1].description = u8"Opens the last file";
    CHECK(luil::macos::dock_menu_problem(list) == std::nullopt);

    luil::taskbar_jump_list untitled { list };
    untitled.tasks[1].title.clear();
    CHECK(luil::macos::dock_menu_problem(untitled) == std::optional<std::u8string> { u8"Jump list task 2 has an empty title." });

    luil::taskbar_jump_list nul_argument { list };
    nul_argument.tasks[0].arguments = { std::u8string { u8"a\0b", 3 } };
    CHECK(luil::macos::dock_menu_problem(nul_argument) == std::optional<std::u8string> { u8"Jump list task 1 has an argument that contains NUL." });

    luil::taskbar_jump_list nul_description { list };
    nul_description.tasks[1].description = std::u8string { u8"x\0", 2 };
    CHECK(luil::macos::dock_menu_problem(nul_description) == std::optional<std::u8string> { u8"Jump list task 2 has text that contains NUL." });
}
