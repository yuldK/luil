#include "luil/ui/teaching_tip_element.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/field_popup.h"
#include "luil/ui/transition.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 안내 풍선이다 (docs/teaching-tip-design.md). 자리 잡기, 꼬리, 대상 helper, 덮개와 비modal의 초점·Esc·누름을 본다.
namespace {
    constexpr luil::ui_element_kind kind_test { luil::application_element_kind(0) };

    // 코드포인트가 아니라 byte마다 글자 크기의 절반 폭이다. 배율이 2여도 논리 폭은 같다 (message_box test와 같다).
    [[nodiscard]] float half_size_width(const std::u8string_view text, const float pixel_size)
    {
        return static_cast<float>(text.size()) * pixel_size / 2.0f;
    }

    const luil::text_measurer half_size_measurer { half_size_width };

    // 풍선이 낸 메시지다. 닫힘 계기, 행동 단추, 뒤 화면 누름 중 하나다.
    struct tip_event
    {
        std::optional<luil::teaching_tip_dismiss_reason> dismiss {};
        std::optional<std::u8string> pressed {};
    };

    struct flowed_intent
    {
    };

    [[nodiscard]] std::vector<tip_event> events_in(const std::vector<luil::input_action>& actions)
    {
        std::vector<tip_event> events {};
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const event { message->get<tip_event>() }; event != nullptr)
                    events.push_back(*event);
        return events;
    }

    [[nodiscard]] luil::ui_action press_action(const std::u8string_view name)
    {
        return luil::make_message_action(tip_event { .pressed = std::u8string { name } });
    }

    // 정책이 받은 키는 앱으로 흘러간 키다.
    class flowing_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot&) override
        {
            ++keys;
            last = event.key;
            return { luil::make_app_action(flowed_intent {}) };
        }

        int keys { 0 };
        luil::key_code last { luil::key_code::none };
    };

    class test_panel final : public luil::ui_element
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

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {
        }
    };

    // 글이 없고 닫기만 있는 풍선이다. 논리 크기는 200x48이다 (여백 12 + 닫기 24 + 여백 12).
    [[nodiscard]] luil::teaching_tip_config bare_tip()
    {
        luil::teaching_tip_config config {};
        config.owner = u8"tip";
        config.width = 200.0f;
        config.dismiss = [](const luil::teaching_tip_dismiss_reason reason) { return luil::make_app_action(tip_event { .dismiss = reason }); };
        return config;
    }

    [[nodiscard]] luil::teaching_tip_config with_buttons(luil::teaching_tip_config config)
    {
        config.primary = luil::teaching_tip_button { u8"Try", press_action(u8"primary") };
        config.secondary = luil::teaching_tip_button { u8"Later", press_action(u8"secondary") };
        return config;
    }

    constexpr luil::rect_f window { 0.0f, 0.0f, 800.0f, 600.0f };
    // 가운데 대상이다. 가운데는 (400, 300)이다.
    constexpr luil::rect_f target_box { 350.0f, 280.0f, 100.0f, 40.0f };

    [[nodiscard]] luil::teaching_tip_placement place(const luil::teaching_tip_config& config, const luil::rect_f& slot = window, const float scale = 1.0f)
    {
        return luil::teaching_tip_element::place(config, luil::measure_context { .scale = scale, .measurer = &half_size_measurer }, slot);
    }

    [[nodiscard]] luil::teaching_tip_config aimed(const luil::field_popup_side side, const luil::rect_f& target = target_box)
    {
        luil::teaching_tip_config config { bare_tip() };
        config.target = target;
        config.side = side;
        return config;
    }

    [[nodiscard]] bool same(const luil::rect_f& left, const luil::rect_f& right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
    }

    [[nodiscard]] luil::ui_element_id target_id()
    {
        return { kind_test, u8"target" };
    }

    // 뒤 화면(대상 단추 하나)과 풍선을 담은 tree다. 대상은 누르면 `pressed = target`을 낸다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> tip_tree(std::optional<luil::teaching_tip_config> config)
    {
        auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ window, 1.0f });
        auto target { std::make_unique<test_panel>(target_id()) };
        target->set_action(luil::ui_trigger::left_click, press_action(u8"target"));
        target->arrange({ target_box, 1.0f });
        root->add(std::move(target));
        if (config.has_value())
        {
            config->target = target_box;
            auto tip { std::make_unique<luil::teaching_tip_element>(std::move(*config)) };
            tip->arrange({ .slot = window, .scale = 1.0f, .measurer = &half_size_measurer });
            root->add(std::move(tip));
        }
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code)
    {
        return luil::key_pressed_event { code, false, false, false, false, std::chrono::steady_clock::time_point {} };
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] std::vector<tip_event> click(luil::interaction_controller& controller, const float x, const float y)
    {
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(0) }));
        return events_in(controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, at(30) }));
    }
}

TEST_CASE("A teaching tip stands outside the asked edge of its target with the tail aimed at the target center", "[ui][teaching-tip][place]")
{
    // 간격은 꼬리 길이 8에 틈 2를 더한 10이다. 풍선은 200x48이다.
    const luil::teaching_tip_placement below { place(aimed(luil::field_popup_side::below)) };
    REQUIRE(same(below.bubble, { 300.0f, 330.0f, 200.0f, 48.0f }));
    REQUIRE(below.side == luil::field_popup_side::below);
    REQUIRE(below.tail_center == 400.0f);

    const luil::teaching_tip_placement above { place(aimed(luil::field_popup_side::above)) };
    REQUIRE(same(above.bubble, { 300.0f, 222.0f, 200.0f, 48.0f }));
    REQUIRE(above.side == luil::field_popup_side::above);
    REQUIRE(above.tail_center == 400.0f);

    const luil::teaching_tip_placement right { place(aimed(luil::field_popup_side::right)) };
    REQUIRE(same(right.bubble, { 460.0f, 276.0f, 200.0f, 48.0f }));
    REQUIRE(right.side == luil::field_popup_side::right);
    REQUIRE(right.tail_center == 300.0f);

    const luil::teaching_tip_placement left { place(aimed(luil::field_popup_side::left)) };
    REQUIRE(same(left.bubble, { 140.0f, 276.0f, 200.0f, 48.0f }));
    REQUIRE(left.side == luil::field_popup_side::left);
    REQUIRE(left.tail_center == 300.0f);

    // 아래가 모자라면 위로 뒤집고 꼬리도 따라 선다.
    const luil::teaching_tip_placement flipped { place(aimed(luil::field_popup_side::below, { 350.0f, 540.0f, 100.0f, 40.0f })) };
    REQUIRE(flipped.side == luil::field_popup_side::above);
    REQUIRE(flipped.bubble.y == 482.0f);
}

TEST_CASE("The tail base stays inside the corner radius when the target center is past the bubble end", "[ui][teaching-tip][place]")
{
    // 왼쪽 끝 대상의 가운데(20)는 limit 왼쪽(8)에 막힌 풍선의 모서리 안쪽(8 + 8 + 꼬리 반폭 8 = 24)에 멈춘다.
    const luil::teaching_tip_placement left_end { place(aimed(luil::field_popup_side::below, { 10.0f, 280.0f, 20.0f, 40.0f })) };
    REQUIRE(left_end.bubble.x == 8.0f);
    REQUIRE(left_end.tail_center == 24.0f);
    // 오른쪽 끝도 같다. 풍선은 x 592..792이고 꼬리는 776에 멈춘다.
    const luil::teaching_tip_placement right_end { place(aimed(luil::field_popup_side::below, { 780.0f, 280.0f, 20.0f, 40.0f })) };
    REQUIRE(right_end.bubble.x == 592.0f);
    REQUIRE(right_end.tail_center == 776.0f);
}

TEST_CASE("Without a visible target the tip sits in the middle of the room with no tail", "[ui][teaching-tip][place]")
{
    // limit은 여백 8을 뺀 784x584다. 가운데는 (300, 276)이다.
    const luil::rect_f middle { 300.0f, 276.0f, 200.0f, 48.0f };
    const luil::teaching_tip_placement none { place(bare_tip()) };
    REQUIRE(same(none.bubble, middle));
    REQUIRE(none.side.has_value() == false);
    // 자리 밖 대상과 넓이 없는 대상은 대상 없음과 같다.
    for (const luil::rect_f& target : { luil::rect_f { 900.0f, 700.0f, 50.0f, 50.0f }, luil::rect_f { 400.0f, 300.0f, 0.0f, 20.0f } })
    {
        const luil::teaching_tip_placement outside { place(aimed(luil::field_popup_side::below, target)) };
        REQUIRE(same(outside.bubble, middle));
        REQUIRE(outside.side.has_value() == false);
    }
    // 네 방향 모두 모자라면 바라는 방향에 밀어 두고 꼬리를 그리지 않는다.
    const luil::teaching_tip_placement crowded { place(aimed(luil::field_popup_side::below, { 50.0f, 20.0f, 100.0f, 30.0f }), { 0.0f, 0.0f, 220.0f, 70.0f }) };
    REQUIRE(crowded.side.has_value() == false);
    REQUIRE(crowded.bubble.y == 14.0f);
}

TEST_CASE("A teaching tip narrows to the room and trims the body lines when the room is short", "[ui][teaching-tip][place]")
{
    // 폭은 자리 폭에서 여백 둘을 뺀 값을 넘지 않는다.
    const luil::teaching_tip_placement narrow { place(bare_tip(), { 0.0f, 0.0f, 150.0f, 600.0f }) };
    REQUIRE(narrow.bubble.width == 134.0f);

    luil::teaching_tip_config talkative { bare_tip() };
    talkative.title = u8"New";
    talkative.body = u8"This body is long enough to wrap onto many lines in a narrow bubble so that a short room has to trim it.";
    const luil::teaching_tip_placement roomy { place(talkative) };
    const luil::teaching_tip_placement cramped { place(talkative, { 0.0f, 0.0f, 800.0f, 120.0f }) };
    // 넓은 자리에서는 줄을 다 쓰고, 좁은 자리에서는 limit 높이(104) 안으로 줄인다.
    REQUIRE(roomy.bubble.height > 104.0f);
    REQUIRE(cramped.bubble.height <= 104.0f);
    REQUIRE(cramped.bubble.height > 48.0f);
}

TEST_CASE("A teaching tip keeps the same logical place at scale 1, 1.5 and 2", "[ui][teaching-tip][place][scale]")
{
    for (const luil::field_popup_side side : { luil::field_popup_side::below, luil::field_popup_side::above, luil::field_popup_side::left, luil::field_popup_side::right })
    {
        const luil::teaching_tip_placement base { place(aimed(side)) };
        for (const float scale : { 1.5f, 2.0f })
        {
            CAPTURE(scale);
            luil::teaching_tip_config config { aimed(side) };
            config.target = luil::rect_f { target_box.x * scale, target_box.y * scale, target_box.width * scale, target_box.height * scale };
            const luil::teaching_tip_placement scaled { place(config, { 0.0f, 0.0f, 800.0f * scale, 600.0f * scale }, scale) };
            REQUIRE(scaled.side == base.side);
            REQUIRE(scaled.bubble.x / scale == Catch::Approx(base.bubble.x));
            REQUIRE(scaled.bubble.y / scale == Catch::Approx(base.bubble.y));
            REQUIRE(scaled.bubble.width / scale == Catch::Approx(base.bubble.width));
            REQUIRE(scaled.bubble.height / scale == Catch::Approx(base.bubble.height));
            REQUIRE(scaled.tail_center / scale == Catch::Approx(base.tail_center));
        }
    }
}

TEST_CASE("A teaching tip names its parts by owner and builds only the buttons it can press", "[ui][teaching-tip]")
{
    REQUIRE(luil::teaching_tip_element::close_id(u8"tip") == luil::ui_element_id { luil::ui_element_kind::teaching_tip_close, u8"tip\u001fclose" });
    REQUIRE(luil::teaching_tip_element::primary_id(u8"tip") == luil::ui_element_id { luil::ui_element_kind::teaching_tip_action, u8"tip\u001fprimary" });
    REQUIRE(luil::teaching_tip_element::secondary_id(u8"tip") == luil::ui_element_id { luil::ui_element_kind::teaching_tip_action, u8"tip\u001fsecondary" });

    const auto full { tip_tree(with_buttons(bare_tip())) };
    REQUIRE(full->find({ luil::ui_element_kind::teaching_tip, u8"tip" }) != nullptr);
    REQUIRE(full->find({ luil::ui_element_kind::teaching_tip_scrim, u8"tip" }) != nullptr);
    REQUIRE(full->find({ luil::ui_element_kind::teaching_tip_body, u8"tip" }) != nullptr);
    REQUIRE(full->find(luil::teaching_tip_element::close_id(u8"tip")) != nullptr);
    REQUIRE(full->find(luil::teaching_tip_element::primary_id(u8"tip")) != nullptr);
    REQUIRE(full->find(luil::teaching_tip_element::secondary_id(u8"tip")) != nullptr);

    // dismiss가 비면 닫기 단추가 없다. 글이나 액션 하나만 있는 단추도 서지 않는다.
    luil::teaching_tip_config partial { bare_tip() };
    partial.dismiss = nullptr;
    partial.primary = luil::teaching_tip_button { u8"Try", {} };
    partial.secondary = luil::teaching_tip_button { u8"", press_action(u8"secondary") };
    const auto bare { tip_tree(partial) };
    REQUIRE(bare->find(luil::teaching_tip_element::close_id(u8"tip")) == nullptr);
    REQUIRE(bare->find(luil::teaching_tip_element::primary_id(u8"tip")) == nullptr);
    REQUIRE(bare->find(luil::teaching_tip_element::secondary_id(u8"tip")) == nullptr);
    // 비modal에는 덮개가 없다.
    luil::teaching_tip_config modeless { bare_tip() };
    modeless.modal = false;
    REQUIRE(tip_tree(modeless)->find({ luil::ui_element_kind::teaching_tip_scrim, u8"tip" }) == nullptr);
}

TEST_CASE("The action buttons sit right aligned in one row and stack when the row is too narrow", "[ui][teaching-tip]")
{
    const auto tree { tip_tree(with_buttons(bare_tip())) };
    const luil::rect_f primary { tree->find(luil::teaching_tip_element::primary_id(u8"tip"))->bounds() };
    const luil::rect_f secondary { tree->find(luil::teaching_tip_element::secondary_id(u8"tip"))->bounds() };
    const luil::rect_f body { tree->find({ luil::ui_element_kind::teaching_tip_body, u8"tip" })->bounds() };
    // 한 줄이고 primary가 왼쪽이다. 오른쪽 끝은 안쪽 여백 12에 붙는다.
    REQUIRE(primary.y == secondary.y);
    REQUIRE(primary.x < secondary.x);
    REQUIRE(secondary.x + secondary.width == Catch::Approx(body.x + body.width - 12.0f));
    // 닫기는 풍선 오른쪽 위다.
    const luil::rect_f close { tree->find(luil::teaching_tip_element::close_id(u8"tip"))->bounds() };
    REQUIRE(close.x + close.width == Catch::Approx(body.x + body.width - 12.0f));
    REQUIRE(close.y == Catch::Approx(body.y + 12.0f));

    luil::teaching_tip_config tight { with_buttons(bare_tip()) };
    tight.width = 120.0f;
    const auto stacked { tip_tree(tight) };
    const luil::rect_f top { stacked->find(luil::teaching_tip_element::primary_id(u8"tip"))->bounds() };
    const luil::rect_f bottom { stacked->find(luil::teaching_tip_element::secondary_id(u8"tip"))->bounds() };
    REQUIRE(top.x == bottom.x);
    REQUIRE(top.width == 96.0f);
    REQUIRE(bottom.y > top.y);
}

TEST_CASE("The target helper answers the visible part of an arranged branch", "[ui][teaching-tip][target]")
{
    // 자르는 판(0, 0) 200x100 안의 대상 셋이다. 하나는 걸치고, 하나는 판 밖이며, 숨은 가지 안에 하나가 있다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ window, 1.0f });
    auto clip { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"clip" }) };
    clip->set_clip_children(true);
    clip->arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    auto partly { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"partly" }) };
    partly->arrange({ { 150.0f, 50.0f, 100.0f, 100.0f }, 1.0f });
    clip->add(std::move(partly));
    auto scrolled_out { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"scrolled-out" }) };
    scrolled_out->arrange({ { 0.0f, 300.0f, 100.0f, 40.0f }, 1.0f });
    clip->add(std::move(scrolled_out));
    root->add(std::move(clip));
    auto hidden { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"hidden" }) };
    hidden->set_visible(false);
    hidden->arrange({ { 300.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    auto inside_hidden { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"inside-hidden" }) };
    inside_hidden->arrange({ { 300.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    hidden->add(std::move(inside_hidden));
    root->add(std::move(hidden));
    const luil::ui_tree tree { std::move(root) };

    const std::optional<luil::rect_f> visible { luil::teaching_tip_target(*tree.root(), { kind_test, u8"partly" }) };
    REQUIRE(visible.has_value());
    REQUIRE(same(*visible, { 150.0f, 50.0f, 50.0f, 50.0f }));
    REQUIRE(luil::teaching_tip_target(*tree.root(), { kind_test, u8"scrolled-out" }).has_value() == false);
    REQUIRE(luil::teaching_tip_target(*tree.root(), { kind_test, u8"missing" }).has_value() == false);

    // 자르는 조상·창 밖·없는 id는 `ui_tree::visible_bounds`와 같은 답이다.
    const luil::ui_element* const partly_element { tree.find({ kind_test, u8"partly" }) };
    REQUIRE(same(*tree.visible_bounds(*partly_element), *visible));
    REQUIRE(tree.visible_bounds(*tree.find({ kind_test, u8"scrolled-out" })).has_value() == false);
    REQUIRE(tree.find({ kind_test, u8"missing" }) == nullptr);
}

TEST_CASE("The target helper skips a hidden branch", "[ui][teaching-tip][target]")
{
    // `ui_tree::visible_bounds`는 숨김을 보지 않으므로 숨은 가지는 helper만 nullopt다. 그래서 둘을 견주지 않는다.
    auto root { std::make_unique<test_panel>(luil::ui_element_id { luil::ui_element_kind::root }) };
    root->arrange({ window, 1.0f });
    auto hidden { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"hidden" }) };
    hidden->set_visible(false);
    hidden->arrange({ { 300.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    auto inside { std::make_unique<test_panel>(luil::ui_element_id { kind_test, u8"inside-hidden" }) };
    inside->arrange({ { 300.0f, 0.0f, 50.0f, 50.0f }, 1.0f });
    hidden->add(std::move(inside));
    root->add(std::move(hidden));
    const luil::ui_tree tree { std::move(root) };
    REQUIRE(luil::teaching_tip_target(*tree.root(), { kind_test, u8"inside-hidden" }).has_value() == false);
    REQUIRE(luil::teaching_tip_target(*tree.root(), { kind_test, u8"hidden" }).has_value() == false);
}

TEST_CASE("A modal tip enters on primary, traps Tab, reports Esc and the scrim, and returns the focus", "[ui][teaching-tip][modal]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };
    const auto focused = [&controller] { return controller.snapshot().focused; };
    luil::teaching_tip_config config { with_buttons(bare_tip()) };
    config.focus_return = target_id();
    const auto tree { tip_tree(config) };
    REQUIRE(tree->focus_trap() == tree->find({ luil::ui_element_kind::teaching_tip, u8"tip" }));

    controller.set_tree(tree);
    REQUIRE(focused() == luil::teaching_tip_element::primary_id(u8"tip"));
    // Tab 순서는 닫기, primary, secondary이고 풍선 밖으로 나가지 않는다.
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::secondary_id(u8"tip"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::close_id(u8"tip"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::primary_id(u8"tip"));

    // Esc는 가둠의 것이다. 앱으로 흐르지 않는다.
    std::vector<tip_event> events { events_in(controller.process(key(luil::key_code::escape))) };
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::teaching_tip_dismiss_reason::escape);
    REQUIRE(policy.keys == 0);

    // 대상 위를 눌러도 덮개가 받는다. 대상은 눌리지 않는다.
    events = click(controller, 400.0f, 300.0f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::teaching_tip_dismiss_reason::outside);
    REQUIRE(events[0].pressed.has_value() == false);

    // 행동 단추는 앱 액션이다. 닫지 않는다.
    const luil::rect_f primary { tree->find(luil::teaching_tip_element::primary_id(u8"tip"))->bounds() };
    events = click(controller, primary.x + 4.0f, primary.y + 4.0f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].pressed == std::u8string { u8"primary" });

    // 닫힌 뒤 연 자리로 돌아간다.
    controller.set_tree(tip_tree(std::nullopt));
    REQUIRE(focused() == target_id());
}

TEST_CASE("A modal tip without a dismiss absorbs the scrim and lets Esc flow to the app", "[ui][teaching-tip][modal]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };
    luil::teaching_tip_config config { with_buttons(bare_tip()) };
    config.dismiss = nullptr;
    const auto tree { tip_tree(config) };
    controller.set_tree(tree);
    REQUIRE(controller.snapshot().focused == luil::teaching_tip_element::primary_id(u8"tip"));
    REQUIRE(tree->find({ luil::ui_element_kind::teaching_tip, u8"tip" })->dismiss_action() == nullptr);
    // 가둠에 dismiss 액션이 없으면 Esc는 앱으로 간다.
    static_cast<void>(controller.process(key(luil::key_code::escape)));
    REQUIRE(policy.keys == 1);
    // 덮개는 누름만 흡수한다. 대상은 눌리지 않는다.
    REQUIRE(click(controller, 400.0f, 300.0f).empty());
}

TEST_CASE("A modal tip enters on the next part that exists and has no auto focus without any", "[ui][teaching-tip][modal]")
{
    luil::interaction_controller controller {};
    luil::teaching_tip_config only_secondary { bare_tip() };
    only_secondary.secondary = luil::teaching_tip_button { u8"Later", press_action(u8"secondary") };
    controller.set_tree(tip_tree(only_secondary));
    REQUIRE(controller.snapshot().focused == luil::teaching_tip_element::secondary_id(u8"tip"));

    luil::interaction_controller close_only {};
    close_only.set_tree(tip_tree(bare_tip()));
    REQUIRE(close_only.snapshot().focused == luil::teaching_tip_element::close_id(u8"tip"));

    luil::interaction_controller nothing {};
    luil::teaching_tip_config empty { bare_tip() };
    empty.dismiss = nullptr;
    const auto tree { tip_tree(empty) };
    REQUIRE(tree->find({ luil::ui_element_kind::teaching_tip, u8"tip" })->focus_entry() == luil::ui_element_id {});
    nothing.set_tree(tree);
    REQUIRE(nothing.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("A modeless tip keeps the focus, sits at the end of Tab and lets every Esc flow to the app", "[ui][teaching-tip][modeless]")
{
    flowing_policy policy {};
    luil::interaction_controller controller { &policy };
    const auto focused = [&controller] { return controller.snapshot().focused; };
    luil::teaching_tip_config config { with_buttons(bare_tip()) };
    config.modal = false;

    // 대상에 초점을 둔 채 풍선이 떠도 초점은 그대로다.
    controller.set_tree(tip_tree(std::nullopt));
    static_cast<void>(click(controller, 400.0f, 300.0f));
    REQUIRE(focused() == target_id());
    const auto tree { tip_tree(config) };
    REQUIRE(tree->focus_trap() == nullptr);
    const luil::ui_element& root { *tree->find({ luil::ui_element_kind::teaching_tip, u8"tip" }) };
    REQUIRE(root.dismiss_action() == nullptr);
    REQUIRE(root.accessibility().role == luil::access_role::pane);
    controller.set_tree(tree);
    REQUIRE(focused() == target_id());

    // 풍선 밖 Esc도 앱으로 간다. dismiss는 나오지 않는다.
    std::vector<tip_event> events { events_in(controller.process(key(luil::key_code::escape))) };
    REQUIRE(events.empty());
    REQUIRE(policy.keys == 1);

    // 풍선의 단추는 Tab 순서의 끝이다 (닫기, primary, secondary). 풍선 안 Esc도 앱으로 간다.
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::close_id(u8"tip"));
    events = events_in(controller.process(key(luil::key_code::escape)));
    REQUIRE(events.empty());
    REQUIRE(policy.keys == 2);
    REQUIRE(policy.last == luil::key_code::escape);
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::primary_id(u8"tip"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == luil::teaching_tip_element::secondary_id(u8"tip"));
    static_cast<void>(controller.process(key(luil::key_code::tab)));
    REQUIRE(focused() == target_id());

    // 뒤 화면은 그대로 눌린다. 닫는 길은 닫기 단추다.
    events = click(controller, 400.0f, 300.0f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].pressed == std::u8string { u8"target" });
    const luil::rect_f close { tree->find(luil::teaching_tip_element::close_id(u8"tip"))->bounds() };
    events = click(controller, close.x + close.width / 2.0f, close.y + close.height / 2.0f);
    REQUIRE(events.size() == 1u);
    REQUIRE(events[0].dismiss == luil::teaching_tip_dismiss_reason::close_button);
    // 풍선의 빈 자리는 흡수한다.
    const luil::rect_f body { tree->find({ luil::ui_element_kind::teaching_tip_body, u8"tip" })->bounds() };
    REQUIRE(tree->hit_test(body.x + 4.0f, body.y + body.height - 4.0f)->id().kind == luil::ui_element_kind::teaching_tip_body);
}

TEST_CASE("A teaching tip asks for frames only while it appears and never under reduced motion", "[ui][teaching-tip][motion]")
{
    luil::teaching_tip_config config { bare_tip() };
    const luil::teaching_tip_element still { config };
    REQUIRE(still.next_update({ .now = at(50) }, {}).has_value() == false);

    config.opened_at = at(1000);
    const luil::teaching_tip_element appearing { config };
    REQUIRE(appearing.next_update({ .now = at(900) }, {}) == at(1000));
    REQUIRE(appearing.next_update({ .now = at(1050) }, {}) == at(1050) + luil::transition_frame);
    REQUIRE(appearing.next_update({ .now = at(1000) + luil::transition_duration }, {}).has_value() == false);
    REQUIRE(appearing.next_update({ .now = at(1050), .reduce_motion = true }, {}).has_value() == false);
}
