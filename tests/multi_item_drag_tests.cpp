#include "raster_probe.h"

#include "luil/theme/ui_theme.h"
#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"
#include "luil/ui/virtual_list_element.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 고른 여러 항목을 한 번에 끄는 drag & drop이다 (`drag_payload::dragged_owners`).
namespace {
    constexpr float raster_scale { 2.0f };

    // 놓은 자리와 함께 끌려 온 항목들이다. 놓는 순간 Ctrl이면 복사다.
    struct dropped_intent
    {
        std::u8string folder {};
        std::vector<std::u8string> items {};
        bool copy { false };
    };

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    [[nodiscard]] bool folder(const std::u8string& key)
    {
        return key == u8"docs" || key == u8"pics";
    }

    // 여섯 줄(20px)짜리 파일 목록이다. 폴더 행(docs·pics)만 놓을 자리이고, 고른 항목은 a·b·c다.
    [[nodiscard]] std::shared_ptr<const luil::ui_tree> make_tree()
    {
        luil::virtual_list_config config {};
        config.owner = u8"files";
        config.row_height = 20.0f;
        for (const char8_t* const key : { u8"docs", u8"a", u8"b", u8"c", u8"pics", u8"d" })
            config.items.push_back({ key, key });
        config.multiple_selection = true;
        config.selection = { u8"a", u8"b", u8"c" };
        config.select = [](const std::u8string&) { return luil::input_action {}; };
        config.drag = [selection = config.selection](const std::u8string& key, const luil::ui_action_context&) {
            luil::drag_payload payload {};
            payload.dragged_owner = key;
            // 고른 항목을 누르고 끌면 고른 것 모두가, 그 밖을 끌면 그것 하나가 간다.
            if (std::find(selection.begin(), selection.end(), key) != selection.end())
                payload.dragged_owners = selection;
            payload.rejected_cursor = luil::ui_cursor::not_allowed;
            return payload;
        };
        config.accepts_drop = [](const std::u8string& key, const luil::drag_payload& payload) { return folder(key) && payload.carries(key) == false; };
        config.drop = [](const std::u8string& key, const luil::drag_payload& payload, const luil::ui_action_context& context) {
            return luil::make_app_action(dropped_intent { key, payload.dragged_owners.empty() ? std::vector<std::u8string> { payload.dragged_owner } : payload.dragged_owners, context.control });
        };
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::virtual_list_element>(std::move(config)), { 0.0f, 0.0f, 200.0f, 200.0f }, 1.0f));
    }

    // 행 하나의 가운데 높이다 (위에서 index번째 줄).
    [[nodiscard]] float row_y(const int index)
    {
        return 20.0f * static_cast<float>(index) + 10.0f;
    }

    // 놓기의 앱 메시지를 **값으로** 꺼낸다. 메시지 몸은 액션 목록이 쥐고 있어서, `process`가 돌려준 임시 목록에서
    // 포인터를 받으면 그 문장이 끝나는 순간 해제된 자리를 가리킨다.
    [[nodiscard]] std::optional<dropped_intent> dropped_of(const std::vector<luil::input_action>& actions)
    {
        for (const luil::input_action& action : actions)
            if (const auto* const message { std::get_if<luil::app_message>(&action) }; message != nullptr)
                if (const auto* const intent { message->get<dropped_intent>() }; intent != nullptr)
                    return *intent;
        return std::nullopt;
    }
}

TEST_CASE("A payload counts the items it carries", "[ui][drag][multi-drag]")
{
    luil::drag_payload single {};
    single.dragged_owner = u8"a";
    REQUIRE(single.dragged_count() == 1u);
    REQUIRE(single.carries(u8"a"));
    REQUIRE_FALSE(single.carries(u8"b"));
    REQUIRE_FALSE(single.carries(u8""));
    REQUIRE(luil::drag_payload {}.dragged_count() == 0u);

    luil::drag_payload many {};
    many.dragged_owner = u8"b";
    many.dragged_owners = { u8"a", u8"b", u8"c" };
    REQUIRE(many.dragged_count() == 3u);
    REQUIRE(many.carries(u8"c"));
}

TEST_CASE("A payload tells rows of another list apart by its container", "[ui][drag][multi-drag]")
{
    const luil::ui_element_id left { luil::ui_element_kind::virtual_list, u8"left" };
    const luil::ui_element_id right { luil::ui_element_kind::virtual_list, u8"right" };
    luil::drag_payload payload {};
    payload.dragged_owner = u8"a";
    payload.container = left;
    REQUIRE(payload.carries(left, u8"a"));
    // 다른 목록의 같은 키 행은 끌리는 것이 아니다.
    REQUIRE_FALSE(payload.carries(right, u8"a"));
    // 출발 목록을 모르면(앱이 지은 payload) 키만 견준다.
    payload.container = {};
    REQUIRE(payload.carries(right, u8"a"));
}

TEST_CASE("A virtual list row drag names its list as the container", "[ui][interaction][multi-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, row_y(2), luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(2) + 10.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.container == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
}

TEST_CASE("The default ghost names the lead item and counts the rest", "[ui][drag][multi-drag]")
{
    luil::drag_payload payload {};
    payload.dragged_owner = u8"보고서.txt";
    luil::drag_ghost_label label { luil::drag_ghost_label_of(payload) };
    REQUIRE(label.text == u8"보고서.txt");
    REQUIRE(label.more == 0u);

    payload.dragged_owners = { u8"보고서.txt", u8"그림.png", u8"메모.md" };
    label = luil::drag_ghost_label_of(payload);
    REQUIRE(label.text == u8"보고서.txt");
    REQUIRE(label.more == 2u);

    // 이름을 따로 주면 그것이다. 대표 키가 없으면 목록의 첫 키다.
    payload.label = u8"파일 3개";
    REQUIRE(luil::drag_ghost_label_of(payload).text == u8"파일 3개");
    payload.label.clear();
    payload.dragged_owner.clear();
    REQUIRE(luil::drag_ghost_label_of(payload).text == u8"보고서.txt");
}

TEST_CASE("Dragging a selected row carries the whole selection to a folder row", "[ui][interaction][multi-drag]")
{
    luil::interaction_controller controller {};
    const std::shared_ptr<const luil::ui_tree> tree { make_tree() };
    controller.set_tree(tree);

    // 고른 행 b를 누르고 끈다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, row_y(2), luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(2) + 10.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());
    const luil::drag_payload& payload { controller.snapshot().drag->payload };
    REQUIRE(payload.dragged_owner == u8"b");
    REQUIRE(payload.dragged_owners == std::vector<std::u8string> { u8"a", u8"b", u8"c" });
    REQUIRE(payload.source == luil::virtual_list_row_id(u8"files", u8"b"));

    // 놓을 수 없는 행(파일 a) 위에서는 금지 모양이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(1), at(20) }));
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id {});
    REQUIRE(tree->cursor_at(50.0f, row_y(1), controller.snapshot()) == luil::ui_cursor::not_allowed);

    // 폴더 행 위에서는 그 행이 놓을 자리로 서고 잡은 모양이다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(0), at(30) }));
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::virtual_list_row_id(u8"files", u8"docs"));
    REQUIRE(tree->cursor_at(50.0f, row_y(0), controller.snapshot()) == luil::ui_cursor::grabbing);

    // 놓으면 셋이 함께 그 폴더로 간다. Ctrl 없이 놓으면 옮기기다.
    const std::vector<luil::input_action> actions { controller.process(luil::pointer_released_event { 50.0f, row_y(0), luil::pointer_button::left, at(40) }) };
    const std::optional<dropped_intent> dropped { dropped_of(actions) };
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->folder == u8"docs");
    REQUIRE(dropped->items == std::vector<std::u8string> { u8"a", u8"b", u8"c" });
    REQUIRE(dropped->copy == false);
}

TEST_CASE("Holding Ctrl when releasing copies instead of moving", "[ui][interaction][multi-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, row_y(1), luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(2), at(10) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(4), at(20) }));
    luil::pointer_released_event release { 50.0f, row_y(4), luil::pointer_button::left, at(30) };
    release.control = true;
    const std::optional<dropped_intent> dropped { dropped_of(controller.process(release)) };
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->folder == u8"pics");
    REQUIRE(dropped->items.size() == 3u);
    REQUIRE(dropped->copy);
}

TEST_CASE("A row outside the selection drags alone and a folder refuses itself", "[ui][interaction][multi-drag]")
{
    luil::interaction_controller controller {};
    const std::shared_ptr<const luil::ui_tree> tree { make_tree() };
    controller.set_tree(tree);
    // 고르지 않은 폴더 pics를 끈다 — 그것 하나다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, row_y(4), luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(4) + 8.0f, at(10) }));
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.dragged_count() == 1u);
    // 자기 자신 위는 놓을 자리가 아니다.
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::ui_element_id {});
    REQUIRE(tree->cursor_at(50.0f, row_y(4), controller.snapshot()) == luil::ui_cursor::not_allowed);
    // 다른 폴더 위에 놓으면 그것 하나가 간다.
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(0), at(20) }));
    const std::optional<dropped_intent> dropped { dropped_of(controller.process(luil::pointer_released_event { 50.0f, row_y(0), luil::pointer_button::left, at(30) })) };
    REQUIRE(dropped.has_value());
    REQUIRE(dropped->items == std::vector<std::u8string> { u8"pics" });
}

TEST_CASE("Losing the mouse capture over a folder drops nothing", "[ui][interaction][multi-drag]")
{
    luil::interaction_controller controller {};
    controller.set_tree(make_tree());
    static_cast<void>(controller.process(luil::pointer_pressed_event { 50.0f, row_y(2), luil::pointer_button::left, at(0) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(2) + 10.0f, at(5) }));
    static_cast<void>(controller.process(luil::pointer_moved_event { 50.0f, row_y(0), at(10) }));
    REQUIRE(controller.snapshot().drag->hovered_drop_target == luil::virtual_list_row_id(u8"files", u8"docs"));
    luil::pointer_released_event lost { -10000.0f, -10000.0f, luil::pointer_button::left, at(20) };
    lost.cancelled = true;
    REQUIRE(dropped_of(controller.process(lost)).has_value() == false);
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("A drag without a rejected cursor keeps the grabbing cursor everywhere", "[ui][interaction][multi-drag]")
{
    const std::shared_ptr<const luil::ui_tree> tree { make_tree() };
    luil::interaction_snapshot interaction {};
    interaction.drag = luil::drag_visual {};
    interaction.drag->payload.source = luil::virtual_list_row_id(u8"files", u8"b");
    interaction.drag->payload.dragged_owner = u8"b";
    REQUIRE(tree->cursor_at(50.0f, row_y(1), interaction) == luil::ui_cursor::grabbing);
}

TEST_CASE("The ghost of a multi-item drag shows a count pill", "[ui][raster][multi-drag]")
{
    const luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };
    // 포인터 (150, 120)의 ghost는 (170, 140)에서 224×48이다 (물리 픽셀, 배율 2). 개수 알약은 오른쪽 끝에 선다.
    const luil::rect_f pill_area { 340.0f, 150.0f, 50.0f, 28.0f };
    const auto draw = [&palette, &pill_area](const std::vector<std::u8string>& owners) {
        auto root { std::make_unique<luil::virtual_list_element>(luil::virtual_list_config { .owner = u8"empty" }) };
        const luil::ui_tree tree { luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, 400.0f, 220.0f }, raster_scale) };
        luil::interaction_snapshot interaction {};
        luil::drag_visual drag {};
        drag.payload.dragged_owner = u8"a";
        drag.payload.dragged_owners = owners;
        drag.x = 150.0f;
        drag.y = 120.0f;
        interaction.drag = drag;
        luil::testing::raster_frame frame { 400, 220, palette, raster_scale };
        frame.draw(tree, interaction);
        return frame.count_color(pill_area, palette.accent);
    };
    // 하나뿐이면 테두리의 획만 강조색이다.
    const int single { draw({}) };
    // 여럿이면 강조색으로 채운 알약이 선다.
    const int many { draw({ u8"a", u8"b", u8"c" }) };
    REQUIRE(many > single + 200);
}
