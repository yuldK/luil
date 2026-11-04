#include "demo/basics_page.h"

#include "luil/ui/message_box.h"
#include "luil/ui/root_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using time_point = std::chrono::steady_clock::time_point;

    template<typename Message>
    void deliver(demo::basics_page& page, Message message)
    {
        REQUIRE(page.handle(luil::app_message { std::move(message) }));
    }

    // 입력 경로가 돌려준 메시지를 페이지에 그대로 전한다. 앱 메시지가 아닌 것은 건너뛴다.
    void deliver_all(demo::basics_page& page, const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                REQUIRE(page.handle(*message));
    }

    // 900 x 900 논리 픽셀에 배치한 페이지 내용이다. 그림 파일이 없는 환경이라 그림 칸은 오류 글로 선다.
    [[nodiscard]] luil::ui_tree page_tree(demo::basics_page& page)
    {
        return luil::make_arranged_tree(page.build(900.0f, 900.0f, 1.0f), { 0.0f, 0.0f, 900.0f, 900.0f }, 1.0f);
    }

    [[nodiscard]] std::u8string label_of(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner)
    {
        const luil::ui_element* const label { tree.find({ kind, std::u8string { owner } }) };
        REQUIRE(label != nullptr);
        return label->accessibility().name;
    }

    // 화면의 글이 곧 보조 기술이 읽는 이름이다.
    [[nodiscard]] std::u8string text_of(demo::basics_page& page, const std::u8string_view owner)
    {
        return label_of(page_tree(page), demo::kind_text, owner);
    }

    [[nodiscard]] std::u8string counter_of(demo::basics_page& page)
    {
        return label_of(page_tree(page), demo::kind_counter_label, {});
    }

    // 창 전체를 덮는 대화 상자의 tree다. 열려 있을 때만 의미가 있다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> overlay_tree(demo::basics_page& page)
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, 900.0f, 700.0f, 1.0f);
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 900.0f, 700.0f }, 1.0f));
    }

    [[nodiscard]] luil::ui_element_id button(const std::u8string_view owner, const std::u8string_view key)
    {
        return luil::message_box_button_id(owner, key);
    }

    // 자리 한가운데를 눌렀다 뗀 입력이 내는 메시지다. 실제 입력 경로를 그대로 탄다.
    [[nodiscard]] std::vector<luil::input_action> click(luil::interaction_controller& controller, const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::rect_f box { element->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        static_cast<void>(controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, time_point {} }));
        return controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } });
    }

    [[nodiscard]] luil::key_pressed_event key(const luil::key_code code)
    {
        return luil::key_pressed_event { code, false, false, false, false, time_point {} };
    }
}

TEST_CASE("The reset box opens from the page and the page reads its state back", "[demo][basics][message-box]")
{
    demo::basics_page page {};
    REQUIRE(page.dialog_open() == false);
    REQUIRE(text_of(page, u8"dialog-state") == u8"대화: 닫힘");
    REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: 없음");

    deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
    REQUIRE(page.dialog_open());
    REQUIRE(text_of(page, u8"dialog-state") == u8"대화: 열림");

    // 열림은 앱 상태이고 같은 값이 거듭 와도 결과가 같다.
    deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
    REQUIRE(page.dialog_open());

    const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
    REQUIRE(tree->root() != nullptr);
    const luil::ui_element* const host { tree->find({ luil::ui_element_kind::modal_host, u8"reset" }) };
    REQUIRE(host != nullptr);
    // 위험한 초기화는 기본 버튼이 아니다. 뜨면 초점이 취소에 서서 Enter도 취소다.
    REQUIRE(tree->default_button() == nullptr);
    REQUIRE(host->focus_entry() == button(u8"reset", u8"cancel"));
    REQUIRE(host->focus_return() == luil::ui_element_id { demo::kind_dialog_open });
}

TEST_CASE("Every way to close the reset box sends the one result and the page closes on it", "[demo][basics][message-box]")
{
    demo::basics_page page {};

    SECTION("취소 단추는 카운터를 그대로 두고 닫는다")
    {
        deliver(page, demo::increment_intent {});
        deliver(page, demo::increment_intent {});
        deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
        const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        deliver_all(page, click(controller, *tree, button(u8"reset", u8"cancel")));
        REQUIRE(page.dialog_open() == false);
        REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: cancel");
        REQUIRE(counter_of(page) == u8"버튼을 누른 횟수: 2");
    }

    SECTION("초기화 단추는 카운터를 0으로 되돌리고 닫는다")
    {
        deliver(page, demo::increment_intent {});
        deliver(page, demo::increment_intent {});
        deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
        const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        deliver_all(page, click(controller, *tree, button(u8"reset", u8"reset")));
        REQUIRE(page.dialog_open() == false);
        REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: reset");
        REQUIRE(counter_of(page) == u8"버튼을 누른 횟수: 0");
    }

    SECTION("Esc는 취소의 결과로 닫는다")
    {
        deliver(page, demo::increment_intent {});
        deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
        luil::interaction_controller controller {};
        controller.set_tree(overlay_tree(page));
        deliver_all(page, controller.process(key(luil::key_code::escape)));
        REQUIRE(page.dialog_open() == false);
        REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: cancel");
        REQUIRE(counter_of(page) == u8"버튼을 누른 횟수: 1");
    }

    SECTION("뜨자마자 친 Enter는 초점이 선 취소를 누른다")
    {
        deliver(page, demo::increment_intent {});
        deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
        luil::interaction_controller controller {};
        controller.set_tree(overlay_tree(page));
        deliver_all(page, controller.process(key(luil::key_code::enter)));
        REQUIRE(page.dialog_open() == false);
        // 카운터가 지워지지 않는다. 기본 버튼의 채움이 초기화에 있던 옛 대화가 하던 일의 반대다.
        REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: cancel");
        REQUIRE(counter_of(page) == u8"버튼을 누른 횟수: 1");
    }

    SECTION("바깥을 누르면 같은 결과로 닫는다")
    {
        deliver(page, demo::dialog_intent { demo::dialog_kind::reset });
        const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        static_cast<void>(controller.process(luil::pointer_pressed_event { 5.0f, 5.0f, luil::pointer_button::left, time_point {} }));
        deliver_all(page, controller.process(luil::pointer_released_event { 5.0f, 5.0f, luil::pointer_button::left, time_point {} + std::chrono::milliseconds { 40 } }));
        REQUIRE(page.dialog_open() == false);
        REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: cancel");
    }
}

TEST_CASE("The long box keeps its check separate from the result and closes on its buttons", "[demo][basics][message-box]")
{
    demo::basics_page page {};
    deliver(page, demo::dialog_intent { demo::dialog_kind::details });
    REQUIRE(page.dialog_open());

    // 확인 칸은 닫지 않는다. 켜진 값은 앱 상태라 다음에 지은 설정에 실린다.
    REQUIRE(page.make_dialog_config().check.has_value());
    REQUIRE(page.make_dialog_config().check->checked == false);
    {
        const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        deliver_all(page, click(controller, *tree, luil::message_box_check_id(u8"details")));
    }
    REQUIRE(page.dialog_open());
    REQUIRE(page.make_dialog_config().check->checked);
    REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: 없음");

    // 마지막 긍정(저장)이 Enter의 대상이고 뜨면 초점이 선다.
    {
        const std::shared_ptr<const luil::ui_tree> tree { overlay_tree(page) };
        REQUIRE(tree->default_button() != nullptr);
        REQUIRE(tree->default_button()->id() == button(u8"details", u8"ok"));
        luil::interaction_controller controller {};
        controller.set_tree(tree);
        REQUIRE(controller.snapshot().focused == button(u8"details", u8"ok"));
        deliver_all(page, controller.process(key(luil::key_code::enter)));
    }
    REQUIRE(page.dialog_open() == false);
    REQUIRE(text_of(page, u8"dialog-result") == u8"마지막 결과: ok");

    // 둘째 대화의 결과는 카운터를 건드리지 않는다.
    REQUIRE(counter_of(page) == u8"버튼을 누른 횟수: 0");
}

TEST_CASE("The page builds its dialog settings from one place for the overlay and the open state", "[demo][basics][message-box]")
{
    demo::basics_page page {};
    // 닫힌 때는 빈 설정이다.
    REQUIRE(page.make_dialog_config().buttons.empty());
    {
        auto root { std::make_unique<luil::root_element>() };
        page.add_overlay(*root, 900.0f, 700.0f, 1.0f);
        REQUIRE(root->children().empty());
    }

    deliver(page, demo::dialog_intent { demo::dialog_kind::details });
    const luil::message_box_config details { page.make_dialog_config() };
    REQUIRE(details.buttons.size() == 3u);
    REQUIRE(details.severity.has_value());
    REQUIRE(details.body.empty() == false);

    deliver(page, demo::dialog_intent { demo::dialog_kind::none });
    REQUIRE(page.dialog_open() == false);
    REQUIRE(page.make_dialog_config().buttons.empty());
}

TEST_CASE("The read-only field reads the access name the page gave it", "[demo][basics][read-only]")
{
    // 보조 기술이 읽는 이름은 페이지가 단 이름이다. 칸에 안내 글이 없어도 이름이 빈 채로 읽히지 않는다.
    demo::basics_page page {};
    const luil::ui_tree tree { page_tree(page) };
    const luil::ui_element* const area { tree.find({ demo::kind_link_view_input }) };
    REQUIRE(area != nullptr);
    REQUIRE(area->accessibility().role == luil::access_role::edit);
    REQUIRE(area->accessibility().name == u8"읽기 전용 안내 칸");
}
