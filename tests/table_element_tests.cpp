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

    constexpr luil::ui_element_kind kind_editor { luil::application_element_kind(1) };

    struct cell_intent
    {
        std::u8string verb {};
        luil::table_cell_ref cell {};
    };

    struct column_intent
    {
        std::u8string column {};
    };

    // 칸 편집기다. 앱 정책이 텍스트 칸으로 알아본다.
    class test_editor final : public luil::ui_element
    {
    public:
        explicit test_editor(std::u8string owner)
            : ui_element { luil::ui_element_id { kind_editor, std::move(owner) } }
        {
            set_action(luil::ui_trigger::left_click, [](const luil::ui_action_context&) -> std::vector<luil::input_action> { return {}; });
        }

        void arrange(const luil::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(luil::draw_context&, const luil::interaction_snapshot&) const override
        {}

        [[nodiscard]] std::optional<luil::text_input_snapshot> text_input() const override
        {
            return luil::text_input_snapshot { u8"row-b.name", 0u, 0u };
        }
    };

    class editor_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(const luil::ui_element_kind kind) const override
        {
            return kind == kind_editor ? std::optional<luil::text_input_target> { luil::text_input_target { 1 } } : std::nullopt;
        }

        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree*, const luil::key_pressed_event&, const luil::interaction_snapshot&) override
        {
            ++keys;
            return {};
        }

        int keys { 0 };
    };

    // 셀 탐색과 편집을 켠 표다. 이름·작성자 열을 편집할 수 있다.
    [[nodiscard]] luil::table_config editable_config(const std::u8string& cell_column)
    {
        luil::table_config config { make_config() };
        config.columns[0].editable = true;
        config.columns[1].editable = true;
        config.rows.cursor = u8"row-b";
        config.cell_column = cell_column;
        config.move_column = [](const std::u8string& column) { return luil::make_app_action(column_intent { column }); };
        config.begin_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"begin", cell }); };
        config.commit_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"commit", cell }); };
        config.cancel_edit = [](const luil::table_cell_ref& cell) { return luil::make_app_action(cell_intent { u8"cancel", cell }); };
        config.build_editor = [](const luil::virtual_list_item& row, std::size_t, const luil::table_column& column) -> std::unique_ptr<luil::ui_element> {
            return std::make_unique<test_editor>(row.key + u8"/" + column.key);
        };
        return config;
    }

    void focus_body(luil::interaction_controller& controller)
    {
        static_cast<void>(controller.process(luil::key_pressed_event { luil::key_code::tab, false, false, false, false, at(0) }));
        REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    }

    [[nodiscard]] luil::key_pressed_event press(const luil::key_code key, const int milliseconds)
    {
        return luil::key_pressed_event { key, false, false, false, false, at(milliseconds) };
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

TEST_CASE("With cell navigation left and right move the cell cursor and reveal it", "[ui][table][cells]")
{
    luil::interaction_controller controller {};
    controller.set_tree(published(editable_config(u8"author")));
    focus_body(controller);

    const auto right { controller.process(press(luil::key_code::arrow_right, 10)) };
    REQUIRE(message_at<column_intent>(right, 0) != nullptr);
    REQUIRE(message_at<column_intent>(right, 0)->column == u8"revision");
    // 리비전 열(250~320)은 창(0~200) 밖이라 끝을 맞춰 들인다.
    REQUIRE(message_at<scroll_x_intent>(right, 1) != nullptr);
    REQUIRE(message_at<scroll_x_intent>(right, 1)->delta == 120.0f);

    const auto left { controller.process(press(luil::key_code::arrow_left, 20)) };
    REQUIRE(message_at<column_intent>(left, 0) != nullptr);
    REQUIRE(message_at<column_intent>(left, 0)->column == u8"name");
    // 이미 보이는 열은 흘리지 않는다.
    REQUIRE(left.size() == 1u);

    // 끝 열에서 →는 키만 삼킨다.
    controller.set_tree(published(editable_config(u8"revision")));
    REQUIRE(controller.process(press(luil::key_code::arrow_right, 30)).empty());
}

TEST_CASE("A cell cursor in the tree column walks the hierarchy before it leaves the column", "[ui][table][cells][tree]")
{
    luil::table_config config { editable_config(u8"name") };
    config.tree_column = u8"name";
    config.rows.items[1].expansion = luil::list_expansion::expanded;
    config.rows.set_expanded = [](const std::u8string& key, const bool expanded) { return luil::make_app_action(cell_intent { expanded ? u8"expand" : u8"collapse", { key, {} } }); };
    luil::interaction_controller controller {};
    controller.set_tree(published(std::move(config)));
    focus_body(controller);

    const auto collapsed { controller.process(press(luil::key_code::arrow_left, 10)) };
    REQUIRE(message_at<cell_intent>(collapsed, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(collapsed, 0)->verb == u8"collapse");
    // 펼친 가지 위의 →는 첫 자식으로 가려 하지만 자식이 없어 키만 삼킨다 (계층 키가 이긴다).
    REQUIRE(controller.process(press(luil::key_code::arrow_right, 20)).empty());
}

TEST_CASE("F2 starts editing an editable cell and nothing else", "[ui][table][edit]")
{
    editor_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(editable_config(u8"author")));
    focus_body(controller);
    const auto started { controller.process(press(luil::key_code::f2, 10)) };
    REQUIRE(message_at<cell_intent>(started, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(started, 0)->verb == u8"begin");
    REQUIRE(message_at<cell_intent>(started, 0)->cell == luil::table_cell_ref { u8"row-b", u8"author" });

    // 편집할 수 없는 열에서 F2는 앱으로 흐른다.
    controller.set_tree(published(editable_config(u8"revision")));
    static_cast<void>(controller.process(press(luil::key_code::f2, 20)));
    REQUIRE(policy.keys == 1);
}

TEST_CASE("An editor takes focus, commits on Enter, cancels on Escape and hands focus back", "[ui][table][edit]")
{
    editor_policy policy {};
    luil::interaction_controller controller { &policy };
    controller.set_tree(published(editable_config(u8"name")));
    focus_body(controller);

    luil::table_config editing { editable_config(u8"name") };
    editing.editing = luil::table_cell_ref { u8"row-b", u8"name" };
    const std::shared_ptr<const luil::ui_tree> with_editor { published(editing) };
    REQUIRE(with_editor->duplicate_ids().empty());
    controller.set_tree(with_editor);
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { kind_editor, u8"row-b/name" });
    REQUIRE(controller.snapshot().focused_input == luil::ui_element_id { kind_editor, u8"row-b/name" });
    // 편집기는 그 칸 자리에 선다.
    REQUIRE(find(*with_editor, { kind_editor, u8"row-b/name" }).bounds().x == 0.0f);

    const auto committed { controller.process(press(luil::key_code::enter, 10)) };
    REQUIRE(message_at<cell_intent>(committed, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(committed, 0)->verb == u8"commit");
    REQUIRE(message_at<cell_intent>(committed, 0)->cell == luil::table_cell_ref { u8"row-b", u8"name" });

    const auto cancelled { controller.process(press(luil::key_code::escape, 20)) };
    REQUIRE(cancelled.size() == 1u);
    REQUIRE(message_at<cell_intent>(cancelled, 0) != nullptr);
    REQUIRE(message_at<cell_intent>(cancelled, 0)->verb == u8"cancel");

    // 앱이 편집을 끝내면 편집기가 사라지고 초점이 본문으로 돌아온다.
    controller.set_tree(published(editable_config(u8"name")));
    REQUIRE(controller.snapshot().focused == luil::ui_element_id { luil::ui_element_kind::virtual_list, u8"files" });
    REQUIRE(policy.keys == 0);
}

TEST_CASE("Dragging a header onto another asks the app to reorder the columns", "[ui][table][columns]")
{
    luil::table_config config { make_config() };
    config.reorder_column = [](const std::u8string& moved, const std::u8string& target) { return luil::make_app_action(resize_intent { moved + u8">" + target, 0.0f }); };
    config.column_menu = [](const std::u8string& column, const luil::ui_action_context&) { return luil::make_app_action(column_intent { column }); };
    const std::shared_ptr<const luil::ui_tree> tree { published(std::move(config)) };
    const luil::ui_element& name { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"name") }) };
    const luil::ui_element& author { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"author") }) };

    REQUIRE(author.drag() != nullptr);
    const luil::drag_payload payload { author.drag()->make_payload({ author.id(), 170.0f, 10.0f }) };
    REQUIRE(payload.dragged_owner == u8"author");
    REQUIRE(payload.grab_offset_x == 10.0f);
    // 자기 자리에는 놓지 않는다. 다른 표의 머리글 칸은 받지 않는다.
    REQUIRE(author.drop()->accepts(payload) == false);
    REQUIRE(name.drop()->accepts(payload));
    luil::drag_payload foreign { payload };
    foreign.container.owner = u8"other";
    REQUIRE(name.drop()->accepts(foreign) == false);
    const auto dropped { name.drop()->on_drop(payload, { name.id(), 0.0f, 0.0f }) };
    REQUIRE(message_at<resize_intent>(dropped, 0) != nullptr);
    REQUIRE(message_at<resize_intent>(dropped, 0)->column == u8"author>name");

    // 정렬할 수 없는 열의 머리글도 끌고 메뉴를 열 수 있다.
    const luil::ui_element& revision { find(*tree, { luil::ui_element_kind::table_header_cell, part(u8"revision") }) };
    REQUIRE(revision.drag() != nullptr);
    const luil::ui_action* const menu { revision.action(luil::ui_trigger::right_click) };
    REQUIRE(menu != nullptr);
    const auto opened { (*menu)({ revision.id(), 260.0f, 10.0f }) };
    REQUIRE(message_at<column_intent>(opened, 0) != nullptr);
    REQUIRE(message_at<column_intent>(opened, 0)->column == u8"revision");
}
