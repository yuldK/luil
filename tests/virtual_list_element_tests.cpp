#include "luil/ui/virtual_list_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 앱이 정의하는 kind다. 커스텀 행의 내용이 이 자리에 선다.
    constexpr luil::ui_element_kind kind_row_content { luil::application_element_kind(0) };

    struct move_intent
    {
        std::u8string key {};
    };

    struct select_intent
    {
        std::u8string key {};
    };

    struct activate_intent
    {
        std::u8string key {};
    };

    struct scroll_intent
    {
        float delta { 0.0f };
    };

    // 모델의 자리표다. 만 줄까지 가므로 글자 하나로는 모자란다.
    [[nodiscard]] std::u8string item_key(const std::size_t index)
    {
        std::u8string digits {};
        std::size_t rest { index };
        while (true)
        {
            digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + rest % 10u));
            rest /= 10u;
            if (rest == 0u)
                break;
        }
        return u8"item-" + digits;
    }

    [[nodiscard]] std::vector<luil::virtual_list_item> make_items(const std::size_t count)
    {
        std::vector<luil::virtual_list_item> items {};
        items.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            luil::virtual_list_item item {};
            item.key = item_key(index);
            item.label = item.key;
            items.push_back(std::move(item));
        }
        return items;
    }

    [[nodiscard]] luil::virtual_list_config make_config(const std::size_t count)
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.row_height = 20.0f;
        config.items = make_items(count);
        return config;
    }

    [[nodiscard]] luil::ui_element_id list_id()
    {
        return luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" };
    }

    [[nodiscard]] luil::ui_element_id row_id(const std::u8string& key)
    {
        return luil::ui_element_id { luil::ui_element_kind::virtual_list_row, key };
    }

    [[nodiscard]] luil::virtual_list_message_factory move_factory()
    {
        return [](const std::u8string& key) { return luil::input_action { luil::app_message { move_intent { key } } }; };
    }

    [[nodiscard]] luil::virtual_list_message_factory select_factory()
    {
        return [](const std::u8string& key) { return luil::input_action { luil::app_message { select_intent { key } } }; };
    }

    [[nodiscard]] luil::virtual_list_message_factory activate_factory()
    {
        return [](const std::u8string& key) { return luil::input_action { luil::app_message { activate_intent { key } } }; };
    }

    // 한글 이름의 모델이다.
    // 한 글자가 **세 byte**라, 질의의 byte 길이로 "첫 글자인가"를 되짚으면 첫
    // 글자부터 이어 친 글자로 세어진다 — 그 되짚기를 잡는 것이 이 모델의 몫이다.
    [[nodiscard]] std::vector<luil::virtual_list_item> korean_items()
    {
        std::vector<luil::virtual_list_item> items { make_items(4) };
        items[0].label = u8"가나다";
        items[1].label = u8"가나라";
        items[2].label = u8"나비";
        items[3].label = u8"가지";
        return items;
    }

    [[nodiscard]] std::function<luil::input_action(float)> scroll_factory()
    {
        return [](const float delta) { return luil::input_action { luil::app_message { scroll_intent { delta } } }; };
    }

    // 행은 목록의 직접 자식이 아니라 **영역 안 창 안의 레인**에 담긴다.
    // 조립이 바뀌어도 test가 짚는 자리는 이 두 도우미 안에서만 바뀐다
    // (`list_element_tests.cpp`가 같은 이유로 같은 짝을 둔다).
    [[nodiscard]] const luil::ui_element& virtual_lane(const luil::virtual_list_element& list)
    {
        return *list.children()[0]->children()[0]->children()[0];
    }

    [[nodiscard]] const luil::ui_element& row_at(const luil::virtual_list_element& list, const std::size_t index)
    {
        return *virtual_lane(list).children()[index];
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    // 값을 꺼내는 두 도우미다. 종류가 어긋나도 test가 죽지 않고 지도록 빈 값으로 답한다.
    [[nodiscard]] std::u8string moved_key(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        const move_intent* const message { message_at<move_intent>(actions, index) };
        return message != nullptr ? message->key : std::u8string {};
    }

    [[nodiscard]] float scrolled_delta(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        const scroll_intent* const message { message_at<scroll_intent>(actions, index) };
        return message != nullptr ? message->delta : 0.0f;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 앱이 짓는 행 내용이다. 라이브러리가 넘겨준 값을 그대로 들고 있어 test가
    // "무엇을 받았는가"를 확인한다.
    class row_probe final : public luil::ui_element
    {
    public:
        row_probe(luil::ui_element_id id, const std::size_t index, const bool selected)
            : ui_element { std::move(id) }
            , index_ { index }
            , selected_ { selected }
        {
        }

        [[nodiscard]] std::size_t index() const noexcept
        {
            return index_;
        }

        [[nodiscard]] bool selected() const noexcept
        {
            return selected_;
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

    private:
        std::size_t index_ { 0 };
        bool selected_ { false };
    };

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::virtual_list_config config, const float height)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, height }, 1.0f));
    }

    // 목록에 Tab으로 초점을 준 뒤 키 하나를 누른다.
    //  - 키마다 tree를 새로 짓는 것이 실물의 걸음이다. 커서도 스크롤도 앱
    //    상태라, 메시지를 받은 앱이 다음 frame에 새 값으로 목록을 다시 짓는다.
    [[nodiscard]] std::vector<luil::input_action> press_key_on(luil::virtual_list_config config, const luil::key_code key)
    {
        luil::interaction_controller controller {};
        controller.set_tree(published(std::move(config), 100.0f));
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
        REQUIRE(controller.snapshot().focused == list_id());

        std::vector<luil::input_action> actions { controller.process(luil::key_pressed_event { key, false, false, false, false, at(10) }) };
        // 초점은 목록에 그대로 선다 — 옮겨 가는 것은 앱 상태인 커서뿐이다.
        REQUIRE(controller.snapshot().focused == list_id());
        return actions;
    }

    // 커서와 스크롤 자리를 정한 목록에서 키 하나를 누른다.
    [[nodiscard]] std::vector<luil::input_action> press_on_list(const std::u8string& cursor, const float offset, const luil::key_code key)
    {
        luil::virtual_list_config config { make_config(100) };
        config.cursor = cursor;
        config.scroll_offset = offset;
        config.move = move_factory();
        config.scroll = scroll_factory();
        return press_key_on(std::move(config), key);
    }
} // namespace

TEST_CASE("Virtual list geometry answers the content height and each row span without a tree", "[ui][list]")
{
    SECTION("높이가 균일하면 자리는 곱셈 하나다")
    {
        const std::vector<luil::virtual_list_item> items { make_items(4) };
        REQUIRE(luil::virtual_list_content_height(items, 20.0f) == 80.0f);
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 0) == luil::virtual_list_span { 0.0f, 20.0f });
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 2) == luil::virtual_list_span { 40.0f, 20.0f });
    }

    SECTION("섞인 높이는 앞에서부터 더한 합이 자리를 정한다")
    {
        // 0은 "없음"이라 `row_height`로 물러선다 (`stack_item`과 같은 규칙).
        std::vector<luil::virtual_list_item> items { make_items(4) };
        items[1].height = 10.0f;
        items[2].height = 50.0f;
        REQUIRE(luil::virtual_list_content_height(items, 20.0f) == 100.0f);
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 1) == luil::virtual_list_span { 20.0f, 10.0f });
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 2) == luil::virtual_list_span { 30.0f, 50.0f });
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 3) == luil::virtual_list_span { 80.0f, 20.0f });
    }

    SECTION("범위 밖 색인은 빈 자리다")
    {
        // 부르는 쪽이 "그 행이 있는가"를 따로 세지 않게 하는 답이다.
        const std::vector<luil::virtual_list_item> items { make_items(3) };
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 3) == luil::virtual_list_span {});
        REQUIRE(luil::virtual_list_row_span(items, 20.0f, 900) == luil::virtual_list_span {});
        REQUIRE(luil::virtual_list_content_height({}, 20.0f) == 0.0f);
    }
}

TEST_CASE("The visible range is half open and only the overscan widens it", "[ui][list]")
{
    const std::vector<luil::virtual_list_item> items { make_items(5) };

    SECTION("창이 행 높이의 배수여도 한 줄을 더 짓지 않는다")
    {
        // 경계에 정확히 닿은 행은 **다음** 행의 것이다. 이 규칙이 아니면 배수인
        // 창마다 언제나 한 줄이 덤으로 선다.
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 0.0f, 40.0f, 0) == luil::virtual_list_range { 0u, 2u });
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 40.0f, 40.0f, 0) == luil::virtual_list_range { 2u, 4u });
    }

    SECTION("경계에 걸친 스크롤은 걸치는 행을 전부 담는다")
    {
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 10.0f, 40.0f, 0) == luil::virtual_list_range { 0u, 3u });
    }

    SECTION("overscan은 양끝에서 모델에 붙어 멈춘다")
    {
        // 아래쪽이 모자란 자리 — 끝을 넘겨 짓지 않는다.
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 40.0f, 40.0f, 1) == luil::virtual_list_range { 1u, 5u });
        // 위쪽이 모자란 자리 — 0 밑으로 내려가지 않는다.
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 0.0f, 40.0f, 3) == luil::virtual_list_range { 0u, 5u });
        // 음수는 0으로 본다 (overscan을 끄는 값은 0 하나다).
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 0.0f, 40.0f, -4) == luil::virtual_list_range { 0u, 2u });
    }

    SECTION("높이가 0인 행은 어떤 창에도 걸치지 않는다")
    {
        // `height`가 0이면 `row_height`로 물러서므로, 진짜 0짜리 행은 그 기본값
        // 자체가 0일 때만 생긴다. 그리는 것이 없는 행이라 창을 차지하지 않는다.
        std::vector<luil::virtual_list_item> mixed { make_items(3) };
        mixed[1].height = 20.0f;
        mixed[2].height = 20.0f;
        REQUIRE(luil::virtual_list_content_height(mixed, 0.0f) == 40.0f);
        REQUIRE(luil::virtual_list_visible_range(mixed, 0.0f, 0.0f, 20.0f, 0) == luil::virtual_list_range { 1u, 2u });
    }

    SECTION("모델이 비었거나 창이 없으면 빈 구간이다")
    {
        REQUIRE(luil::virtual_list_visible_range({}, 20.0f, 0.0f, 100.0f, 2).empty());
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 0.0f, 0.0f, 2).empty());
        REQUIRE(luil::virtual_list_visible_range(items, 20.0f, 0.0f, -10.0f, 2).empty());
    }
}

TEST_CASE("A virtual list step walks the model, skips disabled rows and stops at the ends", "[ui][list]")
{
    std::vector<luil::virtual_list_item> items { make_items(6) };
    items[2].enabled = false;
    items[3].enabled = false;

    SECTION("한 칸은 한 칸이고 건너뛴 행은 칸으로 세지 않는다")
    {
        REQUIRE(luil::virtual_list_step_target(items, 0u, luil::value_step::increase, 1) == 1u);
        // 1에서 아래로 한 칸이 4다 — 비활성 둘을 지나쳐도 걸음은 하나다.
        REQUIRE(luil::virtual_list_step_target(items, 1u, luil::value_step::increase, 1) == 4u);
        REQUIRE(luil::virtual_list_step_target(items, 4u, luil::value_step::decrease, 1) == 1u);
    }

    SECTION("Page는 칸 수만큼이고 0 이하는 한 칸으로 본다")
    {
        REQUIRE(luil::virtual_list_step_target(items, 0u, luil::value_step::increase_page, 2) == 4u);
        REQUIRE(luil::virtual_list_step_target(items, 5u, luil::value_step::decrease_page, 2) == 1u);
        // 창이 한 줄보다 낮아도 키가 멈추지 않는다.
        REQUIRE(luil::virtual_list_step_target(items, 0u, luil::value_step::increase_page, 0) == 1u);
        // 남은 칸보다 크면 끝에서 멈춘다 (모자란 만큼 되돌아가지 않는다).
        REQUIRE(luil::virtual_list_step_target(items, 0u, luil::value_step::increase_page, 99) == 5u);
    }

    SECTION("Home과 End는 처음과 끝의 활성 항목이다")
    {
        std::vector<luil::virtual_list_item> edged { items };
        edged[0].enabled = false;
        edged[5].enabled = false;
        REQUIRE(luil::virtual_list_step_target(edged, 4u, luil::value_step::minimum, 1) == 1u);
        REQUIRE(luil::virtual_list_step_target(edged, 1u, luil::value_step::maximum, 1) == 4u);
    }

    SECTION("끝에서는 멈춘다 — 돌지 않는다")
    {
        // 십만 줄에서 ↓ 한 번에 맨 위로 돌아가면 그것은 이동이 아니라 사고다.
        REQUIRE(luil::virtual_list_step_target(items, 5u, luil::value_step::increase, 1).has_value() == false);
        REQUIRE(luil::virtual_list_step_target(items, 0u, luil::value_step::decrease, 1).has_value() == false);
        REQUIRE(luil::virtual_list_step_target(items, 5u, luil::value_step::increase_page, 4).has_value() == false);
    }

    SECTION("서 있던 자리를 모르면 첫 활성 항목에서 시작한 것으로 본다")
    {
        // 아직 아무 데도 서지 않은 목록에서 ↓ 한 번이 첫 항목을 집는다.
        REQUIRE(luil::virtual_list_step_target(items, std::nullopt, luil::value_step::increase, 1) == 0u);
        REQUIRE(luil::virtual_list_step_target(items, std::nullopt, luil::value_step::decrease, 1) == 0u);
        // 모델에 없는 색인도 같은 자리다 (앱 상태가 뒤늦게 도착한 frame).
        REQUIRE(luil::virtual_list_step_target(items, 900u, luil::value_step::increase, 1) == 0u);
    }

    SECTION("갈 곳이 없는 모델은 빈 값이다")
    {
        std::vector<luil::virtual_list_item> dead { make_items(3) };
        for (luil::virtual_list_item& item : dead)
            item.enabled = false;
        REQUIRE(luil::virtual_list_step_target(dead, std::nullopt, luil::value_step::increase, 1).has_value() == false);
        REQUIRE(luil::virtual_list_step_target(dead, 1u, luil::value_step::minimum, 1).has_value() == false);
        REQUIRE(luil::virtual_list_step_target(dead, 1u, luil::value_step::maximum, 1).has_value() == false);

        REQUIRE(luil::virtual_list_step_target({}, std::nullopt, luil::value_step::increase, 1).has_value() == false);
        REQUIRE(luil::virtual_list_step_target({}, 0u, luil::value_step::minimum, 1).has_value() == false);
    }
}

TEST_CASE("A virtual list search matches a label prefix anywhere in the model", "[ui][list]")
{
    std::vector<luil::virtual_list_item> items { make_items(5) };
    items[0].label = u8"apple";
    items[1].label = u8"apricot";
    items[2].label = u8"banana";
    items[3].label = u8"avocado";
    items[3].enabled = false;
    items[4].label = u8"cherry";

    SECTION("첫 글자는 다음 항목부터 찾고 끝에서 처음으로 돈다")
    {
        // 같은 글자를 거듭 치면 그 글자로 시작하는 항목들을 돈다.
        REQUIRE(luil::virtual_list_search_target(items, 0u, u8"a", true) == 1u);
        REQUIRE(luil::virtual_list_search_target(items, 4u, u8"a", true) == 0u);
    }

    SECTION("이어 친 글자는 지금 항목부터 찾는다")
    {
        // 글을 더 적은 것이지 다음으로 가자는 뜻이 아니다.
        REQUIRE(luil::virtual_list_search_target(items, 0u, u8"ap", false) == 0u);
        REQUIRE(luil::virtual_list_search_target(items, 1u, u8"apr", false) == 1u);
    }

    SECTION("비활성 항목은 맞지 않는다")
    {
        // "av"로 시작하는 것은 avocado뿐이지만 고를 수 없는 행이다.
        REQUIRE(luil::virtual_list_search_target(items, 1u, u8"av", true).has_value() == false);
    }

    SECTION("빈 질의와 맞지 않는 글자는 빈 값이다")
    {
        REQUIRE(luil::virtual_list_search_target(items, 0u, std::u8string_view {}, true).has_value() == false);
        REQUIRE(luil::virtual_list_search_target(items, 0u, u8"z", true).has_value() == false);
        REQUIRE(luil::virtual_list_search_target({}, std::nullopt, u8"a", true).has_value() == false);
        // 서 있던 자리를 몰라도 모델 전체를 한 바퀴 돈다.
        REQUIRE(luil::virtual_list_search_target(items, std::nullopt, u8"c", true) == 4u);
    }
}

TEST_CASE("A ten thousand row model realises only the rows the viewport can hold", "[ui][list]")
{
    luil::virtual_list_config config { make_config(10000) };
    config.move = move_factory();
    config.select = select_factory();
    config.scroll = scroll_factory();
    auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
    const luil::virtual_list_element* const view { list.get() };
    // 창은 다섯 줄만 보여 준다 (만 줄 × 20 = 200000 중 100).
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

    REQUIRE(view->content_height() == 200000.0f);
    // 걸치는 다섯 줄에 아래쪽 overscan 둘이다. 위쪽은 모델의 처음에 붙어 0에서 멈춘다.
    REQUIRE(view->realized() == luil::virtual_list_range { 0u, 7u });
    REQUIRE(virtual_lane(*view).children().size() == 7u);
    REQUIRE(row_at(*view, 0).id() == row_id(item_key(0)));
    REQUIRE(row_at(*view, 6).id() == row_id(item_key(6)));

    // 창에 걸치지 않는 행은 **tree에 아예 없다**. 이것이 `list_element`와 갈리는
    // 자리다 — 저쪽은 같은 모델에서 만 개의 element를 세운다 (list-view-design.md).
    REQUIRE(tree.find(row_id(item_key(6))) != nullptr);
    REQUIRE(tree.find(row_id(item_key(7))) == nullptr);
    REQUIRE(tree.find(row_id(item_key(5000))) == nullptr);
    REQUIRE(tree.find(row_id(item_key(9999))) == nullptr);

    // 지은 것은 전부 배치되고 자리표가 겹치지 않는다.
    REQUIRE(tree.duplicate_ids().empty());
    REQUIRE(tree.unarranged().empty());
}

TEST_CASE("The cursor row is realised even where the viewport cannot reach it", "[ui][list]")
{
    luil::virtual_list_config config { make_config(10000) };
    config.cursor = item_key(9000);
    config.move = move_factory();
    config.scroll = scroll_factory();
    auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
    const luil::virtual_list_element* const view { list.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

    REQUIRE(view->cursor() == 9000u);
    // 창의 구간은 그대로다 — 커서 행은 그 구간 **밖에** 따로 선다.
    REQUIRE(view->realized() == luil::virtual_list_range { 0u, 7u });
    REQUIRE(virtual_lane(*view).children().size() == 8u);
    REQUIRE(row_at(*view, 7).id() == row_id(item_key(9000)));
    REQUIRE(tree.find(row_id(item_key(9000))) != nullptr);
    // 초점 테를 그릴 자리가 있고, 커스텀 행 안의 컨트롤이 스크롤 한 번에
    // 사라지지 않는다. 자리는 창 밑이다 — 지었다고 보이는 것은 아니다.
    REQUIRE(row_at(*view, 7).bounds().y > 100.0f);
}

TEST_CASE("A virtual list is one Tab stop and its rows are not", "[ui][list]")
{
    SECTION("자리는 목록 자신 하나다")
    {
        luil::virtual_list_config config { make_config(12) };
        config.move = move_factory();
        config.select = select_factory();
        config.scroll = scroll_factory();
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        const luil::virtual_list_element* const view { list.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

        const std::vector<luil::ui_element_id> order { tree.focus_order() };
        REQUIRE(order.size() == 1u);
        REQUIRE(order[0] == list_id());

        // 흘릴 것이 있어 막대가 실제로 섰는데도 자리는 하나다.
        // 목록 자신이 ↑/↓·Page·Home/End를 가진 자리라, 막대까지 자리가 되면 같은
        // 목록에 Tab의 자리가 둘 선다 (`scroll_area_config::bar_tab_stop`).
        const luil::ui_element* const bar { tree.find({ luil::ui_element_kind::scroll_area_bar, u8"files" }) };
        REQUIRE(bar != nullptr);
        REQUIRE(bar->visible());
        REQUIRE(view->metrics().overflowing());
        REQUIRE(bar->tab_stop() == false);

        // 누를 수 있는 행이어도 자리가 아니다 — `tab_stop`의 기본값("누를 수
        // 있으면 자리")을 목록이 뒤집는다. 걸치는 것만 자리가 되면 자리의 수가
        // 스크롤에 따라 달라진다.
        REQUIRE(row_at(*view, 0).action(luil::ui_trigger::left_click) != nullptr);
        REQUIRE(row_at(*view, 0).tab_stop() == false);
        // 묶음도 아니다. 화살표는 묶음이 아니라 목록 자신이 받는다
        // (`key_step_target`).
        REQUIRE(view->focus_group() == luil::focus_axis::none);
        REQUIRE(view->key_step() != nullptr);
        REQUIRE(tree.focus_group_of(list_id()).members.empty());
    }

    SECTION("옮길 길도 실행할 길도 없으면 자리가 아니다")
    {
        // 커서는 앱 상태라 라이브러리가 고칠 수 없고, 옮길 길도 실행할 길도 없는
        // 초점은 아무 일도 하지 않는 자리가 된다 ("없는 것은 두지 않는다").
        luil::virtual_list_config config { make_config(12) };
        config.select = select_factory();
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        const luil::virtual_list_element* const view { list.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

        REQUIRE(view->tab_stop() == false);
        REQUIRE(view->key_step() == nullptr);
        REQUIRE(view->key_search() == nullptr);
        REQUIRE(tree.focus_order().empty());
    }

    SECTION("실행할 길만 있어도 자리다")
    {
        // `activate`만 든 목록은 훑을 수는 없어도 **커서 행을 실행할 수는 있다.**
        // 그 자리에 초점이 서지 않으면 Space·Enter가 닿을 길이 아예 없다.
        luil::virtual_list_config config { make_config(12) };
        config.activate = activate_factory();
        auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
        const luil::virtual_list_element* const view { list.get() };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };

        REQUIRE(view->tab_stop());
        REQUIRE(tree.focus_order() == std::vector<luil::ui_element_id> { list_id() });
        // 화살표와 글자는 여전히 남의 것이다 — `move`가 없으면 옮길 자리가 없다.
        REQUIRE(view->key_step() == nullptr);
        REQUIRE(view->key_search() == nullptr);
    }
}

TEST_CASE("Space and Enter run the activate message of the cursor row", "[ui][list]")
{
    // 커서와 선택을 갈라 둔 값이 여기서 나온다. 화살표로 훑고 Enter로 고르는
    // 모델은 이 factory가 있어야 실제로 표현된다 — 없으면 키보드에서 고를 길이
    // 없어, 앱은 `move`를 받을 때 선택까지 함께 옮기는 수밖에 없다.
    const auto activated = [](const luil::key_code key) {
        luil::virtual_list_config config { make_config(100) };
        config.cursor = item_key(3);
        config.move = move_factory();
        config.select = select_factory();
        config.activate = activate_factory();
        return press_key_on(std::move(config), key);
    };

    SECTION("Space와 Enter가 커서 행을 실행한다")
    {
        const std::vector<luil::input_action> space { activated(luil::key_code::space) };
        REQUIRE(space.size() == 1u);
        REQUIRE(message_at<activate_intent>(space, 0) != nullptr);
        REQUIRE(message_at<activate_intent>(space, 0)->key == item_key(3));

        // 두 키가 같은 문을 지난다 — controller가 초점의 `left_click`을 실행한다.
        const std::vector<luil::input_action> enter { activated(luil::key_code::enter) };
        REQUIRE(enter.size() == 1u);
        REQUIRE(message_at<activate_intent>(enter, 0) != nullptr);
        REQUIRE(message_at<activate_intent>(enter, 0)->key == item_key(3));
        // 고름과 갈린 메시지다. `select`는 행을 눌렀을 때의 것이라 여기 섞이지 않는다.
        REQUIRE(message_at<select_intent>(enter, 0) == nullptr);
    }

    SECTION("factory가 없으면 두 키가 아무것도 내지 않는다")
    {
        // "없는 것은 두지 않는다" — 실행할 메시지가 없으면 목록은 `left_click`을
        // 아예 들지 않으므로 키가 그대로 앱으로 흐른다.
        luil::virtual_list_config config { make_config(100) };
        config.cursor = item_key(3);
        config.move = move_factory();
        config.select = select_factory();
        REQUIRE(press_key_on(config, luil::key_code::space).empty());
        REQUIRE(press_key_on(std::move(config), luil::key_code::enter).empty());
    }
}

TEST_CASE("A pointer press below the last row of a virtual list runs nothing", "[ui][list]")
{
    // `activate`가 세운 `left_click`은 **키보드만의 문**이다. 기본 hit test는 액션을
    // 든 element에게 자기 bounds를 돌려주므로, 그대로 두면 행 아래 빈 자리를 누른
    // 것이 커서 행의 실행이 된다 — 누른 자리와 실행된 자리가 다른, 설명할 수 없는
    // 클릭이다.
    luil::virtual_list_config config { make_config(3) };
    config.cursor = item_key(0);
    config.move = move_factory();
    config.select = select_factory();
    config.activate = activate_factory();
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config), 100.0f) };

    // 목록은 실행할 액션을 실제로 들고 있고 그 자리는 [60, 100)까지 뻗어 있다.
    // 기본 hit test라면 바로 그 액션 때문에 목록이 답이 되는 자리다.
    const luil::ui_element* const list { tree->find(list_id()) };
    REQUIRE(list != nullptr);
    REQUIRE(list->action(luil::ui_trigger::left_click) != nullptr);
    REQUIRE(list->bounds().height == 100.0f);

    // 세 줄은 [0, 60)이고 그 아래 [60, 100)은 목록의 빈 자리다.
    REQUIRE(tree->hit_test(100.0f, 10.0f) == tree->find(row_id(item_key(0))));
    REQUIRE(tree->hit_test(100.0f, 80.0f) == nullptr);

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    REQUIRE(controller.process(luil::pointer_pressed_event { 100.0f, 80.0f, luil::pointer_button::left, at(0) }).empty());
    REQUIRE(controller.snapshot().pressed == luil::ui_element_id {});
    REQUIRE(controller.process(luil::pointer_released_event { 100.0f, 80.0f, luil::pointer_button::left, at(50) }).empty());

    // 행을 실제로 누르면 그 행의 메시지가 난다 — 막은 것은 목록의 빈 자리뿐이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 10.0f, luil::pointer_button::left, at(100) }));
    REQUIRE(controller.snapshot().pressed == row_id(item_key(0)));
    const std::vector<luil::input_action> clicked { controller.process(luil::pointer_released_event { 100.0f, 10.0f, luil::pointer_button::left, at(150) }) };
    REQUIRE(clicked.size() == 1u);
    REQUIRE(message_at<select_intent>(clicked, 0) != nullptr);
}

TEST_CASE("A row click keeps keyboard navigation on the list after publication", "[ui][list][interaction]")
{
    luil::virtual_list_config config { make_config(8) };
    config.move = move_factory();
    config.select = select_factory();
    config.activate = activate_factory();
    luil::interaction_controller controller {};
    controller.set_tree(published(config, 100.0f));
    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 70.0f, luil::pointer_button::left, at(0) }));
    REQUIRE(controller.snapshot().focused == list_id());
    REQUIRE(controller.snapshot().pressed == row_id(item_key(3)));
    const auto clicked { controller.process(luil::pointer_released_event { 100.0f, 70.0f, luil::pointer_button::left, at(10) }) };
    REQUIRE(moved_key(clicked, 1) == item_key(3));

    config.cursor = item_key(3);
    config.selected = item_key(3);
    controller.set_tree(published(config, 100.0f));
    REQUIRE(controller.snapshot().focused == list_id());
    const auto activated { controller.process(luil::key_pressed_event { luil::key_code::enter, false, false, false, false, at(20) }) };
    REQUIRE(message_at<activate_intent>(activated, 0) != nullptr);
    REQUIRE(message_at<activate_intent>(activated, 0)->key == item_key(3));
    const auto moved { controller.process(luil::key_pressed_event { luil::key_code::arrow_down, false, false, false, false, at(30) }) };
    REQUIRE(moved_key(moved, 0) == item_key(4));
}

TEST_CASE("Disabled cursor rows cannot be activated", "[ui][list][interaction]")
{
    for (const auto key : { luil::key_code::enter, luil::key_code::space })
    {
        luil::virtual_list_config config { make_config(8) };
        config.cursor = item_key(3);
        config.items[3].enabled = false;
        config.activate = activate_factory();
        REQUIRE(press_key_on(config, key).empty());
        config.selected = config.cursor;
        config.cursor.clear();
        REQUIRE(press_key_on(config, key).empty());
    }
}

TEST_CASE("Page movement measures the rows in the requested direction", "[ui][list][interaction]")
{
    SECTION("PageUp at the last row still moves a full page")
    {
        REQUIRE(moved_key(press_on_list(item_key(99), 1900.0f, luil::key_code::page_up), 0) == item_key(94));
    }
    SECTION("PageUp uses preceding heights and counts disabled rows as distance")
    {
        luil::virtual_list_config config { make_config(10) };
        config.cursor = item_key(6);
        config.move = move_factory();
        config.items[6].height = 200.0f;
        config.items[5].height = 60.0f;
        config.items[4].height = 40.0f;
        config.items[5].enabled = false;
        REQUIRE(moved_key(press_key_on(config, luil::key_code::page_up), 0) == item_key(4));
    }
    SECTION("PageDown counts disabled heights without adding extra active steps")
    {
        luil::virtual_list_config config { make_config(10) };
        config.cursor = item_key(0);
        config.move = move_factory();
        config.items[0].height = 60.0f;
        config.items[1].height = 40.0f;
        config.items[1].enabled = false;
        REQUIRE(moved_key(press_key_on(config, luil::key_code::page_down), 0) == item_key(2));
    }
}

TEST_CASE("A row click selects the row and moves the cursor onto it", "[ui][list]")
{
    // 커서를 함께 옮기지 않으면, 누른 다음 누른 화살표가 눌린 행이 아니라 커서가
    // 있던 옛 자리에서 출발한다 — 화면에서는 "방금 누른 곳에서 한 칸"으로 보여야
    // 하는 몸짓이 엉뚱한 데로 뛴다.
    luil::virtual_list_config config { make_config(8) };
    config.overscan = 0;
    config.cursor = item_key(1);
    config.select = select_factory();
    config.move = move_factory();
    auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
    const luil::virtual_list_element* const view { list.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    REQUIRE(view->cursor() == 1u);

    SECTION("커서 밖의 행은 고름과 커서 이동을 함께 낸다")
    {
        const luil::ui_element& row { row_at(*view, 3) };
        const luil::ui_action* const click { row.action(luil::ui_trigger::left_click) };
        REQUIRE(click != nullptr);
        const std::vector<luil::input_action> actions { (*click)({ row.id(), 0.0f, 0.0f }) };
        REQUIRE(actions.size() == 2u);
        REQUIRE(message_at<select_intent>(actions, 0) != nullptr);
        REQUIRE(message_at<select_intent>(actions, 0)->key == item_key(3));
        REQUIRE(moved_key(actions, 1) == item_key(3));
    }

    SECTION("이미 커서가 선 행은 고름만 낸다")
    {
        // `move_cursor`의 방벽과 같은 이유다 — 제자리로 옮기자는 메시지가 앱을
        // 깨워 tree를 통째로 다시 짓는다.
        const luil::ui_element& row { row_at(*view, 1) };
        const luil::ui_action* const click { row.action(luil::ui_trigger::left_click) };
        REQUIRE(click != nullptr);
        const std::vector<luil::input_action> actions { (*click)({ row.id(), 0.0f, 0.0f }) };
        REQUIRE(actions.size() == 1u);
        REQUIRE(message_at<select_intent>(actions, 0) != nullptr);
        REQUIRE(message_at<select_intent>(actions, 0)->key == item_key(1));
    }
}

TEST_CASE("A custom row is the content while the library keeps the row itself", "[ui][list]")
{
    luil::virtual_list_config config { make_config(8) };
    config.overscan = 0;
    config.selected = item_key(2);
    config.select = select_factory();
    config.build_row = [](const luil::virtual_list_item& item, const std::size_t index, const bool selected) -> std::unique_ptr<luil::ui_element> {
        return std::make_unique<row_probe>(luil::ui_element_id { kind_row_content, item.key }, index, selected);
    };
    auto list { std::make_unique<luil::virtual_list_element>(std::move(config)) };
    const luil::virtual_list_element* const view { list.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(list), { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f) };
    REQUIRE(view->realized() == luil::virtual_list_range { 0u, 5u });
    REQUIRE(tree.duplicate_ids().empty());

    // 앱이 돌려준 element가 정말 행의 내용이다.
    const luil::ui_element& row { row_at(*view, 2) };
    REQUIRE(row.children().size() == 1u);
    const auto* const content { static_cast<const row_probe*>(row.children()[0].get()) };
    REQUIRE(content->id() == luil::ui_element_id { kind_row_content, item_key(2) });
    REQUIRE(content->index() == 2u);
    // 고름의 판정은 라이브러리가 한다 — 앱은 그 답을 받아 그리기만 한다.
    REQUIRE(content->selected());
    REQUIRE(static_cast<const row_probe*>(row_at(*view, 0).children()[0].get())->selected() == false);

    // 자리표·선택 표시·누름·글자 탐색 이름은 그래도 목록의 것이다.
    // 그러지 않으면 앱마다 선택과 키보드를 다시 짜고 그중 하나를 반드시 틀린다.
    REQUIRE(row.id() == row_id(item_key(2)));
    REQUIRE(row.search_label() == item_key(2));
    REQUIRE(row.accessibility().role == luil::access_role::list_item);
    REQUIRE(row.accessibility().selected == true);
    REQUIRE(row_at(*view, 0).accessibility().selected == false);

    const luil::ui_action* const select { row.action(luil::ui_trigger::left_click) };
    REQUIRE(select != nullptr);
    const std::vector<luil::input_action> selected { (*select)({ row.id(), 0.0f, 0.0f }) };
    REQUIRE(selected.size() == 1u);
    REQUIRE(message_at<select_intent>(selected, 0) != nullptr);
    REQUIRE(message_at<select_intent>(selected, 0)->key == item_key(2));

    // nullptr을 돌려주면 그 행은 내용 없이 배경과 선택 표시만 선다.
    luil::virtual_list_config bare_config { make_config(3) };
    bare_config.overscan = 0;
    bare_config.build_row = [](const luil::virtual_list_item&, const std::size_t, const bool) -> std::unique_ptr<luil::ui_element> { return nullptr; };
    luil::virtual_list_element bare { std::move(bare_config) };
    bare.arrange({ { 0.0f, 0.0f, 200.0f, 100.0f }, 1.0f });
    REQUIRE(row_at(bare, 0).children().empty());
}

TEST_CASE("Keys on a focused virtual list move the cursor and scroll only where it is hidden", "[ui][list]")
{
    // 창은 다섯 줄이다 (100 ÷ 20). 키가 내는 것은 커서 메시지와 스크롤 메시지
    // **둘**이고, 옮긴 자리가 이미 보이면 스크롤은 내지 않는다.
    SECTION("보이는 자리로 옮기면 커서 메시지 하나뿐이다")
    {
        // 0짜리 스크롤이 logic을 깨워 tree를 통째로 다시 짓는 것을 막는 방벽이다
        // (`route_reveal`의 그 자리와 같다).
        const std::vector<luil::input_action> down { press_on_list(item_key(3), 0.0f, luil::key_code::arrow_down) };
        REQUIRE(down.size() == 1u);
        REQUIRE(moved_key(down, 0) == item_key(4));

        const std::vector<luil::input_action> up { press_on_list(item_key(5), 20.0f, luil::key_code::arrow_up) };
        REQUIRE(up.size() == 1u);
        REQUIRE(moved_key(up, 0) == item_key(4));
    }

    SECTION("창 밖으로 나가면 스크롤 메시지가 함께 간다")
    {
        // 되살리기를 앱에 미루지 않는다 — 초점은 목록에 그대로 서 있어
        // `on_focus_moved`의 계기가 아예 오지 않는다.
        const std::vector<luil::input_action> down { press_on_list(item_key(4), 0.0f, luil::key_code::arrow_down) };
        REQUIRE(down.size() == 2u);
        REQUIRE(moved_key(down, 0) == item_key(5));
        REQUIRE(scrolled_delta(down, 1) == 20.0f);

        const std::vector<luil::input_action> up { press_on_list(item_key(5), 100.0f, luil::key_code::arrow_up) };
        REQUIRE(up.size() == 2u);
        REQUIRE(moved_key(up, 0) == item_key(4));
        REQUIRE(scrolled_delta(up, 1) == -20.0f);
    }

    SECTION("Page는 창 하나만큼 걷는다")
    {
        // 묶음이 갖지 못하는 키가 여기서는 저절로 온다 — 걸음의 임자가 목록이다.
        const std::vector<luil::input_action> down { press_on_list(item_key(0), 0.0f, luil::key_code::page_down) };
        REQUIRE(down.size() == 2u);
        REQUIRE(moved_key(down, 0) == item_key(5));
        REQUIRE(scrolled_delta(down, 1) == 20.0f);

        const std::vector<luil::input_action> up { press_on_list(item_key(20), 400.0f, luil::key_code::page_up) };
        REQUIRE(up.size() == 2u);
        REQUIRE(moved_key(up, 0) == item_key(15));
        REQUIRE(scrolled_delta(up, 1) == -100.0f);
    }

    SECTION("Home과 End는 모델의 처음과 끝이다")
    {
        // 창에 걸치는 범위가 아니라 **모델**의 끝이다. 그것이 가상화한 목록에서
        // End가 뜻을 잃지 않는 유일한 길이다.
        const std::vector<luil::input_action> home { press_on_list(item_key(20), 400.0f, luil::key_code::home) };
        REQUIRE(home.size() == 2u);
        REQUIRE(moved_key(home, 0) == item_key(0));
        REQUIRE(scrolled_delta(home, 1) == -400.0f);

        const std::vector<luil::input_action> end { press_on_list(item_key(20), 400.0f, luil::key_code::end) };
        REQUIRE(end.size() == 2u);
        REQUIRE(moved_key(end, 0) == item_key(99));
        REQUIRE(scrolled_delta(end, 1) == 1500.0f);
    }

    SECTION("끝에서는 키를 삼키고 아무것도 내지 않는다")
    {
        // 빈 목록이 "내 것이고 삼켰다"다 — 화살표가 앱의 화면 단축키로 새면
        // 마지막 행에서 ↓를 누를 때마다 화면이 함께 뛴다.
        const std::vector<luil::input_action> bottom { press_on_list(item_key(99), 1900.0f, luil::key_code::arrow_down) };
        REQUIRE(bottom.empty());
        const std::vector<luil::input_action> top { press_on_list(item_key(0), 0.0f, luil::key_code::arrow_up) };
        REQUIRE(top.empty());
    }
}

TEST_CASE("Type ahead on a virtual list searches the whole model, not the realised rows", "[ui][list]")
{
    luil::virtual_list_config config { make_config(200) };
    config.items[150].label = u8"zulu";
    config.move = move_factory();
    config.scroll = scroll_factory();

    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config), 100.0f) };
    // 맞을 행은 창에서 한참 밖이라 tree에 아예 없다. 묶음의 글자 탐색은 tree에
    // 선 항목만 보므로 여기서는 답할 수 없는 질문이고, 그래서 모델을 아는
    // 목록이 `key_search_target`으로 직접 답한다.
    REQUIRE(tree->find(row_id(item_key(150))) == nullptr);

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == list_id());

    const std::vector<luil::input_action> found { controller.process(luil::character_typed_event { U'z', at(10) }) };
    REQUIRE(found.size() == 2u);
    REQUIRE(moved_key(found, 0) == item_key(150));
    REQUIRE(scrolled_delta(found, 1) == 2920.0f);
    // 초점은 목록에 그대로다 — 옮길 자리가 tree에 없어 초점이 행에 설 수 없다.
    REQUIRE(controller.snapshot().focused == list_id());

    // 맞는 것이 없어도 글자는 이 목록이 가진다 (빈 목록 = 삼켰다).
    // 새면 그 글자가 앱의 단축키로 흘러, 목록을 훑다가 화면이 바뀐다.
    const std::vector<luil::input_action> missed { controller.process(luil::character_typed_event { U'q', at(20) }) };
    REQUIRE(missed.empty());
    REQUIRE(controller.snapshot().focused == list_id());

    // 시간이 끊기면 앞의 글자를 잊는다 — 질의를 잇고 끊는 규칙은 controller가
    // 쥐고, 모델에서 무엇이 맞는지는 목록이 답한다.
    const std::vector<luil::input_action> again { controller.process(luil::character_typed_event { U'z', at(3000) }) };
    REQUIRE(again.size() == 2u);
    REQUIRE(moved_key(again, 0) == item_key(150));
}

TEST_CASE("Type ahead takes the first-character flag from the controller, not the query length", "[ui][list]")
{
    // 한글은 한 글자가 세 byte다. element가 질의의 길이로 "첫 글자인가"를 되짚으면
    // 첫 글자부터 이어 친 글자로 세어져, 같은 글자를 거듭 쳐도 후보가 돌지 않는다 —
    // 질의를 잇고 끊는 controller만 이 값을 옳게 안다.
    // 두 값의 답이 실제로 갈리는 모델이다. 갈리지 않으면 아래의 걸음이 무엇을
    // 잠그는지 말할 수 없다 — 첫 글자로 세면 커서가 옮겨 가고, 이어 친 글자로
    // 세면 커서가 선 "가나다"가 그대로 맞아 아무 일도 일어나지 않는다.
    const std::vector<luil::virtual_list_item> items { korean_items() };
    REQUIRE(luil::virtual_list_search_target(items, 0u, u8"가", true) == 1u);
    REQUIRE(luil::virtual_list_search_target(items, 0u, u8"가", false) == 0u);

    const auto stand_on = [](const std::u8string& cursor) {
        luil::virtual_list_config config { make_config(4) };
        config.items = korean_items();
        config.cursor = cursor;
        config.move = move_factory();
        return published(std::move(config), 100.0f);
    };

    luil::interaction_controller controller {};
    controller.set_tree(stand_on(item_key(0)));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == list_id());

    SECTION("같은 글자를 거듭 치면 후보를 돈다")
    {
        // 첫 글자는 **다음** 항목부터 찾는다. 커서가 선 "가나다"가 이미 맞는데도
        // 자리가 움직이는 것이 그 증거다.
        const std::vector<luil::input_action> first { controller.process(luil::character_typed_event { U'가', at(100) }) };
        REQUIRE(first.size() == 1u);
        REQUIRE(moved_key(first, 0) == item_key(1));

        // 앱이 커서 메시지를 받아 지은 다음 frame이다. 시간이 끊겨 질의도 새로 선다.
        controller.set_tree(stand_on(item_key(1)));
        const std::vector<luil::input_action> again { controller.process(luil::character_typed_event { U'가', at(2000) }) };
        REQUIRE(again.size() == 1u);
        REQUIRE(moved_key(again, 0) == item_key(3));

        // 끝에 닿으면 처음으로 돈다 — 글자 탐색은 걸음과 달리 도는 것이 어휘다.
        controller.set_tree(stand_on(item_key(3)));
        const std::vector<luil::input_action> wrapped { controller.process(luil::character_typed_event { U'가', at(4000) }) };
        REQUIRE(wrapped.size() == 1u);
        REQUIRE(moved_key(wrapped, 0) == item_key(0));
    }

    SECTION("이어 친 글자는 지금 커서부터 다시 찾는다")
    {
        static_cast<void>(controller.process(luil::character_typed_event { U'가', at(100) }));

        // 커서는 아직 "가나다"다 (앱이 다음 frame을 짓기 전이다). 질의 "가나"는 그
        // 행에 그대로 맞으므로 자리가 움직이지 않는다 — 글을 더 적은 것이지 다음으로
        // 가자는 뜻이 아니다.
        REQUIRE(controller.process(luil::character_typed_event { U'나', at(200) }).empty());

        // 맞는 것이 없어서 조용한 것이 아니라는 확인이다. 질의는 이어져 있고,
        // "가나라"는 커서 다음 행이다.
        const std::vector<luil::input_action> refined { controller.process(luil::character_typed_event { U'라', at(300) }) };
        REQUIRE(refined.size() == 1u);
        REQUIRE(moved_key(refined, 0) == item_key(1));
    }
}

TEST_CASE("A virtual list clamps the scroll offset it was given and reports it back", "[ui][list]")
{
    luil::virtual_list_config config { make_config(10) };
    config.scroll_offset = 500.0f;
    config.scroll = scroll_factory();
    luil::virtual_list_element list { std::move(config) };
    // 내용 200, 창 80이라 최대 120이다.
    list.arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, 1.0f });

    REQUIRE(list.content_height() == 200.0f);
    REQUIRE(list.metrics().content_height == 200.0f);
    REQUIRE(list.metrics().viewport_height == 80.0f);
    REQUIRE(list.metrics().maximum_scroll == 120.0f);
    REQUIRE(list.metrics().scroll_offset == 120.0f);
    REQUIRE(list.metrics().overflowing());
    // 지을 행을 고르는 것도 **다듬은 값**이 정한다. 범위 밖 값 그대로 골랐다면
    // 걸치는 행이 하나도 없어 빈 목록이 그려진다.
    REQUIRE(list.realized() == luil::virtual_list_range { 4u, 10u });

    // 내용이 창보다 짧으면 흘릴 것이 없다.
    luil::virtual_list_config short_config { make_config(2) };
    short_config.scroll_offset = 30.0f;
    short_config.scroll = scroll_factory();
    luil::virtual_list_element short_list { std::move(short_config) };
    short_list.arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    REQUIRE(short_list.metrics().maximum_scroll == 0.0f);
    REQUIRE(short_list.metrics().scroll_offset == 0.0f);
    REQUIRE(short_list.metrics().overflowing() == false);
    REQUIRE(short_list.realized() == luil::virtual_list_range { 0u, 2u });
}

TEST_CASE("A zero height arrange does not freeze the list empty", "[ui][list]")
{
    // 담는 쪽은 자리를 잡기 전에 한 번 0으로 배치해 볼 수 있다. 그것을 계기로
    // 삼아 지으면 아무 행도 걸치지 않은 빈 목록이 그대로 굳는다.
    luil::virtual_list_config config { make_config(10) };
    config.overscan = 0;
    config.scroll = scroll_factory();
    luil::virtual_list_element list { std::move(config) };

    list.arrange({ { 0.0f, 0.0f, 200.0f, 0.0f }, 1.0f });
    REQUIRE(list.realized().empty());
    REQUIRE(virtual_lane(list).children().empty());

    // 높이가 있는 첫 배치가 행을 짓는다 (창 80 ÷ 줄 20 = 네 줄).
    list.arrange({ { 0.0f, 0.0f, 200.0f, 80.0f }, 1.0f });
    REQUIRE(list.realized() == luil::virtual_list_range { 0u, 4u });
    REQUIRE(virtual_lane(list).children().size() == 4u);

    // 그 뒤의 재배치는 다시 짓지 않는다 — 자식은 tree 하나에 한 벌이라 두 번
    // 지으면 같은 자리표가 둘 선다. 넓어진 창을 채우는 것은 다음 frame의 tree다.
    list.arrange({ { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f });
    REQUIRE(list.realized() == luil::virtual_list_range { 0u, 4u });
    REQUIRE(virtual_lane(list).children().size() == 4u);
}
