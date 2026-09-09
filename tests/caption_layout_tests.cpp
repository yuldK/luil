#include "luil/ui/caption_element.h"
#include "luil/ui/caption_metrics.h"
#include "win32/caption_layout.h"
#include "win32/caption_surface.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Caption button bounds scale with DPI", "[caption]")
{
    const auto layout = luil::win32::make_caption_layout(1000, 96);
    REQUIRE(layout.height == luil::default_caption_ui_metrics.height);
    REQUIRE(layout.button_width == luil::default_caption_ui_metrics.button_width);
    REQUIRE(layout.close_left == 958);
    REQUIRE(layout.maximize_left == 916);
    REQUIRE(layout.minimize_left == 874);

    const auto scaled = luil::win32::make_caption_layout(1500, 144);
    REQUIRE(scaled.height == 54);
    REQUIRE(scaled.button_width == 63);
}

TEST_CASE("Caption hit testing distinguishes drag and system buttons", "[caption]")
{
    const auto layout = luil::win32::make_caption_layout(1000, 96);
    REQUIRE(luil::win32::hit_test_caption(layout, 100, 20) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::minimize);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::maximize);
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 100, 60) == luil::win32::caption_hit::client);
}

TEST_CASE("A caption without some buttons pulls the rest to the right edge", "[caption]")
{
    // 최소화·최대화를 뺀 도구 창 캡션이다.
    // 빠진 버튼은 자리를 차지하지 않고 닫기가 오른쪽 끝에 남는다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, close_only);
    REQUIRE(layout.close_left == 958);
    // 없는 버튼의 왼쪽은 그 시점의 오른쪽 끝이라 어떤 x로도 맞지 않는다.
    REQUIRE(layout.maximize_left == layout.close_left);
    REQUIRE(layout.minimize_left == layout.close_left);

    // 판정은 한 줄도 바뀌지 않았는데 결과가 맞는다.
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::drag);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::drag);
}

TEST_CASE("A caption without the close button keeps the others in order", "[caption]")
{
    // 가운데를 빼는 경우도 순서가 유지되어야 한다.
    constexpr luil::caption_buttons no_maximize { .minimize = true, .maximize = false, .close = true };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, no_maximize);
    REQUIRE(layout.close_left == 958);
    REQUIRE(layout.maximize_left == 958);
    REQUIRE(layout.minimize_left == 916);
    REQUIRE(luil::win32::hit_test_caption(layout, 980, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_caption(layout, 930, 20) == luil::win32::caption_hit::minimize);
    REQUIRE(luil::win32::hit_test_caption(layout, 880, 20) == luil::win32::caption_hit::drag);

    // 버튼이 하나도 없으면 캡션 전체가 끌기다.
    constexpr luil::caption_buttons none { .minimize = false, .maximize = false, .close = false };
    const auto bare = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, none);
    REQUIRE(luil::win32::hit_test_caption(bare, 980, 20) == luil::win32::caption_hit::drag);
}

TEST_CASE("The caption element builds only the configured buttons", "[caption]")
{
    // 그리기 쪽 자리가 hit test 쪽과 같아야 한다 — 두 계산이 같은 집합을 본다.
    luil::caption_config config {};
    config.buttons = { .minimize = false, .maximize = false, .close = true };
    auto tree = luil::make_caption_tree(1000.0f, 1.0f, config);

    REQUIRE(tree.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize }) == nullptr);
    REQUIRE(tree.find(luil::ui_element_id { luil::ui_element_kind::caption_maximize }) == nullptr);
    const luil::ui_element* const close = tree.find(luil::ui_element_id { luil::ui_element_kind::caption_close });
    REQUIRE(close != nullptr);

    const auto layout = luil::win32::make_caption_layout(1000, 96, config.metrics, config.buttons);
    REQUIRE(static_cast<int>(close->bounds().x) == layout.close_left);

    // 기본 설정에서는 셋 다 있고 자리도 그대로다.
    auto full = luil::make_caption_tree(1000.0f, 1.0f, luil::caption_config {});
    const luil::ui_element* const minimize = full.find(luil::ui_element_id { luil::ui_element_kind::caption_minimize });
    REQUIRE(minimize != nullptr);
    REQUIRE(static_cast<int>(minimize->bounds().x) == luil::win32::make_caption_layout(1000, 96).minimize_left);
}

TEST_CASE("A missing caption button drops its window style", "[caption]")
{
    // 버튼을 빼면 캡션 더블클릭·Win+↑·시스템 메뉴까지 함께 멎어야 한다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const DWORD style = luil::win32::window_style_for(close_only, luil::win32::window_display_mode::normal);
    REQUIRE((style & WS_MAXIMIZEBOX) == 0);
    REQUIRE((style & WS_MINIMIZEBOX) == 0);
    // 크기 조절과 시스템 메뉴는 버튼과 무관하다.
    REQUIRE((style & luil::win32::retained_window_styles) == luil::win32::retained_window_styles);
    // 만들 때와 크기를 잴 때 쓰는 스타일은 시스템 캡션 하나만 다르다.
    REQUIRE(luil::win32::custom_window_style_for(close_only, luil::win32::window_display_mode::normal) == (style & ~static_cast<DWORD>(WS_CAPTION)));
}

TEST_CASE("The caption geometry is the same computation the window hit test uses", "[caption][window-mode]")
{
    // 캡션 자리를 두 번 재지 않는다는 계약이다.
    // 비클라이언트 판정은 `make_caption_layout`이 낸 자리를 그대로 쓰므로,
    // 버튼을 뺀 캡션의 빈자리는 판정에서도 그대로 끌기가 된다.
    constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
    const luil::win32::window_frame_metrics metrics { luil::default_caption_ui_metrics, close_only, 4, 10 };
    const auto layout = luil::win32::make_caption_layout(1000, 96, luil::default_caption_ui_metrics, close_only);

    // 닫기 버튼의 왼쪽 경계 바로 오른쪽은 두 계산 모두에서 닫기다.
    REQUIRE(luil::win32::hit_test_caption(layout, layout.close_left + 1, 20) == luil::win32::caption_hit::close);
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::normal, 1000, 800, 96, layout.close_left + 1, 20) == luil::win32::window_hit::close_button);
    // 빠진 최대화 버튼의 자리는 끌기다.
    REQUIRE(luil::win32::hit_test_window(metrics, luil::win32::window_display_mode::normal, 1000, 800, 96, 930, 20) == luil::win32::window_hit::caption_drag);
}
