#include "host/surface_invalidate.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {
    using luil::surface_content;
    using luil::surface_update_deadline;

    // 시계를 읽지 않고 세운 기준 시각이다.
    // 예고를 앞뒤로 옮겨도 음수가 되지 않게 넉넉히 잡는다.
    constexpr std::chrono::steady_clock::time_point base { std::chrono::seconds { 1000 } };
    // 창이 실제로 쓰는 연속 애니메이션 주기다 (win32_window.cpp의 33ms).
    constexpr std::chrono::milliseconds continuous { 33 };

    // tree는 **주소만** 쓰이므로 내용이 필요 없다.
    // 서로 다른 주소를 준다는 것이 이 test가 요구하는 전부다.
    const int first_tree { 0 };
    const int second_tree { 0 };
    const int third_tree { 0 };
} // namespace

TEST_CASE("A surface that keeps the same tree is not repainted", "[win32][render]")
{
    // 같은 tree를 실은 게시 둘이다.
    // 게시된 tree는 불변이라 주소가 같으면 그림도 같다.
    const std::vector<surface_content> surfaces {
        { u8"tools", &first_tree, &first_tree },
    };
    REQUIRE(luil::surfaces_to_repaint(surfaces, false).empty());
}

TEST_CASE("Only the surface whose tree changed is repainted", "[win32][render]")
{
    // 창 셋 중 하나만 교체됐다 (첫 항목의 빈 id는 주 창이다).
    const std::vector<surface_content> surfaces {
        { {}, &first_tree, &first_tree },
        { u8"tools", &second_tree, &third_tree },
        { u8"log", &third_tree, &third_tree },
    };
    REQUIRE(luil::surfaces_to_repaint(surfaces, false) == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("A surface that has nothing yet is repainted", "[win32][render]")
{
    // 이번 대조에서 만들어진 표면이다.
    // `current`가 없으므로 첫 그리기를 창 표시에 기대지 않는다.
    const std::vector<surface_content> surfaces {
        { u8"tools", nullptr, &first_tree },
    };
    REQUIRE(luil::surfaces_to_repaint(surfaces, false) == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("A surface that lost its tree is repainted", "[win32][render]")
{
    // 그리던 것이 사라졌으면 화면에서도 지워야 한다.
    // 주소 대조 하나가 양쪽 방향을 함께 덮는다.
    const std::vector<surface_content> surfaces {
        { u8"tools", &first_tree, nullptr },
    };
    REQUIRE(luil::surfaces_to_repaint(surfaces, false) == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("An interaction change repaints every surface", "[win32][render]")
{
    // 상호작용(hover·눌림·초점·메뉴 강조·끌기)과 외양(테마·키 컬러·글꼴)은
    // tree를 바꾸지 않고 그림만 바꾼다. 스냅샷에 표면 표식이 실려 있어 원리상
    // 가릴 수 있지만 **일부러 가리지 않는다** — 이 선별의 실패는 "그려야 할
    // 것을 안 그리는 것"이고 증상이 "가끔 화면이 낡아 있다"라 잡기 어렵다
    // (rendering.md).
    const std::vector<surface_content> surfaces {
        { {}, &first_tree, &first_tree },
        { u8"tools", &second_tree, &second_tree },
        { u8"menu", &third_tree, &third_tree },
    };
    REQUIRE(luil::surfaces_to_repaint(surfaces, false).empty());

    const std::vector<std::u8string> everything { std::u8string {}, u8"tools", u8"menu" };
    REQUIRE(luil::surfaces_to_repaint(surfaces, true) == everything);
}

TEST_CASE("Only the surfaces that answered next_update are woken", "[win32][render]")
{
    // 보조 창 하나가 회전 표시로 현재 시각을 답했고 주 창과 다른 창은 예고가 없다.
    // timer는 해당 보조 창만 깨운다.
    const std::vector<surface_update_deadline> deadlines {
        { {}, std::nullopt },
        { u8"tools", base },
        { u8"log", std::nullopt },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.armed);
    // "지금 이하"의 예고는 연속 애니메이션이라 짧은 주기로 잇는다.
    REQUIRE(plan.delay_milliseconds == 33u);
    REQUIRE(plan.wake == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("A surface whose moment is still far off is left asleep", "[win32][render]")
{
    // 주 창의 caret은 500ms 뒤, 보조 창의 회전 표시는 지금이다.
    // 이 tick은 회전 표시의 것이고 caret은 자기 시각에 따로 깨어난다.
    const std::vector<surface_update_deadline> deadlines {
        { {}, base + std::chrono::milliseconds { 500 } },
        { u8"tools", base },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.delay_milliseconds == 33u);
    REQUIRE(plan.wake == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("Surfaces that come due within the same tick wake together", "[win32][render]")
{
    // 두 창이 함께 애니메이션 중이면 한 tick이 둘을 깨운다.
    // 가장 이른 하나만 깨우면 그 tick에서 나머지가 빠진다.
    const std::vector<surface_update_deadline> deadlines {
        { {}, base + std::chrono::milliseconds { 20 } },
        { u8"tools", base },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.delay_milliseconds == 33u);

    const std::vector<std::u8string> both { std::u8string {}, u8"tools" };
    REQUIRE(plan.wake == both);
}

TEST_CASE("A future moment is announced with slack past it", "[win32][render]")
{
    const std::vector<surface_update_deadline> deadlines {
        { u8"tools", base + std::chrono::milliseconds { 500 } },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.armed);
    // 그 시각을 확실히 지나도록 살짝 늦춘다.
    // 여유 없이 깨우면 element가 "아직 아니다"라며 같은 시각을 다시 예고한다.
    REQUIRE(plan.delay_milliseconds == 515u);
    REQUIRE(plan.wake == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("A moment that already passed still wakes its surface", "[win32][render]")
{
    // 앞선 tick이 늦게 와 예고 시각이 지난 뒤에 계획을 세우는 일이 있다.
    // 지난 예고는 연속 애니메이션과 같은 취급이라 다음 tick에 깨어난다.
    const std::vector<surface_update_deadline> deadlines {
        { u8"tools", base - std::chrono::milliseconds { 200 } },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.delay_milliseconds == 33u);
    REQUIRE(plan.wake == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("A very distant moment is capped and still wakes its surface", "[win32][render]")
{
    // 아주 먼 예고가 밀리초 수로 잘리며 "지금"이 되지 않게 상한을 둔다.
    // 상한이 지연을 줄여도 그 예고를 답한 표면은 반드시 목록에 든다 —
    // 빠지면 예고를 낸 쪽이 영영 깨어나지 않는다.
    const std::vector<surface_update_deadline> deadlines {
        { u8"tools", base + std::chrono::hours { 24 * 365 } },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.armed);
    REQUIRE(plan.delay_milliseconds == luil::maximum_update_timer_delay);
    REQUIRE(plan.wake == std::vector<std::u8string> { u8"tools" });
}

TEST_CASE("No announced moment disarms the timer", "[win32][render]")
{
    // 예고가 없으면 timer도 없다 — 평소에는 이벤트가 있을 때만 그린다.
    const std::vector<surface_update_deadline> deadlines {
        { {}, std::nullopt },
        { u8"tools", std::nullopt },
    };

    const auto plan { luil::plan_update_timer(deadlines, base, continuous) };
    REQUIRE(plan.armed == false);
    REQUIRE(plan.wake.empty());
    REQUIRE(luil::plan_update_timer({}, base, continuous).armed == false);
}
