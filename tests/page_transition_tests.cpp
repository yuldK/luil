#include "luil/ui/draw_primitives.h"
#include "luil/ui/page_transition.h"
#include "luil/ui/page_transition_element.h"
#include "luil/ui/spring.h"
#include "luil/ui/ui_tree.h"
#include "raster_probe.h"

#include "include/core/SkCanvas.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
    using time_point = std::chrono::steady_clock::time_point;
    using std::chrono::milliseconds;

    constexpr luil::ui_element_kind kind_plate { luil::application_element_kind(1) };
    constexpr luil::ui_color prior_color { 0xFFFF0000u };
    constexpr luil::ui_color next_color { 0xFF0000FFu };
    // 요소가 서는 자리다 (물리 픽셀). 화면 안쪽에 두어 bounds 밖 픽셀이 그대로인지 본다.
    constexpr luil::rect_f slot { 40.0f, 20.0f, 100.0f, 60.0f };
    constexpr int frame_width { 200 };
    constexpr int frame_height { 100 };

    struct press_intent
    {
    };

    [[nodiscard]] time_point at(const int value)
    {
        return time_point {} + milliseconds { value };
    }

    // 손으로 검산하려고 곧은 곡선을 쓴다.
    [[nodiscard]] float linear(const float t) noexcept
    {
        return t;
    }

    // 한 색으로 자기 bounds를 칠하는 판이다. 글이 없어 글꼴에 매이지 않는다.
    class plate final : public luil::ui_element
    {
    public:
        plate(std::u8string owner, const luil::ui_color color, const bool pressable = false)
            : ui_element { luil::ui_element_id { kind_plate, std::move(owner) } }
            , color_ { color }
        {
            if (pressable)
                set_action(luil::ui_trigger::left_click, luil::make_message_action(press_intent {}));
        }

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
            for (const std::unique_ptr<ui_element>& child : children())
                child->arrange(context);
        }

        void draw(luil::draw_context& context, const luil::interaction_snapshot& interaction) const override
        {
            const luil::rect_f& box { bounds() };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), luil::solid_paint(color_));
            draw_children(context, interaction);
        }

        [[nodiscard]] luil::access_info accessibility() const override
        {
            return { .role = luil::access_role::static_text, .name = id().owner };
        }

    private:
        luil::ui_color color_ { 0 };
    };

    // 떠나는 화면이다. 요소와 같은 크기로, 원점은 다른 자리에 배치한다 (요소가 옮겨 그린다).
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> prior_screen(const luil::rect_f& size = slot)
    {
        auto screen { std::make_unique<plate>(u8"prior", prior_color) };
        screen->add(std::make_unique<plate>(u8"prior-only", prior_color, true));
        screen->add(std::make_unique<plate>(u8"shared", prior_color, true));
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(screen), { 0.0f, 0.0f, size.width, size.height }, 2.0f));
    }

    [[nodiscard]] luil::page_transition_style style_of(const luil::page_transition_effect effect, const luil::page_transition_direction direction, const luil::page_transition_axis axis = luil::page_transition_axis::horizontal)
    {
        return { effect, axis, direction, luil::page_transition_timing { milliseconds { 1000 }, linear } };
    }

    // 전환 요소가 새 화면을 담은 tree다. 새 화면에도 `shared`가 있어 두 tree에 같은 id가 선다.
    [[nodiscard]] luil::ui_tree transition_tree(luil::page_transition transition, const bool pressable = true)
    {
        auto screen { std::make_unique<plate>(u8"next", next_color, pressable) };
        screen->add(std::make_unique<plate>(u8"shared", next_color, true));
        auto element { std::make_unique<luil::page_transition_element>(luil::page_transition_config { u8"page", std::move(transition) }, std::move(screen)) };
        return luil::make_arranged_tree(std::move(element), slot, 2.0f);
    }

    [[nodiscard]] luil::testing::raster_frame draw_at(const luil::ui_tree& tree, const time_point now, const bool reduce_motion = false)
    {
        luil::testing::raster_frame frame { frame_width, frame_height, luil::color_palette_for(luil::color_theme::dark) };
        frame.set_reduce_motion(reduce_motion);
        frame.draw(tree, luil::interaction_snapshot {}, now);
        return frame;
    }

    // 불투명한 `over`를 `alpha`만큼 `under` 위에 얹은 색이다 (채널마다 곧게 섞는다).
    [[nodiscard]] luil::ui_color mix(const luil::ui_color under, const luil::ui_color over, const float alpha)
    {
        luil::ui_color mixed { 0xFF000000u };
        for (const int shift : { 0, 8, 16 })
        {
            const float a { static_cast<float>((under >> shift) & 0xFFu) };
            const float b { static_cast<float>((over >> shift) & 0xFFu) };
            mixed |= static_cast<luil::ui_color>(std::lround(a + (b - a) * alpha)) << shift;
        }
        return mixed;
    }

    [[nodiscard]] bool near(const luil::ui_color actual, const luil::ui_color expected, const int tolerance = 2)
    {
        for (const int shift : { 0, 8, 16, 24 })
        {
            const int a { static_cast<int>((actual >> shift) & 0xFFu) };
            const int b { static_cast<int>((expected >> shift) & 0xFFu) };
            if (std::abs(a - b) > tolerance)
                return false;
        }
        return true;
    }

    // 요소 bounds 밖은 아무도 칠하지 않는다.
    void require_outside_untouched(const luil::testing::raster_frame& frame)
    {
        const luil::ui_color background { frame.palette().window_background };
        REQUIRE(frame.pixel_at(10, 50) == background);
        REQUIRE(frame.pixel_at(39, 50) == background);
        REQUIRE(frame.pixel_at(140, 50) == background);
        REQUIRE(frame.pixel_at(190, 50) == background);
        REQUIRE(frame.pixel_at(90, 19) == background);
        REQUIRE(frame.pixel_at(90, 80) == background);
    }
}

TEST_CASE("A timed page transition answers progress, end and the next update from the clock alone", "[ui][page-transition]")
{
    const time_point started { at(1000) };
    const luil::page_transition value { prior_screen(), started, { luil::page_transition_effect::slide, luil::page_transition_axis::horizontal, luil::page_transition_direction::forward, luil::page_transition_timing {} } };
    REQUIRE(luil::page_transition_progress(value, at(900)) == 0.0f);
    REQUIRE(luil::page_transition_progress(value, started) == 0.0f);
    REQUIRE(luil::page_transition_progress(value, started + milliseconds { 125 }) == Catch::Approx(luil::ease_in_out(0.5f)));
    REQUIRE(luil::page_transition_progress(value, started + luil::page_transition_duration) == 1.0f);
    REQUIRE(luil::page_transition_end(value) == started + luil::page_transition_duration);
    REQUIRE(luil::page_transition_finished(value, started + milliseconds { 249 }) == false);
    REQUIRE(luil::page_transition_finished(value, started + milliseconds { 250 }));

    // 예고는 `transition_next_update`와 같은 계약이다.
    REQUIRE(luil::page_transition_next_update(value, at(900)) == started);
    REQUIRE(luil::page_transition_next_update(value, started + milliseconds { 100 }) == started + milliseconds { 116 });
    REQUIRE(luil::page_transition_next_update(value, started + milliseconds { 250 }) == std::nullopt);

    // prior가 없으면 언제 물어도 끝이다.
    const luil::page_transition empty {};
    REQUIRE(luil::page_transition_progress(empty, at(0)) == 1.0f);
    REQUIRE(luil::page_transition_finished(empty, at(0)));
    REQUIRE(luil::page_transition_end(empty) == std::nullopt);
    REQUIRE(luil::page_transition_next_update(empty, at(0)) == std::nullopt);
}

TEST_CASE("A spring page transition ends at the spring's first resting frame", "[ui][page-transition][spring]")
{
    const time_point started { at(2000) };
    luil::page_transition_style style { style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) };
    style.curve = luil::page_transition_spring { luil::spring_smooth, 0.0f };
    const luil::page_transition value { luil::start_page_transition(prior_screen(), style, started, false) };
    REQUIRE(value.prior != nullptr);
    const std::optional<time_point> end { luil::page_transition_end(value) };
    REQUIRE(end.has_value());
    // 허용치는 0.001로 낮춘 스프링이다 (결정 6).
    luil::spring_config config { luil::spring_smooth };
    config.rest_threshold = 0.001f;
    REQUIRE(*end == luil::spring_rest_time(luil::spring { 0.0f, 1.0f, started, config, 0.0f }));
    REQUIRE(*end > started);
    // 한 frame 앞에서는 끝나지 않고 그 시각에 끝난다.
    REQUIRE(luil::page_transition_finished(value, *end - luil::transition_frame) == false);
    REQUIRE(luil::page_transition_finished(value, *end));
    REQUIRE(luil::page_transition_progress(value, *end) == 1.0f);
    REQUIRE(luil::page_transition_next_update(value, *end) == std::nullopt);
}

TEST_CASE("Spring page transition progress stays in the unit range for every preset", "[ui][page-transition][spring]")
{
    const time_point started { at(0) };
    for (const luil::spring_config& preset : { luil::spring_smooth, luil::spring_snappy, luil::spring_bouncy })
    {
        CAPTURE(preset.response.count(), preset.damping_fraction);
        luil::page_transition_style style { style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) };
        style.curve = luil::page_transition_spring { preset, 0.0f };
        const luil::page_transition value { prior_screen(), started, style };
        const time_point end { *luil::page_transition_end(value) };
        for (time_point now { started }; now <= end + milliseconds { 40 }; now += milliseconds { 4 })
        {
            const float progress { luil::page_transition_progress(value, now) };
            REQUIRE(progress >= 0.0f);
            REQUIRE(progress <= 1.0f);
        }
    }

    // 앱이 큰 허용치를 줘도 끝 직전 진행은 끝에 붙어 있다. 0.01이면 마지막 1%가 한 번에 붙는다.
    luil::page_transition_style coarse { style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) };
    luil::spring_config config { luil::spring_smooth };
    config.rest_threshold = 0.01f;
    coarse.curve = luil::page_transition_spring { config, 0.0f };
    const luil::page_transition value { prior_screen(), started, coarse };
    const time_point end { *luil::page_transition_end(value) };
    REQUIRE(luil::page_transition_progress(value, end - milliseconds { 1 }) >= 0.999f);
}

TEST_CASE("Starting a page transition with reduced motion or a still curve drops the prior screen", "[ui][page-transition][reduce_motion]")
{
    const time_point now { at(500) };
    const luil::page_transition reduced { luil::start_page_transition(prior_screen(), style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward), now, true) };
    REQUIRE(reduced.prior == nullptr);
    REQUIRE(luil::page_transition_finished(reduced, now));
    REQUIRE(luil::page_transition_end(reduced) == std::nullopt);

    // 움직이지 않는 곡선도 언제 물어도 끝이라 입력을 막는 덮개를 남기지 않는다.
    luil::page_transition_style still { style_of(luil::page_transition_effect::fade, luil::page_transition_direction::forward) };
    still.curve = luil::page_transition_timing { milliseconds { 0 }, linear };
    REQUIRE(luil::start_page_transition(prior_screen(), still, now, false).prior == nullptr);
    still.curve = luil::page_transition_spring { luil::spring_config { milliseconds { 0 }, 1.0f }, 0.0f };
    REQUIRE(luil::start_page_transition(prior_screen(), still, now, false).prior == nullptr);

    const luil::page_transition moving { luil::start_page_transition(prior_screen(), style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward), now, false) };
    REQUIRE(moving.prior != nullptr);
    REQUIRE(moving.started == now);
}

TEST_CASE("A forward slide brings the new screen in from the end edge while the prior leaves", "[ui][page-transition][raster]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };

    const auto start { draw_at(tree, started) };
    REQUIRE(start.count_color(slot, prior_color) == 100 * 60);
    require_outside_untouched(start);

    // 가운데: 경계가 요소의 가운데 열이다. 왼쪽 절반이 이전, 오른쪽 절반이 새 화면이다.
    const auto middle { draw_at(tree, started + milliseconds { 500 }) };
    REQUIRE(middle.pixel_at(40, 50) == prior_color);
    REQUIRE(middle.pixel_at(89, 50) == prior_color);
    REQUIRE(middle.pixel_at(90, 50) == next_color);
    REQUIRE(middle.pixel_at(139, 50) == next_color);
    REQUIRE(middle.count_color(slot, prior_color) == 50 * 60);
    REQUIRE(middle.count_color(slot, next_color) == 50 * 60);
    require_outside_untouched(middle);

    const auto end { draw_at(tree, started + milliseconds { 1000 }) };
    REQUIRE(end.count_color(slot, next_color) == 100 * 60);
    require_outside_untouched(end);
}

TEST_CASE("A backward slide moves both screens the other way", "[ui][page-transition][raster]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::backward) }) };
    const auto middle { draw_at(tree, started + milliseconds { 500 }) };
    REQUIRE(middle.pixel_at(40, 50) == next_color);
    REQUIRE(middle.pixel_at(89, 50) == next_color);
    REQUIRE(middle.pixel_at(90, 50) == prior_color);
    REQUIRE(middle.pixel_at(139, 50) == prior_color);
    require_outside_untouched(middle);

    // 세로 축은 아래(forward의 끝 가장자리)가 아니라 위에서 들어온다.
    const luil::ui_tree vertical { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::backward, luil::page_transition_axis::vertical) }) };
    const auto upright { draw_at(vertical, started + milliseconds { 500 }) };
    REQUIRE(upright.pixel_at(90, 20) == next_color);
    REQUIRE(upright.pixel_at(90, 49) == next_color);
    REQUIRE(upright.pixel_at(90, 50) == prior_color);
    REQUIRE(upright.pixel_at(90, 79) == prior_color);
}

TEST_CASE("A fade hides the prior screen over the first half and never overlays both", "[ui][page-transition][raster]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::fade, luil::page_transition_direction::forward) }) };
    const auto quarter { draw_at(tree, started + milliseconds { 250 }) };
    // 진행 0.25에서 이전 화면의 알파는 0.5이고 새 화면은 아직 없다.
    const luil::ui_color background { quarter.palette().window_background };
    const luil::ui_color expected { mix(background, prior_color, 0.5f) };
    REQUIRE(near(quarter.pixel_at(90, 50), expected));
    REQUIRE(quarter.contains_color(slot, next_color) == false);
    require_outside_untouched(quarter);

    // 가운데에서는 둘 다 없다.
    const auto middle { draw_at(tree, started + milliseconds { 500 }) };
    REQUIRE(middle.count_color(slot, background) == 100 * 60);
}

TEST_CASE("A zoom ends on the new screen alone at its own size", "[ui][page-transition][raster]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::zoom, luil::page_transition_direction::forward) }) };
    const auto end { draw_at(tree, started + milliseconds { 1000 }) };
    REQUIRE(end.count_color(slot, next_color) == 100 * 60);
    REQUIRE(end.contains_color(slot, prior_color) == false);
    require_outside_untouched(end);

    // 도는 중에는 이전 화면이 가운데를 기준으로 커진다. 요소 밖으로는 새지 않는다.
    // 진행 0.1이면 이전 화면의 알파는 0.8이다.
    const auto early { draw_at(tree, started + milliseconds { 100 }) };
    REQUIRE(near(early.pixel_at(90, 50), mix(early.palette().window_background, prior_color, 0.8f)));
    require_outside_untouched(early);
}

TEST_CASE("Reduced motion and a mismatched prior size draw the new screen alone", "[ui][page-transition][raster][reduce_motion]")
{
    const time_point started { at(1000) };
    const time_point middle { started + milliseconds { 500 } };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };
    const auto reduced { draw_at(tree, middle, true) };
    REQUIRE(reduced.count_color(slot, next_color) == 100 * 60);
    require_outside_untouched(reduced);

    // 창 크기·배율이 바뀐 도중이다. 옛 크기 그림을 남기지 않는다 (결정 5).
    const luil::ui_tree resized { transition_tree({ prior_screen({ 0.0f, 0.0f, 120.0f, 60.0f }), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };
    const auto mismatched { draw_at(resized, middle) };
    REQUIRE(mismatched.count_color(slot, next_color) == 100 * 60);
    const auto* const element { resized.find({ luil::ui_element_kind::page_transition, u8"page" }) };
    REQUIRE(element != nullptr);
    REQUIRE(element->next_update({ middle, false }, {}) == std::nullopt);
}

TEST_CASE("The prior tree is not part of the new tree's index, focus or access tree", "[ui][page-transition]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };
    REQUIRE(tree.find({ kind_plate, u8"prior-only" }) == nullptr);
    REQUIRE(tree.find({ kind_plate, u8"prior" }) == nullptr);
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(tree.unarranged().empty());
    const std::vector<luil::ui_element_id> plates { tree.ids_of_kind(kind_plate) };
    REQUIRE(std::find(plates.begin(), plates.end(), luil::ui_element_id { kind_plate, u8"prior-only" }) == plates.end());
    REQUIRE(plates.size() == 2u);
    for (const luil::ui_element_id& id : tree.focus_order())
        REQUIRE(id.owner != u8"prior-only");
    // 요소와 덮개는 구조라 접혀 새 화면이 root 바로 아래에 읽힌다.
    REQUIRE(tree.access_top_level().size() == 1u);
    REQUIRE(tree.access_top_level().front()->id() == luil::ui_element_id { kind_plate, u8"next" });
    const auto* const cover { tree.find({ luil::ui_element_kind::page_transition_cover, u8"page" }) };
    REQUIRE(cover != nullptr);
    REQUIRE(cover->accessibility().role == luil::access_role::none);
    REQUIRE(cover->tab_stop() == false);
}

TEST_CASE("While a prior screen is held the cover takes the pointer and blocks drops", "[ui][page-transition]")
{
    const time_point started { at(1000) };
    luil::drag_payload payload {};
    payload.dragged_owner = u8"file";
    // 새 화면의 판이 아무것이나 받는다.
    const auto make = [](luil::page_transition transition) {
        auto screen { std::make_unique<plate>(u8"next", next_color, true) };
        screen->set_drop_target(luil::drop_target { .accepts = [](const luil::drag_payload&) { return true; } });
        auto element { std::make_unique<luil::page_transition_element>(luil::page_transition_config { u8"page", std::move(transition) }, std::move(screen)) };
        return luil::make_arranged_tree(std::move(element), slot, 2.0f);
    };

    const luil::ui_tree moving { make({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };
    const luil::ui_element* const hit { moving.hit_test(90.0f, 50.0f) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id() == luil::ui_element_id { luil::ui_element_kind::page_transition_cover, u8"page" });
    const luil::drop_hit_result blocked { moving.drop_hit_test(90.0f, 50.0f, payload) };
    REQUIRE(blocked.target == nullptr);
    REQUIRE(blocked.blocked);
    REQUIRE(moving.find_drop_target(90.0f, 50.0f, payload) == nullptr);
    // 요소 밖은 덮개가 막지 않는다.
    REQUIRE(moving.hit_test(10.0f, 50.0f) == nullptr);

    const luil::ui_tree settled { make({}) };
    REQUIRE(settled.find({ luil::ui_element_kind::page_transition_cover, u8"page" }) == nullptr);
    const luil::ui_element* const button { settled.hit_test(90.0f, 50.0f) };
    REQUIRE(button != nullptr);
    REQUIRE(button->id() == luil::ui_element_id { kind_plate, u8"next" });
    REQUIRE(settled.find_drop_target(90.0f, 50.0f, payload) == button);
}

TEST_CASE("The transition element asks for frames only while it moves", "[ui][page-transition]")
{
    const time_point started { at(1000) };
    const luil::ui_tree tree { transition_tree({ prior_screen(), started, style_of(luil::page_transition_effect::slide, luil::page_transition_direction::forward) }) };
    const auto* const element { tree.find({ luil::ui_element_kind::page_transition, u8"page" }) };
    REQUIRE(element != nullptr);
    REQUIRE(element->next_update({ started + milliseconds { 100 }, false }, {}) == started + milliseconds { 116 });
    REQUIRE(element->next_update({ at(900), false }, {}) == started);
    REQUIRE(element->next_update({ started + milliseconds { 1000 }, false }, {}) == std::nullopt);
    REQUIRE(element->next_update({ started + milliseconds { 100 }, true }, {}) == std::nullopt);
    // tree도 같은 예고를 모은다.
    REQUIRE(tree.next_update({ started + milliseconds { 100 }, false }, {}) == started + milliseconds { 116 });

    const luil::ui_tree settled { transition_tree({}) };
    REQUIRE(settled.next_update({ started, false }, {}) == std::nullopt);
}
