#include "macos/window_rules.h"

#include "luil/ui/caption_element.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

// macOS 주 창·popup의 값 규칙이다 (src/macos/window_rules.h). AppKit을 모르는 순수 층이라 core test에서 돈다.
namespace {
    using luil::caption_hit;

    // 주 화면(2560×1600, 메뉴 막대 아래가 작업 영역)과 그 오른쪽 화면이다.
    [[nodiscard]] std::vector<luil::display_info> two_displays()
    {
        return {
            luil::display_info { u8"display-1", { 0, 0, 2560, 1600 }, { 0, 50, 2560, 1600 }, 2.0f, true },
            luil::display_info { u8"display-2", { 2560, 0, 4480, 1080 }, { 2560, 0, 4480, 1080 }, 1.0f, false },
        };
    }

    // caption을 그리는 데스크톱으로 test 동안 둔다. caption_element는 만들 때 플랫폼을 본다.
    class caption_platform
    {
    public:
        caption_platform()
            : previous_ { luil::current_ui_platform() }
        {
            luil::ui_platform platform { previous_ };
            platform.window_caption = true;
            luil::set_ui_platform(platform);
        }

        caption_platform(const caption_platform&) = delete;
        caption_platform& operator=(const caption_platform&) = delete;

        ~caption_platform()
        {
            luil::set_ui_platform(previous_);
        }

    private:
        luil::ui_platform previous_ {};
    };

    // 너비 `width`(논리 픽셀)의 창 맨 위에 caption을 둔 tree다. `offset_y`만큼 내려 둘 수 있다.
    [[nodiscard]] luil::ui_tree caption_tree(const float width, const float scale, luil::caption_config config, const float offset_y = 0.0f)
    {
        auto root { std::make_unique<luil::root_element>() };
        root->arrange({ { 0.0f, 0.0f, width * scale, 400.0f * scale }, scale });
        const float height { luil::caption_element::height_for(config) * scale };
        auto caption { std::make_unique<luil::caption_element>(std::move(config)) };
        caption->arrange({ { 0.0f, offset_y * scale, width * scale, height }, scale });
        root->add(std::move(caption));
        return luil::ui_tree { std::move(root) };
    }
}

TEST_CASE("a saved main window placement is accepted only on a screen", "[macos][window]")
{
    const std::vector<luil::display_info> displays { two_displays() };
    const luil::window_placement saved { 200, 300, 960, 640, true, false };
    CHECK(luil::macos::accepted_initial_placement(saved, 960, 640, displays) == saved);
    // 오른쪽 화면에 반만 걸쳐도 받는다.
    CHECK(luil::macos::accepted_initial_placement({ 4000, 800, 900, 600 }, 480, 320, displays).has_value());
    // 어느 화면에도 걸치지 않는다.
    CHECK_FALSE(luil::macos::accepted_initial_placement({ 5000, 100, 900, 600 }, 480, 320, displays).has_value());
    CHECK_FALSE(luil::macos::accepted_initial_placement({ 100, 1700, 900, 600 }, 480, 320, displays).has_value());
    // 크기가 없거나 최소보다 작다.
    CHECK_FALSE(luil::macos::accepted_initial_placement({ 100, 100, 0, 600 }, 480, 320, displays).has_value());
    CHECK_FALSE(luil::macos::accepted_initial_placement({ 100, 100, 959, 640 }, 960, 640, displays).has_value());
    // 손상된 저장값의 끝이 32-bit 좌표를 넘는다.
    CHECK_FALSE(luil::macos::accepted_initial_placement({ 2'147'483'000, 100, 900, 600 }, 480, 320, displays).has_value());
    CHECK_FALSE(luil::macos::accepted_initial_placement(saved, 480, 320, {}).has_value());
}

TEST_CASE("a borderless window drags from the caption band of its tree", "[macos][window][caption]")
{
    const caption_platform platform {};
    luil::caption_config config {};
    config.title = u8"tool";
    config.app_buttons.push_back(luil::caption_app_button { u8"pin", 0xEB2B, u8"Pin", [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; } });
    const float scale { 2.0f };
    const luil::ui_tree tree { caption_tree(320.0f, scale, config) };
    const int width { 640 };
    // 창 단추는 오른쪽 끝에서 닫기 → 확대 → 최소화다 (단추 폭 42 × 배율 2).
    CHECK(luil::macos::custom_caption_hit(&tree, 630.0f, 10.0f, width, scale) == caption_hit::close);
    CHECK(luil::macos::custom_caption_hit(&tree, 640.0f - 84.0f - 1.0f, 10.0f, width, scale) == caption_hit::maximize);
    CHECK(luil::macos::custom_caption_hit(&tree, 640.0f - 168.0f - 1.0f, 10.0f, width, scale) == caption_hit::minimize);
    // 앱 단추는 최소화 왼쪽에 서고 `caption_client`라 tree의 것이다.
    CHECK(luil::macos::custom_caption_hit(&tree, 640.0f - 336.0f + 4.0f, 10.0f, width, scale) == caption_hit::client);
    // 제목 자리는 끌기, caption 아래는 내용이다.
    CHECK(luil::macos::custom_caption_hit(&tree, 60.0f, 10.0f, width, scale) == caption_hit::drag);
    CHECK(luil::macos::custom_caption_hit(&tree, 60.0f, 72.0f + 1.0f, width, scale) == caption_hit::client);
}

TEST_CASE("the caption band follows where the tree placed it", "[macos][window][caption]")
{
    const caption_platform platform {};
    luil::caption_config config {};
    config.buttons = { .minimize = false, .maximize = false, .close = true };
    const float scale { 1.0f };
    // 창 아래에 둔 caption이다.
    const luil::ui_tree tree { caption_tree(300.0f, scale, config, 200.0f) };
    CHECK(luil::macos::custom_caption_hit(&tree, 20.0f, 10.0f, 300, scale) == caption_hit::client);
    CHECK(luil::macos::custom_caption_hit(&tree, 20.0f, 210.0f, 300, scale) == caption_hit::drag);
    // 빠진 창 단추는 자리를 차지하지 않는다. 확대·최소화 자리였던 곳도 끌기다.
    CHECK(luil::macos::custom_caption_hit(&tree, 290.0f, 210.0f, 300, scale) == caption_hit::close);
    CHECK(luil::macos::custom_caption_hit(&tree, 300.0f - 42.0f - 10.0f, 210.0f, 300, scale) == caption_hit::drag);
}

TEST_CASE("a tree without a caption has nothing to drag", "[macos][window][caption]")
{
    auto root { std::make_unique<luil::root_element>() };
    root->arrange({ { 0.0f, 0.0f, 400.0f, 300.0f }, 1.0f });
    const luil::ui_tree tree { std::move(root) };
    CHECK(luil::macos::custom_caption_hit(&tree, 10.0f, 10.0f, 400, 1.0f) == caption_hit::client);
    CHECK(luil::macos::custom_caption_hit(nullptr, 10.0f, 10.0f, 400, 1.0f) == caption_hit::client);
}

TEST_CASE("the title double click follows the system setting", "[macos][window]")
{
    CHECK(luil::macos::parse_title_double_click(u8"Maximize") == luil::macos::title_double_click::zoom);
    CHECK(luil::macos::parse_title_double_click(u8"Minimize") == luil::macos::title_double_click::minimize);
    CHECK(luil::macos::parse_title_double_click(u8"None") == luil::macos::title_double_click::none);
    CHECK(luil::macos::parse_title_double_click(u8"") == luil::macos::title_double_click::zoom);
}

TEST_CASE("a caption drag moves the window by the pointer and keeps its size", "[macos][window]")
{
    const luil::screen_rect start { 100, 200, 500, 300 };
    const luil::screen_rect moved { luil::macos::dragged_window_bounds(start, { 150, 210 }, { 170, 190 }) };
    CHECK(moved == luil::screen_rect { 120, 180, 520, 280 });
    // 앱이 너비를 바꿔 적어도 크기는 그대로이고 왼쪽 위만 쓴다.
    CHECK(luil::macos::moving_bounds_kept_size(moved, { 0, 50, 10, 60 }) == luil::screen_rect { 0, 50, 400, 150 });
}

TEST_CASE("a popup stands on the screen at its anchor offset", "[macos][popup]")
{
    const luil::screen_area work { 0, 50, 2560, 1600 };
    // 작은 창 오른쪽 곁의 카드는 창 밖에 그대로 선다.
    const luil::macos::popup_screen_frame side { luil::macos::place_popup_on_screen({ 1000, 400 }, 640, 0, 480, 600, work, false) };
    CHECK(side.x == 1640);
    CHECK(side.y == 400);
    CHECK(side.height == 600);
    // 화면 오른쪽 끝을 넘으면 안으로 민다.
    const luil::macos::popup_screen_frame pushed { luil::macos::place_popup_on_screen({ 2200, 400 }, 300, 0, 480, 600, work, false) };
    CHECK(pushed.x == 2560 - 480);
    // 하위 메뉴는 앵커 popup의 왼쪽으로 뒤집는다.
    const luil::macos::popup_screen_frame submenu { luil::macos::place_popup_on_screen({ 2200, 400 }, 300, 0, 480, 600, work, true) };
    CHECK(submenu.x == 2200 - 480);
    // 작업 영역보다 긴 판은 그 높이로 줄고 위가 메뉴 막대 아래에 붙는다.
    const luil::macos::popup_screen_frame tall { luil::macos::place_popup_on_screen({ 100, 20 }, 0, 0, 300, 3000, work, false) };
    CHECK(tall.height == 1550);
    CHECK(tall.y == 50);
}
