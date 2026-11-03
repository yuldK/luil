#include "mobile_demo/page_list.h"

#include "luil/ui/accessibility.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 400dp 폭에서 짜서 배치한 페이지 목록이다. 높이는 내용이 정한다.
    [[nodiscard]] luil::ui_tree list_tree(const std::u8string_view selected, const bool chevron)
    {
        auto content { mobile_demo::make_page_list(400.0f, selected, chevron) };
        const float height { content.height };
        return luil::make_arranged_tree(std::move(content.element), { 0.0f, 0.0f, 400.0f, height }, 1.0f);
    }

    // 보조 기술의 Invoke가 칸에서 받는 메시지가 여는 페이지다. 메시지가 없으면 빈 글이다.
    [[nodiscard]] std::u8string invoked_page(const luil::ui_element& item)
    {
        const std::optional<std::vector<luil::input_action>> planned { luil::plan_access_request(item, luil::access_request { luil::access_command::invoke }) };
        REQUIRE(planned.has_value());
        REQUIRE(planned->size() == 1u);
        const auto* const message { std::get_if<luil::app_message>(&planned->front()) };
        REQUIRE(message != nullptr);
        const auto* const navigate { message->get<mobile_demo::navigate_intent>() };
        REQUIRE(navigate != nullptr);
        return navigate->page;
    }
}

TEST_CASE("Each page entry of the mobile list is a button named after its page and opens it on Invoke", "[mobile-demo][navigation][accessibility]")
{
    // 좁은 화면(다음 장 표시가 있다)과 넓은 화면(고른 칸이 칠해진다) 둘 다 같은 칸이다.
    for (const bool wide : { false, true })
    {
        const luil::ui_tree tree { list_tree(wide ? mobile_demo::page_basics : std::u8string_view {}, wide == false) };
        REQUIRE(mobile_demo::page_entries().size() == 14u);
        for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
        {
            const luil::ui_element_id id { mobile_demo::kind_page_entry, std::u8string { entry.key } };
            const luil::ui_element* const item { tree.find(id) };
            REQUIRE(item != nullptr);
            const luil::access_info info { item->accessibility() };
            REQUIRE(info.role == luil::access_role::button);
            REQUIRE(info.name == entry.title);
            REQUIRE(luil::access_reachable(tree, id));
            REQUIRE(invoked_page(*item) == entry.key);
        }
    }
}

TEST_CASE("The page entry names of the mobile list are unique so a client can open a page by name", "[mobile-demo][navigation][accessibility]")
{
    std::set<std::u8string> names {};
    for (const mobile_demo::page_entry& entry : mobile_demo::page_entries())
        names.insert(std::u8string { entry.title });
    REQUIRE(names.size() == mobile_demo::page_entries().size());
}

TEST_CASE("The page title falls back to the app name for a key the list does not hold", "[mobile-demo][navigation]")
{
    REQUIRE(mobile_demo::page_title(mobile_demo::page_zoom) == u8"확대 보기");
    REQUIRE(mobile_demo::page_title(u8"") == u8"luil mobile demo");
}
