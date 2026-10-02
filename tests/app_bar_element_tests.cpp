#include "luil/ui/app_bar_element.h"
#include "luil/ui/caption_element.h"
#include "luil/ui/ui_platform.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {
    // 시험이 바꾼 플랫폼 성질을 데스크톱 기본값으로 되돌린다.
    // 성질은 프로세스 전역이라 남기면 뒤의 시험이 모바일에서 돈다.
    struct platform_scope
    {
        explicit platform_scope(const luil::ui_platform& platform)
        {
            luil::set_ui_platform(platform);
        }

        platform_scope(const platform_scope&) = delete;
        platform_scope(platform_scope&&) = delete;
        platform_scope& operator=(const platform_scope&) = delete;
        platform_scope& operator=(platform_scope&&) = delete;

        ~platform_scope()
        {
            luil::set_ui_platform({});
        }
    };

    constexpr luil::ui_platform mobile { .form_factor = luil::ui_form_factor::mobile, .window_caption = false };

    [[nodiscard]] luil::app_bar_button make_button(const char32_t glyph, std::u8string label)
    {
        return luil::app_bar_button { .glyph = glyph, .label = std::move(label) };
    }
} // namespace

TEST_CASE("The platform is a desktop with a window caption unless the host says otherwise", "[ui][platform]")
{
    const luil::ui_platform platform { luil::current_ui_platform() };
    REQUIRE(platform.form_factor == luil::ui_form_factor::desktop);
    REQUIRE(platform.window_caption);
}

TEST_CASE("A window caption takes no room on a platform without one", "[ui][platform][caption]")
{
    const luil::caption_config config {};
    REQUIRE(luil::caption_element::height_for(config) == static_cast<float>(config.metrics.height));

    const platform_scope scope { mobile };
    REQUIRE(luil::caption_element::height_for(config) == 0.0f);

    auto caption { std::make_unique<luil::caption_element>(config) };
    caption->arrange({ { 0.0f, 0.0f, 400.0f, 0.0f }, 2.0f });
    REQUIRE(caption->bounds().height == 0.0f);
    // 창 버튼도 크기 0이라 눌리지 않는다.
    const luil::ui_tree tree { std::move(caption) };
    const luil::ui_element* const close { tree.find(luil::caption_button_element_id(luil::caption_button_hover::close)) };
    REQUIRE(close != nullptr);
    REQUIRE(close->bounds().height == 0.0f);
}

TEST_CASE("An app bar puts navigation on the left and actions on the right", "[ui][app-bar]")
{
    const luil::app_bar_config config {
        .title = u8"제목",
        .navigation = make_button(0xEA9Bu, u8"뒤로"),
        .actions = { make_button(0xEA7Bu, u8"찾기"), make_button(0xEA7Cu, u8"더 보기") },
    };
    constexpr float scale { 2.0f };
    auto app_bar { std::make_unique<luil::app_bar_element>(config) };
    app_bar->arrange({ { 0.0f, 10.0f, 800.0f, 0.0f }, scale });
    REQUIRE(app_bar->bounds().y == 10.0f);
    REQUIRE(app_bar->bounds().height == luil::app_bar_element::height_for(config) * scale);
    const luil::ui_tree tree { std::move(app_bar) };

    const luil::app_bar_metrics& metrics { config.metrics };
    const float button { static_cast<float>(metrics.button_size) * scale };
    const float edge { static_cast<float>(metrics.edge_padding) * scale };
    const float top { 10.0f + (static_cast<float>(metrics.height) * scale - button) / 2.0f };

    const luil::ui_element* const navigation { tree.find({ luil::ui_element_kind::app_bar_button, u8"navigation" }) };
    REQUIRE(navigation != nullptr);
    REQUIRE(navigation->bounds().x == edge);
    REQUIRE(navigation->bounds().y == top);
    REQUIRE(navigation->bounds().width == button);

    // 동작 버튼은 오른쪽 끝부터 쌓이고, 목록의 앞 것이 왼쪽에 온다.
    const luil::ui_element* const first { tree.find({ luil::ui_element_kind::app_bar_button, u8"action:0" }) };
    const luil::ui_element* const second { tree.find({ luil::ui_element_kind::app_bar_button, u8"action:1" }) };
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(second->bounds().x == 800.0f - edge - button);
    REQUIRE(first->bounds().x == 800.0f - edge - 2.0f * button);
    REQUIRE(first->bounds().y == top);
}

TEST_CASE("An app bar reads as a title bar with its title", "[ui][app-bar][accessibility]")
{
    const luil::app_bar_element app_bar { luil::app_bar_config { .title = u8"hello luil" } };
    const luil::access_info info { app_bar.accessibility() };
    REQUIRE(info.role == luil::access_role::title_bar);
    REQUIRE(info.name == u8"hello luil");
}
