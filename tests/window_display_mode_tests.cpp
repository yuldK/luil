#include "host/window_display_mode.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace {
    using luil::window_display_mode;
}

TEST_CASE("The window mode is one value that fullscreen wins", "[window-mode]")
{
    REQUIRE(luil::window_mode_of(false, false) == window_display_mode::normal);
    REQUIRE(luil::window_mode_of(true, false) == window_display_mode::maximized);
    REQUIRE(luil::window_mode_of(false, true) == window_display_mode::fullscreen);
    // 최대화된 창에서 들어가면 `WS_MAXIMIZE`가 남아 `IsZoomed`가 계속 참이다.
    // 그때 최대화로 보면 크기 한계가 작업 영역으로 잘려 모니터를 덮지 못한다.
    REQUIRE(luil::window_mode_of(true, true) == window_display_mode::fullscreen);
}

TEST_CASE("Fullscreen targets the whole monitor", "[window-mode]")
{
    // 작업 표시줄을 뺀 rcWork가 아니라 rcMonitor 전체다.
    const luil::screen_area monitor { -1920, 0, 0, 1080 };
    const luil::window_bounds bounds { luil::fullscreen_bounds_for(monitor) };
    // 괄호 한 겹은 전처리기의 몫이다 — 중괄호 안의 쉼표는 매크로 인자를 가르지 못한다.
    REQUIRE((bounds == luil::window_bounds { -1920, 0, 1920, 1080 }));

    // 뒤집힌 사각형은 크기 0으로 접는다 (창을 만들 수 없는 값을 넘기지 않는다).
    const luil::window_bounds folded { luil::fullscreen_bounds_for(luil::screen_area { 10, 10, 0, 0 }) };
    REQUIRE((folded == luil::window_bounds { 10, 10, 0, 0 }));
}

TEST_CASE("Entering fullscreen does not destroy the normal placement", "[window-mode]")
{
    // 사용자가 쓰던 창이다.
    const luil::window_placement normal { 120, 80, 1280, 800, false, false };

    SECTION("전체 화면이 아니면 관측값이 그대로 답이다")
    {
        const luil::window_placement reported { luil::placement_to_report(normal, std::nullopt) };
        REQUIRE(reported == normal);
        REQUIRE(reported.fullscreen == false);
    }

    SECTION("전체 화면 동안의 관측값은 버리고 갈무리한 것을 알린다")
    {
        // 들어갈 때의 SetWindowPos가 rcNormalPosition을 모니터 사각형으로 덮어쓴다.
        // 그대로 저장하면 "나오면 모니터를 덮는 창"이 되어 돌아갈 자리가 사라진다.
        const luil::window_placement observed { 0, 0, 1920, 1080, false, false };
        const luil::window_placement reported { luil::placement_to_report(observed, normal) };
        REQUIRE(reported.x == normal.x);
        REQUIRE(reported.y == normal.y);
        REQUIRE(reported.width == normal.width);
        REQUIRE(reported.height == normal.height);
        REQUIRE(reported.fullscreen);
    }

    SECTION("최대화 창에서 들어가면 최대화도 함께 기억한다")
    {
        // 같은 순간의 WM_SIZE는 SIZE_RESTORED로 와서 관측된 최대화 표식을 지운다.
        // 그것을 믿으면 나왔을 때 창이 복원 크기로 앉는다.
        const luil::window_placement restore { 120, 80, 1280, 800, true, false };
        const luil::window_placement observed { 0, 0, 1920, 1080, false, false };
        const luil::window_placement reported { luil::placement_to_report(observed, restore) };
        REQUIRE(reported.maximized);
        REQUIRE(reported.fullscreen);
    }

    SECTION("저장했다가 그대로 넣으면 전체 화면과 돌아갈 자리가 함께 산다")
    {
        // 저장 → 복원 → 다시 보고의 왕복이다.
        const luil::window_placement saved { luil::placement_to_report(luil::window_placement { 0, 0, 1920, 1080 }, normal) };
        REQUIRE(saved.fullscreen);
        // 복원은 saved의 자리·크기로 정상 배치를 놓고 전체 화면에 들어간다 —
        // 그러면 갈무리되는 것이 다시 saved의 자리다.
        const luil::window_placement again { luil::placement_to_report(luil::window_placement { 0, 0, 1920, 1080 }, saved) };
        REQUIRE(again == saved);
    }

    SECTION("관측값에 실려 온 전체 화면 표식은 믿지 않는다")
    {
        // OS는 전체 화면을 알지 못한다 — 참이 실려 있다면 어디선가 흘러든 값이다.
        const luil::window_placement stale { 120, 80, 1280, 800, false, true };
        REQUIRE(luil::placement_to_report(stale, std::nullopt).fullscreen == false);
    }
}
