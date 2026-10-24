#include "luil/ui/markdown_element.h"

#include "luil/text/grapheme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>

namespace {
    using namespace std::chrono_literals;
    const luil::text_measurer cells { [](const std::u8string_view text, float) { return static_cast<float>(luil::text::grapheme_boundaries(text).size() - 1) * 10.0f; } };
    const luil::ui_element_id owner { luil::application_element_kind(70), u8"owner" };
    const auto started { std::chrono::steady_clock::time_point { 1s } };
    struct visit_link
    {
        std::u8string destination {};
    };

    class test_group final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;
        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }
        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }
        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            draw_children(context, interaction);
        }
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> content(const std::u8string_view first_destination = u8"app:first")
    {
        luil::markdown_config config { .text = u8"[first](" + std::u8string { first_destination } + u8") and [second](https://example.test/?x=1&y=2)" };
        config.activate_link = [](const std::u8string_view destination) { return luil::make_app_action(visit_link { std::u8string { destination } }); };
        auto markdown { std::make_unique<luil::markdown_element>(luil::ui_element_id { luil::ui_element_kind::root, u8"help" }, std::move(config)) };
        markdown->arrange({ .slot = { 0.0f, 0.0f, 120.0f, 72.0f }, .measurer = &cells });
        return std::make_shared<const luil::ui_tree>(std::move(markdown));
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> page(const float scale, std::shared_ptr<const luil::ui_tree> help = content(), const bool hidden = false)
    {
        auto root { std::make_unique<test_group>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f * scale, 300.0f * scale }, scale });
        // 뒤 화면에 링크와 같은 id를 둬도 툴팁의 클릭이 이 액션을 실행해서는 안 된다.
        auto behind { std::make_unique<test_group>(help->root()->children().front()->id()) };
        behind->set_action(luil::ui_trigger::left_click, luil::make_message_action(visit_link { u8"behind" }));
        behind->arrange({ { 0.0f, 0.0f, 400.0f * scale, 300.0f * scale }, scale });
        root->add(std::move(behind));
        auto target { std::make_unique<test_group>(owner) };
        target->set_tooltip_content({ .content = std::move(help) });
        target->set_visible(hidden == false);
        target->arrange({ { 20.0f * scale, 20.0f * scale, 30.0f * scale, 10.0f * scale }, scale });
        root->add(std::move(target));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    void move(luil::interaction_controller& controller, const float x, const float y, const float scale, const int milliseconds)
    {
        static_cast<void>(controller.process(luil::pointer_moved_event { .x = x * scale, .y = y * scale, .time = started + std::chrono::milliseconds { milliseconds }, .scale = scale }));
    }

    [[nodiscard]] std::u8string activated(const std::vector<luil::input_action>& actions)
    {
        REQUIRE(actions.size() == 1);
        const auto* const message { std::get_if<luil::app_message>(&actions.front()) };
        REQUIRE(message != nullptr);
        REQUIRE(message->get<visit_link>() != nullptr);
        return message->get<visit_link>()->destination;
    }
} // namespace

TEST_CASE("CommonMark blocks preserve headings, lists, quotes, fences and nested inline styles", "[ui][markdown]")
{
    const auto blocks { luil::parse_markdown(u8"# Title\n\n**bold *nested*** and `code` and ~~gone~~\n\n> quote\n\n3. third\n4. fourth\n\n```cpp\nif (x < 2) {}\n```\n\n---") };
    REQUIRE(blocks.size() == 7);
    REQUIRE(blocks[0].heading == 1);
    REQUIRE(blocks[0].text == u8"Title");
    REQUIRE(blocks[1].text == u8"bold nested and code and gone");
    REQUIRE(std::any_of(blocks[1].spans.begin(), blocks[1].spans.end(), [](const auto& span) { return span.bold && span.italic; }));
    REQUIRE(std::any_of(blocks[1].spans.begin(), blocks[1].spans.end(), [](const auto& span) { return span.code; }));
    REQUIRE(std::any_of(blocks[1].spans.begin(), blocks[1].spans.end(), [](const auto& span) { return span.strike; }));
    REQUIRE(blocks[2].quote);
    REQUIRE(blocks[3].text == u8"3. third");
    REQUIRE(blocks[4].text == u8"4. fourth");
    REQUIRE(blocks[5].code);
    REQUIRE(blocks[5].text == u8"if (x < 2) {}");
    REQUIRE(blocks[6].rule);
}

TEST_CASE("Markdown resolves reference links, escapes and entities while keeping HTML inert", "[ui][markdown]")
{
    const auto blocks { luil::parse_markdown(u8"[a &amp; b][ref] &copy; &#x1f600; \\*literal\\* <script>run()</script> ![alt](image.png)\n\n[ref]: https://example.test/?a=1&amp;b=2") };
    REQUIRE(blocks.size() == 1);
    REQUIRE(blocks[0].text.find(u8"a & b © 😀 *literal* <script>run()</script> alt") != std::u8string::npos);
    REQUIRE(std::any_of(blocks[0].spans.begin(), blocks[0].spans.end(), [](const auto& span) { return span.link == u8"https://example.test/?a=1&b=2"; }));
    REQUIRE(luil::parse_markdown(u8"").empty());
}

TEST_CASE("Markdown measures wrapped text and exposes exact clickable link fragments", "[ui][markdown]")
{
    luil::markdown_config config { .text = u8"[abcdefgh](app:link)" };
    config.activate_link = [](std::u8string_view) { return luil::make_app_action(visit_link {}); };
    luil::markdown_element markdown { { luil::ui_element_kind::root, u8"body" }, std::move(config) };
    const auto size { *markdown.preferred_size({ .measurer = &cells }, 30.0f) };
    REQUIRE(size.height == 54.0f);
    markdown.arrange({ .slot = { 5.0f, 10.0f, 30.0f, size.height }, .measurer = &cells });
    REQUIRE(markdown.children().size() == 1);
    REQUIRE(markdown.children().front()->accessibility().role == luil::access_role::link);
    REQUIRE(markdown.hit_test(10.0f, 15.0f) == markdown.children().front().get());
    REQUIRE(markdown.hit_test(10.0f, 50.0f) == markdown.children().front().get());
    REQUIRE(markdown.hit_test(34.0f, 50.0f) == nullptr);
}

TEST_CASE("A shown tooltip survives its gap and accepts links at every DPI without clicking the page", "[ui][tooltip][markdown][interaction]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        const auto tree { page(scale) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        // 상자는 hover가 시작된 (25,25)의 커서 그림 아래 (25,45)에 서고, 내용은 여백 8을 둔 (33,53)부터다.
        move(controller, 25.0f, 25.0f, scale, 0);
        move(controller, 25.0f, 31.0f, scale, 501);
        REQUIRE(controller.snapshot().tooltip.has_value());
        REQUIRE(controller.snapshot().hovered == owner);
        // 대상 아래 끝(30)과 상자 위 끝(45) 사이를 건너는 동안 닫히지 않는다.
        move(controller, 30.0f, 44.0f, scale, 502);
        REQUIRE(controller.snapshot().tooltip.has_value());
        move(controller, 40.0f, 60.0f, scale, 503);
        const auto link { controller.snapshot().tooltip->hovered };
        REQUIRE(link.owner == u8"help/link/0:app:first");
        REQUIRE(tree->cursor_at(40.0f * scale, 60.0f * scale, controller.snapshot()) == luil::ui_cursor::hand);
        REQUIRE(controller.process(luil::pointer_pressed_event { .x = 40.0f * scale, .y = 60.0f * scale, .time = started + 504ms, .scale = scale }).empty());
        REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
        controller.set_tree(tree);
        const auto actions { controller.process(luil::pointer_released_event { .x = 40.0f * scale, .y = 60.0f * scale, .time = started + 505ms }) };
        REQUIRE(activated(actions) == u8"app:first");
        REQUIRE(controller.snapshot().tooltip.has_value());
        REQUIRE(controller.snapshot().tooltip->pressed == luil::ui_element_id {});
        move(controller, 300.0f, 200.0f, scale, 506);
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
    }
}

TEST_CASE("Tooltip dismissal and changed content cancel pressed links", "[ui][tooltip][interaction]")
{
    const auto tree { page(1.0f) };
    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const auto enter = [&] {
        move(controller, 25.0f, 25.0f, 1.0f, 0);
        move(controller, 40.0f, 60.0f, 1.0f, 501);
        REQUIRE(controller.snapshot().tooltip.has_value());
    };
    enter();
    SECTION("Escape")
    {
        REQUIRE(controller.process(luil::key_pressed_event { .key = luil::key_code::escape, .time = started + 502ms }).empty());
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
        REQUIRE_FALSE(controller.snapshot().hover_started_at.has_value());
    }
    SECTION("pointer leave")
    {
        static_cast<void>(controller.process(luil::pointer_left_event {}));
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
    }
    SECTION("focus loss")
    {
        static_cast<void>(controller.process(luil::surface_focus_lost_event {}));
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
    }
    SECTION("content replacement")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 40.0f, .y = 60.0f, .time = started + 502ms }));
        controller.set_tree(page(1.0f, content(u8"app:changed")));
        REQUIRE(controller.process(luil::pointer_released_event { .x = 40.0f, .y = 60.0f, .time = started + 503ms }).empty());
    }
    SECTION("same link in a newly published tree")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 40.0f, .y = 60.0f, .time = started + 502ms }));
        controller.set_tree(page(1.0f));
        REQUIRE(activated(controller.process(luil::pointer_released_event { .x = 40.0f, .y = 60.0f, .time = started + 503ms })) == u8"app:first");
    }
    SECTION("hidden owner")
    {
        controller.set_tree(page(1.0f, content(), true));
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
    }
    SECTION("lost input")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 40.0f, .y = 60.0f, .time = started + 502ms }));
        static_cast<void>(controller.cancel_dropped_gestures());
        REQUIRE(controller.process(luil::pointer_released_event { .x = 40.0f, .y = 60.0f, .time = started + 503ms }).empty());
        REQUIRE_FALSE(controller.snapshot().tooltip.has_value());
    }
    SECTION("cancelled press")
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 40.0f, .y = 60.0f, .time = started + 502ms }));
        REQUIRE(controller.process(luil::pointer_released_event { .cancelled = true }).empty());
    }
    auto snapshot { controller.snapshot() };
    REQUIRE_FALSE(luil::interaction_for_surface(std::move(snapshot), u8"other-window").tooltip.has_value());
}

TEST_CASE("Inline emphasis keeps one anchor across all of its styled fragments", "[ui][markdown]")
{
    luil::markdown_config config { .text = u8"[**bold** and *italic*](app:one) [next](app:two)" };
    config.activate_link = [](std::u8string_view) { return luil::make_app_action(visit_link {}); };
    luil::markdown_element markdown { { luil::ui_element_kind::root, u8"styled" }, std::move(config) };
    markdown.arrange({ .slot = { 0.0f, 0.0f, 300.0f, 100.0f }, .measurer = &cells });
    REQUIRE(markdown.children().size() == 2);
    REQUIRE(markdown.children()[0]->accessibility().name == u8"bold and italic");
    REQUIRE(markdown.hit_test(10.0f, 5.0f) == markdown.children()[0].get());
    REQUIRE(markdown.hit_test(100.0f, 5.0f) == markdown.children()[0].get());
}

TEST_CASE("An oversized hover scrolls without passing wheel or clicks to the underlying page", "[ui][tooltip][markdown][interaction]")
{
    const auto help { content() };
    const auto tree { page(1.0f, help) };
    // 내용의 높이를 키워 주 표면보다 긴 툴팁을 만든다.
    auto tall { std::make_unique<test_group>(luil::ui_element_id { luil::ui_element_kind::root, u8"tall" }) };
    tall->arrange({ { 0.0f, 0.0f, 120.0f, 1000.0f }, 1.0f });
    auto bottom { std::make_unique<test_group>(luil::ui_element_id { owner.kind, u8"bottom-link" }) };
    bottom->arrange({ { 0.0f, 960.0f, 120.0f, 40.0f }, 1.0f });
    bottom->set_action(luil::ui_trigger::left_click, luil::make_message_action(visit_link { u8"bottom" }));
    tall->add(std::move(bottom));
    // page helper는 접근 자식 하나를 뒤 화면 id로 쓰므로 같은 경로를 재사용할 수 있다.
    const auto tall_tree { page(1.0f, std::make_shared<const luil::ui_tree>(std::move(tall))) };
    luil::interaction_controller controller {};
    controller.set_tree(tall_tree);
    move(controller, 25.0f, 25.0f, 1.0f, 0);
    move(controller, 35.0f, 48.0f, 1.0f, 501);
    REQUIRE(controller.snapshot().tooltip.has_value());
    REQUIRE(controller.process(luil::mouse_wheel_event { .x = 35.0f, .y = 48.0f, .delta = -12000.0f, .time = started + 502ms }).empty());
    REQUIRE(controller.snapshot().tooltip->scroll_offset == 716.0f);
    move(controller, 35.0f, 260.0f, 1.0f, 503);
    REQUIRE(controller.snapshot().tooltip->hovered.owner == u8"bottom-link");
    static_cast<void>(controller.process(luil::pointer_pressed_event { .x = 35.0f, .y = 260.0f, .time = started + 504ms }));
    REQUIRE(activated(controller.process(luil::pointer_released_event { .x = 35.0f, .y = 260.0f, .time = started + 505ms })) == u8"bottom");
}