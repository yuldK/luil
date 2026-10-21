#include "luil/ui/table_element.h"

#include "luil/ui/app_message.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    constexpr luil::ui_element_kind kind_cell { luil::application_element_kind(0) };

    struct sort_intent
    {
        std::u8string column {};
    };

    struct resize_intent
    {
        std::u8string column {};
        float delta { 0.0f };
    };

    struct resize_end_intent
    {
        std::u8string column {};
    };

    struct scroll_x_intent
    {
        float delta { 0.0f };
    };

    struct scroll_y_intent
    {
        float delta { 0.0f };
    };

    struct move_intent
    {
        std::u8string key {};
    };

    // 앱이 지은 칸이다. 자리표에 행과 열을 함께 담는다.
    class cell_probe final : public luil::ui_element
    {
    public:
        explicit cell_probe(std::u8string owner)
            : ui_element { luil::ui_element_id { kind_cell, std::move(owner) } }
        {}

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}
    };

    [[nodiscard]] std::u8string part(const std::u8string& key)
    {
        return u8"files" + std::u8string { u8"\u001f" } + key;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    template<typename message_type>
    [[nodiscard]] const message_type* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<message_type>() : nullptr;
    }

    [[nodiscard]] std::vector<luil::table_column> make_columns()
    {
        luil::table_column name {};
        name.key = u8"name";
        name.header = u8"이름";
        name.width = 160.0f;
        name.min_width = 80.0f;
        luil::table_column author {};
        author.key = u8"author";
        author.header = u8"작성자";
        author.width = 90.0f;
        luil::table_column revision {};
        revision.key = u8"revision";
        revision.header = u8"리비전";
        revision.width = 70.0f;
        revision.align = luil::column_align::end;
        revision.sort = luil::sort_direction::descending;
        revision.sortable = false;
        return { name, author, revision };
    }

    // 열 폭의 합은 320이고 창은 200이다 — 가로로 120만큼 흐를 수 있다.
    [[nodiscard]] luil::table_config make_config(const bool custom_cells = false)
    {
        luil::table_config config {};
        config.owner = u8"files";
        config.columns = make_columns();
        for (std::size_t index = 0; index < 20u; ++index)
        {
            luil::virtual_list_item item {};
            item.key = u8"row-" + std::u8string(1u, static_cast<char8_t>(u8'a' + index));
            item.label = item.key;
            config.rows.items.push_back(std::move(item));
        }
        config.rows.row_height = 20.0f;
        config.rows.move = [](const std::u8string& key) { return luil::make_app_action(move_intent { key }); };
        config.rows.scroll = [](const float delta) { return luil::make_app_action(scroll_y_intent { delta }); };
        config.cell_text = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) { return row.key + u8"." + column.key; };
        if (custom_cells)
            config.build_cell = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column, bool) -> std::unique_ptr<luil::ui_element> {
                return std::make_unique<cell_probe>(row.key + u8"/" + column.key);
            };
        config.scroll_horizontal = [](const float delta) { return luil::make_app_action(scroll_x_intent { delta }); };
        config.sort_request = [](const std::u8string& column) { return luil::make_app_action(sort_intent { column }); };
        config.resize_column = [](const std::u8string& column, const float delta) { return luil::make_app_action(resize_intent { column, delta }); };
        config.resize_column_end = [](const std::u8string& column) { return luil::make_app_action(resize_end_intent { column }); };
        return config;
    }

    [[nodiscard]] std::shared_ptr<const luil::ui_tree> published(luil::table_config config, const float scale = 1.0f)
    {
        return std::make_shared<const luil::ui_tree>(luil::make_arranged_tree(std::make_unique<luil::table_element>(std::move(config)), { 0.0f, 0.0f, 200.0f * scale, 300.0f * scale }, scale));
    }

    [[nodiscard]] const luil::ui_element& find(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        return *element;
    }
} // namespace

TEST_CASE("Column geometry is a running sum and clamps stay in range", "[ui][table]")
{
    const std::vector<luil::table_column> columns { make_columns() };
    const std::vector<luil::table_column_span> spans { luil::table_column_spans(columns) };
    REQUIRE(spans == std::vector<luil::table_column_span> { { 0.0f, 160.0f }, { 160.0f, 90.0f }, { 250.0f, 70.0f } });
    REQUIRE(luil::table_content_width(columns) == 320.0f);

    REQUIRE(luil::table_clamp_scroll(500.0f, 320.0f, 200.0f) == 120.0f);
    REQUIRE(luil::table_clamp_scroll(-10.0f, 320.0f, 200.0f) == 0.0f);
    // 내용이 창보다 좁으면 흐르지 않는다.
    REQUIRE(luil::table_clamp_scroll(30.0f, 150.0f, 200.0f) == 0.0f);

    // 이름 열은 160에서 최소 80까지 줄 수 있다.
    REQUIRE(luil::table_resize_delta(columns[0], -50.0f) == -50.0f);
    REQUIRE(luil::table_resize_delta(columns[0], -120.0f) == -80.0f);
    REQUIRE(luil::table_resize_delta(columns[0], 30.0f) == 30.0f);
}

TEST_CASE("Header cells and body cells share column boundaries at every scale", "[ui][table]")
{
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        luil::table_config config { make_config(true) };
        config.scroll_x = 40.0f;
        const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config), scale) };
        REQUIRE(tree->duplicate_ids().empty());
        REQUIRE(tree->unarranged().empty());
        for (const std::u8string column : { u8"name", u8"author", u8"revision" })
        {
            const luil::rect_f header { find(*tree, { luil::ui_element_kind::table_header_cell, part(column) }).bounds() };
            const luil::rect_f cell { find(*tree, { kind_cell, u8"row-a/" + column }).bounds() };
            REQUIRE(header.x == cell.x);
            REQUIRE(header.width == cell.width);
        }
        // 가로 스크롤만큼 왼쪽으로 밀렸다.
        REQUIRE(find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }).bounds().x == -40.0f * scale);
    }
}

TEST_CASE("A table clamps its horizontal scroll and only realises the visible rows", "[ui][table]")
{
    luil::table_config config { make_config() };
    config.scroll_x = 1000.0f;
    auto owned { std::make_unique<luil::table_element>(std::move(config)) };
    const luil::table_element* const table { owned.get() };
    const luil::ui_tree tree { luil::make_arranged_tree(std::move(owned), { 0.0f, 0.0f, 200.0f, 120.0f }, 1.0f) };
    REQUIRE(table->content_width() == 320.0f);
    REQUIRE(table->scroll_x() == 120.0f);
    // 머리글 28을 뺀 92 높이에 20짜리 행 다섯과 overscan이다. 모델 스무 줄을 다 짓지 않는다.
    REQUIRE(table->body().realized().end - table->body().realized().begin < 10u);
}

TEST_CASE("Pressing a header asks the app to sort and keeps focus on the body", "[ui][table]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));

    static_cast<void>(controller.process(luil::pointer_pressed_event { 100.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { 100.0f, 14.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<sort_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<sort_intent>(clicked, 0)->column == u8"name");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    // 정렬할 수 없는 열의 머리글은 누를 것이 없다.
    const luil::table_config config { make_config() };
    const std::shared_ptr<const luil::ui_tree> tree { published(config) };
    REQUIRE(find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"revision") }).action(luil::ui_trigger::left_click) == nullptr);
}

TEST_CASE("Dragging a column boundary reports width changes without going below the minimum", "[ui][table]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));

    // 이름 열의 오른쪽 경계는 x = 160이다.
    static_cast<void>(controller.process(luil::pointer_pressed_event { 160.0f, 14.0f, luil::pointer_button::left, at(0) }));
    const auto widened { controller.process(luil::pointer_moved_event { 175.0f, 14.0f, at(10) }) };
    REQUIRE(message_at<resize_intent>(widened, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(widened, 0)->column == u8"name");
    REQUIRE(message_at<resize_intent>(widened, 0)->delta == 15.0f);

    // 폭은 앱 상태라 이 tree에서는 여전히 160이다. 160에서 최소 80까지만 줄 수 있다.
    const auto narrowed { controller.process(luil::pointer_moved_event { 10.0f, 14.0f, at(20) }) };
    REQUIRE(message_at<resize_intent>(narrowed, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(narrowed, 0)->delta == -80.0f);

    const auto released { controller.process(luil::pointer_released_event { 10.0f, 14.0f, luil::pointer_button::left, at(30) }) };
    REQUIRE(message_at<resize_end_intent>(released, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(released, 0)->column == u8"name");
}

TEST_CASE("A horizontal wheel scrolls the table and a vertical wheel scrolls its body", "[ui][table][wheel]")
{
    const std::shared_ptr<const luil::ui_tree> tree { published(make_config()) };
    const luil::mouse_wheel_event right { .x = 100.0f, .y = 100.0f, .delta = 120.0f, .horizontal = true };
    const auto sideways { luil::route_wheel(*tree, right, -(right.delta / 120.0f) * luil::input_wheel_scroll_step) };
    REQUIRE(message_at<scroll_x_intent>(sideways, 0) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(sideways, 0)->delta == luil::input_wheel_scroll_step);

    const luil::mouse_wheel_event down { .x = 100.0f, .y = 100.0f, .delta = -120.0f };
    const auto downward { luil::route_wheel(*tree, down, -(down.delta / 120.0f) * luil::input_wheel_scroll_step) };
    REQUIRE(message_at<scroll_y_intent>(downward, 0) != nullptr);
}

TEST_CASE("Left and right scroll the table sideways once the body leaves them", "[ui][table][keys]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(make_config()));
    static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    const auto right { controller.process(luil::key_pressed_event { luil::key_code::arrow_right, false, false, false, false, at(10) }) };
    REQUIRE(message_at<scroll_x_intent>(right, 0) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(right, 0)->delta == luil::table_key_scroll_step);

    // 이미 왼쪽 끝이라 ←는 키만 삼킨다.
    REQUIRE(controller.process(luil::key_pressed_event { luil::key_code::arrow_left, false, false, false, false, at(20) }).empty());

    // ↓는 여전히 본문의 것이다.
    const auto down { controller.process(luil::key_pressed_event { luil::key_code::arrow_down, false, false, false, false, at(30) }) };
    REQUIRE(message_at<move_intent>(down, 0) != nullptr);
}

TEST_CASE("Assistive technology reads a data grid with header items and data items", "[ui][table][accessibility]")
{
    luil::table_config config { make_config() };
    config.rows.selected = u8"row-b";
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    REQUIRE(find(*tree, { luil::ui_element_kind::table, u8"files" }).accessibility().role == luil::access_role::none);
    REQUIRE(find(*tree, { luil::ui_element_kind::table_header, u8"files" }).accessibility().role == luil::access_role::header);
    const luil::access_info header { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"author") }).accessibility() };
    REQUIRE(header.role == luil::access_role::header_item);
    REQUIRE(header.name == u8"작성자");
    REQUIRE(find(*tree, { luil::ui_element_kind::virtual_list, u8"files" }).accessibility().role == luil::access_role::table);
    const luil::access_info row { find(*tree, { luil::ui_element_kind::virtual_list_row, u8"row-b" }).accessibility() };
    REQUIRE(row.role == luil::access_role::data_item);
    REQUIRE(row.selected == true);
}

TEST_CASE("The tree column indents its cells and holds the triangles", "[ui][table][tree]")
{
    luil::table_config config { make_config(true) };
    config.tree_column = u8"name";
    config.rows.items[0].expansion = luil::list_expansion::expanded;
    config.rows.items[1].depth = 1;
    config.rows.items[1].expansion = luil::list_expansion::collapsed;
    config.rows.items[2].depth = 2;
    config.rows.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(sort_intent { key + (expanded ? u8"+" : u8"-") }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    REQUIRE(tree->duplicate_ids().empty());

    // 삼각형은 트리 열 안, 깊이만큼 들여 선다. 행 앞의 가상 목록 삼각형은 서지 않는다.
    const luil::ui_element& top { find(*tree, { luil::ui_element_kind::table_expander, part(u8"row-a") }) };
    const luil::ui_element& child { find(*tree, { luil::ui_element_kind::table_expander, part(u8"row-b") }) };
    REQUIRE(top.bounds().x == luil::table_cell_padding);
    REQUIRE(child.bounds().x - top.bounds().x == luil::list_indent_step);
    REQUIRE(tree->find({ luil::ui_element_kind::virtual_list_expander, u8"row-a" }) == nullptr);
    REQUIRE(tree->find({ luil::ui_element_kind::table_expander, part(u8"row-c") }) == nullptr);

    // 앱의 칸은 들여쓰기와 삼각형 뒤에 선다. 다른 열은 밀리지 않는다.
    const float lead { 2.0f * luil::list_indent_step + luil::list_expander_width };
    REQUIRE(find(*tree, { kind_cell, u8"row-c/name" }).bounds().x == lead);
    REQUIRE(find(*tree, { kind_cell, u8"row-c/author" }).bounds().x == 160.0f);

    // 펼침 상태는 행(DataItem)이 알린다.
    REQUIRE(find(*tree, { luil::ui_element_kind::virtual_list_row, u8"row-b" }).accessibility().expanded == false);
}

TEST_CASE("The check column shows each row's state and toggles without stealing focus", "[ui][table][check]")
{
    luil::table_config config { make_config() };
    config.check_column = u8"name";
    config.check_state_of = [](const luil::virtual_list_item& row, std::size_t) {
        return row.key == u8"row-a" ? luil::check_state::mixed : row.key == u8"row-b" ? luil::check_state::checked : luil::check_state::unchecked;
    };
    config.toggle_check = [](const std::u8string& row) { return luil::make_app_action(resize_end_intent { row }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(config) };
    REQUIRE(tree->duplicate_ids().empty());

    const luil::ui_element& mixed { find(*tree, { luil::ui_element_kind::check, part(u8"row-a") }) };
    REQUIRE(mixed.accessibility().mixed);
    REQUIRE(mixed.accessibility().name == u8"row-a");
    REQUIRE(find(*tree, { luil::ui_element_kind::check, part(u8"row-b") }).accessibility().checked == true);
    REQUIRE(mixed.tab_stop() == false);

    luil::interaction_controller controller {};
    controller.set_tree(tree);
    const luil::rect_f box { mixed.bounds() };
    static_cast<void>(controller.process(luil::pointer_pressed_event { box.x + 10.0f, box.y + box.height / 2.0f, luil::pointer_button::left, at(0) }));
    const auto clicked { controller.process(luil::pointer_released_event { box.x + 10.0f, box.y + box.height / 2.0f, luil::pointer_button::left, at(5) }) };
    REQUIRE(message_at<resize_end_intent>(clicked, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(clicked, 0)->column == u8"row-a");
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });

    // 본문이 Space를 실행 키로 쓰지 않으므로 Space는 커서 행의 체크다.
    config.rows.cursor = u8"row-c";
    controller.set_tree(published(config));
    const auto spaced { controller.process(luil::key_pressed_event { luil::key_code::space, false, false, false, false, at(20) }) };
    REQUIRE(message_at<resize_end_intent>(spaced, 0) != nullptr);
    REQUIRE(message_at<resize_end_intent>(spaced, 0)->column == u8"row-c");
}
