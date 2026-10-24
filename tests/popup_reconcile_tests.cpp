#include "host/popup_reconcile.h"

#include "luil/app/menu_popup.h"
#include "luil/ui/ui_platform.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

namespace {
    using luil::popup_placement;
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

    const auto plan { luil::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 1 });
    REQUIRE(plan.move == std::vector<std::size_t> { 0 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"tip" });
}

TEST_CASE("Popup reconciliation leaves an unchanged popup alone", "[win32][popup]")
{
    const std::vector<popup_placement> same {
        { u8"menu", 10, 10, 100, 200 },
    };
    const auto plan { luil::reconcile_popups(same, same) };
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
    REQUIRE(luil::reconcile_popups(alive, wanted).create.empty());

    // 살아 있던 popup이 크기를 잃으면 없앤다.
    const std::vector<popup_placement> shrunk {
        { u8"empty", 10, 10, 0, 40 },
    };
    const std::vector<popup_placement> existing {
        { u8"empty", 10, 10, 100, 40 },
    };
    const auto plan { luil::reconcile_popups(existing, shrunk) };
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

    const auto plan { luil::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 0 });
    REQUIRE(plan.move.empty());
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("A popup that turns rounded or square is rebuilt with its new window class", "[win32][popup]")
{
    // 그림자가 창 클래스의 것이라 둥근 창과 직각 창 사이는 옮길 수 없다. 둥근 정도만 바뀐 것은 같은 창이다.
    const std::vector<popup_placement> alive {
        { u8"menu", 10, 10, 100, 200, {}, false },
        { u8"list", 10, 10, 100, 200, {}, true },
    };
    const std::vector<popup_placement> wanted {
        { u8"menu", 10, 10, 100, 200, {}, true },
        { u8"list", 10, 10, 100, 200, {}, true },
    };
    const auto plan { luil::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 0 });
    REQUIRE(plan.move.empty());
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"menu" });
}

TEST_CASE("A popup shape follows the corner, the border and what the OS supports", "[win32][popup]")
{
    using luil::popup_corner;
    using luil::popup_shape;

    SECTION("rounded corners where the OS cuts them")
    {
        // OS 기본은 이 OS가 popup에 쓰는 모양이다 — Windows 11의 메뉴처럼 작게 둥글다.
        REQUIRE(luil::resolve_popup_corner(popup_corner::system, true) == popup_corner::round_small);
        // 둥근 창은 클래스 그림자도 사각 선도 쓰지 않고, OS 테두리를 팔레트 색으로 긋는다.
        REQUIRE(luil::resolve_popup_shape(popup_corner::round, true, true) == popup_shape { .corner = popup_corner::round, .drop_shadow = false, .line = false, .system_border = true });
        REQUIRE(luil::resolve_popup_shape(popup_corner::system, true, true) == popup_shape { .corner = popup_corner::round_small, .drop_shadow = false, .line = false, .system_border = true });
        // 경계를 끄면 OS 테두리도 없앤다 (`DWMWA_COLOR_NONE`).
        REQUIRE(luil::resolve_popup_shape(popup_corner::round_small, false, true) == popup_shape { .corner = popup_corner::round_small, .drop_shadow = false, .line = false, .system_border = false });
    }

    SECTION("a square popup keeps the original edge")
    {
        // 꼬리 달린 말풍선처럼 모양을 스스로 그리는 popup이 고르는 직각은 지원하는 OS에서도 처음부터의 모양이다.
        REQUIRE(luil::resolve_popup_shape(popup_corner::square, true, true) == popup_shape { .corner = popup_corner::square, .drop_shadow = true, .line = true, .system_border = false });
        REQUIRE(luil::resolve_popup_shape(popup_corner::square, false, true) == popup_shape { .corner = popup_corner::square, .drop_shadow = true, .line = false, .system_border = false });
    }

    SECTION("an OS without corner preference falls back to the square edge")
    {
        // Windows 10 이하와 원격 세션은 무엇을 골라도 직각, 클래스 그림자, 1px 선이다 — 경계가 사라지지 않는다.
        for (const popup_corner corner : { popup_corner::system, popup_corner::round, popup_corner::round_small, popup_corner::square })
        {
            REQUIRE(luil::resolve_popup_shape(corner, true, false) == popup_shape { .corner = popup_corner::square, .drop_shadow = true, .line = true, .system_border = false });
            REQUIRE(luil::resolve_popup_shape(corner, false, false) == popup_shape { .corner = popup_corner::square, .drop_shadow = true, .line = false, .system_border = false });
        }
    }
}

TEST_CASE("The popup corner radius is what the OS actually cuts", "[win32][popup]")
{
    using luil::popup_corner;
    luil::ui_platform rounded {};
    rounded.rounded_popup_corners = true;
    REQUIRE(luil::popup_corner_radius(popup_corner::round, rounded) == 8.0f);
    REQUIRE(luil::popup_corner_radius(popup_corner::round_small, rounded) == 4.0f);
    REQUIRE(luil::popup_corner_radius(popup_corner::system, rounded) == 4.0f);
    REQUIRE(luil::popup_corner_radius(popup_corner::square, rounded) == 0.0f);
    // 둥근 모서리를 못 쓰면 tree도 직각으로 그린다.
    const luil::ui_platform square {};
    REQUIRE(luil::popup_corner_radius(popup_corner::round, square) == 0.0f);
    REQUIRE(luil::popup_corner_radius(popup_corner::system, square) == 0.0f);
}

TEST_CASE("A menu popup drops its own line inside a rounded window and when the border is off", "[win32][popup]")
{
    const auto menu_border = [](const std::optional<luil::popup_corner> corner, const bool border) {
        luil::menu_config menu {};
        menu.items.push_back({ u8"open", u8"Open" });
        luil::menu_popup_config config {};
        config.id = u8"menu";
        config.corner = corner;
        config.border = border;
        const luil::ui_popup popup { luil::make_menu_popup(std::move(menu), std::move(config)) };
        REQUIRE(popup.border == border);
        REQUIRE(popup.corner.has_value());
        return static_cast<const luil::menu_element*>(popup.tree->root())->config().border;
    };
    luil::ui_platform previous { luil::current_ui_platform() };
    luil::ui_platform rounded { previous };
    rounded.rounded_popup_corners = true;
    luil::set_ui_platform(rounded);
    // 둥근 창은 OS 테두리가 경계다.
    REQUIRE(menu_border(luil::popup_corner::round, true) == false);
    // 직각 창은 처음처럼 메뉴가 선을 긋는다. 경계를 끄면 긋지 않는다.
    REQUIRE(menu_border(luil::popup_corner::square, true));
    REQUIRE(menu_border(luil::popup_corner::square, false) == false);
    // 둥근 모서리를 못 쓰는 플랫폼은 무엇을 골라도 직각이라 선을 긋는다.
    luil::ui_platform square { previous };
    square.rounded_popup_corners = false;
    luil::set_ui_platform(square);
    REQUIRE(menu_border(luil::popup_corner::round, true));
    luil::set_ui_platform(previous);
}

TEST_CASE("An anchored popup still moves inside its own surface", "[win32][popup]")
{
    const std::vector<popup_placement> alive {
        { u8"menu", 10, 10, 100, 200, u8"tool" },
    };
    const std::vector<popup_placement> wanted {
        { u8"menu", 10, 40, 100, 200, u8"tool" },
    };

    const auto plan { luil::reconcile_popups(alive, wanted) };
    REQUIRE(plan.create.empty());
    REQUIRE(plan.move == std::vector<std::size_t> { 0 });
    REQUIRE(plan.destroy.empty());
}

TEST_CASE("Window reconciliation creates and destroys by id without moving", "[win32][window]")
{
    const std::vector<std::u8string> alive { u8"tools", u8"log" };
    const std::vector<std::u8string> wanted { u8"tools", u8"inspector" };

    const auto plan { luil::reconcile_windows(alive, wanted) };
    // 없던 id는 만들고 사라진 id는 없앤다.
    REQUIRE(plan.create == std::vector<std::size_t> { 1 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"log" });

    // 같은 목록이면 아무 일도 내지 않는다.
    // 배치는 만들 때 한 번이라 옮기라는 지시 자체가 없다.
    const auto unchanged { luil::reconcile_windows(alive, alive) };
    REQUIRE(unchanged.create.empty());
    REQUIRE(unchanged.destroy.empty());
}

TEST_CASE("Popup clamping pushes the popup inside the work area", "[win32][popup]")
{
    const luil::screen_area work { 0, 0, 1920, 1080 };

    // 오른쪽 아래로 넘치면 안쪽으로 민다.
    const auto [right_x, bottom_y] { luil::clamp_popup_position(1900, 1060, 100, 100, work) };
    REQUIRE(right_x == 1820);
    REQUIRE(bottom_y == 980);

    // 왼쪽 위로 넘치면 경계에 맞춘다.
    const auto [left_x, top_y] { luil::clamp_popup_position(-30, -5, 100, 100, work) };
    REQUIRE(left_x == 0);
    REQUIRE(top_y == 0);

    // 영역보다 크면 왼쪽 위를 맞춰 시작 부분이 보인다.
    const auto [wide_x, wide_y] { luil::clamp_popup_position(500, 20, 3000, 100, work) };
    REQUIRE(wide_x == 0);
    REQUIRE(wide_y == 20);

    // 이미 안에 있으면 그대로다.
    const auto [kept_x, kept_y] { luil::clamp_popup_position(400, 300, 100, 100, work) };
    REQUIRE(kept_x == 400);
    REQUIRE(kept_y == 300);
}

TEST_CASE("Webview reconciliation creates and destroys by id", "[win32][webview]")
{
    using luil::webview_placement;

    const std::vector<webview_placement> alive {
        { u8"docs", {} },
        { u8"help", {} },
    };
    const std::vector<webview_placement> wanted {
        { u8"docs", {} },
        { u8"preview", {} },
    };

    const auto plan { luil::reconcile_webviews(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 1 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"help" });
}

TEST_CASE("Webview reconciliation leaves an unchanged webview alone", "[win32][webview]")
{
    using luil::webview_placement;

    // 자리는 목록이 갖지 않으므로 같은 id·같은 앵커면 할 일이 없다 —
    // 자리가 바뀌어도 여기서는 아무 일도 나지 않는다 (tree가 그 답을 갖는다).
    const std::vector<webview_placement> same {
        { u8"docs", u8"tools" },
    };
    const auto plan { luil::reconcile_webviews(same, same) };
    REQUIRE(plan.create.empty());
    REQUIRE(plan.destroy.empty());
}

TEST_CASE("Webview reconciliation rebuilds a webview whose anchor changed", "[win32][webview]")
{
    using luil::webview_placement;

    // 웹뷰는 그 표면의 합성 visual과 parentWindow에 매여 있어 옮기지 못한다.
    const std::vector<webview_placement> alive {
        { u8"docs", {} },
    };
    const std::vector<webview_placement> wanted {
        { u8"docs", u8"tools" },
    };

    const auto plan { luil::reconcile_webviews(alive, wanted) };
    REQUIRE(plan.create == std::vector<std::size_t> { 0 });
    REQUIRE(plan.destroy == std::vector<std::u8string> { u8"docs" });
}

TEST_CASE("Popup chains are ordered before their children and reject broken topology", "[host][popup]")
{
    const std::vector<std::u8string> roots { u8"", u8"window" };
    const std::vector<luil::popup_placement> wanted {
        { u8"leaf", 0, 0, 100, 100, u8"child" }, { u8"parent", 0, 0, 100, 100, u8"window" },
        { u8"child", 0, 0, 100, 100, u8"parent" }, { u8"cycle", 0, 0, 100, 100, u8"cycle" },
        { u8"missing", 0, 0, 100, 100, u8"absent" }
    };
    REQUIRE(luil::order_popup_placements(wanted, roots) == std::vector<std::size_t> { 1, 2, 0 });
    REQUIRE(luil::popup_is_descendant(wanted, u8"leaf", u8"window"));
    REQUIRE(luil::popup_is_descendant(wanted, u8"parent", u8"leaf") == false);
}

TEST_CASE("Reparenting a popup recreates all of its owned descendants", "[host][popup]")
{
    std::vector<luil::popup_placement> before {
        { u8"parent", 0, 0, 100, 100 }, { u8"child", 100, 0, 100, 100, u8"parent" }
    };
    auto after { before };
    after[0].anchor = u8"other";
    const auto result { luil::reconcile_popups(before, after) };
    REQUIRE(result.destroy.size() == 2u);
    REQUIRE(result.create == std::vector<std::size_t> { 0, 1 });
    REQUIRE(result.move.empty());
}
