#include "raster_probe.h"

#include "luil/ui/draw_primitives.h"
#include "luil/ui/panel_element.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {
    using namespace std::chrono_literals;
    constexpr luil::ui_element_kind kind_hint { luil::application_element_kind(70) };
    const luil::ui_element_id hint_id { kind_hint, u8"hint" };
    const auto started { std::chrono::steady_clock::time_point { 1s } };

    class tooltip_probe final : public luil::ui_element
    {
    public:
        explicit tooltip_probe(const bool animated)
            : ui_element { hint_id }
            , animated_ { animated }
        {
            set_tab_stop(true);
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            const auto color { interaction.hovered == id() || interaction.focused == id() ? context.palette.accent : context.palette.error_accent };
            // 자리를 넘겨 그리는 내용도 툴팁의 내용 영역 밖을 칠할 수 없어야 한다.
            context.canvas.drawRect(SkRect::MakeXYWH(-1000.0f, -1000.0f, 2000.0f, 2000.0f), luil::solid_paint(color));
        }

        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const luil::update_context& context, const luil::interaction_snapshot&) const override
        {
            return animated_ ? std::optional { context.now + 17ms } : std::nullopt;
        }

    private:
        bool animated_ { false };
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_content(const bool animated = false, const float width = 80.0f, const float height = 40.0f)
    {
        auto content { std::make_unique<tooltip_probe>(animated) };
        // 원점이 0이 아니어도 root bounds를 기준으로 옮겨야 한다.
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(content), { 30.0f, 40.0f, width, height }, 1.0f));
    }

    [[nodiscard]] luil::tooltip_config make_config(const bool animated = false)
    {
        return { .content = make_content(animated), .delay = 200ms, .padding = 4.0f, .corner_radius = 0.0f, .shadow = 0.0f };
    }

    [[nodiscard]] luil::ui_tree make_tree(luil::tooltip_config config, const float scale = 1.0f, const luil::rect_f& anchor = { 20.0f, 20.0f, 20.0f, 10.0f },
        const luil::rect_f& window = { 0.0f, 0.0f, 400.0f, 200.0f }, const bool visible = true, const bool clipped = false)
    {
        auto target { std::make_unique<luil::panel_element>(hint_id, luil::panel_config {}) };
        target->set_tooltip_content(std::move(config));
        target->set_visible(visible);
        target->arrange({ { anchor.x * scale, anchor.y * scale, anchor.width * scale, anchor.height * scale }, scale });
        auto root { std::make_unique<luil::panel_element>(luil::ui_element_id { luil::ui_element_kind::root }, luil::panel_config {}) };
        root->arrange({ { window.x * scale, window.y * scale, window.width * scale, window.height * scale }, scale });
        root->set_clip_children(clipped);
        root->set_content(std::move(target));
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::ui_tree make_text_tree(const float scale, const luil::rect_f& anchor)
    {
        auto target { std::make_unique<luil::panel_element>(hint_id, luil::panel_config {}) };
        target->set_tooltip(u8"설명");
        target->arrange({ { anchor.x * scale, anchor.y * scale, anchor.width * scale, anchor.height * scale }, scale });
        auto root { std::make_unique<luil::panel_element>(luil::ui_element_id { luil::ui_element_kind::root }, luil::panel_config {}) };
        root->arrange({ { 0.0f, 0.0f, 400.0f * scale, 200.0f * scale }, scale });
        root->set_content(std::move(target));
        return luil::ui_tree { std::move(root) };
    }

    [[nodiscard]] luil::interaction_snapshot hovering(const std::optional<luil::point_f>& point = std::nullopt)
    {
        return { .hovered = hint_id, .hover_started_at = started, .hover_point = point };
    }

    [[nodiscard]] bool near(const float actual, const float expected)
    {
        return std::abs(actual - expected) < 0.01f;
    }

    void move(luil::interaction_controller& controller, const float x, const float y, const std::chrono::milliseconds after)
    {
        static_cast<void>(controller.process(luil::pointer_moved_event { .x = x, .y = y, .time = started + after }));
    }

    [[nodiscard]] luil::ui_color_palette palette()
    {
        return luil::color_palette_for(luil::color_theme::dark);
    }
}

TEST_CASE("Custom tooltip content makes disabled elements hover targets and preserves the text summary", "[ui][tooltip]")
{
    luil::panel_element target { hint_id, {} };
    target.arrange({ { 0.0f, 0.0f, 20.0f, 20.0f }, 1.0f });
    target.set_enabled(false);
    target.set_tooltip_content(make_config());
    REQUIRE(target.has_tooltip());
    REQUIRE(target.interactive());
    REQUIRE(target.hit_test(10.0f, 10.0f) == &target);
    REQUIRE(target.tooltip().empty());

    target.set_tooltip(u8"여러 줄 도움말의 요약");
    REQUIRE(target.access_name() == u8"여러 줄 도움말의 요약");
    REQUIRE(target.tooltip_content() != nullptr);
    target.set_tooltip_content({});
    REQUIRE(target.tooltip_content() == nullptr);
    REQUIRE(target.tooltip() == u8"여러 줄 도움말의 요약");
    REQUIRE(target.has_tooltip());
    target.set_tooltip({});
    REQUIRE_FALSE(target.has_tooltip());
    REQUIRE_FALSE(target.interactive());
}

TEST_CASE("Empty and unarranged tooltip trees do not create phantom hover targets", "[ui][tooltip]")
{
    luil::panel_element target { hint_id, {} };
    target.set_tooltip_content({ .content = std::make_shared<const luil::ui_tree>(nullptr) });
    REQUIRE_FALSE(target.has_tooltip());
    target.set_tooltip_content({ .content = std::make_shared<const luil::ui_tree>(std::make_unique<tooltip_probe>(false)) });
    REQUIRE_FALSE(target.has_tooltip());
    target.set_tooltip_content({ .content = make_content(false, 0.0f) });
    REQUIRE_FALSE(target.has_tooltip());
    target.set_tooltip(u8"기본 설명");
    REQUIRE(target.has_tooltip());
    REQUIRE(target.tooltip_content() == nullptr);
}

TEST_CASE("Custom tooltip geometry and delay discard invalid settings", "[ui][tooltip]")
{
    luil::panel_element target { hint_id, {} };
    auto config { make_config() };
    config.delay = -1ms;
    config.padding = std::numeric_limits<float>::quiet_NaN();
    config.gap = -3.0f;
    config.corner_radius = std::numeric_limits<float>::infinity();
    config.shadow = 2.0f;
    target.set_tooltip_content(std::move(config));
    const auto* const normalized { target.tooltip_content() };
    REQUIRE(normalized != nullptr);
    REQUIRE(normalized->delay == 0ms);
    REQUIRE(normalized->padding == 0.0f);
    REQUIRE(normalized->gap == 0.0f);
    REQUIRE(normalized->corner_radius == 0.0f);
    REQUIRE(normalized->shadow == 1.0f);
}

TEST_CASE("Only a visible custom tooltip schedules its content animation", "[ui][tooltip][update]")
{
    const auto tree { make_tree(make_config(true)) };
    auto interaction { hovering() };
    REQUIRE(tree.next_update({ started }, interaction) == started + 200ms);
    REQUIRE(tree.next_update({ started + 199ms }, interaction) == started + 200ms);
    REQUIRE(tree.next_update({ started + 200ms }, interaction) == started + 217ms);
    interaction.drag = luil::drag_visual {};
    REQUIRE_FALSE(tree.next_update({ started }, interaction).has_value());
    interaction.drag.reset();
    interaction.hover_started_at.reset();
    REQUIRE_FALSE(tree.next_update({ started }, interaction).has_value());

    const auto hidden { make_tree(make_config(true), 1.0f, { 20.0f, 20.0f, 20.0f, 10.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, false) };
    REQUIRE_FALSE(hidden.next_update({ started }, hovering()).has_value());
    const auto clipped { make_tree(make_config(true), 1.0f, { 500.0f, 20.0f, 20.0f, 10.0f }, { 0.0f, 0.0f, 400.0f, 200.0f }, true, true) };
    REQUIRE_FALSE(clipped.next_update({ started }, hovering()).has_value());
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(tree.unarranged().empty());
    REQUIRE(tree.focus_order().empty());
    REQUIRE(tree.find(hint_id)->tooltip_content() != nullptr);
}

TEST_CASE("Custom tooltip rendering scales logical content and isolates its interaction and clipping", "[ui][tooltip][raster]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        const auto tree { make_tree(make_config(), scale) };
        luil::testing::raster_frame frame { static_cast<int>(400.0f * scale), static_cast<int>(200.0f * scale), palette(), scale };
        auto interaction { hovering() };
        interaction.focused = hint_id;
        const auto pixel = [&](const float x, const float y) { return frame.pixel_at(static_cast<int>(x * scale), static_cast<int>(y * scale)); };
        frame.draw(tree, interaction, started + 199ms);
        REQUIRE(pixel(25.0f, 38.0f) != frame.palette().error_accent);
        frame.draw(tree, interaction, started + 200ms);
        REQUIRE(pixel(25.0f, 38.0f) == frame.palette().error_accent);
        REQUIRE(pixel(102.0f, 75.0f) == frame.palette().error_accent);
        REQUIRE(pixel(22.0f, 35.0f) == frame.palette().tooltip_background);
        REQUIRE(pixel(110.0f, 85.0f) != frame.palette().error_accent);
        interaction.drag = luil::drag_visual {};
        frame.draw(tree, interaction, started + 200ms);
        REQUIRE(pixel(25.0f, 38.0f) != frame.palette().error_accent);
    }
}

TEST_CASE("Custom tooltip placement flips and clamps inside an offset surface", "[ui][tooltip][raster]")
{
    auto config { make_config() };
    config.background = [](const luil::ui_color_palette& colors) { return colors.notice_background; };
    config.border = [](const luil::ui_color_palette& colors) { return colors.accent; };
    const auto tree { make_tree(std::move(config), 2.0f, { 180.0f, 110.0f, 20.0f, 10.0f }, { 10.0f, 15.0f, 200.0f, 110.0f }) };
    luil::testing::raster_frame frame { 440, 280, palette(), 2.0f };
    frame.draw(tree, hovering(), started + 200ms);
    // 오른쪽 끝을 210에 맞추고 아래에 들어가지 않아 y=110-48-3=59로 뒤집는다.
    REQUIRE(frame.pixel_at(260, 132) == frame.palette().error_accent);
    REQUIRE(frame.pixel_at(248, 122) == frame.palette().notice_background);
    REQUIRE(frame.pixel_at(245, 150) == frame.palette().accent);
    REQUIRE_FALSE(frame.contains_color({ 0.0f, 0.0f, 20.0f, 280.0f }, frame.palette().error_accent));
    REQUIRE_FALSE(frame.contains_color({ 420.0f, 0.0f, 20.0f, 280.0f }, frame.palette().error_accent));
}

TEST_CASE("Oversized tooltip content is clipped within even a tiny surface", "[ui][tooltip][raster]")
{
    auto config { make_config() };
    config.content = make_content(false, 600.0f, 500.0f);
    config.padding = 0.0f;
    const auto tree { make_tree(std::move(config), 2.0f, { 30.0f, 30.0f, 10.0f, 10.0f }, { 10.0f, 15.0f, 40.0f, 20.0f }) };
    luil::testing::raster_frame frame { 160, 120, palette(), 2.0f };
    frame.draw(tree, hovering(), started + 200ms);
    REQUIRE(frame.pixel_at(50, 50) == frame.palette().error_accent);
    REQUIRE_FALSE(frame.contains_color({ 0.0f, 0.0f, 20.0f, 120.0f }, frame.palette().error_accent));
    REQUIRE_FALSE(frame.contains_color({ 100.0f, 0.0f, 60.0f, 120.0f }, frame.palette().error_accent));
    REQUIRE_FALSE(frame.contains_color({ 0.0f, 0.0f, 160.0f, 30.0f }, frame.palette().error_accent));
    REQUIRE_FALSE(frame.contains_color({ 0.0f, 70.0f, 160.0f, 50.0f }, frame.palette().error_accent));
}

TEST_CASE("A pointer hover places the tooltip under the cursor and keeps it inside the surface at every DPI", "[ui][tooltip]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        // 상자는 내용 80x40에 여백 4를 더한 88x48이고, 넓은 대상은 창 오른쪽 끝(400) 가까이까지 간다.
        const auto top { make_tree(make_config(), scale, { 20.0f, 20.0f, 375.0f, 10.0f }) };
        const auto bottom { make_tree(make_config(), scale, { 20.0f, 170.0f, 375.0f, 20.0f }) };
        const auto at = [scale](const float x, const float y) { return std::optional { luil::point_f { x * scale, y * scale } }; };

        // 넓은 대상의 오른쪽에서 hover하면 상자가 대상 왼쪽 끝이 아니라 포인터 x에서, 커서 그림(20) 아래에서 시작한다.
        const auto wide { top.place_tooltip(hint_id, scale, at(250.0f, 25.0f)) };
        REQUIRE(wide.has_value());
        REQUIRE(near(wide->box.x, 250.0f * scale));
        REQUIRE(near(wide->box.y, 45.0f * scale));
        // 포인터에서 곧장 내려가는 길이 대상·간격·상자로 끊기지 않는다.
        REQUIRE(wide->bridge.contains(250.0f * scale, 35.0f * scale));

        // 오른쪽으로 넘치면 창 안으로 민다. 간격은 여전히 포인터 아래를 덮는다.
        const auto right { top.place_tooltip(hint_id, scale, at(390.0f, 25.0f)) };
        REQUIRE(near(right->box.x, 312.0f * scale));
        REQUIRE(near(right->box.y, 45.0f * scale));
        REQUIRE(right->bridge.contains(390.0f * scale, 35.0f * scale));

        // 아래로 넘치면 커서를 가리지 않게 포인터보다 간격(3)만큼 위로 뒤집는다.
        const auto flipped { bottom.place_tooltip(hint_id, scale, at(390.0f, 180.0f)) };
        REQUIRE(near(flipped->box.x, 312.0f * scale));
        REQUIRE(near(flipped->box.y, 129.0f * scale));
        REQUIRE(flipped->box.y + flipped->box.height <= 180.0f * scale);

        // 포인터 자리가 없으면(키보드·보조 기술·터치) 예전처럼 대상의 왼쪽 끝 아래다.
        const auto anchored { top.place_tooltip(hint_id, scale) };
        REQUIRE(near(anchored->box.x, 20.0f * scale));
        REQUIRE(near(anchored->box.y, 33.0f * scale));
        const auto anchored_flip { bottom.place_tooltip(hint_id, scale) };
        REQUIRE(near(anchored_flip->box.x, 20.0f * scale));
        REQUIRE(near(anchored_flip->box.y, 119.0f * scale));
    }
}

TEST_CASE("Text and custom tooltips are drawn where the pointer placement puts them", "[ui][tooltip][raster]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        CAPTURE(scale);
        const luil::point_f pointer { 250.0f * scale, 25.0f * scale };
        luil::testing::raster_frame frame { static_cast<int>(400.0f * scale), static_cast<int>(200.0f * scale), palette(), scale };
        const auto pixel = [&](const float x, const float y) { return frame.pixel_at(static_cast<int>(x * scale), static_cast<int>(y * scale)); };

        const auto text { make_text_tree(scale, { 20.0f, 20.0f, 375.0f, 10.0f }) };
        frame.draw(text, hovering(pointer), started + luil::tooltip_delay);
        REQUIRE(pixel(252.0f, 47.0f) == frame.palette().tooltip_background);
        REQUIRE(pixel(22.0f, 35.0f) != frame.palette().tooltip_background);
        frame.draw(text, hovering(), started + luil::tooltip_delay);
        REQUIRE(pixel(22.0f, 35.0f) == frame.palette().tooltip_background);

        // 그리기도 입력 쪽(hit test·간격 건너기)과 같은 `place_tooltip` 자리에 선다.
        const auto custom { make_tree(make_config(), scale, { 20.0f, 20.0f, 375.0f, 10.0f }) };
        const auto placement { custom.place_tooltip(hint_id, scale, pointer) };
        REQUIRE(placement.has_value());
        frame.draw(custom, hovering(pointer), started + 200ms);
        const float center_x { placement->content.x + placement->content.width / 2.0f };
        const float center_y { placement->content.y + placement->content.height / 2.0f };
        REQUIRE(frame.pixel_at(static_cast<int>(center_x), static_cast<int>(center_y)) == frame.palette().error_accent);
        REQUIRE(pixel(25.0f, 38.0f) != frame.palette().error_accent);
    }
}

TEST_CASE("Hover records the tooltip point until the delay ends and forgets it with the hover", "[ui][tooltip][interaction]")
{
    luil::interaction_controller controller {};
    SECTION("text tooltip")
    {
        controller.set_tree(std::make_shared<const luil::ui_tree>(make_text_tree(1.0f, { 20.0f, 20.0f, 375.0f, 10.0f })));
        move(controller, 100.0f, 25.0f, 0ms);
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 100.0f, 25.0f });
        // 지연 전의 이동은 마지막 자리를 남긴다.
        move(controller, 150.0f, 25.0f, 499ms);
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 150.0f, 25.0f });
        // 뜬 뒤에는 포인터를 따라가지 않는다.
        move(controller, 200.0f, 25.0f, 500ms);
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 150.0f, 25.0f });
        // tooltip이 없는 자리로 나가면 잊고, 다시 들어오면 새 자리다.
        move(controller, 100.0f, 100.0f, 600ms);
        REQUIRE_FALSE(controller.snapshot().hover_point.has_value());
        move(controller, 300.0f, 25.0f, 700ms);
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 300.0f, 25.0f });
        static_cast<void>(controller.process(luil::pointer_left_event {}));
        REQUIRE_FALSE(controller.snapshot().hover_point.has_value());
    }
    SECTION("custom tooltip")
    {
        const auto tree { std::make_shared<const luil::ui_tree>(make_tree(make_config(), 1.0f, { 20.0f, 20.0f, 375.0f, 10.0f })) };
        controller.set_tree(tree);
        move(controller, 100.0f, 25.0f, 0ms);
        move(controller, 150.0f, 25.0f, 100ms);
        move(controller, 200.0f, 25.0f, 250ms);
        REQUIRE(controller.snapshot().tooltip.has_value());
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 150.0f, 25.0f });
        const auto placement { tree->place_tooltip(hint_id, 1.0f, controller.snapshot().hover_point) };
        REQUIRE(near(placement->box.x, 150.0f));
        REQUIRE(near(placement->box.y, 45.0f));
        // 열린 상자 위를 지나도 자리는 그대로다.
        move(controller, 300.0f, 25.0f, 300ms);
        REQUIRE(controller.snapshot().hover_point == luil::point_f { 150.0f, 25.0f });
        REQUIRE_FALSE(luil::interaction_for_surface(controller.snapshot(), u8"other-window").hover_point.has_value());
        REQUIRE(controller.process(luil::key_pressed_event { .key = luil::key_code::escape, .time = started + 301ms }).empty());
        REQUIRE_FALSE(controller.snapshot().hover_point.has_value());
    }
}
