#include "win32/webview_layout.h"

#include <catch2/catch_test_macros.hpp>

namespace {
    using luil::rect_f;
    using luil::win32::plan_webview_layout;
} // namespace

TEST_CASE("A webview with no visible bounds takes no place", "[win32][webview]")
{
    // 자리표가 tree에 없거나 통째로 잘렸다.
    const auto layout { plan_webview_layout(std::nullopt, 800, 600, false, 1.0f) };
    REQUIRE(layout.visible() == false);
    REQUIRE(layout.punch_hole == false);
}

TEST_CASE("A visible webview takes its bounds and punches a hole", "[win32][webview]")
{
    const auto layout { plan_webview_layout(rect_f { 100.0f, 80.0f, 300.0f, 220.0f }, 800, 600, false, 1.0f) };
    REQUIRE(layout.x == 100);
    REQUIRE(layout.y == 80);
    REQUIRE(layout.width == 300);
    REQUIRE(layout.height == 220);
    REQUIRE(layout.punch_hole);
}

TEST_CASE("An occluded webview keeps its place and stops punching", "[win32][webview]")
{
    // 자리를 흔들면 페이지가 리플로를 한 번 더 한다.
    // 웹뷰 visual은 우리 아래에 있어 덮이기만 하므로 바뀌는 것은 구멍뿐이다.
    const rect_f bounds { 100.0f, 80.0f, 300.0f, 220.0f };
    const auto shown { plan_webview_layout(bounds, 800, 600, false, 1.0f) };
    const auto covered { plan_webview_layout(bounds, 800, 600, true, 1.0f) };

    REQUIRE(covered.x == shown.x);
    REQUIRE(covered.y == shown.y);
    REQUIRE(covered.width == shown.width);
    REQUIRE(covered.height == shown.height);
    REQUIRE(shown.punch_hole);
    REQUIRE(covered.punch_hole == false);
}

TEST_CASE("A webview is clipped to the client rectangle", "[win32][webview]")
{
    // 넘치는 쪽만 잘린다. 창보다 큰 tree가 와도 자리가 창 밖으로 나가지 않는다.
    const auto layout { plan_webview_layout(rect_f { -40.0f, -10.0f, 900.0f, 700.0f }, 800, 600, false, 1.0f) };
    REQUIRE(layout.x == 0);
    REQUIRE(layout.y == 0);
    REQUIRE(layout.width == 800);
    REQUIRE(layout.height == 600);
}

TEST_CASE("A webview entirely outside the client takes no place", "[win32][webview]")
{
    const auto right { plan_webview_layout(rect_f { 900.0f, 10.0f, 100.0f, 100.0f }, 800, 600, false, 1.0f) };
    REQUIRE(right.visible() == false);
    REQUIRE(right.punch_hole == false);

    const auto above { plan_webview_layout(rect_f { 10.0f, -200.0f, 100.0f, 100.0f }, 800, 600, false, 1.0f) };
    REQUIRE(above.visible() == false);
}

TEST_CASE("Webview edges round instead of truncating", "[win32][webview]")
{
    // 왼쪽·위를 반올림하고 폭을 더하는 대신 오른쪽·아래도 반올림한다 —
    // 그래야 자리가 흔들려도 크기가 1픽셀씩 떠다니지 않는다.
    const auto layout { plan_webview_layout(rect_f { 10.5f, 20.4f, 100.2f, 100.2f }, 800, 600, false, 1.0f) };
    REQUIRE(layout.x == 11);
    REQUIRE(layout.y == 20);
    // 오른쪽 = round(110.7) = 111, 아래 = round(120.6) = 121.
    REQUIRE(layout.width == 100);
    REQUIRE(layout.height == 101);
}

TEST_CASE("A zero-sized client leaves no place for a webview", "[win32][webview]")
{
    // 최소화된 창이 이 자리다.
    const auto layout { plan_webview_layout(rect_f { 0.0f, 0.0f, 300.0f, 200.0f }, 0, 0, false, 1.0f) };
    REQUIRE(layout.visible() == false);
}

TEST_CASE("A pointer inside the webview translates to its own origin", "[win32][webview]")
{
    using luil::win32::translate_webview_pointer;
    using luil::win32::webview_layout;

    const webview_layout layout { 100, 80, 300, 220, true };

    const auto top_left { translate_webview_pointer(layout, 100, 80) };
    REQUIRE(top_left.inside);
    REQUIRE(top_left.x == 0);
    REQUIRE(top_left.y == 0);

    const auto middle { translate_webview_pointer(layout, 250, 190) };
    REQUIRE(middle.inside);
    REQUIRE(middle.x == 150);
    REQUIRE(middle.y == 110);
}

TEST_CASE("The webview rectangle is half open", "[win32][webview]")
{
    using luil::win32::translate_webview_pointer;
    using luil::win32::webview_layout;

    // 자리를 잇대어 놓아도 한 점이 두 웹뷰의 것이 되지 않는다.
    const webview_layout layout { 100, 80, 300, 220, true };

    REQUIRE(translate_webview_pointer(layout, 399, 299).inside);
    REQUIRE(translate_webview_pointer(layout, 400, 299).inside == false);
    REQUIRE(translate_webview_pointer(layout, 399, 300).inside == false);
    REQUIRE(translate_webview_pointer(layout, 99, 190).inside == false);
    REQUIRE(translate_webview_pointer(layout, 250, 79).inside == false);
}

TEST_CASE("A hidden webview never takes the pointer", "[win32][webview]")
{
    using luil::win32::translate_webview_pointer;
    using luil::win32::webview_layout;

    // 자리가 없으면 언제나 바깥이다 — 감춰진 웹뷰가 포인터를 먹지 않는다.
    const webview_layout hidden {};
    REQUIRE(translate_webview_pointer(hidden, 0, 0).inside == false);
    REQUIRE(translate_webview_pointer(hidden, 120, 90).inside == false);
}

TEST_CASE("An occluded webview leaves the pointer to the overlay", "[win32][webview]")
{
    using luil::win32::translate_webview_pointer;
    using luil::win32::webview_layout;

    // 중계는 tree의 입력 처리보다 먼저 일어나므로 가림을 여기서도 지킨다.
    const webview_layout covered { 100, 80, 300, 220, false };
    REQUIRE(translate_webview_pointer(covered, 250, 190).inside == false);
    REQUIRE(translate_webview_pointer(covered, 250, 190, true).inside == false);
}

TEST_CASE("A captured webview receives movement and release outside its rectangle", "[win32][webview]")
{
    const luil::win32::webview_layout layout { 100, 80, 300, 220, true };
    const auto pointer { luil::win32::translate_webview_pointer(layout, 50, 400, true) };
    REQUIRE(pointer.inside);
    REQUIRE(pointer.x == -50);
    REQUIRE(pointer.y == 320);
    REQUIRE(luil::win32::translate_webview_pointer(layout, 50, 400).inside == false);
}

TEST_CASE("A webview place carries the scale it was arranged at", "[win32][webview]")
{
    // 자리와 배율은 같은 tree에서 나온 한 쌍이다. 표면의 배율을 따로 붙이면
    // 배율이 바뀐 직후 옛 tree의 자리에 새 배율이 붙어 페이지가 두 번 리플로한다.
    const rect_f bounds { 100.0f, 80.0f, 300.0f, 220.0f };
    const auto scaled { plan_webview_layout(bounds, 800, 600, false, 2.0f) };
    REQUIRE(scaled.scale == 2.0f);
    // 자리는 이미 물리 픽셀이라 배율을 다시 곱하지 않는다.
    REQUIRE(scaled.width == 300);
    REQUIRE(scaled.height == 220);

    // 배율만 달라도 다른 자리다 — 그래야 호스트가 래스터 배율을 다시 준다.
    const auto unscaled { plan_webview_layout(bounds, 800, 600, false, 1.0f) };
    REQUIRE(scaled != unscaled);

    // 0 이하는 1로 본다 (element의 규칙과 같다).
    REQUIRE(plan_webview_layout(bounds, 800, 600, false, 0.0f).scale == 1.0f);
    REQUIRE(plan_webview_layout(bounds, 800, 600, false, -1.0f).scale == 1.0f);
}
