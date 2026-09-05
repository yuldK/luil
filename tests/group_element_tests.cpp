#include "luil/ui/group_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct toggle_intent
    {
        bool open { false };
    };

    class content_probe final : public luil::ui_element
    {
    public:
        using ui_element::ui_element;

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::unique_ptr<content_probe> make_content()
    {
        return std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(60) });
    }
} // namespace

TEST_CASE("A group's height follows its collapse state", "[ui][group]")
{
    luil::group_config config {};
    config.content_height = 120.0f;
    REQUIRE(luil::group_element::height_for(config) == luil::group_header_height + 120.0f);

    config.collapsed = true;
    REQUIRE(luil::group_element::height_for(config) == luil::group_header_height);

    // slot 높이가 아니라 자기 상태의 높이를 갖는다.
    luil::group_element group { config };
    group.arrange({ { 0.0f, 10.0f, 400.0f, 999.0f }, 2.0f });
    REQUIRE(group.bounds().height == luil::group_header_height * 2.0f);
}

TEST_CASE("A collapsed group leaves its content out of the tree", "[ui][group]")
{
    luil::group_config collapsed {};
    collapsed.collapsed = true;
    collapsed.content_height = 100.0f;
    const luil::group_element hidden { collapsed, make_content() };
    // 제목 줄만 있다. 보이지 않는 내용은 눌리지도 않아야 한다.
    REQUIRE(hidden.children().size() == 1);
    REQUIRE(hidden.children()[0]->id().kind == luil::ui_element_kind::group_header);

    luil::group_config expanded {};
    expanded.content_height = 100.0f;
    auto content { make_content() };
    const content_probe* const probe { content.get() };
    luil::group_element open { expanded, std::move(content) };
    open.arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f });
    REQUIRE(open.children().size() == 2);
    // 내용은 제목 줄 바로 아래에 놓인다.
    REQUIRE(probe->bounds().y == luil::group_header_height);
    REQUIRE(probe->bounds().height == 100.0f);
}

TEST_CASE("A group header toggles only when the config gives an action", "[ui][group]")
{
    const luil::group_element inert { luil::group_config {} };
    const luil::ui_element* const silent_header { inert.children()[0].get() };
    REQUIRE(silent_header->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(silent_header->interactive() == false);

    luil::group_config config {};
    config.owner = u8"advanced";
    config.collapsed = true;
    config.toggle = [](const luil::ui_action_context&) -> std::vector<luil::input_action> {
        return { luil::input_action { luil::app_message { toggle_intent { true } } } };
    };
    luil::group_element group { config };
    group.arrange({ { 0.0f, 0.0f, 400.0f, 200.0f }, 1.0f });

    const luil::ui_element* const header { group.children()[0].get() };
    REQUIRE(header->id() == luil::ui_element_id { luil::ui_element_kind::group_header, u8"advanced" });
    REQUIRE(header->cursor() == luil::ui_cursor::hand);

    // 제목 줄 어디를 눌러도 토글 메시지가 난다.
    const luil::ui_element* const hit { group.hit_test(200.0f, 14.0f) };
    REQUIRE(hit == header);
    auto actions { (*header->action(luil::ui_trigger::left_click))({ header->id(), 200.0f, 14.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<toggle_intent>()->open);
}
