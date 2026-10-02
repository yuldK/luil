#include "win32/window_mode.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

TEST_CASE("Updating the window frame preserves visibility and unrelated state", "[win32][window]")
{
    // WS_VISIBLE, WS_DISABLED, WS_MINIMIZE: 프레임 계산이 지우면 안 되는 상태다.
    constexpr luil::win32::window_style_bits state { 0x10000000UL | 0x08000000UL | 0x20000000UL };
    using luil::win32::window_display_mode;
    const auto current { state | luil::win32::style_overlapped_window | luil::win32::style_maximize };
    const auto full { luil::win32::updated_custom_window_style(current, {}, window_display_mode::fullscreen) };
    REQUIRE((full & state) == state);
    REQUIRE((full & (luil::win32::style_thick_frame | luil::win32::style_maximize | luil::win32::style_caption)) == 0);
    const auto normal { luil::win32::updated_custom_window_style(full, {}, window_display_mode::normal) };
    REQUIRE((normal & state) == state);
    REQUIRE((normal & luil::win32::style_thick_frame) != 0);
    REQUIRE((normal & luil::win32::style_maximize) == 0);
    const auto maximized { luil::win32::updated_custom_window_style(current, { .maximize = false }, window_display_mode::maximized) };
    REQUIRE((maximized & state) == state);
    REQUIRE((maximized & luil::win32::style_maximize) != 0);
    REQUIRE((maximized & luil::win32::style_maximize_box) == 0);
}

namespace {
    using luil::win32::window_display_mode;
    using luil::win32::window_hit;

    // 기본 캡션·기본 테두리의 창이다 (논리 픽셀).
    // 판정 test는 전부 이 치수와 96 DPI를 쓴다 — 배율은 caption_layout이 이미 잰다.
    [[nodiscard]] luil::win32::window_frame_metrics make_metrics()
    {
        return luil::win32::window_frame_metrics {};
    }

    // 1000x800 창의 (x, y)를 이 모드로 판정한다.
    [[nodiscard]] window_hit hit(const window_display_mode mode, const int x, const int y)
    {
        return luil::win32::hit_test_window(make_metrics(), mode, 1000, 800, 96, x, y);
    }
} // namespace

TEST_CASE("The window style is one computation of buttons and mode", "[win32][window-mode]")
{
    SECTION("통상 창은 버튼 집합만 본다")
    {
        constexpr luil::caption_buttons all {};
        const luil::win32::window_style_bits style { luil::win32::window_style_for(all, window_display_mode::normal) };
        REQUIRE((style & luil::win32::style_minimize_box) != 0);
        REQUIRE((style & luil::win32::style_maximize_box) != 0);
        REQUIRE((style & luil::win32::retained_window_styles) == luil::win32::retained_window_styles);
        // 최대화 창의 스타일은 통상 창과 같다 — 최대화는 스타일이 아니라 `showCmd`다.
        REQUIRE(luil::win32::window_style_for(all, window_display_mode::maximized) == style);
    }

    SECTION("전체 화면은 크기 조절과 최대화를 스타일에서 뺀다")
    {
        // 판정만으로 막으면 Alt+Space의 "크기 조정"과 Win+↑가 그 판정을 지나지 않고
        // 창을 바꾼다 — 화면과 상태가 갈라지는 자리다.
        const luil::win32::window_style_bits style { luil::win32::window_style_for({}, window_display_mode::fullscreen) };
        REQUIRE((style & luil::win32::style_thick_frame) == 0);
        REQUIRE((style & luil::win32::style_maximize_box) == 0);
        // 빠져나갈 길은 남긴다 (Alt+Tab의 최소화, Alt+F4의 닫기).
        REQUIRE((style & luil::win32::style_minimize_box) != 0);
        REQUIRE((style & luil::win32::style_system_menu) != 0);
    }

    SECTION("시스템 캡션은 어느 모드에서도 없다")
    {
        // custom caption 창은 만들 때 한 번 뗀 뒤로 다시 붙지 않는다.
        for (const window_display_mode mode : { window_display_mode::normal, window_display_mode::maximized, window_display_mode::fullscreen })
            REQUIRE((luil::win32::custom_window_style_for({}, mode) & luil::win32::style_caption) == 0);
    }

    SECTION("버튼을 뺀 창이 전체 화면이어도 두 규칙이 함께 산다")
    {
        // 이 조합이 곧 `set_caption`의 함정이다: 버튼 집합이 바뀔 때마다 스타일을
        // 통째로 다시 쓰므로, 전체 화면이 스타일 계산의 **인자**가 아니면 그 순간
        // 지워진다.
        constexpr luil::caption_buttons close_only { .minimize = false, .maximize = false, .close = true };
        const luil::win32::window_style_bits style { luil::win32::custom_window_style_for(close_only, window_display_mode::fullscreen) };
        REQUIRE((style & luil::win32::style_minimize_box) == 0);
        REQUIRE((style & luil::win32::style_maximize_box) == 0);
        REQUIRE((style & luil::win32::style_thick_frame) == 0);
        REQUIRE((style & luil::win32::style_system_menu) != 0);
    }

    SECTION("확장 스타일은 전체 화면에서만 테두리 비트를 뗀다")
    {
        // 앱이 준 `WS_EX_APPWINDOW`(0x00040000)에 OS가 스타일을 보고 얹은 테두리
        // 비트가 함께 실린 값이다 — 확장 스타일의 나머지는 우리 것이 아니라 그대로 둔다.
        constexpr luil::win32::window_style_bits application_window { 0x00040000UL };
        constexpr luil::win32::window_style_bits given { application_window | luil::win32::extended_style_window_edge };
        REQUIRE(luil::win32::extended_window_style_for(given, window_display_mode::normal) == given);
        REQUIRE(luil::win32::extended_window_style_for(given, window_display_mode::fullscreen) == application_window);
    }
}

TEST_CASE("Leaving fullscreen recomputes the style instead of replaying it", "[win32][window-mode]")
{
    // 나올 때의 스타일은 갈무리해 둔 비트가 아니라 **지금 버튼 집합과 통상 모드**가
    // 정하는 값이다. 들어갈 때가 같은 계산을 쓰므로 두 방향이 한 식이 된다 —
    // 한쪽만 계산이면 그 사이에 생긴 변화가 다른 쪽에서 조용히 뒤집힌다.
    SECTION("전체 화면 동안 버튼이 빠졌으면 나올 때도 그 버튼이 없다")
    {
        // 들어갈 때의 창은 버튼이 다 있었다.
        constexpr luil::caption_buttons entered {};
        // 전체 화면 앱이 흔히 하는 일이다: 화면을 덮은 뒤 최대화 버튼을 접는다.
        constexpr luil::caption_buttons published { .minimize = true, .maximize = false, .close = true };
        const luil::win32::window_style_bits leaving { luil::win32::custom_window_style_for(published, window_display_mode::normal) };
        // 갈무리한 비트를 그대로 되돌렸다면 `WS_MAXIMIZEBOX`가 되살아난다 — 캡션은
        // 최대화 버튼을 그리지 않는데 Win+↑·캡션 더블클릭·시스템 메뉴의 최대화만
        // 사는 창이고, 버튼 집합이 다시 바뀌기 전까지 그 어긋남은 지워지지 않는다.
        const luil::win32::window_style_bits replayed { luil::win32::custom_window_style_for(entered, window_display_mode::normal) };
        REQUIRE((replayed & luil::win32::style_maximize_box) != 0);
        REQUIRE((leaving & luil::win32::style_maximize_box) == 0);
        // 전체 화면이 뺐던 것은 함께 돌아온다 (크기 조절·시스템 메뉴·최소화).
        REQUIRE((leaving & luil::win32::retained_window_styles) == luil::win32::retained_window_styles);
        REQUIRE((leaving & luil::win32::style_minimize_box) != 0);
    }

    SECTION("버튼이 그대로면 들어가고 나오며 오간 비트만 되돌아온다")
    {
        constexpr luil::caption_buttons buttons { .minimize = false, .maximize = true, .close = true };
        // 나올 때의 목표는 통상 모드다. 갈무리한 배치가 최대화여도 스타일은 같다 —
        // 최대화는 스타일이 아니라 `showCmd`이기 때문이다.
        REQUIRE(luil::win32::custom_window_style_for(buttons, window_display_mode::normal) == luil::win32::custom_window_style_for(buttons, window_display_mode::maximized));
        const luil::win32::window_style_bits difference {
            luil::win32::custom_window_style_for(buttons, window_display_mode::normal) ^ luil::win32::custom_window_style_for(buttons, window_display_mode::fullscreen),
        };
        REQUIRE(difference == (luil::win32::style_thick_frame | luil::win32::style_maximize_box));
    }

    SECTION("다시 계산한 스타일에는 최대화 표식이 없다")
    {
        // 최대화된 창에서 들어가면 `WS_MAXIMIZE`가 전체 화면 내내 남는다 (자리를
        // 직접 준 `SetWindowPos`는 그것을 지우지 않는다). 남은 채로 나오면
        // `SetWindowPlacement`가 이미 최대화된 창을 다시 최대화하지 못해 창이 화면을
        // 덮은 크기 그대로 앉는다 — 이 계산의 값을 **통째로** 쓰는 것이 곧 그 표식을
        // 지우는 일이라, 계산이 그 비트를 만들지 않는다는 것이 나올 때의 자리를
        // 결정적으로 만든다.
        for (const window_display_mode mode : { window_display_mode::normal, window_display_mode::maximized, window_display_mode::fullscreen })
            REQUIRE((luil::win32::custom_window_style_for({}, mode) & luil::win32::style_maximize) == 0);
        REQUIRE((luil::win32::window_style_for({ .minimize = false, .maximize = false, .close = true }, window_display_mode::normal) & luil::win32::style_maximize) == 0);
    }
}

TEST_CASE("The window mode is one value that fullscreen wins", "[win32][window-mode]")
{
    REQUIRE(luil::win32::window_mode_of(false, false) == window_display_mode::normal);
    REQUIRE(luil::win32::window_mode_of(true, false) == window_display_mode::maximized);
    REQUIRE(luil::win32::window_mode_of(false, true) == window_display_mode::fullscreen);
    // 최대화된 창에서 들어가면 `WS_MAXIMIZE`가 남아 `IsZoomed`가 계속 참이다.
    // 그때 최대화로 보면 크기 한계가 작업 영역으로 잘려 모니터를 덮지 못한다.
    REQUIRE(luil::win32::window_mode_of(true, true) == window_display_mode::fullscreen);
}

TEST_CASE("The non-client hit answer depends on the window mode", "[win32][window-mode]")
{
    SECTION("통상 창은 가장자리·모서리·시스템 메뉴·버튼·끌기를 모두 낸다")
    {
        REQUIRE(hit(window_display_mode::normal, 2, 400) == window_hit::resize_left);
        REQUIRE(hit(window_display_mode::normal, 998, 400) == window_hit::resize_right);
        REQUIRE(hit(window_display_mode::normal, 500, 1) == window_hit::resize_top);
        REQUIRE(hit(window_display_mode::normal, 500, 799) == window_hit::resize_bottom);
        REQUIRE(hit(window_display_mode::normal, 1, 1) == window_hit::resize_top_left);
        REQUIRE(hit(window_display_mode::normal, 999, 1) == window_hit::resize_top_right);
        REQUIRE(hit(window_display_mode::normal, 1, 799) == window_hit::resize_bottom_left);
        REQUIRE(hit(window_display_mode::normal, 999, 799) == window_hit::resize_bottom_right);
        // 아이콘 자리는 시스템 메뉴, 그 오른쪽 캡션은 끌기다.
        REQUIRE(hit(window_display_mode::normal, 20, 20) == window_hit::system_menu);
        REQUIRE(hit(window_display_mode::normal, 300, 20) == window_hit::caption_drag);
        REQUIRE(hit(window_display_mode::normal, 880, 20) == window_hit::minimize_button);
        REQUIRE(hit(window_display_mode::normal, 930, 20) == window_hit::maximize_button);
        REQUIRE(hit(window_display_mode::normal, 980, 20) == window_hit::close_button);
        REQUIRE(hit(window_display_mode::normal, 300, 400) == window_hit::client);
    }

    SECTION("최대화 창은 가장자리만 잃고 캡션은 남는다")
    {
        // 이미 작업 영역에 맞물려 조절할 것이 없다.
        // 캡션은 살아 있어야 끌어 내려 복원할 수 있다.
        REQUIRE(hit(window_display_mode::maximized, 2, 400) == window_hit::client);
        REQUIRE(hit(window_display_mode::maximized, 1, 1) == window_hit::system_menu);
        REQUIRE(hit(window_display_mode::maximized, 300, 20) == window_hit::caption_drag);
        REQUIRE(hit(window_display_mode::maximized, 980, 20) == window_hit::close_button);
    }

    SECTION("전체 화면은 어디를 눌러도 client다")
    {
        // 통상 창이라면 다른 답이 나왔을 자리들을 그대로 다시 묻는다.
        // 가장자리가 남아 있으면 화면 끝을 노려 누르다 창 크기가 바뀌고, 캡션 띠가
        // 남아 있으면 끌기 한 번에 전체 화면이 통째로 딸려 나온다.
        REQUIRE(hit(window_display_mode::fullscreen, 0, 0) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 2, 400) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 999, 799) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 20, 20) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 300, 20) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 980, 20) == window_hit::client);
        REQUIRE(hit(window_display_mode::fullscreen, 500, 400) == window_hit::client);
    }

    SECTION("배율이 오르면 캡션과 테두리가 함께 두꺼워진다")
    {
        // 150%에서 캡션 높이는 54px이다 (caption_layout_tests와 같은 값).
        REQUIRE(luil::win32::hit_test_window(make_metrics(), window_display_mode::normal, 1500, 1200, 144, 400, 50) == window_hit::caption_drag);
        REQUIRE(luil::win32::hit_test_window(make_metrics(), window_display_mode::normal, 1500, 1200, 144, 400, 60) == window_hit::client);
    }
}

TEST_CASE("The frame decorations follow the window mode", "[win32][window-mode]")
{
    // 캡션 자리에 남긴 한 줄은 통상 창의 위 테두리이지만 화면을 덮은 창에서는 이음매다.
    REQUIRE(luil::win32::dwm_frame_top_margin_for(window_display_mode::normal) == 1);
    REQUIRE(luil::win32::dwm_frame_top_margin_for(window_display_mode::maximized) == 1);
    REQUIRE(luil::win32::dwm_frame_top_margin_for(window_display_mode::fullscreen) == 0);

    // 최대화 크기의 작업 영역 자르기는 전체 화면에서만 빠진다.
    REQUIRE(luil::win32::maximum_size_follows_work_area(window_display_mode::normal));
    REQUIRE(luil::win32::maximum_size_follows_work_area(window_display_mode::maximized));
    REQUIRE(luil::win32::maximum_size_follows_work_area(window_display_mode::fullscreen) == false);
}

TEST_CASE("Fullscreen targets the whole monitor", "[win32][window-mode]")
{
    // 작업 표시줄을 뺀 rcWork가 아니라 rcMonitor 전체다.
    const luil::screen_area monitor { -1920, 0, 0, 1080 };
    const luil::win32::window_bounds bounds { luil::win32::fullscreen_bounds_for(monitor) };
    // 괄호 한 겹은 전처리기의 몫이다 — 중괄호 안의 쉼표는 매크로 인자를 가르지 못한다.
    REQUIRE((bounds == luil::win32::window_bounds { -1920, 0, 1920, 1080 }));

    // 뒤집힌 사각형은 크기 0으로 접는다 (창을 만들 수 없는 값을 넘기지 않는다).
    const luil::win32::window_bounds folded { luil::win32::fullscreen_bounds_for(luil::screen_area { 10, 10, 0, 0 }) };
    REQUIRE((folded == luil::win32::window_bounds { 10, 10, 0, 0 }));
}

TEST_CASE("Entering fullscreen does not destroy the normal placement", "[win32][window-mode]")
{
    // 사용자가 쓰던 창이다.
    const luil::win32::window_placement normal { 120, 80, 1280, 800, false, false };

    SECTION("전체 화면이 아니면 관측값이 그대로 답이다")
    {
        const luil::win32::window_placement reported { luil::win32::placement_to_report(normal, std::nullopt) };
        REQUIRE(reported == normal);
        REQUIRE(reported.fullscreen == false);
    }

    SECTION("전체 화면 동안의 관측값은 버리고 갈무리한 것을 알린다")
    {
        // 들어갈 때의 SetWindowPos가 rcNormalPosition을 모니터 사각형으로 덮어쓴다.
        // 그대로 저장하면 "나오면 모니터를 덮는 창"이 되어 돌아갈 자리가 사라진다.
        const luil::win32::window_placement observed { 0, 0, 1920, 1080, false, false };
        const luil::win32::window_placement reported { luil::win32::placement_to_report(observed, normal) };
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
        const luil::win32::window_placement restore { 120, 80, 1280, 800, true, false };
        const luil::win32::window_placement observed { 0, 0, 1920, 1080, false, false };
        const luil::win32::window_placement reported { luil::win32::placement_to_report(observed, restore) };
        REQUIRE(reported.maximized);
        REQUIRE(reported.fullscreen);
    }

    SECTION("저장했다가 그대로 넣으면 전체 화면과 돌아갈 자리가 함께 산다")
    {
        // 저장 → 복원 → 다시 보고의 왕복이다.
        const luil::win32::window_placement saved { luil::win32::placement_to_report(luil::win32::window_placement { 0, 0, 1920, 1080 }, normal) };
        REQUIRE(saved.fullscreen);
        // 복원은 saved의 자리·크기로 정상 배치를 놓고 전체 화면에 들어간다 —
        // 그러면 갈무리되는 것이 다시 saved의 자리다.
        const luil::win32::window_placement again { luil::win32::placement_to_report(luil::win32::window_placement { 0, 0, 1920, 1080 }, saved) };
        REQUIRE(again == saved);
    }

    SECTION("관측값에 실려 온 전체 화면 표식은 믿지 않는다")
    {
        // OS는 전체 화면을 알지 못한다 — 참이 실려 있다면 어디선가 흘러든 값이다.
        const luil::win32::window_placement stale { 120, 80, 1280, 800, false, true };
        REQUIRE(luil::win32::placement_to_report(stale, std::nullopt).fullscreen == false);
    }
}
