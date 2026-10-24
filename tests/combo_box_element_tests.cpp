#include "luil/ui/combo_box_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace {
    // 콤보와 그 뒤의 자리를 담는 뿌리이자, 누를 수 있는 자리(Tab의 자리)다. 배치는 시험이 직접 한다.
    class focus_box final : public luil::ui_element
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
        {}
    };

    [[nodiscard]] std::unique_ptr<focus_box> make_stop(std::u8string owner, const float y)
    {
        auto stop { std::make_unique<focus_box>(luil::ui_element_id { luil::application_element_kind(1), std::move(owner) }) };
        stop->arrange({ { 0.0f, y, 200.0f, 24.0f }, 1.0f });
        stop->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) { return std::vector<luil::input_action> {}; });
        return stop;
    }

    // 열림을 닫는 메시지(`set_open(false)`)의 수다.
    [[nodiscard]] int close_count(const std::vector<luil::input_action>& actions)
    {
        int count { 0 };
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr && message->get<bool>() != nullptr && *message->get<bool>() == false)
                ++count;
        return count;
    }

    // 콤보 하나와, 있으면 그 아래 자리 하나를 담은 tree다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> combo_tree(const bool open, const bool with_next)
    {
        luil::combo_box_config config {};
        config.open = open;
        config.set_open = [](const bool value) { return luil::input_action { luil::app_message { value } }; };
        auto root { std::make_unique<focus_box>(luil::ui_element_id { luil::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
        auto combo { std::make_unique<luil::combo_box_element>(luil::ui_element_id { luil::application_element_kind(0), u8"repo" }, luil::text_input_view {}, config) };
        combo->arrange({ { 0.0f, 0.0f, 200.0f, 24.0f }, 1.0f });
        root->add(std::move(combo));
        if (with_next)
            root->add(make_stop(u8"next", 40.0f));
        return std::make_shared<const luil::ui_tree>(std::move(root));
    }
} // namespace

TEST_CASE("Focus leaving an open combo closes its suggestions once", "[ui][combo][focus]")
{
    luil::interaction_controller controller {};
    controller.set_tree(combo_tree(true, true));
    // 첫 Tab은 콤보의 입력 칸에 선다. 들어오는 것은 떠남이 아니다.
    REQUIRE(close_count(controller.process(luil::key_pressed_event { luil::key_code::tab })) == 0);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::application_element_kind(0), u8"repo:input" });
    // 입력 칸에서 다음 자리로 나가면 콤보 root의 훅이 한 번 닫는다.
    REQUIRE(close_count(controller.process(luil::key_pressed_event { luil::key_code::tab })) == 1);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::application_element_kind(1), u8"next" });

    // 창의 비활성화가 거둔 초점은 떠남이 아니다 — Alt+Tab으로 후보가 닫히지 않는다.
    controller.set_tree(combo_tree(true, true));
    static_cast<void>(controller.process(luil::access_focus_event { luil::ui_element_id { luil::application_element_kind(0), u8"repo:input" } }));
    REQUIRE(close_count(controller.process(luil::surface_focus_lost_event {})) == 0);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id {});
}

TEST_CASE("Focus that stays in the combo or a closed combo closes nothing", "[ui][combo][focus]")
{
    // 자리가 입력 칸 하나뿐이면 Tab은 제자리로 돈다. 가지 안에 남은 초점은 떠난 것이 아니다.
    luil::interaction_controller alone {};
    alone.set_tree(combo_tree(true, false));
    REQUIRE(close_count(alone.process(luil::key_pressed_event { luil::key_code::tab })) == 0);
    REQUIRE(close_count(alone.process(luil::key_pressed_event { luil::key_code::tab })) == 0);
    REQUIRE(alone.snapshot().focused == luil::ui_element_id { luil::application_element_kind(0), u8"repo:input" });

    // 닫힌 콤보는 훅을 달지 않는다.
    luil::interaction_controller closed {};
    closed.set_tree(combo_tree(false, true));
    REQUIRE(close_count(closed.process(luil::key_pressed_event { luil::key_code::tab })) == 0);
    REQUIRE(close_count(closed.process(luil::key_pressed_event { luil::key_code::tab })) == 0);
    REQUIRE(closed.snapshot().focused == luil::ui_element_id { luil::application_element_kind(1), u8"next" });
}

TEST_CASE("Combo navigation and Escape use the candidate and editing state", "[ui][combo]")
{
    luil::combo_box_config config {};
    config.open = true;
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"a", u8"Alpha" }, { u8"b", u8"Beta" } });
    config.move = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    config.set_open = [](const bool open) { return luil::input_action { luil::app_message { open } }; };
    luil::combo_box_element combo { { luil::application_element_kind(0), u8"repo" }, {}, config };
    // Home·End는 글 칸의 caret 몫이라 콤보가 받지 않는다. 처음·끝은 Page로 간다.
    REQUIRE(combo.on_key({ luil::key_code::home }).has_value() == false);
    REQUIRE(combo.on_key({ luil::key_code::end }).has_value() == false);
    const auto first { combo.on_key({ luil::key_code::page_up }) };
    const auto last { combo.on_key({ luil::key_code::page_down }) };
    REQUIRE(first->size() == 1u);
    REQUIRE(last->size() == 1u);
    REQUIRE(*std::get<luil::app_message>(first->front()).get<std::u8string>() == u8"a");
    REQUIRE(*std::get<luil::app_message>(last->front()).get<std::u8string>() == u8"b");
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"a", u8"Alpha" }, { u8"b", u8"Beta" }, { u8"c", u8"Disabled", false } });
    luil::combo_box_element paged { { luil::application_element_kind(0), u8"paged" }, {}, config };
    const auto page { paged.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(page->front()).get<std::u8string>() == u8"b");
    const auto close { combo.on_key({ luil::key_code::escape }) };
    REQUIRE(*std::get<luil::app_message>(close->front()).get<bool>() == false);
    luil::text_input_view composing {};
    composing.composing = true;
    composing.composition_text = u8"베";
    std::u8string query {};
    config.request_suggestions = [&query](std::uint64_t, const std::u8string& text) { query = text; return luil::input_action {}; };
    luil::combo_box_element ime { { luil::application_element_kind(0), u8"ime" }, composing, config };
    REQUIRE(ime.on_key({ luil::key_code::escape }).has_value() == false);
    static_cast<void>(ime.suggestion_request(1));
    REQUIRE(query == u8"베");
}

TEST_CASE("Combo trusts the answered query and filters only stale candidates", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.open = true;
    config.suggestions.query = u8"ab";
    config.suggestions.items = std::make_shared<const std::vector<luil::combo_suggestion>>(
        std::vector<luil::combo_suggestion> { { u8"ab", u8"ab" }, { u8"about", u8"About" } });
    config.move = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    luil::text_input_view value {};
    value.text = u8"ab";
    // 원천이 "ab"에 "About"을 돌려줬으면 대소문자가 달라도 그대로 보인다.
    luil::combo_box_element answered { { luil::application_element_kind(0), u8"answered" }, value, config };
    const auto last { answered.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(last->front()).get<std::u8string>() == u8"about");
    // 받는 중이면 후보가 앞 질의의 것이라 칸의 글로 다시 거른다.
    config.suggestions.loading = true;
    luil::combo_box_element stale { { luil::application_element_kind(0), u8"stale" }, value, config };
    const auto filtered { stale.on_key({ luil::key_code::page_down }) };
    REQUIRE(*std::get<luil::app_message>(filtered->front()).get<std::u8string>() == u8"ab");
}

TEST_CASE("Combo requests suggestions with the query stored for the token", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.suggestions.token = 5;
    config.suggestions.query = u8"ab";
    std::u8string query {};
    config.request_suggestions = [&query](std::uint64_t, const std::u8string& text) { query = text; return luil::input_action {}; };
    luil::text_input_view value {};
    value.text = u8"abc";
    luil::combo_box_element combo { { luil::application_element_kind(0), u8"request" }, value, config };
    static_cast<void>(combo.suggestion_request(5));
    REQUIRE(query == u8"ab");
    static_cast<void>(combo.suggestion_request(6));
    REQUIRE(query == u8"abc");
}

TEST_CASE("Combo keeps previous candidates for Enter while suggestions load", "[ui][combo][async]")
{
    luil::combo_box_config config {};
    config.allow_custom = false;
    const auto first { luil::begin_combo_query(config.suggestions, u8"a") };
    REQUIRE(luil::accept_combo_suggestions(config.suggestions, first,
        std::make_shared<const std::vector<luil::combo_suggestion>>(std::vector<luil::combo_suggestion> { { u8"ab", u8"ab" } })));
    static_cast<void>(luil::begin_combo_query(config.suggestions, u8"ab"));
    config.select = [](const std::u8string& key) { return luil::input_action { luil::app_message { key } }; };
    bool invalid { false };
    config.invalid = [&invalid](const std::u8string&) { invalid = true; return luil::input_action {}; };
    luil::text_input_view value {};
    value.text = u8"ab";
    luil::combo_box_element exact { { luil::application_element_kind(0), u8"exact" }, value, config };
    const auto chosen { exact.on_key({ luil::key_code::enter }) };
    REQUIRE(invalid == false);
    REQUIRE(chosen->size() == 1u);
    REQUIRE(*std::get<luil::app_message>(chosen->front()).get<std::u8string>() == u8"ab");
    // 앞 후보에 없는 글도 받는 중에는 틀렸다고 보내지 않는다.
    value.text = u8"zz";
    luil::combo_box_element pending { { luil::application_element_kind(0), u8"pending" }, value, config };
    const auto swallowed { pending.on_key({ luil::key_code::enter }) };
    REQUIRE(swallowed.has_value());
    REQUIRE(swallowed->empty());
    REQUIRE(invalid == false);
}
