#include "luil/ui/grouped_list_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/scroll_view_element.h"
#include "luil/ui/sidebar_element.h"
#include "luil/ui/stack_element.h"
#include "luil/ui/ui_element.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    struct activate_intent
    {
        std::u8string key {};
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

    // 그룹 두 개: recent(내용 200) + old(내용 300)다.
    [[nodiscard]] std::vector<luil::list_group> make_groups()
    {
        std::vector<luil::list_group> groups {};
        luil::list_group recent {};
        recent.key = u8"recent";
        recent.title = u8"최근";
        recent.content_height = 200.0f;
        recent.content = std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(70), u8"recent" });
        groups.push_back(std::move(recent));

        luil::list_group old {};
        old.key = u8"old";
        old.title = u8"지난";
        old.content_height = 300.0f;
        old.content = std::make_unique<content_probe>(luil::ui_element_id { luil::application_element_kind(70), u8"old" });
        groups.push_back(std::move(old));
        return groups;
    }

    [[nodiscard]] const luil::ui_element* header_of(const luil::grouped_list_element& list, const std::u8string& key)
    {
        for (const std::unique_ptr<luil::ui_element>& child : list.children())
            if (child->id() == luil::ui_element_id { luil::ui_element_kind::list_header, key })
                return child.get();
        return nullptr;
    }
} // namespace

TEST_CASE("A grouped list stacks headers and contents and sums its height", "[ui][grouped-list]")
{
    luil::grouped_list_element list { luil::grouped_list_config {}, make_groups() };
    REQUIRE(list.content_height() == 2.0f * luil::list_header_height + 500.0f);

    // 흘리지 않았을 때는 모두 제자리다.
    list.arrange({ { 0.0f, 0.0f, 300.0f, list.content_height() }, 1.0f });
    const luil::ui_element* const recent { header_of(list, u8"recent") };
    const luil::ui_element* const old { header_of(list, u8"old") };
    REQUIRE(recent->bounds().y == 0.0f);
    REQUIRE(list.children()[0]->bounds().y == luil::list_header_height);
    REQUIRE(old->bounds().y == luil::list_header_height + 200.0f);
}

TEST_CASE("A grouped list pins the passing header to the viewport top", "[ui][grouped-list]")
{
    // scroll view가 하듯 흘러간 만큼 올린 slot과 스크롤 값을 준다.
    // 창은 y=0에서 시작하고 100만큼 흘렀다.
    luil::grouped_list_element list { luil::grouped_list_config {}, make_groups() };
    list.arrange({ { 0.0f, -100.0f, 300.0f, list.content_height() }, 1.0f, 100.0f });

    // 첫 그룹(0~224)을 지나는 중이라 그 머리행이 창 위(y=0)에 붙는다.
    const luil::ui_element* const recent { header_of(list, u8"recent") };
    REQUIRE(recent->bounds().y == 0.0f);
    // 행들은 그대로 흘러간다.
    REQUIRE(list.children()[0]->bounds().y == luil::list_header_height - 100.0f);
    // 다음 그룹 머리행은 아직 제자리다.
    const luil::ui_element* const old { header_of(list, u8"old") };
    REQUIRE(old->bounds().y == luil::list_header_height + 200.0f - 100.0f);

    // 배율이 있어도 논리 값 계산은 같다.
    luil::grouped_list_element scaled { luil::grouped_list_config {}, make_groups() };
    scaled.arrange({ { 0.0f, -200.0f, 600.0f, scaled.content_height() * 2.0f }, 2.0f, 100.0f });
    REQUIRE(header_of(scaled, u8"recent")->bounds().y == 0.0f);
}

TEST_CASE("A grouped list header is pushed out by the next group's arrival", "[ui][grouped-list]")
{
    // 첫 그룹 끝(224)이 창 위로 다가오도록 210만큼 흘린다.
    luil::grouped_list_element list { luil::grouped_list_config {}, make_groups() };
    list.arrange({ { 0.0f, -210.0f, 300.0f, list.content_height() }, 1.0f, 210.0f });

    // 머리행 바닥이 그룹 끝(14)에 걸려 위로 밀려난다.
    const luil::ui_element* const recent { header_of(list, u8"recent") };
    REQUIRE(recent->bounds().y == 14.0f - luil::list_header_height);
    // 다음 머리행이 제자리(창 좌표 14)에서 뒤를 잇는다.
    const luil::ui_element* const old { header_of(list, u8"old") };
    REQUIRE(old->bounds().y == 14.0f);
}

TEST_CASE("A sticky header wins the hit test over the row scrolled beneath it", "[ui][grouped-list][hit]")
{
    // 이 element의 존재 이유가 겹침이다: 고정된 머리행이 지나가는 행 위에 뜬다.
    // 그 겹침에서 hit도 머리행이 이겨야 한다 (그리기와 같은 우선순위).
    luil::grouped_list_config config {};
    config.activate = [](const std::u8string& key) { return luil::input_action { luil::app_message { activate_intent { key } } }; };
    std::vector<luil::list_group> groups { make_groups() };
    // 내용을 상호작용 대상으로 만들어 겹침에서 지는 쪽을 관찰한다.
    for (luil::list_group& group : groups)
        group.content->set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
    luil::grouped_list_element list { std::move(config), std::move(groups) };

    // 100만큼 흘러 recent 머리행이 창 위(y 0..24)에 붙고,
    // recent 내용(-76..124)이 그 아래를 지나간다.
    list.arrange({ { 0.0f, -100.0f, 300.0f, list.content_height() }, 1.0f, 100.0f });

    // 겹치는 자리(10, 10): 머리행이 임자다.
    const luil::ui_element* const on_header { list.hit_test(10.0f, 10.0f) };
    REQUIRE(on_header != nullptr);
    REQUIRE(on_header->id() == luil::ui_element_id { luil::ui_element_kind::list_header, u8"recent" });

    // 머리행 아래(10, 50)는 지나가는 내용이 받는다.
    const luil::ui_element* const on_content { list.hit_test(10.0f, 50.0f) };
    REQUIRE(on_content != nullptr);
    REQUIRE(on_content->id().owner == u8"recent");
    REQUIRE(on_content->id().kind == luil::application_element_kind(70));
}

TEST_CASE("A grouped list works as scroll view content", "[ui][grouped-list]")
{
    auto list { std::make_unique<luil::grouped_list_element>(luil::grouped_list_config {}, make_groups()) };
    const luil::grouped_list_element* const probe { list.get() };
    luil::scroll_view_config config {};
    config.content_height = probe->content_height();
    config.scroll_offset = 100.0f;
    luil::scroll_view_element view { luil::ui_element_id { luil::application_element_kind(71) }, config };
    view.set_content(std::move(list));
    view.arrange({ { 0.0f, 50.0f, 300.0f, 200.0f }, 1.0f });

    // 창 윗변(y=50)에 첫 머리행이 붙는다.
    REQUIRE(header_of(*probe, u8"recent")->bounds().y == 50.0f);
}

TEST_CASE("A grouped list header activates only when the config gives a factory", "[ui][grouped-list]")
{
    const luil::grouped_list_element inert { luil::grouped_list_config {}, make_groups() };
    REQUIRE(header_of(inert, u8"recent")->action(luil::ui_trigger::left_click) == nullptr);

    luil::grouped_list_config config {};
    config.activate = [](const std::u8string& key) { return luil::input_action { luil::app_message { activate_intent { key } } }; };
    const luil::grouped_list_element list { std::move(config), make_groups() };
    const luil::ui_element* const header { header_of(list, u8"old") };
    REQUIRE(header->cursor() == luil::ui_cursor::hand);
    auto actions { (*header->action(luil::ui_trigger::left_click))({ header->id(), 0.0f, 0.0f }) };
    REQUIRE(actions.size() == 1);
    auto* const message { std::get_if<luil::app_message>(&actions[0]) };
    REQUIRE(message != nullptr);
    REQUIRE(message->get<activate_intent>()->key == u8"old");
}

TEST_CASE("Sticky headers survive several layers between the scroll view and the list", "[ui][grouped-list][arrange]")
{
    // scroll view → stack → sidebar → grouped list로 겹쳐 놓는다.
    // 사이의 어느 한 계층이라도 문맥의 scroll_offset을 빠뜨리면 머리행이
    // 창 위에 붙지 못하고 그냥 흘러간다 — 원인은 몇 계층 위의 인자 하나다
    // (tree-arrange-design.md).
    auto list { std::make_unique<luil::grouped_list_element>(luil::grouped_list_config {}, make_groups()) };
    const luil::grouped_list_element* const list_view { list.get() };
    const float content_height { list_view->content_height() };

    luil::sidebar_config sidebar {};
    sidebar.expanded_width = 300.0f;
    auto panel { std::make_unique<luil::sidebar_element>(sidebar, std::move(list)) };

    luil::stack_config column {};
    auto stack { std::make_unique<luil::stack_element>(luil::ui_element_id { luil::application_element_kind(71) }, column) };
    stack->add(std::move(panel), content_height);

    luil::scroll_view_config view {};
    view.content_height = content_height;
    view.scroll_offset = 100.0f;
    luil::scroll_view_element scroller { luil::ui_element_id { luil::application_element_kind(72) }, view };
    scroller.set_content(std::move(stack));
    scroller.arrange({ { 0.0f, 0.0f, 300.0f, 200.0f }, 1.0f });

    // 100만큼 흘렀으므로 첫 그룹의 머리행이 창 위(y=0)에 붙어 있어야 한다.
    const luil::ui_element* const recent { header_of(*list_view, u8"recent") };
    REQUIRE(recent != nullptr);
    REQUIRE(recent->bounds().y == 0.0f);
    // 행들은 그대로 흘러간다 — 붙는 것은 머리행뿐이다.
    REQUIRE(list_view->children()[0]->bounds().y == luil::list_header_height - 100.0f);
}
