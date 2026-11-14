#include "host/docked_placement.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {
    // 주 모니터(150%, 아래에 작업 표시줄 48px)와 왼쪽에 놓인 둘째 모니터(100%)다.
    [[nodiscard]] std::vector<luil::display_info> two_displays()
    {
        luil::display_info primary {};
        primary.id = u8"\\\\.\\DISPLAY1";
        primary.bounds = { 0, 0, 2560, 1440 };
        primary.work_area = { 0, 0, 2560, 1368 };
        primary.scale = 1.5f;
        primary.primary = true;
        luil::display_info left {};
        left.id = u8"\\\\.\\DISPLAY2";
        left.bounds = { -1920, 0, 0, 1080 };
        left.work_area = { -1920, 0, 0, 1080 };
        left.scale = 1.0f;
        return { left, primary };
    }
}

TEST_CASE("A physical rectangle is used as it is and an empty one hides the surface", "[docked]")
{
    const std::vector<luil::display_info> displays { two_displays() };
    const luil::screen_rect strip { 100, 200, 900, 203 };
    REQUIRE(luil::resolve_docked_bounds(strip, displays) == strip);
    REQUIRE(luil::resolve_docked_bounds(luil::screen_rect { 10, 10, 10, 40 }, displays).has_value() == false);
    // 모니터 목록이 없어도 물리 사각형은 앱이 정한 그대로다.
    REQUIRE(luil::resolve_docked_bounds(strip, {}) == strip);
}

TEST_CASE("An edge placement sits on the work area edge at the monitor scale", "[docked]")
{
    const std::vector<luil::display_info> displays { two_displays() };
    // 주 모니터의 아래 가장자리, 오른쪽 끝에서 12만큼, 길이 200, 두께 40 — 작업 표시줄 바로 위의 위젯이다.
    luil::docked_edge_placement widget {};
    widget.edge = luil::screen_edge::bottom;
    widget.alignment = luil::edge_alignment::end;
    widget.offset = 12.0f;
    widget.length = 200.0f;
    widget.thickness = 40.0f;
    REQUIRE(luil::resolve_docked_bounds(widget, displays) == luil::screen_rect { 2560 - 18 - 300, 1368 - 60, 2560 - 18, 1368 });

    // 모니터 전체의 가장자리면 작업 표시줄 위에 겹친다.
    widget.work_area = false;
    REQUIRE(luil::resolve_docked_bounds(widget, displays) == luil::screen_rect { 2560 - 18 - 300, 1440 - 60, 2560 - 18, 1440 });
}

TEST_CASE("An edge placement follows the named monitor and its own scale", "[docked]")
{
    const std::vector<luil::display_info> displays { two_displays() };
    // 둘째 모니터의 왼쪽 가장자리 전체에 3px 띠다. 길이 0은 가장자리 전체다.
    luil::docked_edge_placement band {};
    band.monitor = u8"\\\\.\\DISPLAY2";
    band.edge = luil::screen_edge::left;
    band.thickness = 3.0f;
    REQUIRE(luil::resolve_docked_bounds(band, displays) == luil::screen_rect { -1920, 0, -1917, 1080 });

    // 가운데 맞춤에 안쪽으로 띄운 위쪽 알림 띠다.
    luil::docked_edge_placement banner {};
    banner.edge = luil::screen_edge::top;
    banner.alignment = luil::edge_alignment::center;
    banner.length = 400.0f;
    banner.thickness = 32.0f;
    banner.inset = 8.0f;
    REQUIRE(luil::resolve_docked_bounds(banner, displays) == luil::screen_rect { 980, 12, 1580, 60 });

    // 오른쪽 가장자리의 긴 패널은 가장자리 길이로 줄인다.
    luil::docked_edge_placement panel {};
    panel.edge = luil::screen_edge::right;
    panel.length = 5000.0f;
    panel.thickness = 300.0f;
    REQUIRE(luil::resolve_docked_bounds(panel, displays) == luil::screen_rect { 2560 - 450, 0, 2560, 1368 });
}

TEST_CASE("An edge placement hides the surface without a monitor or thickness", "[docked]")
{
    const std::vector<luil::display_info> displays { two_displays() };
    luil::docked_edge_placement gone {};
    gone.monitor = u8"\\\\.\\DISPLAY9";
    gone.thickness = 40.0f;
    REQUIRE(luil::resolve_docked_bounds(gone, displays).has_value() == false);

    luil::docked_edge_placement flat {};
    flat.thickness = 0.0f;
    REQUIRE(luil::resolve_docked_bounds(flat, displays).has_value() == false);

    // 주 모니터 표식이 없는 목록에서는 첫 모니터가 주 모니터다.
    std::vector<luil::display_info> unmarked { displays };
    for (luil::display_info& display : unmarked)
        display.primary = false;
    luil::docked_edge_placement first {};
    first.thickness = 10.0f;
    REQUIRE(luil::resolve_docked_bounds(first, unmarked) == luil::screen_rect { -1920, 1070, 0, 1080 });
    REQUIRE(luil::resolve_docked_bounds(first, {}).has_value() == false);
}
