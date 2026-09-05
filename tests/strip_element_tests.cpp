#include "luil/ui/strip_element.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_strip { luil::application_element_kind(0) };
    constexpr luil::ui_element_kind kind_content { luil::application_element_kind(1) };

    // 받은 자리를 그대로 잡는 최소 내용이다.
    class content_probe final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            // 띠가 자기 스크롤 값을 문맥에 싣지 않는 것을 여기서 본다.
            seen_scroll_offset = context.scroll_offset;
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        float seen_scroll_offset { -1.0f };
    };
} // namespace

TEST_CASE("A strip that is not overflowed keeps its origin and fills the slot", "[ui][strip]")
{
    luil::strip_config config {};
    config.content_width = 200.0f;
    luil::strip_element strip { luil::ui_element_id { kind_strip }, config };

    auto content { std::make_unique<content_probe>(luil::ui_element_id { kind_content }) };
    const content_probe* const probe { content.get() };
    strip.set_content(std::move(content));
    strip.arrange({ { 10.0f, 20.0f, 400.0f, 40.0f }, 1.0f });

    REQUIRE(strip.maximum_scroll() == 0.0f);
    REQUIRE(strip.scroll_offset() == 0.0f);
    // 내용이 짧아도 slot 폭을 다 받는다 (세로 창과 같은 규칙).
    REQUIRE(probe->bounds().x == 10.0f);
    REQUIRE(probe->bounds().width == 400.0f);
    REQUIRE(probe->bounds().y == 20.0f);
    REQUIRE(probe->bounds().height == 40.0f);
    // 문맥의 scroll_offset은 띠가 건드리지 않는다 — 그 값은 세로 창의 것이다.
    REQUIRE(probe->seen_scroll_offset == 0.0f);
}

TEST_CASE("An overflowed strip pushes its content left by the clamped offset", "[ui][strip]")
{
    const auto place = [](const float scale, const float offset) {
        luil::strip_config config {};
        config.content_width = 1000.0f;
        config.scroll_offset = offset;
        auto strip { std::make_unique<luil::strip_element>(luil::ui_element_id { kind_strip }, config) };
        auto content { std::make_unique<content_probe>(luil::ui_element_id { kind_content }) };
        const content_probe* const probe { content.get() };
        strip->set_content(std::move(content));
        strip->arrange({ { 0.0f, 0.0f, 400.0f * scale, 40.0f * scale }, scale });
        return std::tuple { strip->maximum_scroll(), strip->scroll_offset(), probe->bounds().x, probe->bounds().width };
    };

    // 최대치는 논리 픽셀이라 배율과 무관하게 600이다 (1000 - 400).
    for (const float scale : { 1.0f, 1.25f, 2.0f })
    {
        const auto [maximum, offset, x, width] { place(scale, 150.0f) };
        REQUIRE(maximum == 600.0f);
        REQUIRE(offset == 150.0f);
        // 원점은 흘러간 만큼 왼쪽으로 밀리고 내용은 제 폭을 그대로 받는다.
        REQUIRE(x == -150.0f * scale);
        REQUIRE(width == 1000.0f * scale);
    }
}

TEST_CASE("A strip clamps a scroll offset that is out of range", "[ui][strip]")
{
    const auto clamped = [](const float offset) {
        luil::strip_config config {};
        config.content_width = 1000.0f;
        config.scroll_offset = offset;
        luil::strip_element strip { luil::ui_element_id { kind_strip }, config };
        strip.arrange({ { 0.0f, 0.0f, 400.0f, 40.0f }, 1.0f });
        return strip.scroll_offset();
    };

    // 내용이 없어도 담는 쪽이 알려 준 폭으로 다듬는다.
    REQUIRE(clamped(-30.0f) == 0.0f);
    REQUIRE(clamped(900.0f) == 600.0f);
    REQUIRE(clamped(600.0f) == 600.0f);
}

TEST_CASE("A strip hides what falls outside from hit testing", "[ui][strip]")
{
    luil::strip_config config {};
    config.content_width = 1000.0f;
    luil::strip_element strip { luil::ui_element_id { kind_strip }, config };

    auto content { std::make_unique<content_probe>(luil::ui_element_id { kind_content }) };
    content->set_tooltip(u8"내용");
    strip.set_content(std::move(content));
    strip.arrange({ { 0.0f, 0.0f, 400.0f, 40.0f }, 1.0f });

    // 띠 안은 내용이 받고, 오른쪽으로 넘친 부분은 아무도 받지 않는다.
    REQUIRE(strip.hit_test(200.0f, 20.0f) != nullptr);
    REQUIRE(strip.hit_test(600.0f, 20.0f) == nullptr);
}

TEST_CASE("A strip hides drop targets outside its window", "[ui][strip][drag]")
{
    luil::strip_config config {};
    config.content_width = 1000.0f;
    auto strip { std::make_unique<luil::strip_element>(luil::ui_element_id { kind_strip }, config) };
    auto content { std::make_unique<content_probe>(luil::ui_element_id { kind_content }) };
    luil::drop_target drop {};
    drop.accepts = [](const luil::drag_payload&) { return true; };
    drop.on_drop = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    content->set_drop_target(std::move(drop));
    const content_probe* const probe { content.get() };
    strip->set_content(std::move(content));
    strip->arrange({ { 0.0f, 0.0f, 400.0f, 40.0f }, 1.0f });
    const luil::ui_tree tree { std::move(strip) };

    const luil::drag_payload payload { {}, u8"item" };
    REQUIRE(tree.find_drop_target(200.0f, 20.0f, payload) == probe);
    REQUIRE(tree.find_drop_target(600.0f, 20.0f, payload) == nullptr);

    // 강조가 그려질 보이는 영역은 띠와의 교집합이다.
    const auto visible { tree.visible_bounds(*probe) };
    REQUIRE(visible.has_value());
    REQUIRE(visible->x == 0.0f);
    REQUIRE(visible->width == 400.0f);
}

TEST_CASE("A strip answers the offset change that brings a tab into view", "[ui][strip][focus]")
{
    // 세로 창과 같은 식이고 축만 다르다.
    const auto reveal = [](const float scale, const luil::rect_f& target) {
        luil::strip_config config {};
        config.content_width = 1000.0f;
        config.scroll_offset = 150.0f;
        luil::strip_element strip { luil::ui_element_id { kind_strip }, config };
        strip.set_content(std::make_unique<content_probe>(luil::ui_element_id { kind_content }));
        strip.arrange({ { 0.0f, 0.0f, 400.0f * scale, 40.0f * scale }, scale });
        return strip.scroll_delta_to_reveal(target);
    };

    // 띠는 물리 [0, 400)이다.
    REQUIRE(reveal(1.0f, { 100.0f, 0.0f, 80.0f, 40.0f }) == 0.0f);
    // 오른쪽으로 30 넘친 탭은 30 흘린다.
    REQUIRE(reveal(1.0f, { 350.0f, 0.0f, 80.0f, 40.0f }) == 30.0f);
    // 왼쪽으로 벗어난 탭은 음수다.
    REQUIRE(reveal(1.0f, { -20.0f, 0.0f, 80.0f, 40.0f }) == -20.0f);
    // 답은 논리 픽셀이다 — 배율 2에서 물리 60 넘친 탭이 논리 30이다.
    REQUIRE(reveal(2.0f, { 700.0f, 0.0f, 160.0f, 80.0f }) == 30.0f);
}
