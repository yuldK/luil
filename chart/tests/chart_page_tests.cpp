#include "demo/chart_page.h"
#include "mobile_demo/chart_page.h"

#include "chart_layout.h"
#include "luil/chart.h"
#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/label_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 두 데모의 차트 페이지다 (docs/chart-design.md의 확인 절). 페이지가 내는 메시지를 페이지에 되돌려 상태 label과 차트의 접근 글을 잰다.
// 차트 페이지는 차트를 링크하지 않는 core test에 들지 못해 여기에 있다.
namespace {
    constexpr float page_width { 700.0f };
    constexpr float page_height { 900.0f };

    // 두 페이지를 같은 말로 재는 틀이다. 다른 것은 짓는 함수와 owner의 접두뿐이다.
    struct desktop_page
    {
        demo::chart_page page {};

        [[nodiscard]] luil::ui_tree tree()
        {
            return luil::make_arranged_tree(page.build(), { 0.0f, 0.0f, page_width, page_height }, 1.0f);
        }

        [[nodiscard]] static std::u8string name(const std::u8string_view owner)
        {
            return u8"chart/" + std::u8string { owner };
        }

        [[nodiscard]] static luil::ui_element_kind text_kind() noexcept
        {
            return demo::kind_text;
        }
    };

    struct mobile_page
    {
        mobile_demo::chart_page page {};

        [[nodiscard]] luil::ui_tree tree()
        {
            mobile_demo::page_content content { page.build(page_width) };
            return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, page_width, content.height }, 1.0f);
        }

        [[nodiscard]] static std::u8string name(const std::u8string_view owner)
        {
            return std::u8string { owner };
        }

        [[nodiscard]] static luil::ui_element_kind text_kind() noexcept
        {
            return mobile_demo::kind_text;
        }
    };

    template<typename Page>
    [[nodiscard]] std::u8string label_text(const luil::ui_tree& tree, const std::u8string_view owner)
    {
        const luil::ui_element* const label { tree.find({ Page::text_kind(), Page::name(owner) }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    template<typename Page>
    [[nodiscard]] const luil::chart_element& chart_of(const luil::ui_tree& tree)
    {
        const auto* const chart { dynamic_cast<const luil::chart_element*>(tree.find({ luil::ui_element_kind::chart, Page::name(u8"sales") })) };
        REQUIRE(chart != nullptr);
        return *chart;
    }

    template<typename Page>
    [[nodiscard]] const luil::ui_element& plot_of(const luil::ui_tree& tree)
    {
        const luil::ui_element* const plot { tree.find({ luil::ui_element_kind::chart_plot, Page::name(u8"sales") }) };
        REQUIRE(plot != nullptr);
        return *plot;
    }

    // 낸 메시지를 페이지에 되돌린다. 앱 메시지만 있어야 한다.
    template<typename Page>
    void deliver(Page& page, const std::vector<luil::input_action>& actions)
    {
        REQUIRE(actions.empty() == false);
        for (const luil::input_action& action : actions)
        {
            const auto* const message { std::get_if<luil::app_message>(&action) };
            REQUIRE(message != nullptr);
            REQUIRE(page.page.handle(*message));
        }
    }

    template<typename Page>
    void press_right(Page& page)
    {
        const luil::ui_tree tree { page.tree() };
        const luil::key_step_target* const target { plot_of<Page>(tree).key_step() };
        REQUIRE(target != nullptr);
        const std::optional<std::vector<luil::input_action>> actions { target->on_step(luil::value_step::increase) };
        REQUIRE(actions.has_value());
        deliver(page, *actions);
    }

    template<typename Page>
    void press_element(Page& page, const luil::ui_element_id& id)
    {
        const luil::ui_tree tree { page.tree() };
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const click { element->action(luil::ui_trigger::left_click) };
        REQUIRE(click != nullptr);
        deliver(page, (*click)({ id }));
    }

    template<typename Page>
    void choose_kind(Page& page, const std::u8string_view kind)
    {
        press_element(page, { luil::ui_element_kind::choice, Page::name(u8"kind") + u8"-" + std::u8string { kind } });
    }

    template<typename Page>
    void toggle_large(Page& page)
    {
        press_element(page, { luil::ui_element_kind::check, Page::name(u8"large") });
    }

    template<typename Page>
    void check_page()
    {
        Page page {};

        SECTION("The first screen has twelve months and no chosen point")
        {
            const luil::ui_tree tree { page.tree() };
            REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 없음");
            const luil::access_info info { chart_of<Page>(tree).accessibility() };
            REQUIRE(info.role == luil::access_role::image);
            REQUIRE(info.name == u8"매출과 비용");
            REQUIRE(info.value == u8"계열 2개, 범주 12개, 최솟값 60, 최댓값 200");
            REQUIRE(label_text<Page>(tree, u8"summary") == info.value);
            REQUIRE(plot_of<Page>(tree).accessibility().role == luil::access_role::slider);
            REQUIRE(plot_of<Page>(tree).accessibility().value.empty());
            REQUIRE(plot_of<Page>(tree).children().size() == 12u);
        }

        SECTION("The keys walk the points and the label says what the plot says")
        {
            press_right(page);
            {
                const luil::ui_tree tree { page.tree() };
                REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 매출, 1월, 80 · 계열 2 · 범주 12");
                REQUIRE(plot_of<Page>(tree).accessibility().value == u8"매출, 1월, 80");
            }
            press_right(page);
            press_right(page);
            const luil::ui_tree tree { page.tree() };
            REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 매출, 3월, 120 · 계열 2 · 범주 12");
            REQUIRE(plot_of<Page>(tree).accessibility().value == u8"매출, 3월, 120");
        }

        SECTION("A chosen point survives a change of kind")
        {
            press_right(page);
            choose_kind(page, u8"area");
            const luil::ui_tree tree { page.tree() };
            REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 매출, 1월, 80 · 계열 2 · 범주 12");
        }

        SECTION("The kind changes how the sales series is drawn")
        {
            const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
            const luil::ui_color sales { luil::chart_layout::series_table_color(true, 0u) };
            // 첫 범주의 "매출" 막대 자리에서 바닥 위쪽 한 곳이다. 비용 막대 왼쪽에 서고, 선 계열은 그 높이를 지나지 않는다.
            const auto sales_pixel = [&](const luil::ui_tree& tree) {
                const luil::rect_f plot { chart_of<Page>(tree).plot_bounds() };
                const float band { plot.width / 12.0f };
                const int x { static_cast<int>(plot.x + band * (0.5f - 0.35f / 2.0f)) };
                const int y { static_cast<int>(plot.y + plot.height * 0.95f) };
                // 페이지가 흐를 여유까지 칠할 만큼 크게 잡는다.
                luil::testing::raster_frame frame { static_cast<int>(page_width), 1600, palette, 1.0f };
                frame.draw(tree, {});
                return frame.pixel_at(x, y);
            };
            REQUIRE(sales_pixel(page.tree()) != sales);
            choose_kind(page, u8"bar");
            REQUIRE(sales_pixel(page.tree()) == sales);
            choose_kind(page, u8"area");
            // 영역은 알파 0.25로 칠해 계열색과 바탕 사이의 색이다.
            const luil::ui_color area { sales_pixel(page.tree()) };
            REQUIRE(area != sales);
            REQUIRE(area != palette.window_background);
            choose_kind(page, u8"line");
            REQUIRE(sales_pixel(page.tree()) != sales);
        }

        SECTION("The large data turns on with a hundred thousand points and clears the chosen point")
        {
            press_right(page);
            toggle_large(page);
            {
                const luil::ui_tree tree { page.tree() };
                REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 없음");
                REQUIRE(label_text<Page>(tree, u8"summary").starts_with(u8"계열 2개, 범주 100000개"));
                // 점이 10만 개여도 칸은 플롯 폭을 넘지 않는다.
                REQUIRE(plot_of<Page>(tree).children().size() <= static_cast<std::size_t>(chart_of<Page>(tree).plot_bounds().width / 4.0f));
            }
            // 이름표가 없는 범주는 번호로 읽는다.
            press_right(page);
            {
                const luil::ui_tree tree { page.tree() };
                REQUIRE(label_text<Page>(tree, u8"state").starts_with(u8"고른 점: 매출, 1, "));
                REQUIRE(label_text<Page>(tree, u8"state").ends_with(u8" · 계열 2 · 범주 100000"));
            }
            toggle_large(page);
            const luil::ui_tree tree { page.tree() };
            REQUIRE(label_text<Page>(tree, u8"state") == u8"고른 점: 없음");
            REQUIRE(label_text<Page>(tree, u8"summary") == u8"계열 2개, 범주 12개, 최솟값 60, 최댓값 200");
        }

        SECTION("A message from another page is not taken")
        {
            REQUIRE(page.page.handle(luil::app_message { 1 }) == false);
        }
    }
}

TEST_CASE("The desktop chart page turns its messages into the labels", "[chart][demo]")
{
    check_page<desktop_page>();
}

TEST_CASE("The mobile chart page turns its messages into the labels", "[chart][demo]")
{
    check_page<mobile_page>();
}

TEST_CASE("The desktop chart page scrolls its body", "[chart][demo]")
{
    demo::chart_page page {};
    REQUIRE(page.handle(luil::app_message { demo::chart_scroll_intent { 40.0f } }));
    REQUIRE(page.handle(luil::app_message { demo::chart_scroll_intent { -500.0f } }));
}

TEST_CASE("The mobile chart page key is a page of its own", "[chart][demo]")
{
    REQUIRE(mobile_demo::page_chart == u8"chart");
    REQUIRE(demo::page_chart == u8"chart");
}
