#include "luil/app/menu_popup.h"
#include "luil/ui/commands.h"
#include "luil/ui/menu_bar_element.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using luil::key_code;
    using luil::menu_bar_state;
    using luil::ui_element_id;
    using luil::ui_element_kind;

    // 막대가 낸 상태를 실은 메시지다.
    struct bar_intent
    {
        menu_bar_state state {};
    };

    // 메뉴 항목을 고른 메시지와 하위 메뉴를 연 메시지다.
    struct pick_intent
    {
        std::u8string key {};
    };

    struct submenu_intent
    {
        std::u8string key {};
    };

    // 앱 정책까지 흘러온 키를 적는다.
    class bar_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override
        {
            return luil::menu_kinds { ui_element_kind::menu, ui_element_kind::menu_item };
        }

        [[nodiscard]] std::vector<luil::input_action> close_menu() const override
        {
            return { luil::make_app_action(pick_intent { u8"close_menu" }) };
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event& event, const luil::interaction_snapshot&) override
        {
            keys.push_back(event.key);
            return {};
        }

        std::vector<key_code> keys {};
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

    // 바이트마다 글자 크기의 절반인 고정 폭이다. Core test의 기본 측정기는 글꼴이 없어 0을 잰다.
    const luil::text_measurer half_width { [](const std::u8string_view text, const float size) { return static_cast<float>(text.size()) * size * 0.5f; } };

    // "File(F)·Edit(E)·View(V)·Help(X)"다. Help는 라벨에 X가 없어 "(X)"가 붙는다. `view_enabled`가 거짓이면 View가 꺼진다.
    [[nodiscard]] luil::menu_bar_config bar_config(const menu_bar_state state, const bool view_enabled = true)
    {
        luil::menu_bar_config config {};
        config.owner = u8"main";
        config.items = {
            { .key = u8"file", .label = u8"File", .access_key = U'f' },
            { .key = u8"edit", .label = u8"Edit", .access_key = U'E' },
            { .key = u8"view", .label = u8"View", .access_key = U'v', .enabled = view_enabled },
            { .key = u8"help", .label = u8"Help", .access_key = U'x' },
        };
        config.state = state;
        config.set_state = [](const menu_bar_state& next) { return luil::make_app_action(bar_intent { next }); };
        return config;
    }

    // 막대를 (0, 0)에 높이 28로 세운 주 tree다. 막대 아래에 누를 단추 하나를 둔다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> bar_tree(luil::menu_bar_config config, const float scale = 1.0f, const float width = 400.0f)
    {
        auto root { std::make_unique<test_panel>(ui_element_id { ui_element_kind::root }) };
        root->arrange({ .slot = { 0.0f, 0.0f, width, 200.0f }, .scale = scale });
        auto bar { std::make_unique<luil::menu_bar_element>(std::move(config)) };
        bar->arrange({ .slot = { 0.0f, 0.0f, width, luil::menu_bar_height * scale }, .scale = scale, .measurer = &half_width });
        root->add(std::move(bar));
        auto button { std::make_unique<test_panel>(ui_element_id { luil::application_element_kind(0), u8"button" }) };
        button->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> { luil::make_app_action(pick_intent { u8"button" }) }; });
        button->arrange({ .slot = { 0.0f, 100.0f, 80.0f, 30.0f }, .scale = scale });
        root->add(std::move(button));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }

    // 막대 메뉴 하나다. owner가 항목 키라 controller가 막대 메뉴로 안다 (`make_menu_bar_popup`과 같다).
    //  - 첫 줄은 꺼져 있다. 키보드로 연 메뉴는 그다음 활성 항목을 강조한다.
    //  - "open"은 하위 메뉴를 담는다. "save"와 "saveas"는 접근 키 S를 함께 쓴다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> menu_tree(std::u8string owner)
    {
        luil::menu_config config {};
        config.owner = std::move(owner);
        config.items = {
            { .key = u8"off", .label = u8"Off", .enabled = false },
            { .key = u8"new", .label = u8"New", .access_key = U'n' },
            { .key = u8"open", .label = u8"Open", .children = { { .key = u8"recent", .label = u8"Recent" } }, .access_key = U'o' },
            { .key = u8"save", .label = u8"Save", .access_key = U's' },
            { .key = u8"saveas", .label = u8"Save as", .access_key = U's' },
        };
        config.select = [](const std::u8string& key) { return luil::make_app_action(pick_intent { key }); };
        config.open_submenu = [](const std::u8string& key) { return luil::make_app_action(submenu_intent { key }); };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::menu_element>(std::move(config)), { 0.0f, 0.0f, 160.0f, 140.0f }, 1.0f));
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_of(const luil::input_action& action)
    {
        const auto* const message { std::get_if<luil::app_message>(&action) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    // 액션 목록의 첫 막대 상태다. 없으면 nullopt다.
    [[nodiscard]] std::optional<menu_bar_state> bar_state_of(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const bar_intent* const intent { message_of<bar_intent>(action) }; intent != nullptr)
                return intent->state;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::u8string> picked(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const pick_intent* const intent { message_of<pick_intent>(action) }; intent != nullptr)
                return intent->key;
        return std::nullopt;
    }

    [[nodiscard]] luil::key_pressed_event key(const key_code code, const bool control = false, const bool shift = false, const bool alt = false)
    {
        return luil::key_pressed_event { code, control, shift, alt };
    }

    [[nodiscard]] std::vector<luil::input_action> click(luil::interaction_controller& controller, const float x, const float y, std::u8string surface = {})
    {
        std::vector<luil::input_action> actions { controller.process(luil::pointer_pressed_event { .x = x, .y = y, .surface = surface }) };
        for (luil::input_action& action : controller.process(luil::pointer_released_event { .x = x, .y = y, .surface = std::move(surface) }))
            actions.push_back(std::move(action));
        return actions;
    }

    [[nodiscard]] const luil::menu_bar_element& bar_in(const luil::ui_tree& tree)
    {
        const auto* const bar { luil::active_menu_bar(tree) };
        REQUIRE(bar != nullptr);
        return *bar;
    }

    [[nodiscard]] bool same(const std::optional<luil::rect_f>& actual, const luil::rect_f& expected)
    {
        return actual.has_value() && actual->x == expected.x && actual->y == expected.y && actual->width == expected.width && actual->height == expected.height;
    }

    [[nodiscard]] ui_element_id item(std::u8string key)
    {
        return ui_element_id { ui_element_kind::menu_item, std::move(key) };
    }
}

TEST_CASE("Menu bar items are sized from their text, append a missing access key and scale with the surface", "[ui][menu-bar]")
{
    // 글 폭(바이트 × 12 × 0.5)에 양옆 여백 10씩이다. "Help(X)"는 일곱 바이트다.
    const luil::menu_bar_config config { bar_config({}) };
    REQUIRE(luil::menu_bar_element::width_for(config, half_width, 12.0f, 1.0f) == 44.0f + 44.0f + 44.0f + 62.0f);
    // 물리 글자 크기로 재고 논리로 되돌린다. 고정 폭 측정기는 배율 2에서도 같은 논리 폭이다.
    REQUIRE(luil::menu_bar_element::width_for(config, half_width, 12.0f, 2.0f) == 44.0f + 44.0f + 44.0f + 62.0f);

    const auto tree { bar_tree(config) };
    const luil::menu_bar_element& bar { bar_in(*tree) };
    REQUIRE(same(bar.item_bounds(u8"file"), { 0.0f, 0.0f, 44.0f, 28.0f }));
    REQUIRE(same(bar.item_bounds(u8"help"), { 132.0f, 0.0f, 62.0f, 28.0f }));
    REQUIRE(bar.item_bounds(u8"missing").has_value() == false);
    for (const auto& child : bar.children())
        REQUIRE(same(bar.item_bounds(child->id().owner), child->bounds()));

    // 배율 2는 물리 픽셀로 두 배다.
    const auto scaled { bar_tree(bar_config({}), 2.0f, 800.0f) };
    REQUIRE(same(bar_in(*scaled).item_bounds(u8"edit"), { 88.0f, 0.0f, 88.0f, 56.0f }));

    // 좁으면 넘친 항목은 막대 밖에 서고 막대가 잘라 그린다. 키보드로는 여전히 닿는다.
    const auto narrow { bar_tree(bar_config({ u8"file", false, true }), 1.0f, 100.0f) };
    const luil::menu_bar_element& clipped { bar_in(*narrow) };
    REQUIRE(clipped.clip_children());
    REQUIRE(clipped.item_bounds(u8"help")->x >= clipped.bounds().width);
    const std::optional<std::vector<luil::input_action>> end { clipped.handle_key(key(key_code::end), false) };
    REQUIRE(end.has_value());
    REQUIRE(bar_state_of(*end) == menu_bar_state { u8"help", false, true });
}

TEST_CASE("Clicking a menu bar item opens or closes its menu and hover switches while open", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({})));
    // 닫힌 항목은 포인터로 연다.
    REQUIRE(bar_state_of(click(controller, 60.0f, 14.0f)) == menu_bar_state { u8"edit", true, false });

    // 열린 항목은 닫고, 다른 항목은 그 메뉴로 옮긴다.
    controller.set_tree(bar_tree(bar_config({ u8"edit", true, false })));
    REQUIRE(bar_state_of(click(controller, 60.0f, 14.0f)) == menu_bar_state {});
    REQUIRE(bar_state_of(click(controller, 20.0f, 14.0f)) == menu_bar_state { u8"file", true, false });

    // 열린 동안 다른 항목에 포인터가 오면 지연 없이 그 메뉴다. 열린 항목 위는 아무 일도 없다.
    luil::interaction_controller hover { &policy };
    hover.set_tree(bar_tree(bar_config({ u8"edit", true, false })));
    REQUIRE(bar_state_of(hover.process(luil::pointer_moved_event { .x = 60.0f, .y = 14.0f })).has_value() == false);
    REQUIRE(bar_state_of(hover.process(luil::pointer_moved_event { .x = 150.0f, .y = 14.0f })) == menu_bar_state { u8"help", true, false });
    // 꺼진 항목은 hover로도 열리지 않는다.
    luil::interaction_controller disabled { &policy };
    disabled.set_tree(bar_tree(bar_config({ u8"edit", true, false }, false)));
    REQUIRE(bar_state_of(disabled.process(luil::pointer_moved_event { .x = 110.0f, .y = 14.0f })).has_value() == false);

    // 닫힌 막대 위의 hover는 메뉴를 열지 않는다.
    luil::interaction_controller resting { &policy };
    resting.set_tree(bar_tree(bar_config({})));
    REQUIRE(bar_state_of(resting.process(luil::pointer_moved_event { .x = 60.0f, .y = 14.0f })).has_value() == false);
}

TEST_CASE("A menu bar without set_state is inert to pointer and keys", "[ui][menu-bar]")
{
    luil::menu_bar_config config { bar_config({}) };
    config.set_state = {};
    const auto tree { bar_tree(std::move(config)) };
    REQUIRE(luil::active_menu_bar(*tree) == nullptr);
    const auto* const file { tree->find({ ui_element_kind::menu_bar_item, u8"file" }) };
    REQUIRE(file != nullptr);
    REQUIRE(file->action(luil::ui_trigger::left_click) == nullptr);
    const auto* const bar { dynamic_cast<const luil::menu_bar_element*>(tree->find({ ui_element_kind::menu_bar, u8"main" })) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->handle_key(key(key_code::alt_tap), false).has_value() == false);

    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(tree);
    REQUIRE(controller.process(key(key_code::alt_tap)).empty());
    REQUIRE(controller.snapshot().menu_keys == false);
}

TEST_CASE("Alt tap, F10 and Alt plus an access key raise the menu bar", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({})));
    REQUIRE(controller.snapshot().menu_keys == false);
    REQUIRE(bar_state_of(controller.process(key(key_code::alt_tap))) == menu_bar_state { u8"file", false, true });
    REQUIRE(controller.last_key_handled());
    REQUIRE(bar_state_of(controller.process(key(key_code::f10))) == menu_bar_state { u8"file", false, true });
    REQUIRE(controller.last_key_handled());
    // Alt+글자는 그 메뉴를 바로 연다. 대소문자를 가리지 않는다.
    REQUIRE(bar_state_of(controller.process(key(key_code::key_e, false, false, true))) == menu_bar_state { u8"edit", true, true });
    REQUIRE(controller.last_key_handled());
    REQUIRE(bar_state_of(controller.process(key(key_code::key_x, false, false, true))) == menu_bar_state { u8"help", true, true });
    // 꺼진 항목의 접근 키와 맞지 않는 Alt+글자는 앱 단축키로 흐른다.
    luil::interaction_controller disabled { &policy };
    disabled.set_tree(bar_tree(bar_config({}, false)));
    policy.keys.clear();
    REQUIRE(bar_state_of(disabled.process(key(key_code::key_v, false, false, true))).has_value() == false);
    REQUIRE(disabled.last_key_handled() == false);
    REQUIRE(bar_state_of(disabled.process(key(key_code::key_q, false, false, true))).has_value() == false);
    REQUIRE(disabled.last_key_handled() == false);
    REQUIRE(policy.keys == std::vector<key_code> { key_code::key_v, key_code::key_q });
    // Shift+F10은 지금처럼 컨텍스트 메뉴다. 막대는 받지 않는다.
    REQUIRE(bar_state_of(controller.process(key(key_code::f10, false, true))).has_value() == false);
    // Ctrl+Alt+글자는 접근 키가 아니다.
    REQUIRE(bar_state_of(controller.process(key(key_code::key_e, true, false, true))).has_value() == false);
}

TEST_CASE("F10 flows to the app when the menu bar does not take it", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    luil::menu_bar_config config { bar_config({}) };
    config.f10_activates = false;
    controller.set_tree(bar_tree(std::move(config)));
    REQUIRE(bar_state_of(controller.process(key(key_code::f10))).has_value() == false);
    REQUIRE(policy.keys == std::vector<key_code> { key_code::f10 });
    // Alt 단독은 그대로 막대를 세운다.
    REQUIRE(bar_state_of(controller.process(key(key_code::alt_tap))) == menu_bar_state { u8"file", false, true });
}

TEST_CASE("A selected menu bar moves, opens and steps back with the keyboard and swallows the rest", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"file", false, true }, false)));
    REQUIRE(controller.snapshot().menu_keys);
    // 끝에서 돌고 꺼진 View는 건너뛴다.
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_right))) == menu_bar_state { u8"edit", false, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_left))) == menu_bar_state { u8"help", false, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::end))) == menu_bar_state { u8"help", false, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::home))) == menu_bar_state { u8"file", false, true });
    controller.set_tree(bar_tree(bar_config({ u8"edit", false, true }, false)));
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_right))) == menu_bar_state { u8"help", false, true });
    // ↓·↑·Enter·Space는 선 항목의 메뉴를 연다.
    for (const key_code code : { key_code::arrow_down, key_code::arrow_up, key_code::enter, key_code::space })
        REQUIRE(bar_state_of(controller.process(key(code))) == menu_bar_state { u8"edit", true, true });
    // 수식 키 없는 접근 키 글자도 그 메뉴를 연다.
    REQUIRE(bar_state_of(controller.process(key(key_code::key_f))) == menu_bar_state { u8"file", true, true });
    // Esc·Alt 단독·F10은 막대를 쉬게 한다.
    for (const key_code code : { key_code::escape, key_code::alt_tap, key_code::f10 })
        REQUIRE(bar_state_of(controller.process(key(code))) == menu_bar_state {});
    // 막대의 것이 아닌 키는 삼킨다. 앱 정책까지 가지 않는다.
    policy.keys.clear();
    REQUIRE(controller.process(key(key_code::key_q)).empty());
    REQUIRE(controller.process(key(key_code::tab)).empty());
    REQUIRE(controller.process(key(key_code::key_s, true)).empty());
    REQUIRE(policy.keys.empty());
    // 같은 키가 글자로도 온다. 막대가 선 동안의 글자는 버린다.
    REQUIRE(controller.process(luil::character_typed_event { U'q' }).empty());
}

TEST_CASE("Pressing outside a selected menu bar or deactivating its window rests it first", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"file", false, true })));
    // 밖 누름은 막대를 쉬게 하는 상태가 먼저이고 그 자리의 클릭은 그 뒤다.
    const std::vector<luil::input_action> actions { click(controller, 40.0f, 110.0f) };
    REQUIRE(actions.size() == 2u);
    REQUIRE(bar_state_of({ actions[0] }) == menu_bar_state {});
    REQUIRE(picked({ actions[1] }) == u8"button");
    // 막대 위 누름은 막대의 것이다.
    REQUIRE(bar_state_of(click(controller, 60.0f, 14.0f)) == menu_bar_state { u8"edit", true, false });
    REQUIRE(bar_state_of(controller.process(luil::surface_focus_lost_event {})) == menu_bar_state {});

    // 열린 동안의 바깥 누름은 popup dismiss의 몫이다. popup 표면 안의 누름은 메뉴의 것이라 더하지 않는다.
    luil::interaction_controller open { &policy };
    open.set_tree(bar_tree(bar_config({ u8"file", true, false })));
    open.set_surface_trees({ { u8"menu", menu_tree(u8"file") } });
    REQUIRE(bar_state_of(open.process(luil::pointer_pressed_event { .x = 40.0f, .y = 110.0f })).has_value() == false);
    REQUIRE(bar_state_of(open.process(luil::pointer_pressed_event { .x = 40.0f, .y = 30.0f, .surface = u8"menu" })).has_value() == false);

    // 쉬는 막대는 밖 누름에 아무것도 내지 않는다.
    luil::interaction_controller resting { &policy };
    resting.set_tree(bar_tree(bar_config({})));
    REQUIRE(bar_state_of(click(resting, 40.0f, 110.0f)).has_value() == false);
}

TEST_CASE("A menu bar outside a focus trap takes no keys", "[ui][menu-bar]")
{
    auto root { std::make_unique<test_panel>(ui_element_id { ui_element_kind::root }) };
    root->arrange({ .slot = { 0.0f, 0.0f, 400.0f, 200.0f } });
    auto bar { std::make_unique<luil::menu_bar_element>(bar_config({})) };
    bar->arrange({ .slot = { 0.0f, 0.0f, 400.0f, 28.0f }, .measurer = &half_width });
    root->add(std::move(bar));
    auto dialog { std::make_unique<test_panel>(ui_element_id { luil::application_element_kind(1), u8"dialog" }) };
    dialog->set_focus_trap(true);
    dialog->arrange({ .slot = { 50.0f, 50.0f, 200.0f, 100.0f } });
    root->add(std::move(dialog));
    const auto tree { std::make_shared<const luil::ui_tree>(std::move(root)) };
    REQUIRE(luil::active_menu_bar(*tree) == nullptr);

    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(tree);
    REQUIRE(bar_state_of(controller.process(key(key_code::alt_tap))).has_value() == false);
    REQUIRE(bar_state_of(controller.process(key(key_code::key_f, false, false, true))).has_value() == false);
}

TEST_CASE("A keyboard opened menu bar menu highlights its first item and pointer opened menus do not", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"file", true, true })));
    controller.set_surface_trees({ { u8"menu", menu_tree(u8"file") } });
    REQUIRE(controller.snapshot().menu_highlight == item(u8"new"));
    REQUIRE(controller.snapshot().menu_surface == u8"menu");

    // ←/→로 옮긴 메뉴도 새 메뉴다. 키보드로 왔으니 첫 활성 항목이다.
    static_cast<void>(controller.process(key(key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item(u8"open"));
    controller.set_tree(bar_tree(bar_config({ u8"edit", true, true })));
    controller.set_surface_trees({ { u8"menu", menu_tree(u8"edit") } });
    REQUIRE(controller.snapshot().menu_highlight == item(u8"new"));

    // hover로 옮긴 메뉴는 포인터 메뉴라 강조 없이 선다.
    controller.set_tree(bar_tree(bar_config({ u8"help", true, false })));
    controller.set_surface_trees({ { u8"menu", menu_tree(u8"help") } });
    REQUIRE(controller.snapshot().menu_highlight == ui_element_id {});

    luil::interaction_controller pointer { &policy };
    pointer.set_tree(bar_tree(bar_config({ u8"file", true, false })));
    pointer.set_surface_trees({ { u8"menu", menu_tree(u8"file") } });
    REQUIRE(pointer.snapshot().menu_highlight == ui_element_id {});
    REQUIRE(pointer.snapshot().menu_keys);
}

TEST_CASE("Left and right at the top of an open menu bar menu switch menus after submenus", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"edit", true, true })));
    controller.set_surface_trees({ { u8"menu", menu_tree(u8"edit") } });
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_right))) == menu_bar_state { u8"view", true, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_left))) == menu_bar_state { u8"file", true, true });

    // 강조 항목에 하위 메뉴가 있으면 →는 하위 메뉴가 먼저다.
    static_cast<void>(controller.process(key(key_code::arrow_down)));
    REQUIRE(controller.snapshot().menu_highlight == item(u8"open"));
    const std::vector<luil::input_action> opened { controller.process(key(key_code::arrow_right)) };
    REQUIRE(bar_state_of(opened).has_value() == false);
    REQUIRE(opened.size() == 1u);
    REQUIRE(message_of<submenu_intent>(opened.front()) != nullptr);

    // 깊이 0의 Esc는 앱 정책의 close_menu 대신 막대로 가고 막대는 선 채 남는다.
    const std::vector<luil::input_action> escaped { controller.process(key(key_code::escape)) };
    REQUIRE(bar_state_of(escaped) == menu_bar_state { u8"edit", false, true });
    REQUIRE(picked(escaped).has_value() == false);

    // 열린 동안의 Alt+글자는 그 메뉴로, Alt 단독·F10은 막대를 쉬게 한다.
    REQUIRE(bar_state_of(controller.process(key(key_code::key_x, false, false, true))) == menu_bar_state { u8"help", true, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::alt_tap))) == menu_bar_state {});
    REQUIRE(bar_state_of(controller.process(key(key_code::f10))) == menu_bar_state {});

    // 막대가 쉬는 동안 열린 컨텍스트 메뉴의 Esc는 지금처럼 앱 정책이다.
    luil::interaction_controller context { &policy };
    context.set_tree(bar_tree(bar_config({})));
    context.set_surface_trees({ { u8"menu", menu_tree(u8"context") } });
    REQUIRE(picked(context.process(key(key_code::escape))) == u8"close_menu");
}

TEST_CASE("Access keys inside a menu bar menu run one match and cycle through several", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"file", true, true })));
    controller.set_surface_trees({ { u8"menu", menu_tree(u8"file") } });
    REQUIRE(picked(controller.process(key(key_code::key_n))) == u8"new");
    // 하위 메뉴가 있는 항목은 연다.
    const std::vector<luil::input_action> opened { controller.process(key(key_code::key_o)) };
    REQUIRE(opened.size() == 1u);
    REQUIRE(message_of<submenu_intent>(opened.front())->key == u8"open");
    // 여럿이면 강조만 다음 것으로 옮기고 끝에서 돈다.
    REQUIRE(controller.process(key(key_code::key_s)).empty());
    REQUIRE(controller.snapshot().menu_highlight == item(u8"save"));
    REQUIRE(controller.process(key(key_code::key_s)).empty());
    REQUIRE(controller.snapshot().menu_highlight == item(u8"saveas"));
    REQUIRE(controller.process(key(key_code::key_s)).empty());
    REQUIRE(controller.snapshot().menu_highlight == item(u8"save"));
    // 맞는 항목이 없는 글자는 삼킨다. 막대 메뉴 밖으로 흐르지 않는다.
    policy.keys.clear();
    REQUIRE(controller.process(key(key_code::key_q)).empty());
    REQUIRE(policy.keys.empty());

    // 막대가 쉬는 동안의 컨텍스트 메뉴에서는 접근 키가 없다. 글자는 문자 경로의 것이다.
    luil::interaction_controller context { &policy };
    context.set_tree(bar_tree(bar_config({})));
    context.set_surface_trees({ { u8"menu", menu_tree(u8"context") } });
    REQUIRE(picked(context.process(key(key_code::key_n))).has_value() == false);
}

TEST_CASE("A menu bar opened before its menu stands still owns the keys", "[ui][menu-bar]")
{
    bar_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(bar_tree(bar_config({ u8"file", true, true })));
    REQUIRE(bar_state_of(controller.process(key(key_code::arrow_right))) == menu_bar_state { u8"edit", true, true });
    REQUIRE(bar_state_of(controller.process(key(key_code::escape))) == menu_bar_state { u8"file", false, true });
    REQUIRE(controller.process(key(key_code::key_q)).empty());
    REQUIRE(policy.keys.empty());
}

TEST_CASE("Menu bar accessibility reports the bar, expanded items and their access keys", "[ui][menu-bar]")
{
    const auto tree { bar_tree(bar_config({ u8"file", true, false })) };
    const luil::menu_bar_element& bar { bar_in(*tree) };
    REQUIRE(bar.accessibility().role == luil::access_role::menu_bar);
    REQUIRE(bar.accessibility().name == u8"메뉴 막대");
    const auto* const file { tree->find({ ui_element_kind::menu_bar_item, u8"file" }) };
    const auto* const help { tree->find({ ui_element_kind::menu_bar_item, u8"help" }) };
    REQUIRE(file->accessibility().role == luil::access_role::menu_item);
    REQUIRE(file->accessibility().expanded == true);
    REQUIRE(file->accessibility().access_key == u8"Alt+F");
    // 덧붙여 그린 "(X)"는 이름에 없다.
    REQUIRE(help->accessibility().name == u8"Help");
    REQUIRE(help->accessibility().expanded == false);
    REQUIRE(help->accessibility().access_key == u8"Alt+X");
    REQUIRE(file->tab_stop() == false);
    REQUIRE(file->preserves_pointer_focus());

    // 펼치기·접기는 목표 상태의 절대 메시지다.
    const auto expand { luil::plan_access_request(*help, { luil::access_command::expand }) };
    REQUIRE(expand.has_value());
    REQUIRE(bar_state_of(*expand) == menu_bar_state { u8"help", true, false });
    const auto collapse { luil::plan_access_request(*file, { luil::access_command::collapse }) };
    REQUIRE(collapse.has_value());
    REQUIRE(bar_state_of(*collapse) == menu_bar_state {});

    // 앱이 준 이름이 기본 문구를 대신한다.
    luil::menu_bar_config named { bar_config({}) };
    named.access_name = u8"주 메뉴";
    REQUIRE(luil::menu_bar_element { std::move(named) }.accessibility().name == u8"주 메뉴");

    // 메뉴 안 항목의 접근 키는 수식 키 없는 글자다.
    const auto menu { menu_tree(u8"file") };
    REQUIRE(menu->find(item(u8"new"))->accessibility().access_key == u8"N");
    REQUIRE(menu->find(item(u8"off"))->accessibility().access_key.empty());
}

TEST_CASE("Alt shortcuts that collide with menu bar access keys are reported", "[ui][menu-bar]")
{
    std::vector<luil::app_command> commands {
        { .id = u8"find", .label = u8"Find", .shortcut = { key_code::key_f, false, false, true } },
        { .id = u8"control", .label = u8"Control", .shortcut = { key_code::key_f, true, false, false } },
        { .id = u8"shifted", .label = u8"Shifted", .shortcut = { key_code::key_e, false, true, true } },
        { .id = u8"free", .label = u8"Free", .shortcut = { key_code::key_z, false, false, true } },
        { .id = u8"extra", .label = u8"Extra", .shortcut = { key_code::key_z, true }, .extra_shortcuts = { { key_code::key_x, false, false, true } } },
        { .id = u8"view", .label = u8"View", .shortcut = { key_code::key_v, false, false, true } },
    };
    // 꺼진 View 항목의 접근 키는 막대가 받지 않으므로 겹침이 아니다.
    REQUIRE(luil::menu_bar_access_conflicts(bar_config({}, false), commands) == std::vector<std::u8string> { u8"find", u8"extra" });
    REQUIRE(luil::menu_bar_access_conflicts(bar_config({}), commands) == std::vector<std::u8string> { u8"find", u8"extra", u8"view" });
}

TEST_CASE("A menu bar popup sits under its item and maps Escape to keeping the bar", "[ui][menu-bar]")
{
    const auto tree { bar_tree(bar_config({ u8"edit", true, true })) };
    const luil::menu_bar_element& bar { bar_in(*tree) };
    luil::menu_config menu {};
    menu.items = { { .key = u8"undo", .label = u8"Undo" } };
    menu.select = [](const std::u8string& key) { return luil::make_app_action(pick_intent { key }); };
    luil::menu_popup_config popup {};
    popup.id = u8"menu";
    popup.width = 120.0f;
    const luil::ui_popup made { luil::make_menu_bar_popup(bar, std::move(menu), std::move(popup), { 0.0f, 0.0f, 400.0f, 300.0f }) };
    // 항목(44, 0, 44, 28) 바로 아래다.
    REQUIRE(made.x == 44.0f);
    REQUIRE(made.y == 28.0f);
    const auto* const root { dynamic_cast<const luil::menu_element*>(made.tree->root()) };
    REQUIRE(root != nullptr);
    REQUIRE(root->config().owner == u8"edit");
    REQUIRE(bar_state_of({ made.dismiss(luil::popup_dismiss_reason::escape_key) }) == menu_bar_state { u8"edit", false, true });
    REQUIRE(bar_state_of({ made.dismiss(luil::popup_dismiss_reason::pointer_press_outside) }) == menu_bar_state {});
    REQUIRE(bar_state_of({ made.dismiss(luil::popup_dismiss_reason::activation_changed) }) == menu_bar_state {});
    // 고르면 메뉴의 메시지 뒤에 막대가 쉰다.
    const auto* const undo { made.tree->find(item(u8"undo")) };
    const std::vector<luil::input_action> chosen { (*undo->action(luil::ui_trigger::left_click))({ undo->id() }) };
    REQUIRE(chosen.size() == 2u);
    REQUIRE(picked(chosen) == u8"undo");
    REQUIRE(bar_state_of({ chosen[1] }) == menu_bar_state {});
}
