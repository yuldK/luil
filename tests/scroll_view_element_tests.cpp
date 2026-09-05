#include "luil/ui/scroll_view_element.h"

#include "luil/ui/ui_element.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {
    class content_probe final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            scroll_offset = context.scroll_offset;
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        // 내용이 받은 스크롤 값이다.
        // 가상화가 보이는 범위를 계산할 때 쓰는 값이라 그대로 전해지는지 본다.
        float scroll_offset { -1.0f };
    };
} // namespace

TEST_CASE("clamp_scroll matches the arrange clamp so app state and view agree", "[ui][scroll]")
{
    // arrange가 다듬는 식과 같은 공개 함수다.
    // 앱은 tree를 만들기 전에 이 함수로 상태를 다듬는다.
    STATIC_REQUIRE(luil::clamp_scroll(400.0f, 100.0f, 150.0f) == 150.0f);
    STATIC_REQUIRE(luil::clamp_scroll(400.0f, 100.0f, 500.0f) == 300.0f);
    STATIC_REQUIRE(luil::clamp_scroll(400.0f, 100.0f, -10.0f) == 0.0f);
    // 내용이 창보다 짧으면 흘릴 것이 없다.
    STATIC_REQUIRE(luil::clamp_scroll(50.0f, 100.0f, 30.0f) == 0.0f);
}

TEST_CASE("A scroll view lifts its content by the scrolled amount", "[ui][scroll]")
{
    luil::scroll_view_config config {};
    config.content_height = 500.0f;
    config.scroll_offset = 120.0f;
    luil::scroll_view_element view { luil::ui_element_id { luil::application_element_kind(40) }, config };

    auto content { std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(41) }) };
    const content_probe* const probe { content.get() };
    view.set_content(std::move(content));
    view.arrange({ { 0.0f, 50.0f, 200.0f, 300.0f }, 1.0f });

    REQUIRE(view.viewport().height == 300.0f);
    REQUIRE(view.maximum_scroll() == 200.0f);
    // 내용은 창보다 크고 흘러간 만큼 위로 올라간다.
    REQUIRE(probe->bounds().y == -70.0f);
    REQUIRE(probe->bounds().height == 500.0f);
    REQUIRE(probe->scroll_offset == 120.0f);
}

TEST_CASE("A scroll view clamps the scrolled amount to what actually overflows", "[ui][scroll]")
{
    luil::scroll_view_config config {};
    config.content_height = 320.0f;
    config.scroll_offset = 999.0f;
    luil::scroll_view_element view { luil::ui_element_id { luil::application_element_kind(42) }, config };
    view.set_content(std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(43) }));

    view.arrange({ { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f });
    REQUIRE(view.maximum_scroll() == 20.0f);
    REQUIRE(view.scroll_offset() == 20.0f);

    // 내용이 창보다 짧으면 흘릴 것이 없다.
    luil::scroll_view_config short_config {};
    short_config.content_height = 100.0f;
    short_config.scroll_offset = 40.0f;
    luil::scroll_view_element short_view { luil::ui_element_id { luil::application_element_kind(44) }, short_config };
    short_view.set_content(std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(45) }));
    short_view.arrange({ { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f });
    REQUIRE(short_view.maximum_scroll() == 0.0f);
    REQUIRE(short_view.scroll_offset() == 0.0f);
}

TEST_CASE("A clipping container hides what falls outside from hit testing too", "[ui][scroll]")
{
    luil::scroll_view_config config {};
    config.content_height = 600.0f;
    luil::scroll_view_element view { luil::ui_element_id { luil::application_element_kind(46) }, config };

    auto content { std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(47) }) };
    content->set_tooltip(u8"내용");
    view.set_content(std::move(content));
    view.arrange({ { 0.0f, 0.0f, 200.0f, 300.0f }, 1.0f });

    // 창 안은 내용이 받고, 창 밖으로 넘친 부분은 아무도 받지 않는다.
    REQUIRE(view.hit_test(100.0f, 150.0f) != nullptr);
    REQUIRE(view.hit_test(100.0f, 450.0f) == nullptr);
}

TEST_CASE("A clipping container hides drop targets outside its window", "[ui][scroll][drag]")
{
    // 창(0,0,200,100) 밖으로 절반쯤 걸친 항목이 drop 대상이다.
    luil::scroll_view_config config {};
    config.content_height = 600.0f;
    auto view { std::make_unique<luil::scroll_view_element>(luil::ui_element_id { luil::application_element_kind(48) }, config) };
    auto content { std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(49) }) };
    luil::drop_target drop {};
    drop.accepts = [](const luil::drag_payload&) { return true; };
    drop.on_drop = [](const luil::drag_payload&, const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; };
    content->set_drop_target(std::move(drop));
    const content_probe* const probe { content.get() };
    view->set_content(std::move(content));
    view->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    const luil::ui_tree tree { std::move(view) };

    const luil::drag_payload payload { {}, u8"item" };
    // 창 안은 대상이 되고, 창 밖(잘린 부분)은 놓을 자리가 아니다.
    REQUIRE(tree.find_drop_target(100.0f, 50.0f, payload) == probe);
    REQUIRE(tree.find_drop_target(100.0f, 150.0f, payload) == nullptr);

    // 강조가 그려질 보이는 영역은 창과의 교집합이다.
    const auto visible { tree.visible_bounds(*probe) };
    REQUIRE(visible.has_value());
    REQUIRE(visible->y == 0.0f);
    REQUIRE(visible->height == 100.0f);
    REQUIRE(visible->width == 200.0f);
}

TEST_CASE("scroll_delta_to_reveal moves only what the viewport cannot already show", "[ui][scroll][focus]")
{
    // 부호를 잠그는 그물이다. 뒤집으면 목록이 초점에서 **달아나고**,
    // clamp_scroll이 0에 붙여 화면에는 "아무 일도 안 일어남"으로 보인다.
    // 양수 = offset 증가 = 내용이 위로 (휠과 같은 규칙).
    //
    // 창은 [100, 200)이다.
    STATIC_REQUIRE(luil::scroll_delta_to_reveal(120.0f, 20.0f, 100.0f, 100.0f) == 0.0f);
    // 아래로 넘치면 넘친 만큼만 흘린다 (끝을 맞춘다).
    STATIC_REQUIRE(luil::scroll_delta_to_reveal(180.0f, 40.0f, 100.0f, 100.0f) == 20.0f);
    // 위로 벗어나면 음수다 (앞을 맞춘다).
    STATIC_REQUIRE(luil::scroll_delta_to_reveal(70.0f, 20.0f, 100.0f, 100.0f) == -30.0f);
    // 대상이 창보다 길면 **앞쪽 끝**을 맞춘다 — 뒤를 맞추면 첫 줄이 잘린 채 선다.
    STATIC_REQUIRE(luil::scroll_delta_to_reveal(120.0f, 300.0f, 100.0f, 100.0f) == 20.0f);
    // 가장자리에 딱 맞으면 움직이지 않는다.
    STATIC_REQUIRE(luil::scroll_delta_to_reveal(100.0f, 100.0f, 100.0f, 100.0f) == 0.0f);
}

TEST_CASE("A scroll view answers the offset change that brings a row into view", "[ui][scroll][focus]")
{
    // 답은 **논리 픽셀 델타**다. 물리 bounds를 배율로 나누는 그 한 번이 빠지면
    // 고DPI 화면에서 두 배로 튄다.
    const auto reveal = [](const float scale, const luil::rect_f& target) {
        luil::scroll_view_config config {};
        config.content_height = 500.0f;
        config.scroll_offset = 120.0f;
        luil::scroll_view_element view { luil::ui_element_id { luil::application_element_kind(50) }, config };
        view.set_content(std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(51) }));
        view.arrange({ { 0.0f, 0.0f, 200.0f * scale, 300.0f * scale }, scale });
        return view.scroll_delta_to_reveal(target);
    };

    // 창은 물리 [0, 300)이다. 그 안의 행은 흘릴 것이 없다.
    REQUIRE(reveal(1.0f, { 0.0f, 100.0f, 200.0f, 20.0f }) == 0.0f);
    // 아래로 40 넘친 행은 40 흘린다.
    REQUIRE(reveal(1.0f, { 0.0f, 320.0f, 200.0f, 20.0f }) == 40.0f);
    // 위로 벗어난 행은 음수다.
    REQUIRE(reveal(1.0f, { 0.0f, -25.0f, 200.0f, 20.0f }) == -25.0f);

    // 배율 2에서 물리 80 넘친 행의 답은 **논리 40**이다.
    REQUIRE(reveal(2.0f, { 0.0f, 640.0f, 400.0f, 40.0f }) == 40.0f);
    REQUIRE(reveal(2.0f, { 0.0f, 100.0f, 400.0f, 40.0f }) == 0.0f);
}

TEST_CASE("An element that only clips does not offer to scroll", "[ui][scroll][focus]")
{
    // 기본은 "나는 흘리지 않는다"다. 자르기만 하는 컨테이너가 여기 남는다.
    content_probe probe { luil::ui_element_id { luil::application_element_kind(52) } };
    probe.arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    REQUIRE(probe.scroll_delta_to_reveal({ 0.0f, 500.0f, 100.0f, 20.0f }) == 0.0f);
}
